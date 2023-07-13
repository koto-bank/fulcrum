#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

#include <llvm/IR/IRBuilder.h>

namespace llvm {
class Type;
class Value;
} // namespace llvm

struct CodegenContext;
class Function;
struct LanguageType;
struct IntegerType;
struct FloatType;

struct VariableDefinition {
    std::string name;
    LanguageType *type;
    llvm::Value *value;

    VariableDefinition(const std::string &name, LanguageType *type);
};

struct ExpressionGenContext {
    llvm::IRBuilder<> &builder;
    Function *function = nullptr;

    using VariableScope = std::map<std::string, VariableDefinition>;
    std::vector<VariableScope> variableScopes = {};
    CodegenContext &codegenContext;

    VariableDefinition *lookupVariable(const std::string &name);
    VariableDefinition *insertVariable(const std::string &name, LanguageType *type);

    void pushScope();
    void popScope();
};

struct Expression {
protected:
    llvm::Value *value = nullptr;

    std::string indentSpaces(int n);

    LanguageType *type = nullptr;

    void assumeExpression(ExpressionGenContext &genContext, Expression *expr, const std::string &errorMessage);

public:
    Expression(LanguageType *type);

    virtual LanguageType *languageType(ExpressionGenContext &genContext);
    virtual llvm::Type *llvmType(ExpressionGenContext &genContext);
    virtual llvm::Value *llvmValue(ExpressionGenContext &genContext);
    virtual bool isTerminator();

    virtual std::string dump(int indent = 0) = 0;

    virtual ~Expression() = default;
};

struct ConstantExpression : Expression {
    using Expression::Expression;

    virtual llvm::Constant *llvmConstant(CodegenContext &context);
};

template<typename T>
concept IsLongInteger = std::same_as<T, uint64_t> || std::same_as<T, int64_t>;

struct IntegerConstant : ConstantExpression {
    std::variant<uint64_t, int64_t> constValue;

    IntegerConstant(IntegerType *type, IsLongInteger auto _constValue);

    std::string dump(int indent) override;
};

template<typename T>
concept IsFloatingPoint = std::same_as<T, float> || std::same_as<T, double>;

struct FloatConstant : ConstantExpression {
    std::variant<float, double> constValue;

    FloatConstant(LanguageType *type, IsFloatingPoint auto constValue_);

    std::string dump(int indent) override;
};

struct StringConstant : ConstantExpression {
private:
    llvm::GlobalVariable *llvmConst = nullptr;

public:
    std::string constValue;

    StringConstant(LanguageType *type, const std::string &constValue);

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    llvm::Constant *llvmConstant(CodegenContext &context) override;
    std::string dump(int indent) override;
};

struct BoolConstant : ConstantExpression {
public:
    bool constValue;

    BoolConstant(LanguageType *type, bool constValue);

    std::string dump(int indent) override;
};

struct FunctionCall : Expression {
private:
    llvm::Value *returnProcessor(ExpressionGenContext &genContext);
    llvm::Value *doProcessor(ExpressionGenContext &genContext);
    llvm::Value *ifProcessor(ExpressionGenContext &genContext);
    llvm::Value *arithmeticsProcessor(ExpressionGenContext &genContext);
    llvm::Value *setProcessor(ExpressionGenContext &genContext);
    llvm::Value *whileProcessor(ExpressionGenContext &genContext);
    llvm::Value *notProcessor(ExpressionGenContext &genContext);

    LanguageType *arithmeticsProcessorType(ExpressionGenContext &genContext);
    LanguageType *voidProcessorType(ExpressionGenContext &genContext);
    LanguageType *notProcessorType(ExpressionGenContext &genContext);

public:
    using Args = std::vector<std::unique_ptr<Expression>>;

    std::string name;
    Args args;

    FunctionCall(const std::string &name, Args &&args);

    using SpecialFunctionProcessor = std::function<llvm::Value *(FunctionCall *, ExpressionGenContext &)>;
    using SpecialFunctionTyping = std::function<LanguageType *(FunctionCall *, ExpressionGenContext &)>;

    std::map<std::string, std::pair<SpecialFunctionProcessor, SpecialFunctionTyping>> specialFunctions{
        { "return", { &FunctionCall::returnProcessor, &FunctionCall::voidProcessorType } },
        { "if", { &FunctionCall::ifProcessor, &FunctionCall::voidProcessorType } },
        { "do", { &FunctionCall::doProcessor, &FunctionCall::voidProcessorType } },
        { "set", { &FunctionCall::setProcessor, &FunctionCall::voidProcessorType } },
        { "while", { &FunctionCall::whileProcessor, &FunctionCall::voidProcessorType } },
        { "not", { &FunctionCall::notProcessor, &FunctionCall::notProcessorType } },

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
    };

    bool isTerminator() override;

    LanguageType *languageType(ExpressionGenContext &genContext) override;

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;

    std::string dump(int indent) override;
};

struct VarAccess : Expression {
private:
    std::vector<std::string> varPath;

public:
    std::string name;

    VarAccess(const std::string &name);
    LanguageType *languageType(ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;

    const std::vector<std::string> &path();
    llvm::Value *varAddress(ExpressionGenContext &genContext);
};

struct AddrOf : Expression {
private:
    std::unique_ptr<Expression> target;

public:
    AddrOf(std::unique_ptr<Expression> &&target);
    LanguageType *languageType(ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;
};

struct Dereference : Expression {
    std::unique_ptr<Expression> target;

    Dereference(std::unique_ptr<Expression> &&target);
    LanguageType *languageType(ExpressionGenContext &genContext) override;
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;
};

struct VariableDeclaration : Expression {
    std::unique_ptr<Expression> initialValue = nullptr;

    std::string name;

    VariableDeclaration(const std::string &name, LanguageType *type, std::unique_ptr<Expression> &&initialValue);
    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    llvm::Type *llvmType(ExpressionGenContext &genContext) override;

    std::string dump(int indent) override;
};

struct Sizeof : Expression {
    LanguageType *targetType;

    Sizeof(CodegenContext &context, LanguageType *targetType);

    std::string dump(int indent) override;
};

struct Cast : Expression {
    std::unique_ptr<Expression> targetExpression;

    Cast(CodegenContext &context, LanguageType *targetType, std::unique_ptr<Expression> &&targetExpression);

    llvm::Value *llvmValue(ExpressionGenContext &genContext) override;
    std::string dump(int indent) override;
};
