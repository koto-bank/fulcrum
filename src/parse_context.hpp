#pragma once

#include <string>
#include <vector>
#include <iostream>

#include <stdint.h>

#include "codegen_context.hpp"
#include "expressions.hpp"

union ParsedLine {
    long iNum;
    double fNum;
    char *str;
    bool boolConst;
};

// Types

struct ASTType {
    virtual ~ASTType() {}

    virtual LanguageType *languageType(CodegenContext &context) = 0;
};

struct ASTBuiltinType : ASTType {
    std::string builtinName;

    ASTBuiltinType(std::string name) : builtinName(name) { }

    LanguageType *languageType(CodegenContext &context) override {
        assert(context.types.contains(builtinName));

        return context.types.at(builtinName).get();
    };
};

struct ASTNamedType : ASTType {
    std::string name;

    ASTNamedType(std::string name) : name(name) { }

    LanguageType *languageType(CodegenContext &context) override {
        if (!context.types.contains(name))
            throw CodegenError(fmt::format("Unknown named type {}", name));

        return context.types.at(name).get();
    };
};

struct ASTPointerType : ASTType {
    std::unique_ptr<ASTType> targetType;

    ASTPointerType(std::unique_ptr<ASTType> &&targetType) : targetType(std::move(targetType)) { }

    LanguageType *languageType(CodegenContext &context) override {
        auto targetLangType = targetType->languageType(context);

        auto pointeeName = targetLangType->signature();
        auto ptrName = pointeeName + "*";
        return context.getOrEmplaceType<PointerType>(ptrName, targetLangType);
    };
};

struct ASTArrayType : ASTType {
    std::unique_ptr<ASTType> targetType;
    size_t size;

    ASTArrayType(std::unique_ptr<ASTType> &&targetType, size_t size) : targetType(std::move(targetType)), size(size) { }

    LanguageType *languageType(CodegenContext &context) override {
        auto targetLangType = targetType->languageType(context);

        auto pointeeName = targetLangType->signature();
        auto arrName = fmt::format("{}[{}]", pointeeName, size);
        return context.getOrEmplaceType<ArrayType>(pointeeName, targetLangType, size);
    };
};

// Nodes

struct ASTNode {
    virtual ~ASTNode() {}

    virtual std::unique_ptr<Expression> expression(CodegenContext &context) = 0;
};

struct StructNode : ASTNode {
    std::string name;
    bool isPublic;
    std::vector<std::tuple<std::string, std::unique_ptr<ASTType>>> fields;

    StructNode() = default;

    std::unique_ptr<Expression> expression(CodegenContext &context) override { return nullptr; }

    void emplaceStructType(CodegenContext &context) {
        StructType::Fields exprFields;
        for (auto &[name, astType] : fields)
            exprFields.emplace_back(name, astType->languageType(context));

        context.emplaceType<StructType>(name, name, exprFields, isPublic);
    }
};

struct FunctionNode : ASTNode {
    using Args = std::vector<std::pair<std::string, std::unique_ptr<ASTType>>>;
    using Body = std::vector<std::unique_ptr<ASTNode>>;

    bool isPublic;
    std::string name;

    Args arguments;
    std::unique_ptr<ASTType> returnType;

    Body body;

    FunctionNode() = default;

    std::unique_ptr<Expression> expression(CodegenContext &context) override { return nullptr; }

    void emplaceFunction(CodegenContext &context) {
        Function::Args exprArgs;
        for (auto &[name, astType] : arguments)
            exprArgs.emplace_back(name, astType->languageType(context));
        Function::Body exprBody;
        for (auto &node : body)
            exprBody.push_back(node->expression(context));

        context.emplaceFn(name, exprArgs, returnType->languageType(context),
                          std::move(exprBody), isPublic);
    }
};

struct ConstantStringNode : ASTNode {
    std::string value;

    ConstantStringNode(std::string value) : value(value) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<StringConstant>(context.getType("str"), value);
    }
};

struct ConstantIntNode : ASTNode {
    ASTBuiltinType intType;
    std::variant<uint64_t, int64_t> value;
    bool isSigned;

    ConstantIntNode(ASTBuiltinType intType, IsLongInteger auto constValue_) : intType(intType), value(constValue_) {
        isSigned = std::holds_alternative<int64_t>(value);
    }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        auto tp = (IntegerType*)intType.languageType(context);
        return isSigned
            ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(value))
            : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(value));
    }
};

struct ConstantBoolNode : ASTNode {
    bool value;

    ConstantBoolNode(bool value) : value(value) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<BoolConstant>(context.getType("bool"), value);
    }
};

struct FunctionCallNode : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> args;

    FunctionCallNode(std::string name) : name(name) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        FunctionCall::Args argsExprs;
        for (auto &arg : args)
            argsExprs.push_back(arg->expression(context));

        return std::make_unique<FunctionCall>(name, std::move(argsExprs));
    }
};

struct VarAccessNode : ASTNode {
    std::string name;

    VarAccessNode(std::string name) : name(name) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<VarAccess>(name);
    }
};

struct AddrOfNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    AddrOfNode(std::unique_ptr<ASTNode> &&target) : target(std::move(target)) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<AddrOf>(target->expression(context));
    }
};

struct DereferenceNode : ASTNode {
    std::unique_ptr<ASTNode> target;

    DereferenceNode() = default;

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<Dereference>(target->expression(context));
    }
};

struct VariableDeclarationNode : ASTNode {
    std::unique_ptr<ASTNode> initialValue = nullptr;
    std::unique_ptr<ASTType> type;
    std::string name;

    VariableDeclarationNode(std::string name) : name(name) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<VariableDeclaration>(
            name, type->languageType(context),
            initialValue ? initialValue->expression(context) : nullptr
        );
    }
};

struct SizeofNode : ASTNode {
    std::unique_ptr<ASTType> targetType;

    SizeofNode(std::unique_ptr<ASTType> &&targetType) : targetType(std::move(targetType)) { }

    std::unique_ptr<Expression> expression(CodegenContext &context) override {
        return std::make_unique<Sizeof>(context, targetType->languageType(context));
    }
};

struct ModuleNode : ASTNode {
    std::string name;

    struct Import {
        std::string target; // C header or Fulcrum module name
        std::vector<std::string> keywords;
    };
    std::vector<Import> imports;
    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::vector<std::unique_ptr<StructNode>> structs;

    ModuleNode(std::string name) : name(name) { };

    std::unique_ptr<Expression> expression(CodegenContext &context) override { return nullptr; }
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

    bool isSigned; // for integer types and literals

    IntLiteral intLiteral;

    size_t arraySize;
};

bool parse(CodegenContext *);
