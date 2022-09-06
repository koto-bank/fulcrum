#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>

#include <fmt/format.h>

using llvm::LLVMContext;
using llvm::Type;

struct LanguageType {
protected:
    LLVMContext &context;

public:
    virtual Type* llvmType() = 0;
    virtual std::string signature() = 0;

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

    std::string signature() override {
        return std::string(isSigned ? "i" : "u") + std::to_string(bits);
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

    std::string signature() override {
        return bits == Bits::Float ? "f32" : "f64";
    }
};

struct StringType : LanguageType {
    StringType(LLVMContext &context) : LanguageType(context) { }

    Type* llvmType() override {
        return Type::getIntNPtrTy(context, 8);
    }

    std::string signature() override {
        return "str";
    }
};

struct CustomType : LanguageType {
    std::string name;

    CustomType(LLVMContext &context, std::string name)
        : LanguageType(context), name(name) { }

    Type* llvmType() override { return Type::getVoidTy(context); }

    std::string signature() override {
        return name;
    }
};

struct AliasType : CustomType {
    LanguageType *aliasTo;

    AliasType(LLVMContext &context, std::string name, LanguageType *aliasTo_)
        : CustomType(context, name), aliasTo(aliasTo_) { }

    Type* llvmType() override {
        return aliasTo->llvmType();
    }
};

struct StructType : CustomType {
private:
    llvm::StructType *structType;

public:
    using Fields = std::vector<std::tuple<std::string, LanguageType *>>;
    Fields fields;
    bool isPublic;

    StructType(LLVMContext &context, std::string name, const Fields &fields_, bool isPublic)
        : CustomType(context, name), fields(fields_), isPublic(isPublic) {
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

    std::string signature() override {
        return "char";
    }
};

struct PointerType : LanguageType {
    LanguageType *pointerTo;

    PointerType(LLVMContext &context, LanguageType *pointerTo_)
        : LanguageType(context), pointerTo(pointerTo_) { }

    Type* llvmType() override {
        return llvm::PointerType::get(pointerTo->llvmType(), 0);
    }

    std::string signature() override {
        return pointerTo->signature() + "*";
    }
};

struct VoidType : LanguageType {
    using LanguageType::LanguageType;

    Type *llvmType() override {
        return Type::getVoidTy(context);
    }

    std::string signature() override {
        return "void";
    }
};

struct BoolType : LanguageType {
    using LanguageType::LanguageType;

    Type *llvmType() override {
        return Type::getInt1Ty(context);
    }

    std::string signature() override {
        return "bool";
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

    std::string signature() override {
        std::string argSignatures;
        for (int i = 0; i < arguments.size(); i++) {
            if (i != 0)
                argSignatures += ", ";
            argSignatures += arguments[i]->signature();
        }

        return returnType->signature() + " (" + argSignatures + ")";
    }
};

struct ArrayType : LanguageType {
private:
    LanguageType *targetType;
    size_t size;

public:
    ArrayType(LLVMContext &context, LanguageType *targetType, size_t size)
        : LanguageType(context)
        , targetType(targetType)
        , size(size) { }

    Type* llvmType() override {
        return llvm::ArrayType::get(targetType->llvmType(), size);
    }

    std::string signature() override {
        return fmt::format("{}[{}]", targetType->signature(), size);
    }
};
