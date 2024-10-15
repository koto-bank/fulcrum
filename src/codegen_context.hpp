#pragma once

#include <filesystem>
#include <map>

#include <fmt/format.h>

#include "expressions.hpp"
#include "types.hpp"

struct ASTModuleNode;
struct ASTType;
struct ASTNode;
struct CodegenContext;
struct CModule;
struct ExpressionGenContext;
struct FulcrumModule;
struct FunctionNode;
struct StructNode;
struct TypeAliasNode;
struct VariableDeclarationNode;

namespace llvm {
class Function;
class Module;
} // namespace llvm

struct Function {
    struct Arg {
        std::string name;
        LanguageType *type;
    };

    using Args = std::vector<Arg>;
    using Body = std::vector<std::unique_ptr<Expression>>;

    bool isPublic;

    Function(CodegenContext &context,
             llvm::Module &module,
             const std::string &name,
             const Args &arguments,
             LanguageType *returnType,
             Body &&body,
             bool isPublic,
             bool isVariadic);

    const std::string &getName() const;
    FunctionType *functionType();
    llvm::Function *llvmFunction();

    bool generateTerminates = false;
    void generateExpressions(ExpressionGenContext &genContext,
                             const std::vector<std::unique_ptr<Expression>> &expressions);
    void generateExpressions(ExpressionGenContext &genContext, std::vector<Expression *> expressions);

    void generateBody(ExpressionGenContext &builder);

    std::string dump();

private:
    llvm::Function *function;

    std::string name;

    std::vector<std::string> argumentNames;
    std::unique_ptr<FunctionType> type;

    std::vector<std::unique_ptr<Expression>> body;
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
    StackedCodegenErrors(const std::string &message, std::vector<std::unique_ptr<CodegenError>> &&errors);

    const char *what() const noexcept override;

    const char *whatIndented(int indent) const override;
};

struct NamedValue {
    virtual ~NamedValue() = default;

    virtual std::string valueNamedType() const = 0;

    template<typename T> T *as() { return dynamic_cast<T *>(this); }
};

struct NamedFunctionValue : NamedValue {
    using ValueType = Function;
    std::unique_ptr<ValueType> value;

    static std::string namedType();
    std::string valueNamedType() const override;

    template<typename... Args> NamedFunctionValue(Args &&...args) {
        value = std::make_unique<ValueType>(std::forward<Args>(args)...);
    }
};

struct NamedVariableValue : NamedValue {
    using ValueType = VariableDefinition;

    std::unique_ptr<ValueType> value;

    static std::string namedType();
    std::string valueNamedType() const override;

    NamedVariableValue(VariableDefinition varDef);
};

struct NamedTypeValue : NamedValue {
    using ValueType = LanguageType;
    std::unique_ptr<ValueType> value;

    static std::string namedType();
    std::string valueNamedType() const override;

    NamedTypeValue(std::unique_ptr<ValueType> &&value);
};

struct CodegenContext {
    llvm::LLVMContext &context;
    llvm::Module module;

    std::map<std::string, std::unique_ptr<NamedValue>> names;

    CodegenContext(std::string moduleName, llvm::LLVMContext &context);

    bool existsNamed(const std::string &name) const;

    void generate(FulcrumModule &&fulcrumModule);
    void generate(CModule &&cModule);

    LanguageType *getLanguageType(const ASTType *type);
    std::unique_ptr<Expression> getExpression(std::unique_ptr<ASTNode> &&node);

    void emplaceStructType(StructNode &&);
    void emplaceAliasType(TypeAliasNode &&);
    void emplaceGlobalVar(VariableDeclarationNode &&);
    void emplaceFulcrumFunction(FunctionNode &&);
    void fillStructTypeFields(StructNode &&);

    void emplaceCStructType(StructNode &&);
    void emplaceCAliasType(TypeAliasNode &&);
    void emplaceCFunction(FunctionNode &&);

    template<typename T> typename T::ValueType *getNamed(const std::string &name) const {
        if (!names.contains(name)) throw CodegenError(fmt::format("Undefined {}: {}", T::namedType(), name));
        auto namedValue = names.at(name).get();
        auto maybeResultValue = dynamic_cast<T *>(namedValue);
        if (maybeResultValue == nullptr)
            throw CodegenError(
                fmt::format("Name {} is defined as a {}, not a {}", name, namedValue->valueNamedType(), T::namedType())
            );

        return maybeResultValue->value.get();
    }

    template<typename T> void assumeNamedDoesNotExist(const std::string &name) {
        if (!names.contains(name)) return;

        auto namedValue = names[name].get();
        auto maybeResultValue = dynamic_cast<T *>(namedValue);
        if (maybeResultValue == nullptr)
            throw CodegenError(
                fmt::format("Name {} is already defined as a {}, trying to define it as {}",
                            name,
                            namedValue->valueNamedType(),
                            T::namedType())
            );
        else
            throw CodegenError(fmt::format("A {} named {} is already defined", T::namedType(), name));
    }

    template<typename T, typename... Args> void emplaceNamed(const std::string &name, Args &&...args) {
        assumeNamedDoesNotExist<T>(name);

        names.emplace(name, std::make_unique<T>(std::forward<Args>(args)...));
    }

    template<typename Type, typename... Args> Type *getOrEmplaceType(const std::string &name, Args &&...args) {
        if (!existsNamed(name))
            emplaceNamed<NamedTypeValue>(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
        return static_cast<Type *>(getNamed<NamedTypeValue>(name));
    }

    template<typename Type, typename... Args> void ensureType(const std::string &name, Args &&...args) {
        if (!existsNamed(name))
            emplaceNamed<NamedTypeValue>(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
    }

    template<typename Type, typename... Args> void emplaceType(const std::string &name, Args &&...args) {
        emplaceNamed<NamedTypeValue>(name, std::make_unique<Type>(*this, std::forward<Args>(args)...));
    }

    template<typename Expr, typename Type, typename... Args>
    std::unique_ptr<Expr> makeExpression(Type *type, Args &&...args) {
        return std::make_unique<Expr>(context, type, std::forward<Args>(args)...);
    }
};
