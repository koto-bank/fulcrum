#pragma once

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"

#include "types.hpp"

#include <iostream>
#include <variant>

using llvm::Type;

struct Expression {
protected:
    LanguageType *type = nullptr;
    llvm::Value *value = nullptr;
public:
    Expression(LanguageType *type) : type(type) { }

    Type *llvmType() { return type->llvmType(); }
    virtual llvm::Value *llvmValue() { return value; }

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
};

struct StringConstant : Expression {
private:
    llvm::GlobalVariable *llvmConst;
public:
    std::string constValue;

    StringConstant(llvm::Module &mod, LanguageType *type, std::string constValue) : Expression(type), constValue(constValue) {
        auto constStr = llvm::ConstantDataArray::getString(type->llvmType()->getContext(), constValue.data());

        // New here is overriden in llvm, so supposedly it's not just allocating on the heap
        llvmConst = new llvm::GlobalVariable(
            mod, constStr->getType(), true,
            llvm::GlobalValue::PrivateLinkage, constStr
        );
    }

    llvm::Value *llvmValue() override {
        auto Zero = llvm::ConstantInt::get(Type::getInt32Ty(type->llvmType()->getContext()), 0);
        llvm::Constant *Indices[] = {Zero, Zero};
        return llvm::ConstantExpr::getInBoundsGetElementPtr(llvmConst->getValueType(), llvmConst, Indices);
    }
};

struct BoolConstant : Expression {
public:
    bool constValue;

    BoolConstant(LanguageType *type, bool constValue) : Expression(type), constValue(constValue) {
        value = llvm::ConstantInt::get(type->llvmType(), constValue ? 1 : 0);
    }
};
