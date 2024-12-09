#pragma once

#include <fmt/format.h>

#include <string>
#include <tuple>
#include <vector>

#include "ast_name_path.hpp"

namespace llvm {
class FunctionType;
class StructType;
class Type;
} // namespace llvm

struct CodegenContext;

struct LanguageType {
protected:
    CodegenContext &context;

public:
    // Used for allocating variables of the type
    virtual llvm::Type *llvmType() const = 0;

    virtual std::string signature() const = 0;


    LanguageType(CodegenContext &context);

    virtual ~LanguageType() = default;

    static bool same(const LanguageType *type, const LanguageType *other);
    static bool assignable(const LanguageType *to, const LanguageType *from);
};

struct IntegerType : LanguageType {
    unsigned int bits;
    bool isSigned;

    IntegerType(CodegenContext &context, unsigned int bits, bool isSigned);

    llvm::Type *llvmType() const override;
    std::string signature() const override;
};

struct FloatType : LanguageType {
    enum class Bits { Float, Double };
    Bits bits;

    FloatType(CodegenContext &context, Bits bits);

    llvm::Type *llvmType() const override;
    std::string signature() const override;
};

struct AliasType : LanguageType {
    NamePath name;
    const LanguageType *targetType;
    const LanguageType *type;

    AliasType(CodegenContext &codegenContext, const NamePath &name, const LanguageType *targetType);

    llvm::Type *llvmType() const override;

    std::string signature() const override;
};

struct StructType : LanguageType {
protected:
    llvm::StructType *structType = nullptr;

public:
    NamePath name;

    struct Field {
        std::string name;
        const LanguageType *type;
    };
    using Fields = std::vector<Field>;
    Fields fields;
    bool isPublic;

    StructType(CodegenContext &codegenContext, const NamePath &name, bool isPublic);

    virtual void fillFields(const Fields &fields_);

    llvm::Type *llvmType() const override;
    std::string signature() const override;

    int32_t fieldIndex(const std::string &fieldName) const;
};

struct UnionType : StructType {
    UnionType(CodegenContext &codegenContext, const NamePath &name, long long biggestSize);

    llvm::Type *llvmType() const override;
};

struct PointerType : LanguageType {
    const LanguageType *targetType;
    const LanguageType *type;

    PointerType(CodegenContext &context, const LanguageType *targetType);

    llvm::Type *llvmType() const override;
    std::string signature() const override;

};

struct VoidType : LanguageType {
    using LanguageType::LanguageType;

    llvm::Type *llvmType() const override;

    std::string signature() const override;
};

struct BoolType : LanguageType {
    using LanguageType::LanguageType;

    llvm::Type *llvmType() const override;

    std::string signature() const override;
};

struct FunctionType : LanguageType {
private:
    llvm::FunctionType *funcType = nullptr;

public:
    std::vector<const LanguageType *> arguments;
    const LanguageType *returnType;
    bool isVariadic;

    FunctionType(CodegenContext &context, std::vector<const LanguageType *> args, const LanguageType *returnType, bool isVariadic);

    llvm::Type *llvmType() const override;
    std::string signature() const override;
};

struct ArrayType : LanguageType {
    const LanguageType *targetType;
    const PointerType *decayedType;
    size_t size;

    ArrayType(CodegenContext &context, const LanguageType *targetType, size_t size);

    const PointerType *decay() const;

    llvm::Type *llvmType() const override;
    std::string signature() const override;
};

struct VAType : LanguageType {
    using LanguageType::LanguageType;

    llvm::Type *llvmType() const override;
    std::string signature() const override;

    constexpr static auto Signature = "(va-list)";
};
