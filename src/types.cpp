#include "types.hpp"
#include "codegen_context.hpp"

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

Type* CustomType::llvmType() {
    if (resolved == nullptr) {
        if (!codegenContext.types.contains(name))
            throw CodegenError(fmt::format("Unknown type {}", name));
        resolved = codegenContext.types[name].get();
    }

    return resolved->llvmType();
}

StructType::StructType(CodegenContext &codegenContext, std::string name, const Fields &fields_, bool isPublic)
    : CustomType(codegenContext, name), fields(fields_), isPublic(isPublic) {
    std::vector<Type *> fieldTypes;
    std::transform(fields.begin(), fields.end(), std::back_inserter(fieldTypes), [](auto &type) {
        auto &[_, tp] = type;
        return tp->llvmType();
    });

    structType = llvm::StructType::create(context.context, fieldTypes, name);
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
