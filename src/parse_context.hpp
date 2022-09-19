#pragma once

#include <iostream>
#include <string>
#include <vector>

#include <stdint.h>

#include "codegen_context.hpp"
#include "expressions.hpp"

union ParsedLine {
    long iNum;
    double fNum;
    char *str;
    bool boolConst;
};

struct ModuleNode;

// Types

struct ASTType {
    virtual ~ASTType() {}

    virtual LanguageType *languageType(ModuleNode *module, CodegenContext &context) = 0;
};

struct ASTBuiltinType : ASTType {
    std::string builtinName;

    ASTBuiltinType(std::string name);

    LanguageType *languageType(ModuleNode *module, CodegenContext &context) override;
};

struct ASTNamedType : ASTType {
    std::string name;

    ASTNamedType(std::string name);

    LanguageType *languageType(ModuleNode *module, CodegenContext &context) override;
};

struct ASTPointerType : ASTType {
    std::unique_ptr<ASTType> targetType;

    ASTPointerType(std::unique_ptr<ASTType> &&targetType);

    LanguageType *languageType(ModuleNode *module, CodegenContext &context) override;
};

struct ASTArrayType : ASTType {
    std::unique_ptr<ASTType> targetType;
    size_t size;

    ASTArrayType(std::unique_ptr<ASTType> &&targetType, size_t size);

    LanguageType *languageType(ModuleNode *module, CodegenContext &context) override;
};

struct ASTFunctionType : ASTType {
    using Args = std::vector<std::unique_ptr<ASTType>>;
    Args arguments;

    std::unique_ptr<ASTType> returnType;

    ASTFunctionType(Args &&arguments, std::unique_ptr<ASTType> &&returnType);

    LanguageType *languageType(ModuleNode *module, CodegenContext &context) override;
};

// Nodes

struct ASTNode {
    virtual ~ASTNode() = default;

    virtual std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) = 0;
};

struct StructNode : ASTNode {
    using Fields = std::vector<std::tuple<std::string, std::unique_ptr<ASTType>>>;

    std::string name;
    bool isPublic;
    Fields fields;

    StructNode() = default;
    StructNode(std::string name, Fields &&fields, bool isPublic);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;

    void emplaceStructType(ModuleNode *module, CodegenContext &context);
    void fillStructTypeFields(ModuleNode *module, CodegenContext &context);
};

struct AliasNode : ASTNode {
    std::string name;
    std::unique_ptr<ASTType> target;

    AliasNode(std::string name, std::unique_ptr<ASTType> &&target);
    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;

    void emplaceAliasType(ModuleNode *module, CodegenContext &context);
};

struct FunctionNode : ASTNode {
    using Args = std::vector<std::pair<std::string, std::unique_ptr<ASTType>>>;
    using Body = std::vector<std::unique_ptr<ASTNode>>;

    std::string name;
    bool isPublic;

    Args arguments;
    std::unique_ptr<ASTType> returnType;

    Body body;

    FunctionNode() = default;
    FunctionNode(std::string name, Args &&arguments, std::unique_ptr<ASTType> &&returnType, Body &&body, bool isPublic);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
    void emplaceFunction(ModuleNode *module, CodegenContext &context);
};

struct ConstantStringNode : ASTNode {
    std::string value;

    ConstantStringNode(std::string value);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct ConstantIntNode : ASTNode {
    ASTBuiltinType intType;
    std::variant<uint64_t, int64_t> value;
    bool isSigned;

    ConstantIntNode(ASTBuiltinType intType, IsLongInteger auto constValue);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct ConstantBoolNode : ASTNode {
    bool value;

    ConstantBoolNode(bool value);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct FunctionCallNode : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> args;

    FunctionCallNode(std::string name);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct VarAccessNode : ASTNode {
    std::string name;

    VarAccessNode(std::string name);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct AddrOfNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    AddrOfNode(std::unique_ptr<ASTNode> &&target);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct DereferenceNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    DereferenceNode() = default;

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct VariableDeclarationNode : ASTNode {
    std::unique_ptr<ASTNode> initialValue = nullptr;
    std::unique_ptr<ASTType> type;
    std::string name;

    VariableDeclarationNode(std::string name);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
    void emplaceGlobalVar(ModuleNode *module, CodegenContext &context);
};

struct SizeofNode : ASTNode {
    std::unique_ptr<ASTType> targetType;

    SizeofNode(std::unique_ptr<ASTType> &&targetType);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;
};

struct ModuleNode : ASTNode {
    std::string name;

    struct Import {
        std::string target; // C header or Fulcrum module name
        std::vector<std::string> keywords;
    };
    std::vector<Import> imports;
    std::map<std::string, std::string> importedNames;

    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::vector<std::unique_ptr<StructNode>> structs;
    std::vector<std::unique_ptr<AliasNode>> aliases;
    std::vector<std::unique_ptr<VariableDeclarationNode>> globalVariables;

    ModuleNode(std::string name);

    std::unique_ptr<Expression> expression(ModuleNode *module, CodegenContext &context) override;

    void generate(CodegenContext &codegenContext);

    std::map<std::string, std::string> allNames();
    void importName(const std::string &baseName, const std::string &fullName);
};

struct ParseContext {
    std::unique_ptr<ModuleNode> module;

    struct IntLiteral {
        union {
            uint64_t u;
            int64_t i;
        } num;
        std::string str;
        std::string bits;
        bool isSigned;
    };

    std::unique_ptr<ModuleNode::Import> import;
    std::unique_ptr<FunctionNode> fnDef;
    std::unique_ptr<FunctionCallNode> fnCall;

    std::unique_ptr<ASTType> type = nullptr;
    std::unique_ptr<StructNode> structDef;
    std::vector<std::unique_ptr<ASTNode>> exprStack;

    std::string structFieldName;
    bool isSigned; // for integer types and literals

    IntLiteral intLiteral;

    size_t arraySize;
};

std::unique_ptr<ModuleNode> parse(CodegenContext *, std::istream *input);

std::string resolveName(ModuleNode *module, const std::string &name);
