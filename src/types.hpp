#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>

using llvm::LLVMContext;
using llvm::Type;

struct LanguageType {
protected:
    LLVMContext &context;
public:
    virtual Type* llvmType() = 0;

    LanguageType(LLVMContext &context) : context(context) { }

    virtual ~LanguageType() = default;
};

struct IntegerType : LanguageType {
    unsigned int bits;
    bool isSigned;

    IntegerType(LLVMContext &context, unsigned int bits, bool isSigned) : LanguageType(context), bits(bits), isSigned(isSigned) { }

    Type* llvmType() override {
        return Type::getIntNTy(context, bits);
    }
};

struct FloatType : LanguageType {
    enum class Bits { Float, Double };
    Bits bits;

    FloatType(LLVMContext &context, Bits bits) : LanguageType(context), bits(bits) { }

    Type* llvmType() override {
        return bits == Bits::Float
            ? Type::getFloatTy(context)
            : Type::getDoubleTy(context);
    }
};

struct StringType : LanguageType {
    StringType(LLVMContext &context) : LanguageType(context) { }

    Type* llvmType() override {
        return Type::getIntNPtrTy(context, 8);
    }
};

struct StructType : LanguageType {
private:
    llvm::StructType *structType;
public:
    std::string name;
    std::vector<std::tuple<std::string, LanguageType *>> fields;

    StructType(LLVMContext &context, std::string name, decltype(fields) fields_)
        : LanguageType(context), name(name), fields(fields_) {
        std::vector<Type *> fieldTypes;
        std::transform(fields.begin(), fields.end(), std::back_inserter(fieldTypes), [](auto &type) {
            auto &[_, tp] = type;
            return tp->llvmType();
        });

        structType = llvm::StructType::create(context, fieldTypes, name);
    }

    Type* llvmType() override {
        return structType;
    }
};

struct CharType : LanguageType {
    using LanguageType::LanguageType;

    Type *llvmType() override {
        return Type::getInt8Ty(context);
    }
};

struct PointerType : LanguageType {
    LanguageType *pointerTo;

    PointerType(LLVMContext &context, LanguageType *pointerTo_)
        : LanguageType(context), pointerTo(pointerTo_) { }

    Type* llvmType() override {
        return llvm::PointerType::get(pointerTo->llvmType(), 0);
    }
};

struct VoidType : LanguageType {
    using LanguageType::LanguageType;

    Type *llvmType() override {
        return Type::getVoidTy(context);
    }
};

struct AliasType : LanguageType {
    LanguageType *aliasTo;

    AliasType(LLVMContext &context, LanguageType *aliasTo_)
        : LanguageType(context), aliasTo(aliasTo_) { }

    Type* llvmType() override {
        return aliasTo->llvmType();
    }
};

struct BoolType : LanguageType {
    using LanguageType::LanguageType;

    Type *llvmType() override {
        return Type::getInt1Ty(context);
    }
};

struct FunctionType : LanguageType {
private:
    llvm::FunctionType *funcType;
public:
    std::vector<LanguageType *> arguments;
    LanguageType *returnType;

    FunctionType(LLVMContext &context, std::vector<LanguageType *> args, LanguageType *returnType_)
        : LanguageType(context), arguments(args), returnType(returnType_) {
        std::vector<Type *> argTypes;
        std::transform(arguments.begin(), arguments.end(), std::back_inserter(argTypes), [](auto &type) { return type->llvmType(); });
        Type *retType = returnType->llvmType();

        funcType = llvm::FunctionType::get(retType, argTypes, false);
    }

    Type* llvmType() override {
        return funcType;
    }
};
