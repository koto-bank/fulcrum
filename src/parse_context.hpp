#pragma once

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <stdint.h>

#include "is_long_integer.hpp"

// Types

struct ASTType {
    virtual ~ASTType() = default;
};

struct ASTBuiltinType : ASTType {
    const std::string builtinName;

    ASTBuiltinType(const std::string &name);
};

struct ASTIntegerType : ASTBuiltinType {
    const uint32_t bits;
    const bool isSigned;

    ASTIntegerType(bool isSigned, uint32_t bits);
};

struct ASTNamedType : ASTType {
    std::string name;

    ASTNamedType(const std::string &name);
};

struct ASTPointerType : ASTType {
    std::unique_ptr<ASTType> targetType;

    ASTPointerType(std::unique_ptr<ASTType> &&targetType);
};

struct ASTArrayType : ASTType {
    std::unique_ptr<ASTType> targetType;
    size_t size;

    ASTArrayType(std::unique_ptr<ASTType> &&targetType, size_t size);
};

struct ASTFunctionType : ASTType {
    using Args = std::vector<std::unique_ptr<ASTType>>;
    Args arguments;

    std::unique_ptr<ASTType> returnType;

    ASTFunctionType(Args &&arguments, std::unique_ptr<ASTType> &&returnType);
};

// Nodes

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct StructNode : ASTNode {
    using Fields = std::vector<std::tuple<std::string, std::unique_ptr<ASTType>>>;

    std::string name;
    bool isPublic;
    Fields fields;

    StructNode() = default;
    StructNode(std::string name, Fields &&fields, bool isPublic);
};

struct UnionNode : StructNode {
    long long biggestSize = 0;
    using StructNode::StructNode;
};

struct AliasNode : ASTNode {
    std::string name;
    std::unique_ptr<ASTType> target;

    AliasNode(std::string name, std::unique_ptr<ASTType> &&target);
};

using ArgList = std::vector<std::pair<std::string, std::unique_ptr<ASTType>>>;

struct FunctionNode : ASTNode {
    using Body = std::vector<std::unique_ptr<ASTNode>>;

    std::string name;
    bool isPublic;

    ArgList arguments;
    std::unique_ptr<ASTType> returnType;

    Body body;

    FunctionNode() = default;
    FunctionNode(std::string name, ArgList &&arguments, std::unique_ptr<ASTType> &&returnType, Body &&body, bool isPublic);
};

struct ConstantStringNode : ASTNode {
    std::string value;

    ConstantStringNode(std::string value);
};

struct ConstantIntNode : ASTNode {
    std::unique_ptr<ASTType> intType;
    std::variant<uint64_t, int64_t> value;
    bool isSigned;

    ConstantIntNode(std::unique_ptr<ASTType> &&intType, IsLongInteger auto constValue);
};

struct ConstantBoolNode : ASTNode {
    bool value;

    ConstantBoolNode(bool value);
};

struct FunctionCallNode : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> args;

    FunctionCallNode(std::string name);
};

struct VarAccessNode : ASTNode {
    std::string name;

    VarAccessNode(std::string name);
};

struct DereferenceNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    DereferenceNode(std::unique_ptr<ASTNode> &&target);
};

struct VarDeclarationNode : ASTNode {
    std::unique_ptr<ASTNode> initialValue = nullptr;

    std::unique_ptr<ASTType> type;
    const std::string name;

    VarDeclarationNode(const std::string &name, std::unique_ptr<ASTType> &&type);
};

struct SizeofNode : ASTNode {
    std::unique_ptr<ASTType> targetType;

    SizeofNode(std::unique_ptr<ASTType> &&targetType);
};

struct CastNode : ASTNode {
    std::unique_ptr<ASTType> targetType;
    std::unique_ptr<ASTNode> targetExpression;

    CastNode(std::unique_ptr<ASTNode> &&target, std::unique_ptr<ASTType> &&targetType);
};

struct ModuleNode : ASTNode {
    std::string name;

    struct Import {
        std::string target; // C header or Fulcrum module name
        std::vector<std::string> keywords;
    };
    std::vector<Import> imports;

    using ImportedNames = std::map<std::string, std::string>;
    ImportedNames importedNames;

    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::vector<std::unique_ptr<StructNode>> structs;
    std::vector<std::unique_ptr<AliasNode>> aliases;
    std::vector<std::unique_ptr<VarDeclarationNode>> globalVariables;

    ModuleNode(const std::string &name);

    std::string resolveName(const std::string &name) const;
    std::map<std::string, std::string> allNames();
    void importName(const std::string &baseName, const std::string &fullName);
};
