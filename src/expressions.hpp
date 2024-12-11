#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include <llvm/IR/IRBuilder.h>

#include "ast_name_path.hpp"
#include "is_long_integer.hpp"

namespace llvm {
class Type;
class Value;
} // namespace llvm

struct CodegenContext;
struct Function;
struct LanguageType;
struct IntegerType;
struct FloatType;

struct VariableDefinition {
    std::string name;
    const LanguageType *type;
    llvm::Value *value;

    VariableDefinition(const std::string &name, const LanguageType *type);
};

struct ExpressionGenContext {
    llvm::IRBuilder<> &builder;
    Function *function = nullptr;

    using VariableScope = std::map<std::string, VariableDefinition>;
    std::vector<VariableScope> variableScopes = {};
    CodegenContext &codegenContext;

    const VariableDefinition *lookupVariable(const std::string &name) const;
    VariableDefinition *insertVariable(const std::string &name, const LanguageType *type);
    VariableDefinition *insertFunctionArgument(const std::string &name, const LanguageType *type);

    void pushScope();
    void popScope();
};

struct Expression {
protected:
    llvm::Value *value = nullptr;

    std::string indentSpaces(int n) const;

    const LanguageType *type = nullptr;
    void assumeExpression(ExpressionGenContext &genContext, Expression *expr, const std::string &errorMessage);

public:
    Expression(const LanguageType *type);

    virtual const LanguageType *languageType(const ExpressionGenContext &genContext);
    virtual llvm::Type *llvmType(ExpressionGenContext &genContext);
    virtual llvm::Value *llvmValue(ExpressionGenContext &genContext);
    virtual bool isTerminator();

    virtual std::string dump(int indent = 0) const = 0;

    virtual ~Expression() = default;
};

struct ConstantExpression : Expression {
    using Expression::Expression;

    virtual llvm::Constant *llvmConstant(CodegenContext &context);
};

struct IntegerConstant : ConstantExpression {
    std::variant<uint64_t, int64_t> constValue;

    IntegerConstant(const IntegerType *type, IsLongInteger auto _constValue);

    std::string dump(int indent) const override;
};

template<typename T>
concept IsFloatingPoint = std::same_as<T, float> || std::same_as<T, double>;

struct FloatConstant : ConstantExpression {
    std::variant<float, double> constValue;

    FloatConstant(const FloatType *type, IsFloatingPoint auto constValue_);

    std::string dump(int indent) const override;
};

struct StringConstant : ConstantExpression {
private:
    llvm::GlobalVariable *llvmConst = nullptr;

public:
    std::string constValue;

    StringConstant(const LanguageType *type, const std::string &constValue);

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    llvm::Constant *llvmConstant(CodegenContext &context) override;
    std::string dump(int indent) const override;
};

struct BoolConstant : ConstantExpression {
public:
    bool constValue;

    BoolConstant(const LanguageType *type, bool constValue);

    std::string dump(int indent) const override;
};

struct FunctionCall : Expression {
private:
    llvm::Value *returnProcessor(ExpressionGenContext &genContext);
    llvm::Value *doProcessor(ExpressionGenContext &genContext);
    llvm::Value *ifProcessor(ExpressionGenContext &genContext);
    llvm::Value *arithmeticsProcessor(ExpressionGenContext &genContext);
    llvm::Value *ptrArithmeticsProcessor(ExpressionGenContext &genContext);
    llvm::Value *setProcessor(ExpressionGenContext &genContext);
    llvm::Value *whileProcessor(ExpressionGenContext &genContext);
    llvm::Value *boolProcessor(ExpressionGenContext &genContext);
    llvm::Value *addrOfProcessor(ExpressionGenContext &genContext);
    llvm::Value *prognValue(ExpressionGenContext &genContext);

