#pragma once

#include <map>

#include "llvm/IR/Function.h"

#include "fmt/format.h"

#include "types.hpp"
#include "expressions.hpp"

struct ASTModuleNode;
struct ExpressionGenContext;
struct CodegenContext;
struct VariableDefinition;

using llvm::LLVMContext;

class Function {
public:
    using Args = std::vector<std::pair<std::string, LanguageType *>>;
    using Body = std::vector<std::unique_ptr<Expression>>;

private:
    llvm::Function *function;

    std::string name;

    std::vector<std::string> argumentNames;
    std::unique_ptr<FunctionType> type;

    std::vector<std::unique_ptr<Expression>> body;
public:
    bool isPublic;

    Function(CodegenContext &context, llvm::Module &module,
             const std::string &name, const Args &arguments,
             LanguageType *returnType, Body &&body,
             bool isPublic);

    const std::string &getName() const { return name; }
    FunctionType *functionType() { return type.get(); }
    llvm::Function *llvmFunction() { return function; }

    bool generateTerminates = false;
    void generateExpressions(ExpressionGenContext &genContext, const std::vector<std::unique_ptr<Expression>> &expressions) {
        std::vector<Expression *> args;
        for (auto &expr : expressions)
            args.push_back(expr.get());
        generateExpressions(genContext, args);
    }
    void generateExpressions(ExpressionGenContext &genContext, std::vector<Expression *> expressions);

    void generateBody(ExpressionGenContext &builder);

    std::string dump();
};

class CodegenError : public std::exception {
protected:
    std::string message;
    mutable std::string indentedMessage;

    std::string indentSpaces(int n) const {
        return fmt::format("{: >{}}", "", n);
    }
public:
    CodegenError(std::string message) : message(message) { }

    const char *what() const noexcept override {
        return whatIndented(0);
    }

    virtual const char *whatIndented(int indent) const {
        indentedMessage = fmt::format("{}{}", indentSpaces(indent), message.data());
        return indentedMessage.data();
    }

    virtual ~CodegenError() { }
};

class StackedCodegenErrors : public CodegenError {
    std::vector<std::unique_ptr<CodegenError>> errors;
    mutable std::string indentedMsg;
public:
    StackedCodegenErrors(std::string message, std::vector<std::unique_ptr<CodegenError>> &&errors)
        : CodegenError(message), errors(std::move(errors)) { }

    const char *what() const noexcept override {
        return whatIndented(0);
    }

    virtual const char *whatIndented(int indent) const override {
        indentedMsg = fmt::format("{}{}\n", indentSpaces(indent), message);
        for (auto &err : errors) {
            indentedMsg += fmt::format("{}\n", err->whatIndented(indent + 2));
        }
        return indentedMsg.data();
    }
};

struct CodegenContext {
    LLVMContext &context;
    llvm::Module module;


    std::map<std::string, Function> functions;
    std::map<std::string, VariableDefinition> globalVariables;
    std::map<std::string, std::unique_ptr<LanguageType>> types;

    CodegenContext(std::string moduleName, LLVMContext &context) : context(context), module(moduleName, context) {
        emplaceType<FloatType>("f32", FloatType::Bits::Float);
        emplaceType<FloatType>("f64", FloatType::Bits::Double);
        emplaceType<VoidType>("void");
        emplaceType<BoolType>("bool");
        emplaceType<IntegerType>("i32", 32, true);
        emplaceType<IntegerType>("u32",  32, false);
        emplaceType<CharType>("char");
    }

    template <typename Type, typename ...Args>
    Type *getOrEmplaceType(const std::string& name, Args &&...args) {
        if (!types.contains(name)) {
            types.emplace(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
        }
        return static_cast<Type *>(types.at(name).get());
    }

    template <typename Type, typename ...Args>
    void ensureType(const std::string& name, Args &&...args) {
        if (!types.contains(name))
            types.emplace(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
    }

    template <typename Type, typename ...Args>
    void emplaceType(const std::string& name, Args &&...args) {
        if (types.contains(name))
            throw CodegenError(fmt::format("Type {} already defined", name));

        assert(!types.contains(name));

        types.emplace(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
    }

    LanguageType *getType(const std::string &&name) const;

    template <typename Expr, typename Type, typename ...Args>
    std::unique_ptr<Expr> makeExpression(Type *type, Args &&...args) {
        return std::make_unique<Expr>(context, type, std::forward<Args>(args)...);
    }

    void emplaceFn(const std::string &name, const Function::Args &args,
                   LanguageType *returnType, Function::Body &&body,
                   bool isPublic) {
        if (functions.contains(name))
            throw CodegenError(fmt::format("Function {} already defined", name));

        functions.emplace(name, Function(*this, module, name, args, returnType,
                                         std::move(body), isPublic));
    }
};
