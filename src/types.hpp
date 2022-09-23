#pragma once

#include <fmt/format.h>

#include <string>
#include <tuple>
#include <vector>

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
    virtual llvm::Type *llvmType() = 0;
    virtual std::string signature() = 0;
    virtual LanguageType *actualLanguageType();

    LanguageType(CodegenContext &context);

    virtual ~LanguageType() = default;
};

struct IntegerType : LanguageType {
    unsigned int bits;
    bool isSigned;

    IntegerType(CodegenContext &context, unsigned int bits, bool isSigned);

    llvm::Type *llvmType() override;
    std::string signature() override;
};

struct FloatType : LanguageType {
    enum class Bits { Float, Double };
    Bits bits;

    FloatType(CodegenContext &context, Bits bits);

    llvm::Type *llvmType() override;
    std::string signature() override;
};

struct StringType : LanguageType {
    StringType(CodegenContext &context);

    llvm::Type *llvmType() override;
    std::string signature() override;
};

struct AliasType : LanguageType {
    std::string name;
    LanguageType *aliasTo;

    AliasType(CodegenContext &codegenContext, std::string name, LanguageType *aliasTo);

    llvm::Type *llvmType() override;
    LanguageType *actualLanguageType() override;

    std::string signature() override;
};

struct StructType : LanguageType {
protected:
    llvm::StructType *structType = nullptr;

public:
    std::string name;

    using Fields = std::vector<std::tuple<std::string, LanguageType *>>;
    Fields fields;
    bool isPublic;

    StructType(CodegenContext &codegenContext, std::string name, bool isPublic);

    void fillFields(const Fields &fields_);

    llvm::Type *llvmType() override;
    std::string signature() override;

    int fieldIndex(const std::string &fieldName);
};

struct UnionType : StructType {
    UnionType(CodegenContext &codegenContext, std::string name);

    llvm::Type *llvmType() override;
};

struct CharType : LanguageType {
    using LanguageType::LanguageType;

    llvm::Type *llvmType() override;
    std::string signature() override;
};

struct PointerType : LanguageType {
    LanguageType *pointerTo;

    PointerType(CodegenContext &context, LanguageType *pointerTo);

    llvm::Type *llvmType() override;
    std::string signature() override;
    LanguageType *actualLanguageType() override;
};

struct VoidType : LanguageType {
    using LanguageType::LanguageType;

    llvm::Type *llvmType() override;

    std::string signature() override;
};

struct BoolType : LanguageType {
    using LanguageType::LanguageType;

    llvm::Type *llvmType() override;

    std::string signature() override;
};

struct FunctionType : LanguageType {
private:
    llvm::FunctionType *funcType = nullptr;

public:
    std::vector<LanguageType *> arguments;
    LanguageType *returnType;

    FunctionType(CodegenContext &context, std::vector<LanguageType *> args, LanguageType *returnType);

    llvm::Type *llvmType() override;
    std::string signature() override;

    static std::string signatureFrom(const std::vector<LanguageType *> &arguments, LanguageType *returnType);
};

struct ArrayType : LanguageType {
private:
    LanguageType *targetType;
    size_t size;

public:
    ArrayType(CodegenContext &context, LanguageType *targetType, size_t size);

    llvm::Type *llvmType() override;
    std::string signature() override;
};
