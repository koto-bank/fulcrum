#pragma once

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"

#include "fmt/format.h"

#include "codegen_context.hpp"
#include "types.hpp"

#include <iostream>
#include <variant>

using llvm::Type;

struct ExpressionGenContext {
    llvm::IRBuilder<> &builder;
    Function *function;
};

struct Expression {
protected:
    llvm::Value *value = nullptr;

    std::string indentSpaces(int n) {
        return fmt::format("{: >{}}", "", n);
    }

    LanguageType *type = nullptr;
public:
    Expression(LanguageType *type) : type(type) { }

    virtual LanguageType *languageType() { return type; }
    virtual Type *llvmType() { return languageType()->llvmType(); }
    virtual llvm::Value *llvmValue(ExpressionGenContext &genContext) { return value; }
    virtual std::string dump(int indent = 0) = 0;

    virtual ~Expression() = default;
};

template<typename T>
concept IsLongInteger = std::same_as<T, uint64_t> || std::same_as<T, int64_t>;

struct IntegerConstant : Expression {
    std::variant<uint64_t, int64_t> constValue;

    IntegerConstant(IntegerType *type, IsLongInteger auto _constValue) : Expression(type), constValue(_constValue) {
        if (!llvm::ConstantInt::isValueValidForType(type->llvmType(), _constValue)) {
            std::cout << "Integer " << value << "does not fit into its type" << std::endl;
            return;
        }

        value = llvm::ConstantInt::get(type->llvmType(), _constValue);
    }

    std::string dump(int indent) override {
        auto isSigned = static_cast<IntegerType *>(type)->isSigned;

        return isSigned
            ? fmt::format(
                "{}{}{}", indentSpaces(indent), std::get<int64_t>(constValue), type->signature())
            : fmt::format(
                "{}{}{}", indentSpaces(indent), std::get<uint64_t>(constValue), type->signature());
    }
};

template<typename T>
concept IsFloatingPoint = std::same_as<T, float> || std::same_as<T, double>;

struct FloatConstant : Expression {
    std::variant<float, double> constValue;

    FloatConstant(LanguageType *type, IsFloatingPoint auto constValue) : Expression(type), constValue(constValue) {
        auto apFloat = llvm::APFloat(constValue);
        if (!llvm::ConstantFP::isValueValidForType(type->llvmType(), apFloat)) {
            std::cout << "Float " << value << "does not fit into its type" << std::endl;
            return;
        }
        value = llvm::ConstantFP::get(type->llvmType(), apFloat);
    }

    std::string dump(int indent) override {
        auto floatbits = ((FloatType*)type)->bits;

        return fmt::format(
            "{}{}{}",
            indentSpaces(indent),
            floatbits == FloatType::Bits::Double ? std::get<double>(constValue) : std::get<float>(constValue),
            type->signature()
        );
    }
};

struct StringConstant : Expression {
private:
    llvm::GlobalVariable *llvmConst;

public:
    std::string constValue;

    StringConstant(LanguageType *type, llvm::Module &mod, const std::string& constValue) : Expression(type), constValue(constValue) {
        auto constStr = llvm::ConstantDataArray::getString(type->llvmType()->getContext(), constValue.data());

        // New here is overriden in llvm, so supposedly it's not just allocating on the heap
        llvmConst = new llvm::GlobalVariable(
            mod, constStr->getType(), true,
            llvm::GlobalValue::PrivateLinkage, constStr
        );
    }

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override {
        auto Zero = llvm::ConstantInt::get(Type::getInt32Ty(type->llvmType()->getContext()), 0);
        llvm::Constant *Indices[] = {Zero, Zero};
        return llvm::ConstantExpr::getInBoundsGetElementPtr(llvmConst->getValueType(), llvmConst, Indices);
    }

    std::string dump(int indent) override {
        return fmt::format("{}\"{}\"", indentSpaces(indent), constValue);
    }
};

struct BoolConstant : Expression {
public:
    bool constValue;

