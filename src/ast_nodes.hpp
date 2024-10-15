#pragma once

#include <unordered_set>
#include <string>
#include <variant>
#include <vector>

#include <stdint.h>

#include "is_long_integer.hpp"

// Fulcrum types and expressions.

struct ASTType {
    virtual ~ASTType() = default;
};

struct ASTBuiltinType : ASTType {
    const std::string builtinName;

    ASTBuiltinType(std::string &&name);
};

struct ASTIntegerType : ASTBuiltinType {
    const uint32_t bits;
    const bool isSigned;

    ASTIntegerType(bool isSigned, uint32_t bits);
};

struct ASTFloatType : ASTBuiltinType {
    const uint32_t bits;

    ASTFloatType(uint32_t bits);
};

struct ASTNamedType : ASTType {
    const std::string name;

    ASTNamedType(std::string &&name);
};

struct ASTPointerType : ASTType {
    ASTType *targetType;

    ASTPointerType(ASTType *targetType);
};

struct ASTArrayType : ASTType {
    ASTType *targetType;
    size_t size;

    ASTArrayType(ASTType *targetType, size_t size);
};

struct ASTFunctionType : ASTType {
    using ArgTypes = std::vector<ASTType *>;
    ArgTypes argumentTypes;

    ASTType *returnType;

    ASTFunctionType(ArgTypes &&arguments, ASTType *returnType);
};

// TODO: move to separate header?
struct ASTTypeStorage {
    template <typename T, typename ...Args>
    ASTType *getType(Args  &&...args) {
        return allTypes.emplace(std::make_unique<T>(std::forward<Args>(args)...))
            .first->get();
    }

private:
    std::unordered_set<std::unique_ptr<ASTType>> allTypes;
};

// Nodes

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct StructNode : ASTNode {
    using Fields = std::vector<std::tuple<std::string, ASTType *>>;

    std::string name;
    bool isPublic;
    Fields fields;

    StructNode() = default;
    StructNode(std::string&& name, Fields &&fields, bool isPublic);
};

struct UnionNode : StructNode {
    long long biggestSize = 0;
    using StructNode::StructNode;
};

struct TypeAliasNode : ASTNode {
    std::string name;
    ASTType *target;

    TypeAliasNode(std::string &&name, ASTType *target);
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

    FunctionNode(std::string &&name, ArgList &&arguments, ASTType *returnType,
                 Body &&body, bool isPublic, bool isVariadic);
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

    FunctionCallNode(std::string&& name);
};

struct VariableAccessNode : ASTNode {
    std::string name;

    VariableAccessNode(std::string&& name);
};

struct DereferenceNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    DereferenceNode(std::unique_ptr<ASTNode> &&target);
};

struct ArrayNthNode : ASTNode {
    std::unique_ptr<ASTNode> array;
    std::unique_ptr<ASTNode> subscript;

    ArrayNthNode(std::unique_ptr<ASTNode> &&array, std::unique_ptr<ASTNode> &&subscript);
};

struct VariableDeclarationNode : ASTNode {
    ASTType *type;
    std::string name;

    VariableDeclarationNode(std::string &&name, ASTType *type);

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
