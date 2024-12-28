#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/TypedPointerType.h>
#include <llvm/IR/Type.h>

#include <spdlog/spdlog.h>

#include "assert.hpp"
#include "codegen_context.hpp"
#include "types.hpp"

LanguageType::LanguageType(CodegenContext &context)
    : context(context) {}

bool LanguageType::same(const LanguageType *type, const LanguageType *other) {
    if (auto at = dynamic_cast<const AliasType *>(type); at != nullptr) {
        return same(at->targetType, other);
    }

    if (auto at = dynamic_cast<const AliasType *>(other); at != nullptr) {
        return same(type, at->targetType);
    }

    return type->signature() == other->signature();
}

bool LanguageType::assignable(const LanguageType *to, const LanguageType *from) {
    // Fuck it, let's have this whole check in one place, instead of all over the types
    // _this_ kind of polymorphic behavior doesn't seem right for some reason

    // 1. This method is only for primitive types, i.e. numeric types, pointers, arrays
    // 2. One of the purposes of this method is to extract underlying type from type alias
    // 3. CV-correctness is checked as in C
    // 4. This is very low-level and should not leak

    // void type is unassignable
    if (dynamic_cast<const VoidType *>(to) != nullptr
        || dynamic_cast<const VoidType *>(from) != nullptr) {
        return false;
    }

    // we can assign from array to pointer, if their underlying types are the same...
    auto ptTo = dynamic_cast<const PointerType *>(to);
    auto atFrom = dynamic_cast<const ArrayType *>(from);
    if (ptTo != nullptr && atFrom != nullptr) {
        return same(ptTo->targetType, atFrom->targetType);
    }

    // ...the same when both are pointers...
    auto ptFrom = dynamic_cast<const PointerType *>(from);
    if (ptTo != nullptr && ptFrom != nullptr) {
        return same(ptTo->targetType, ptFrom->targetType);
    }

    // ...and when both are arrays. Can't assign from pointer to array though
    auto atTo = dynamic_cast<const ArrayType *>(to);
    if (atTo != nullptr && atFrom != nullptr) {
        return same(atTo->targetType, atFrom->targetType);
    }

    // Integer types
    auto itTo = dynamic_cast<const IntegerType *>(to);
    auto itFrom = dynamic_cast<const IntegerType *>(from);
    if (itTo != nullptr && itFrom != nullptr) {
        // can assign to wider type
        return itTo->bits >= itFrom->bits;
    }

    // Can assign same to same, of course
    return same(to, from);
}

IntegerType::IntegerType(CodegenContext &context, unsigned int bits, bool isSigned)
    : LanguageType(context),
      bits(bits),
      isSigned(isSigned) {}

llvm::Type *IntegerType::llvmType() const { return llvm::Type::getIntNTy(context.context, bits); }

std::string IntegerType::signature() const { return std::string(isSigned ? "i" : "u") + std::to_string(bits); }

FloatType::FloatType(CodegenContext &context, Bits bits)
    : LanguageType(context),
      bits(bits) {}

std::string FloatType::signature() const {
   switch (bits) {
   case Bits::Half:
       return "f16";
   case Bits::Float:
       return "f32";
   case Bits::Double:
       return "f64";
   case Bits::Quad:
       return "f128";
   }
}

llvm::Type *FloatType::llvmType() const {
    switch (bits) {
    case Bits::Half:
        return llvm::Type::getHalfTy(context.context);
    case Bits::Float:
        return llvm::Type::getFloatTy(context.context);
    case Bits::Double:
        return llvm::Type::getDoubleTy(context.context);
    case Bits::Quad:
        return llvm::Type::getFP128Ty(context.context);
    }
}

AliasType::AliasType(CodegenContext &codegenContext, const std::string &name, const LanguageType *targetType)
    : LanguageType(codegenContext),
      name(name),
      targetType(targetType) {
}

llvm::Type *AliasType::llvmType() const { return targetType->llvmType(); }

std::string AliasType::signature() const { return fmt::format("{}", name); }

StructType::StructType(CodegenContext &codegenContext, const std::string &name, bool isPublic)
    : LanguageType(codegenContext),
      name(name),
      isPublic(isPublic) {}

bool StructType::fillFields(const Fields &newFields, const llvm::DataLayout &) {
    fc_assert(structType == nullptr);

    fields = newFields;

    std::vector<llvm::Type *> fieldTypes;
    std::transform(fields.begin(), fields.end(), std::back_inserter(fieldTypes), [](auto &field) {
        auto tp = field.type;
        return tp->llvmType();
    });

    structType = llvm::StructType::create(context.context, fieldTypes, name);
    return true;
}

const LanguageType *StructType::fieldType(const std::string &fieldName) const {
    auto fieldIter
        = std::find_if(fields.begin(), fields.end(), [&fieldName](const StructType::Fields::value_type &elem) {
              return elem.name == fieldName;
          });
    if (fieldIter == fields.end()) {
        throw CodegenError(fmt::format("No field named {} in type {}", fieldName, signature()));
    }
    return fieldIter->type;
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

std::string StructType::signature() const {
    return name;
}

llvm::Type *StructType::llvmType() const {
    return structType;
}

bool UnionType::fillFields(const Fields &newFields, const llvm::DataLayout &dl) {
    fc_assert(structType == nullptr);

    fields = newFields;

    llvm::Type *biggestType = nullptr;
    size_t biggestSize;
    for (auto& field : fields) {
        auto t = field.type->llvmType();
        if (t == nullptr) {
            spdlog::info("Failed to get llvm type for type {} of {}.{}",
                                           field.type->signature(),
                                           name,
                                           field.name);
            return false;
        }
        if (biggestType == nullptr) {
            biggestType = t;
            biggestSize = dl.getTypeAllocSize(t);
        } else if (auto s = dl.getTypeAllocSize(t); s > biggestSize) {
            biggestType = t;
            biggestSize = s;
        }
    }

    std::vector<llvm::Type *> types;
    if (biggestType != nullptr) {
        types.push_back(biggestType);
    }

    structType = llvm::StructType::create(context.context, types, name);
    return true;
}

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
