#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include "codegen_context.hpp"
#include "types.hpp"

using llvm::Type;

LanguageType::LanguageType(CodegenContext &context)
    : context(context) {}

IntegerType::IntegerType(CodegenContext &context, unsigned int bits, bool isSigned)
    : LanguageType(context),
      bits(bits),
      isSigned(isSigned) {}

Type *IntegerType::llvmType() { return Type::getIntNTy(context.context, bits); }

std::string IntegerType::signature() { return std::string(isSigned ? "i" : "u") + std::to_string(bits); }

LanguageType *LanguageType::actualLanguageType() { return this; }

FloatType::FloatType(CodegenContext &context, Bits bits)
    : LanguageType(context),
      bits(bits) {}

std::string FloatType::signature() { return bits == Bits::Float ? "f32" : "f64"; }

Type *FloatType::llvmType() {
    return bits == Bits::Float ? Type::getFloatTy(context.context) : Type::getDoubleTy(context.context);
}

StringType::StringType(CodegenContext &context)
    : LanguageType(context) {}

std::string StringType::signature() { return "str"; }

Type *StringType::llvmType() { return Type::getIntNPtrTy(context.context, 8); }

AliasType::AliasType(CodegenContext &codegenContext, std::string name, LanguageType *aliasTo_)
    : LanguageType(codegenContext),
      name(name),
      aliasTo(aliasTo_) {}

llvm::Type *AliasType::llvmType() { return aliasTo->llvmType(); }

std::string AliasType::signature() { return fmt::format("{} ({})", name, actualLanguageType()->signature()); }

LanguageType *AliasType::actualLanguageType() { return aliasTo->actualLanguageType(); }

StructType::StructType(CodegenContext &codegenContext, std::string name, bool isPublic)
    : LanguageType(codegenContext),
      name(name),
      isPublic(isPublic) {}

void StructType::fillFields(const Fields &fields_) { fields = fields_; }

int StructType::fieldIndex(const std::string &fieldName) {
    auto fieldIter
        = std::find_if(fields.begin(), fields.end(), [&fieldName](const StructType::Fields::value_type &elem) {
              return std::get<0>(elem) == fieldName;
          });
    if (fieldIter == fields.end())
        throw CodegenError(fmt::format("No field named {} in type {}", fieldName, signature()));
    return std::distance(fields.begin(), fieldIter);
}

std::string StructType::signature() { return name; }

Type *StructType::llvmType() {
    if (structType == nullptr) {
        std::vector<Type *> fieldTypes;
        std::transform(fields.begin(), fields.end(), std::back_inserter(fieldTypes), [](auto &type) {
            auto &[_, tp] = type;
            return tp->llvmType();
        });

        structType = llvm::StructType::create(context.context, fieldTypes, name);
    }

    return structType;
}

UnionType::UnionType(CodegenContext &codegenContext, std::string name)
    : StructType(codegenContext, name, true) {}

Type *UnionType::llvmType() {
    if (structType == nullptr) {
        llvm::LinearPolySize<llvm::TypeSize>::ScalarTy biggestSize = 0;
        for (auto [_, fieldTp] : fields) {
            auto size = context.module.getDataLayout().getTypeAllocSize(fieldTp->llvmType()).getFixedSize();
            if (size > biggestSize) biggestSize = size;
        }

        auto unionArrayType = ArrayType(context, context.getNamed<NamedTypeValue>("i8"), biggestSize);
        structType = llvm::StructType::create(context.context, { unionArrayType.llvmType() }, name);
    }

    return structType;
}

std::string CharType::signature() { return "char"; }

Type *CharType::llvmType() { return Type::getInt8Ty(context.context); }

Type *VoidType::llvmType() { return Type::getVoidTy(context.context); }

Type *BoolType::llvmType() { return Type::getInt1Ty(context.context); }

PointerType::PointerType(CodegenContext &context, LanguageType *pointerTo_)
    : LanguageType(context),
      pointerTo(pointerTo_) {}

llvm::Type *PointerType::llvmType() { return llvm::PointerType::get(context.context, 0); }

std::string PointerType::signature() { return pointerTo->signature() + "*"; }

LanguageType *PointerType::actualLanguageType() {
    auto actualInternal = pointerTo->actualLanguageType();
    auto pointeeName = actualInternal->signature();
    auto ptrName = pointeeName + "*";
    return context.getOrEmplaceType<PointerType>(ptrName, actualInternal);
}

std::string VoidType::signature() { return "void"; }

std::string BoolType::signature() { return "bool"; }

FunctionType::FunctionType(CodegenContext &context, std::vector<LanguageType *> args, LanguageType *returnType_)
    : LanguageType(context),
      arguments(args),
      returnType(returnType_) {}

llvm::Type *FunctionType::llvmType() {
    if (funcType == nullptr) {
        std::vector<llvm::Type *> argTypes;
        std::transform(arguments.begin(), arguments.end(), std::back_inserter(argTypes), [](auto &type) {
            return type->llvmType();
        });
        llvm::Type *retType = returnType->llvmType();

        funcType = llvm::FunctionType::get(retType, argTypes, false);
    }

    return funcType;
}

std::string FunctionType::signatureFrom(const std::vector<LanguageType *> &arguments, LanguageType *returnType) {
    std::string argSignatures;
    for (auto i = 0u; i < arguments.size(); i++) {
        if (i != 0) argSignatures += ", ";
        argSignatures += arguments[i]->signature();
    }

    return returnType->signature() + " (" + argSignatures + ")";
}

std::string FunctionType::signature() { return signatureFrom(arguments, returnType); }

ArrayType::ArrayType(CodegenContext &context, LanguageType *targetType, size_t size)
    : LanguageType(context),
      targetType(targetType),
      size(size) {}

llvm::Type *ArrayType::llvmType() { return llvm::ArrayType::get(targetType->llvmType(), size); }

std::string ArrayType::signature() { return fmt::format("{}[{}]", targetType->signature(), size); }
