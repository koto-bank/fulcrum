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

struct VariableDefinition {
    std::string name;
    LanguageType *type;
    llvm::Value *value;

    VariableDefinition(std::string name, LanguageType *type) : name(name), type(type) { }
};

struct ExpressionGenContext {
    llvm::IRBuilder<> &builder;
    Function *function;
    std::vector<std::map<std::string, VariableDefinition>> variableScopes;

    VariableDefinition *lookupVariable(std::string name);
    VariableDefinition *insertVariable(std::string name, LanguageType *type);
    void variableSet(VariableDefinition *var, llvm::Value *value);
    void pushScope() { variableScopes.push_back({}); }
    void popScope() { variableScopes.pop_back(); }
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

    virtual LanguageType *languageType(ExpressionGenContext &genContext) { return type; }
    virtual Type *llvmType(ExpressionGenContext &genContext) { return languageType(genContext)->llvmType(); }
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
            throw CodegenError(fmt::format("Integer {} does not fit into its type", _constValue));
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

    FloatConstant(LanguageType *type, IsFloatingPoint auto constValue_) : Expression(type), constValue(constValue_) {
        auto apFloat = llvm::APFloat(constValue_);
        if (!llvm::ConstantFP::isValueValidForType(type->llvmType(), apFloat)) {
            throw CodegenError(fmt::format("Float {} does not fit into its type", constValue_));
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

    LanguageType *arithmeticsProcessorType(ExpressionGenContext &genContext);
    LanguageType *voidProcessorType(ExpressionGenContext &genContext) { return context.getType("void"); }
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;

    FunctionCall(CodegenContext &codegenContext, std::string name)
        : Expression(nullptr), context(codegenContext), name(name) { }

    FunctionCall(CodegenContext &codegenContext, std::string name, std::vector<std::unique_ptr<Expression>> &&args)
        // Initialize type with nullptr for now, since we don't know the return type yet
        : Expression(nullptr), context(codegenContext), name(name), args(std::move(args))  { }

    using SpecialFunctionProcessor = std::function<llvm::Value *(FunctionCall *, ExpressionGenContext &)>;
    using SpecialFunctionTyping = std::function<LanguageType *(FunctionCall *, ExpressionGenContext &)>;

    std::map<std::string, std::pair<SpecialFunctionProcessor, SpecialFunctionTyping>> specialFunctions {
        {"return", {&FunctionCall::returnProcessor, &FunctionCall::voidProcessorType}},
        {"if", {&FunctionCall::ifProcessor, &FunctionCall::voidProcessorType}},
        {"do", {&FunctionCall::doProcessor, &FunctionCall::voidProcessorType}},

        {"+", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
        {"-", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
        {"/", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
        {"%", {&FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType}},
    };

    LanguageType *languageType(ExpressionGenContext &genContext) override {
        if (type == nullptr) {
            if (specialFunctions.contains(name)) {
                type = specialFunctions[name].second(this, genContext);
            } else {
                if (!context.functions.contains(name)) {
                    throw CodegenError(fmt::format("Undefined function {}", name));
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
            throw CodegenError(fmt::format("Undefined function {}", name));
        }
        auto &calledFunction = context.functions.at(name);

        std::vector<llvm::Value *> argValues;
        for (auto i = 0; i < args.size(); i++) {
            LanguageType *argType = args[i]->languageType(genContext);
            LanguageType *expectedType = calledFunction.functionType()->arguments[i];
            if (argType->llvmType() != expectedType->llvmType()) {
                throw CodegenError(
                    fmt::format("Incompatible argument type in {}: for argument #{} "
                                " expected {}, but received {}", name, i, expectedType->signature(), argType->signature())
                );
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
    std::string name;

    VarAccess(const std::string& name);
    LanguageType *languageType(ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;
};

struct AddrOf : Expression {
private:
    CodegenContext &context;
    std::unique_ptr<Expression> target;

public:
    AddrOf(CodegenContext &codegenContext, std::unique_ptr<Expression> &&target);
    LanguageType *languageType(ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;
};

struct Dereference : Expression {
    std::unique_ptr<Expression> target;

    Dereference();
    LanguageType *languageType(ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;
};

struct VariableDeclaration : Expression {
    std::unique_ptr<Expression> initialValue = nullptr;
    std::string name;
    LanguageType *type;

    VariableDeclaration(const std::string& name);

    std::string dump(int indent) override;
};

struct Sizeof : Expression {
    LanguageType *targetType;

    Sizeof(LanguageType *targetType);

    std::string dump(int indent) override;
};