    BoolConstant(LanguageType *type, bool constValue) : Expression(type), constValue(constValue) {
        value = llvm::ConstantInt::get(type->llvmType(), constValue ? 1 : 0);
    }

    std::string dump(int indent) override {
        return fmt::format("{}{}", indentSpaces(indent), constValue);
    }
};

struct FunctionCall : Expression {
private:
    CodegenContext &context;

    llvm::Value *returnProcessor(ExpressionGenContext &genContext);
    llvm::Value *doProcessor(ExpressionGenContext &genContext);
    llvm::Value *ifProcessor(ExpressionGenContext &genContext);
    llvm::Value *arithmeticsProcessor(ExpressionGenContext &genContext);

    LanguageType *arithmeticsProcessorType();
    LanguageType *voidProcessorType() { return context.getType("void"); }
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;

    FunctionCall(CodegenContext &codegenCont, std::string name)
        : Expression(nullptr), context(codegenCont), name(name) { }

    FunctionCall(CodegenContext &codegenCont, std::string name, std::vector<std::unique_ptr<Expression>> &&args)
        // Initialize type with nullptr for now, since we don't know the return type yet
        : Expression(nullptr), context(codegenCont), name(name), args(std::move(args))  { }

    using SpecialFunctionProcessor = std::function<llvm::Value *(FunctionCall *, ExpressionGenContext &)>;
    using SpecialFunctionTyping = std::function<LanguageType *(FunctionCall *)>;

    std::map<std::string, std::pair<SpecialFunctionProcessor, SpecialFunctionTyping>> specialFunctions {
        {"return", {&FunctionCall::returnProcessor, &FunctionCall::voidProcessorType}},
        {"if", {&FunctionCall::ifProcessor, &FunctionCall::voidProcessorType}},
        {"do", {&FunctionCall::doProcessor, &FunctionCall::voidProcessorType}},

        {"+", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
        {"-", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
        {"/", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
        {"%", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
    };

    LanguageType *languageType() override {
        if (type == nullptr) {
            if (specialFunctions.contains(name)) {
                type = specialFunctions[name].second(this);
            } else {
                if (!context.functions.contains(name)) {
                    // TODO: error here
                    std::cout << fmt::format("Undefined function {}", name) << std::endl;
                    return nullptr;
                }

                type = context.functions.at(name).functionType()->returnType;
            }
        }

        return type;
    }

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override {
        if (specialFunctions.contains(name))
            return specialFunctions[name].first(this, genContext);

        if (!context.functions.contains(name)) {
            // TODO: error here
           std::cout << fmt::format("Undefined function {}", name) << std::endl;
           return nullptr;
        }
        auto &calledFunction = context.functions.at(name);

        std::vector<llvm::Value *> argValues;
        for (auto i = 0; i < args.size(); i++) {
            LanguageType *argType = args[i]->languageType();
            LanguageType *expectedType = calledFunction.functionType()->arguments[i];
            if (argType->llvmType() != expectedType->llvmType()) {
                std::cout <<
                    fmt::format("Incompatible argument type in {}: for argument #{} "
                                " expected {}, but received {}", name, i, expectedType->signature(), argType->signature());
                // TODO: error here
                return nullptr;
            }
            argValues.push_back(args[i]->llvmValue(genContext));
        }

        return genContext.builder.CreateCall(calledFunction.llvmFunction(), argValues);
    }

    std::string dump(int indent) override {
        std::vector<std::string> argDumps;
        for (auto &arg : args) {
            if (arg != nullptr) {
                argDumps.push_back(arg->dump());
            } else {
                argDumps.push_back("nullptr");
            }
        }

        return fmt::format("{}({} {})", indentSpaces(indent), name, fmt::join(argDumps, " "));
    }
};

struct VarAccess : Expression {
private:
    CodegenContext &context;

public:
    std::string name;

    VarAccess(CodegenContext &codegenCont, const std::string& name)
        : Expression(nullptr), context(codegenCont), name(name) { }

    std::string dump(int indent) override {
        return fmt::format("{}{}", indentSpaces(indent), name);
    }
};
