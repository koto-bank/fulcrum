#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/TypedPointerType.h>
#include <llvm/IR/Type.h>

#include "codegen_context.hpp"
#include "types.hpp"

LanguageType::LanguageType(CodegenContext &context)
    : context(context) {}

llvm::Type* LanguageType::llvmTypeAccess() {
    return llvmType();
}

IntegerType::IntegerType(CodegenContext &context, unsigned int bits, bool isSigned)
    : LanguageType(context),
      bits(bits),
      isSigned(isSigned) {}

llvm::Type *IntegerType::llvmType() { return llvm::Type::getIntNTy(context.context, bits); }

std::string IntegerType::signature() { return std::string(isSigned ? "i" : "u") + std::to_string(bits); }

LanguageType *LanguageType::actualLanguageType() { return this; }

bool LanguageType::assignable(LanguageType *other) const {
    // whoa
    return other == this;
}

FloatType::FloatType(CodegenContext &context, Bits bits)
    : LanguageType(context),
      bits(bits) {}

std::string FloatType::signature() { return bits == Bits::Float ? "f32" : "f64"; }

llvm::Type *FloatType::llvmType() {
    return bits == Bits::Float ? llvm::Type::getFloatTy(context.context) : llvm::Type::getDoubleTy(context.context);
}

AliasType::AliasType(CodegenContext &codegenContext, const NamePath &name, LanguageType *aliasTo_)
    : LanguageType(codegenContext),
      name(name),
      aliasTo(aliasTo_) {}

llvm::Type *AliasType::llvmType() { return aliasTo->llvmType(); }

std::string AliasType::signature() { return fmt::format("{} ({})", name.join(), actualLanguageType()->signature()); }

LanguageType *AliasType::actualLanguageType() { return aliasTo->actualLanguageType(); }

StructType::StructType(CodegenContext &codegenContext, const NamePath &name, bool isPublic)
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

std::string StructType::signature() { return name.join(); }

llvm::Type *StructType::llvmType() {
    if (structType == nullptr) {
        std::vector<llvm::Type *> fieldTypes;
        std::transform(fields.begin(), fields.end(), std::back_inserter(fieldTypes), [](auto &type) {
            auto &[_, tp] = type;
            return tp->llvmType();
        });

        structType = llvm::StructType::create(context.context, fieldTypes, name.join());
    }

    return structType;
}

UnionType::UnionType(CodegenContext &codegenContext, const NamePath &name, long long biggestSize)
    : StructType(codegenContext, name, true) {
    auto unionArrayType = ArrayType(context, context.namedTypes.at("i8").get(), biggestSize);
    structType = llvm::StructType::create(context.context, { unionArrayType.llvmType() }, name.join());
}

llvm::Type *UnionType::llvmType() { return structType; }

llvm::Type *VoidType::llvmType() { return llvm::Type::getVoidTy(context.context); }

llvm::Type *BoolType::llvmType() { return llvm::Type::getInt1Ty(context.context); }

PointerType::PointerType(CodegenContext &context, LanguageType *targetType)
    : LanguageType(context),
      targetType(targetType) {}

llvm::Type *PointerType::llvmType() { return llvm::PointerType::get(llvm::Type::getInt8Ty(context.context), 0); }

std::string PointerType::signature() { return targetType->signature() + "*"; }

LanguageType *PointerType::actualLanguageType() {
    auto actualInternal = targetType->actualLanguageType();
    auto pointeeName = actualInternal->signature();
    auto ptrName = pointeeName + "*";
    // TODO: error check
    return context.emplaceType<PointerType>(ptrName, actualInternal);
}

bool PointerType::assignable(LanguageType *other) const {
    if (other == this) {
        return true;
    }
    if (auto at = dynamic_cast<ArrayType *>(other); at != nullptr) {
        return targetType->assignable(at->targetType);
    }
    return false;
}

std::string VoidType::signature() { return "void"; }

std::string BoolType::signature() { return "bool"; }

FunctionType::FunctionType(CodegenContext &context, std::vector<LanguageType *> args, LanguageType *returnType, bool isVariadic)
    : LanguageType(context),
      arguments(args),
      returnType(returnType),
      isVariadic(isVariadic) {}

llvm::Type *FunctionType::llvmType() {
    if (funcType == nullptr) {
        std::vector<llvm::Type *> argTypes;
        std::transform(arguments.begin(), arguments.end(), std::back_inserter(argTypes), [](auto &type) {
            return type->llvmTypeAccess();
        });
        llvm::Type *retType = returnType->llvmTypeAccess();

        funcType = llvm::FunctionType::get(retType, argTypes, isVariadic);
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
llvm::Type *ArrayType::llvmTypeAccess() { return llvm::PointerType::get(targetType->llvmType(), 0); }

std::string ArrayType::signature() { return fmt::format("{}[{}]", targetType->signature(), size); }

llvm::Type *VAType::llvmType() {
    return llvm::PointerType::get(llvm::Type::getInt8Ty(context.context), 0);
}

std::string VAType::signature() {
    return VAType::Signature;
}
