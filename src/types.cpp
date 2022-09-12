#include "types.hpp"
#include "codegen_context.hpp"

using llvm::Type;

Type* IntegerType::llvmType() {
    return Type::getIntNTy(context.context, bits);
}

Type* FloatType::llvmType() {
    return bits == Bits::Float
        ? Type::getFloatTy(context.context)
        : Type::getDoubleTy(context.context);
}

Type* StringType::llvmType() {
    return Type::getIntNPtrTy(context.context, 8);
}

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

Type *CharType::llvmType() {
    return Type::getInt8Ty(context.context);
}

Type *VoidType::llvmType() {
    return Type::getVoidTy(context.context);
}

Type *BoolType::llvmType() {
    return Type::getInt1Ty(context.context);
}
