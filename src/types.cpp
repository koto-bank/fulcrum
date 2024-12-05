#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/TypedPointerType.h>
#include <llvm/IR/Type.h>

#include "assert.hpp"
#include "codegen_context.hpp"
#include "types.hpp"

LanguageType::LanguageType(CodegenContext &context)
    : context(context) {}

IntegerType::IntegerType(CodegenContext &context, unsigned int bits, bool isSigned)
    : LanguageType(context),
      bits(bits),
      isSigned(isSigned) {}

llvm::Type *IntegerType::llvmType() const { return llvm::Type::getIntNTy(context.context, bits); }

std::string IntegerType::signature() const { return std::string(isSigned ? "i" : "u") + std::to_string(bits); }

bool LanguageType::assignable(const LanguageType *other) const {
    // whoa
    return other == this;
}

FloatType::FloatType(CodegenContext &context, Bits bits)
    : LanguageType(context),
      bits(bits) {}

std::string FloatType::signature() const { return bits == Bits::Float ? "f32" : "f64"; }

llvm::Type *FloatType::llvmType() const {
    return bits == Bits::Float ? llvm::Type::getFloatTy(context.context) : llvm::Type::getDoubleTy(context.context);
}

AliasType::AliasType(CodegenContext &codegenContext, const NamePath &name, const LanguageType *targetType)
    : LanguageType(codegenContext),
      name(name),
      targetType(targetType) {
}

llvm::Type *AliasType::llvmType() const { return targetType->llvmType(); }

std::string AliasType::signature() const { return fmt::format("{} ({})", name.join(), targetType->signature()); }

StructType::StructType(CodegenContext &codegenContext, const NamePath &name, bool isPublic)
    : LanguageType(codegenContext),
      name(name),
      isPublic(isPublic) {}

void StructType::fillFields(const Fields &newFields) {
    fc_assert(structType == nullptr);

    fields = newFields;

    std::vector<llvm::Type *> fieldTypes;
    std::transform(fields.begin(), fields.end(), std::back_inserter(fieldTypes), [](auto &field) {
        auto tp = field.type;
        return tp->llvmType();
    });

    structType = llvm::StructType::create(context.context, fieldTypes, name.join());
}

int32_t StructType::fieldIndex(const std::string &fieldName) const {
    auto fieldIter
        = std::find_if(fields.begin(), fields.end(), [&fieldName](const StructType::Fields::value_type &elem) {
              return elem.name == fieldName;
          });
    if (fieldIter == fields.end()) {
        throw CodegenError(fmt::format("No field named {} in type {}", fieldName, signature()));
    }
    return std::distance(fields.begin(), fieldIter);
}

std::string StructType::signature() const { return name.join(); }

llvm::Type *StructType::llvmType() const {
    return structType;
}

UnionType::UnionType(CodegenContext &codegenContext, const NamePath &name, long long biggestSize)
    : StructType(codegenContext, name, true) {
    auto unionArrayType = ArrayType(context, context.namedTypes.at("i8").get(), biggestSize);
    structType = llvm::StructType::create(context.context, { unionArrayType.llvmType() }, name.join());
}

llvm::Type *UnionType::llvmType() const { return structType; }

llvm::Type *VoidType::llvmType() const { return llvm::Type::getVoidTy(context.context); }

llvm::Type *BoolType::llvmType() const { return llvm::Type::getInt1Ty(context.context); }

PointerType::PointerType(CodegenContext &context, const LanguageType *targetType)
    : LanguageType(context),
      targetType(targetType) {}

llvm::Type *PointerType::llvmType() const {
    // what. why Int8Ty?
    return llvm::PointerType::get(llvm::Type::getInt8Ty(context.context), 0);
}

std::string PointerType::signature() const { return targetType->signature() + "*"; }

bool PointerType::assignable(const LanguageType *other) const {
    if (other == this) {
        return true;
    }
    if (auto at = dynamic_cast<const ArrayType *>(other); at != nullptr) {
        return targetType->assignable(at->targetType);
    }
    return false;
}

std::string VoidType::signature() const { return "void"; }

std::string BoolType::signature() const { return "bool"; }

FunctionType::FunctionType(CodegenContext &context, std::vector<const LanguageType *> args, const LanguageType *returnType, bool isVariadic)
    : LanguageType(context),
      arguments(args),
      returnType(returnType),
      isVariadic(isVariadic) {
    std::vector<llvm::Type *> argTypes;
    std::transform(arguments.begin(), arguments.end(), std::back_inserter(argTypes), [](auto &type) {
        return type->llvmType();
    });
    llvm::Type *retType = returnType->llvmType();

    funcType = llvm::FunctionType::get(retType, argTypes, isVariadic);
}

llvm::Type *FunctionType::llvmType() const {
    return funcType;
}

std::string FunctionType::signature() const {
    std::string argSignatures;
    for (auto i = 0u; i < arguments.size(); i++) {
        if (i != 0) argSignatures += ", ";
        argSignatures += arguments[i]->signature();
    }

    return returnType->signature() + " (" + argSignatures + ")";
}

ArrayType::ArrayType(CodegenContext &context, const LanguageType *targetType, size_t size)
    : LanguageType(context),
      targetType(targetType),
      size(size) {
    decayedType = context.emplaceType<PointerType>(targetType);
}

llvm::Type *ArrayType::llvmType() const { return llvm::ArrayType::get(targetType->llvmType(), size); }

std::string ArrayType::signature() const { return fmt::format("{}[{}]", targetType->signature(), size); }

const PointerType *ArrayType::decay() const {
    return decayedType;
}

llvm::Type *VAType::llvmType() const {
    return llvm::PointerType::get(llvm::Type::getInt8Ty(context.context), 0);
}

std::string VAType::signature() const {
    return VAType::Signature;
}
