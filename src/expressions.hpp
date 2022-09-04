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

public:
    LanguageType *type = nullptr;

    Expression(LanguageType *type) : type(type) { }

    Type *llvmType() { return type->llvmType(); }
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

    llvm::Value *returnProcessor(ExpressionGenContext &genContext) {
        if (args.size() != 0 && args.size() != 1) {
            std::cout << "Return must have 0 or 1 arguments" << std::endl;
            return nullptr;
        }

        auto returnType = genContext.function->functionType()->returnType;
        if (args.size() == 0 && returnType != context.types.at("void").get()) {
            std::cout << "Only void function can return nothing" << std::endl;
            return nullptr;
        } else if (args.size() == 1 && returnType != args[0]->type) {
            std::cout << fmt::format(
                "Function expected to return {}, but returns {}",
                returnType->signature(),
                args[0]->type->signature()) << std::endl;
            return nullptr;
        }

        if (args.size() == 0)
            genContext.builder.CreateRetVoid();
        else
            genContext.builder.CreateRet(args[0]->llvmValue(genContext));

        return nullptr;
    }

    llvm::Value *doProcessor(ExpressionGenContext &genContext) {
        genContext.function->generateExpressions(genContext, args);

        return nullptr;
    }

    llvm::Value *ifProcessor(ExpressionGenContext &genContext) {
        if (args.size() < 2 || args.size() > 3) {
            std::cout << "If must have from 2 to 3 arguments" << std::endl;
            return nullptr;
        }
        if (args[0]->type != context.types.at("bool").get()) {
            std::cout << "First argument to if must be boolean" << std::endl;
            return nullptr;
        }

        auto &builder = genContext.builder;

        auto ifCondition = args[0]->llvmValue(genContext);

        auto thenBlock = llvm::BasicBlock::Create(context.context, "if-then", genContext.function->llvmFunction());
        auto elseBlock = args.size() == 3
            ? llvm::BasicBlock::Create(context.context, "if-else", genContext.function->llvmFunction())
            : nullptr;
        auto afterIfBlock = llvm::BasicBlock::Create(context.context, "after-if", genContext.function->llvmFunction());
        thenBlock->moveAfter(builder.GetInsertBlock());
        if (elseBlock != nullptr) elseBlock->moveAfter(thenBlock);
        afterIfBlock->moveAfter(elseBlock != nullptr ? elseBlock : thenBlock);

        builder.CreateCondBr(ifCondition, thenBlock, elseBlock != nullptr ? elseBlock : afterIfBlock);

        builder.SetInsertPoint(thenBlock);

        if (!genContext.function->generateExpressions(genContext, {args[1].get()})) {
            builder.CreateBr(afterIfBlock);
        }

        if (args.size() == 3) {
            builder.SetInsertPoint(elseBlock);

            if (!genContext.function->generateExpressions(genContext, {args[2].get()})) {
                builder.CreateBr(afterIfBlock);
            }
        }
        builder.SetInsertPoint(afterIfBlock);

        return nullptr;
    }

public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;

    FunctionCall(CodegenContext &codegenCont, std::string name)
        : Expression(nullptr), context(codegenCont), name(name) { }

    FunctionCall(CodegenContext &codegenCont, std::string name, std::vector<std::unique_ptr<Expression>> &&args)
        // Initialize type with nullptr for now, since we don't know the return type yet
        : Expression(nullptr), context(codegenCont), name(name), args(std::move(args))  { }

    using SpecialFunctionProcessor = std::function<llvm::Value *(FunctionCall *, ExpressionGenContext &)>;
    std::map<std::string, SpecialFunctionProcessor> specialFunctions {
        {"return", &FunctionCall::returnProcessor},
        {"if", &FunctionCall::ifProcessor},
        {"do", &FunctionCall::doProcessor}
    };

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override {
        if (specialFunctions.contains(name))
            return specialFunctions[name](this, genContext);

        if (!context.functions.contains(name)) {
            // TODO: error here
           std::cout << fmt::format("Undefined function {}", name) << std::endl;
           return nullptr;
        }
        auto &calledFunction = context.functions.at(name);

        std::vector<llvm::Value *> argValues;
        for (auto i = 0; i < args.size(); i++) {
            LanguageType *argType = args[i]->type;
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
