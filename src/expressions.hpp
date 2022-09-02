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
    virtual llvm::Value *llvmValue(llvm::IRBuilder<> &builder) { return value; }
    virtual std::string dump(int indent = 0) = 0;

    virtual ~Expression() = default;
};

template<typename T>
concept IsLongInteger = std::same_as<T, long> || std::same_as<T, unsigned long>;

struct IntegerConstant : Expression {
    std::variant<long, unsigned long> constValue;

    IntegerConstant(LanguageType *type, IsLongInteger auto constValue) : Expression(type), constValue(constValue) {
        if (!llvm::ConstantInt::isValueValidForType(type->llvmType(), constValue)) {
            std::cout << "Integer " << value << "does not fit into its type" << std::endl;
            return;
        }
        value = llvm::ConstantInt::get(type->llvmType(), constValue);
    }

    std::string dump(int indent) override {
        auto isSigned = ((IntegerType*)type)->isSigned;

        return fmt::format(
            "{}{}{}",
            indentSpaces(indent),
            isSigned ? std::get<long>(constValue) : std::get<unsigned long>(constValue),
            type->signature()
        );
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

    StringConstant(llvm::Module &mod, LanguageType *type, const std::string& constValue) : Expression(type), constValue(constValue) {
        auto constStr = llvm::ConstantDataArray::getString(type->llvmType()->getContext(), constValue.data());

        // New here is overriden in llvm, so supposedly it's not just allocating on the heap
        llvmConst = new llvm::GlobalVariable(
            mod, constStr->getType(), true,
            llvm::GlobalValue::PrivateLinkage, constStr
        );
    }

    llvm::Value *llvmValue(llvm::IRBuilder<> &builder) override {
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

public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;

    FunctionCall(CodegenContext &codegenCont, std::string name, std::vector<std::unique_ptr<Expression>> &&args)
        // Initialize type with nullptr for now, since we don't know the return type yet
        : Expression(nullptr), context(codegenCont), name(name), args(std::move(args))  { }

    llvm::Value *llvmValue(llvm::IRBuilder<> &builder) override {
        if (!context.functions.contains(name)) {
            // TODO: error here
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
            argValues.push_back(args[i]->llvmValue(builder));
        }

        return builder.CreateCall(calledFunction.llvmFunction(), argValues);
    }

    std::string dump(int indent) override {
        std::vector<std::string> argDumps;
        for (auto &arg : args)
            argDumps.push_back(arg->dump());

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
