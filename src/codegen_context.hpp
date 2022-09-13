#pragma once

#include <filesystem>
#include <map>

#include <fmt/format.h>

#include "expressions.hpp"
#include "types.hpp"

struct ASTModuleNode;
struct ExpressionGenContext;
struct CodegenContext;
struct VariableDefinition;

namespace llvm {
class Function;
class Module;
} // namespace llvm

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

    Function(CodegenContext &context, llvm::Module &module, const std::string &name, const Args &arguments, LanguageType *returnType, Body &&body, bool isPublic);

    const std::string &getName() const;
    FunctionType *functionType();
    llvm::Function *llvmFunction();

    bool generateTerminates = false;
    void generateExpressions(ExpressionGenContext &genContext, const std::vector<std::unique_ptr<Expression>> &expressions);
    void generateExpressions(ExpressionGenContext &genContext, std::vector<Expression *> expressions);

    void generateBody(ExpressionGenContext &builder);

    std::string dump();
};

class CodegenError : public std::exception {
protected:
    std::string message;
    mutable std::string indentedMessage;

    std::string indentSpaces(int n) const;

public:
    CodegenError(std::string message);
    const char *what() const noexcept override;
    virtual const char *whatIndented(int indent) const;
};

class StackedCodegenErrors : public CodegenError {
    std::vector<std::unique_ptr<CodegenError>> errors;
    mutable std::string indentedMsg;

public:
    StackedCodegenErrors(std::string message, std::vector<std::unique_ptr<CodegenError>> &&errors);

    const char *what() const noexcept override;

    const char *whatIndented(int indent) const override;
};

struct CodegenContext {
    llvm::LLVMContext &context;
    llvm::Module module;


    std::map<std::string, Function> functions;
    std::map<std::string, VariableDefinition> globalVariables;
    std::map<std::string, std::unique_ptr<LanguageType>> types;

    std::vector<std::filesystem::path> includeDirectories;

    CodegenContext(std::string moduleName, llvm::LLVMContext &context);

    template<typename Type, typename... Args>
    Type *getOrEmplaceType(const std::string &name, Args &&...args) {
        if (!types.contains(name)) {
            types.emplace(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
        }
        return static_cast<Type *>(types.at(name).get());
    }

    template<typename Type, typename... Args>
    void ensureType(const std::string &name, Args &&...args) {
        if (!types.contains(name))
            types.emplace(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
    }

    template<typename Type, typename... Args>
    void emplaceType(const std::string &name, Args &&...args) {
        if (types.contains(name))
            throw CodegenError(fmt::format("Type {} already defined", name));

        assert(!types.contains(name));

        types.emplace(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
    }

    LanguageType *getType(const std::string &&name) const;

    template<typename Expr, typename Type, typename... Args>
    std::unique_ptr<Expr> makeExpression(Type *type, Args &&...args) {
        return std::make_unique<Expr>(context, type, std::forward<Args>(args)...);
    }

    void emplaceFn(const std::string &langName, const std::string &funcName, const Function::Args &args, LanguageType *returnType, Function::Body &&body, bool isPublic);
};
