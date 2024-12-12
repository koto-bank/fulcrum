#pragma once

#include <fmt/format.h>

#include <string>
#include <tuple>
#include <vector>

namespace llvm {
class DataLayout;
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
    std::string name;
    const LanguageType *targetType;
    const LanguageType *type;

    AliasType(CodegenContext &codegenContext, const std::string &name, const LanguageType *targetType);

    llvm::Type *llvmType() const override;

    std::string signature() const override;
};

struct StructType : LanguageType {
protected:
    llvm::StructType *structType = nullptr;

public:
    std::string name;

    struct Field {
        std::string name;
        const LanguageType *type;
    };
    using Fields = std::vector<Field>;
    Fields fields;
    bool isPublic;

    StructType(CodegenContext &codegenContext, const std::string &name, bool isPublic);

    virtual bool fillFields(const Fields &fields, const llvm::DataLayout &);

    llvm::Type *llvmType() const override;
    std::string signature() const override;

    const LanguageType *fieldType(const std::string &fieldName) const;
    int32_t fieldIndex(const std::string &fieldName) const;
};

struct UnionType : StructType {
    using StructType::StructType;

    bool fillFields(const Fields &fields, const llvm::DataLayout &) override;
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