    const LanguageType *arithmeticsProcessorType(const ExpressionGenContext &genContext) const;
    const LanguageType *ptrArithmeticsProcessorType(const ExpressionGenContext &genContext) const;
    const LanguageType *voidProcessorType(const ExpressionGenContext &genContext) const;
    const LanguageType *boolProcessorType(const ExpressionGenContext &genContext) const;
    const LanguageType *addrOfProcessorType(const ExpressionGenContext &genContext) const;
    const LanguageType *prognType(const ExpressionGenContext &genContext) const;

public:
    using Args = std::vector<std::unique_ptr<Expression>>;

    std::string name;
    Args args;

    FunctionCall(const std::string &name, Args &&args);

    using SpecialFunctionProcessor = std::function<llvm::Value *(FunctionCall *, ExpressionGenContext &)>;
    using SpecialFunctionTyping = std::function<const LanguageType *(FunctionCall *, const ExpressionGenContext &)>;

    const std::map<std::string, std::pair<SpecialFunctionProcessor, SpecialFunctionTyping>> specialFunctions {
        { "return", { &FunctionCall::returnProcessor, &FunctionCall::voidProcessorType } },
        { "if", { &FunctionCall::ifProcessor, &FunctionCall::voidProcessorType } },
        { "do", { &FunctionCall::doProcessor, &FunctionCall::voidProcessorType } },
        { "set", { &FunctionCall::setProcessor, &FunctionCall::voidProcessorType } },
        { "while", { &FunctionCall::whileProcessor, &FunctionCall::voidProcessorType } },
        { "addr-of", { &FunctionCall::addrOfProcessor, &FunctionCall::addrOfProcessorType } },

        { "not", { &FunctionCall::boolProcessor, &FunctionCall::boolProcessorType } },
        { "and", { &FunctionCall::boolProcessor, &FunctionCall::boolProcessorType } },
        { "or", { &FunctionCall::boolProcessor, &FunctionCall::boolProcessorType } },
        { "xor", { &FunctionCall::boolProcessor, &FunctionCall::boolProcessorType } },

        { "ptr+", { &FunctionCall::ptrArithmeticsProcessor, &FunctionCall::ptrArithmeticsProcessorType } },
        { "ptr-", { &FunctionCall::ptrArithmeticsProcessor, &FunctionCall::ptrArithmeticsProcessorType } },

        { "+", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "-", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "*", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "/", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "%", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "=", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "!=", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { ">", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { ">=", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "<", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },
        { "<=", { &FunctionCall::arithmeticsProcessor, &FunctionCall::arithmeticsProcessorType } },

        { "progn", { &FunctionCall::prognValue, &FunctionCall::prognType } },
    };

    bool isTerminator() override;
    const LanguageType *languageType(const ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) const override;
};

struct VariableAccess : Expression {
public:
    NamePath name;

    VariableAccess(const NamePath &name);
    const LanguageType *languageType(const ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) const override;

    llvm::Value *varAddress(ExpressionGenContext &genContext);
};

struct Dereference : Expression {
    std::unique_ptr<Expression> target;

    Dereference(std::unique_ptr<Expression> &&target);
    const LanguageType *languageType(const ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) const override;
};

struct Subscription : Expression {
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> subscript;
    const LanguageType *targetType = nullptr;

    Subscription(std::unique_ptr<Expression> &&array, std::unique_ptr<Expression> &&subscript);

    llvm::Value *getElementPtr(ExpressionGenContext &genContext);

    const LanguageType *languageType(const ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) const override;
};

struct VariableDeclaration : Expression {
    std::unique_ptr<Expression> initialValue = nullptr;

    std::string name;

    VariableDeclaration(const std::string &name, const LanguageType *type, std::unique_ptr<Expression> &&initialValue);
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    llvm::Type *llvmType(ExpressionGenContext &genContext) override;

    std::string dump(int indent) const override;
};

struct Sizeof : Expression {
    const LanguageType *targetType;

    Sizeof(CodegenContext &context, const LanguageType *targetType);

    std::string dump(int indent) const override;
};

struct Cast : Expression {
    std::unique_ptr<Expression> targetExpression;

    Cast(CodegenContext &context, const LanguageType *targetType, std::unique_ptr<Expression> &&targetExpression);

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) const override;
};
