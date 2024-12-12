#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <stdint.h>

#include "is_long_integer.hpp"

// Fulcrum types and expressions.

struct ASTType {
    virtual std::string signature() const = 0;
    virtual ~ASTType();
};

struct ASTBuiltinType : ASTType {
    const std::string builtinName;

    std::string signature() const override;

    ASTBuiltinType(const std::string &name);
};

struct ASTIntegerType : ASTBuiltinType {
    const uint32_t bits;
    const bool isSigned;

    std::string signature() const override;

    ASTIntegerType(bool isSigned, uint32_t bits);
};

struct ASTFloatType : ASTBuiltinType {
    const uint32_t bits;

    std::string signature() const override;

    ASTFloatType(uint32_t bits);
};

struct ASTBoolType : ASTBuiltinType {
    std::string signature() const override;

    ASTBoolType();
};

struct ASTVoidType : ASTBuiltinType {
    std::string signature() const override;

    ASTVoidType();
};

struct ASTNamedType : ASTType {
    std::string name;

    std::string signature() const override;

    ASTNamedType(const std::string &name);
};

struct ASTPointerType : ASTType {
    ASTType *targetType;

    std::string signature() const override;

    ASTPointerType(ASTType *targetType);
};

struct ASTArrayType : ASTType {
    ASTType *targetType;
    size_t size;

    std::string signature() const override;

    ASTArrayType(ASTType *targetType, size_t size);
};

struct ASTFunctionType : ASTType {
    using ArgTypes = std::vector<ASTType *>;
    ArgTypes argumentTypes;

    ASTType *returnType;

    std::string signature() const override;

    ASTFunctionType(ArgTypes &&arguments, ASTType *returnType);
};

// Nodes

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct StructNode : ASTNode {
    struct Field {
        std::string name;
        const ASTType *type;
    };
    using Fields = std::vector<Field>;

    std::string name;
    bool isUnion = false;
    bool isPublic;
    Fields fields;

    StructNode() = default;
    StructNode(const std::string &name, Fields &&fields, bool isPublic);
};

struct EnumNode : ASTNode {
    std::string name;
    // TODO: how and where store values? separate case for C?
};

struct TypeAliasNode : ASTNode {
    std::string name;
    ASTType *target;

    TypeAliasNode(const std::string &name, ASTType *target);
};

using ArgList = std::vector<std::pair<std::string, ASTType *>>;

struct FunctionNode : ASTNode {
    using Body = std::vector<std::unique_ptr<ASTNode>>;

    std::string name;
    bool isPublic;

    ArgList arguments;
    ASTType *returnType;

    Body body;

    bool isVariadic;

    FunctionNode(const std::string &name, ArgList &&arguments, ASTType *returnType,
                 Body &&body, bool isPublic, bool isVariadic);

    std::string signature() const;
};

struct ConstantStringNode : ASTNode {
    std::string value;

    ConstantStringNode(std::string value);
};

struct ConstantIntNode : ASTNode {
    ASTType *intType;
    std::variant<uint64_t, int64_t> value;
    bool isSigned;

    ConstantIntNode(ASTType *intType, IsLongInteger auto constValue);
};

struct ConstantFloatNode : ASTNode {
    ASTType *floatType;
    double value;

    ConstantFloatNode(ASTType *floatType, double constValue);
};

struct ConstantBoolNode : ASTNode {
    bool value;

    ConstantBoolNode(bool value);
};

struct FunctionCallNode : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> args;

    FunctionCallNode(const std::string &name);
};

struct SymbolNode : ASTNode {
    std::string name;

    SymbolNode(const std::string &name);
};

struct DereferenceNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    DereferenceNode(std::unique_ptr<ASTNode> &&target);
};

struct AtNode : ASTNode {
    std::unique_ptr<ASTNode> target;
    std::unique_ptr<ASTNode> subscript;

    AtNode(std::unique_ptr<ASTNode> &&target, std::unique_ptr<ASTNode> &&subscript);
};

struct VariableDeclarationNode : ASTNode {
    ASTType *type;
    std::string name;

    VariableDeclarationNode(const std::string &name, ASTType *type);

    std::unique_ptr<ASTNode> initialValue;
};

struct SizeofNode : ASTNode {
    ASTType *targetType;

    SizeofNode(ASTType *targetType);
};

struct CastNode : ASTNode {
    ASTType *targetType;
    std::unique_ptr<ASTNode> targetExpression;

    CastNode(std::unique_ptr<ASTNode> &&target, ASTType *targetType);
};
