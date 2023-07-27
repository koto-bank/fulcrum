#pragma once

#include <filesystem>
#include <map>

#include <fmt/format.h>

#include "expressions.hpp"
#include "types.hpp"

struct AliasNode;
struct ASTModuleNode;
struct ASTType;
struct ASTNode;
struct CodegenContext;
struct CModule;
struct ExpressionGenContext;
struct FulcrumModule;
struct FunctionNode;
struct StructNode;
struct VarDeclarationNode;
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

    Function(CodegenContext &context,
             llvm::Module &module,
             const std::string &name,
             const Args &arguments,
             LanguageType *returnType,
             Body &&body,
             bool isPublic);

    const std::string &getName() const;
    FunctionType *functionType();
    llvm::Function *llvmFunction();

    bool generateTerminates = false;
    void generateExpressions(ExpressionGenContext &genContext,
                             const std::vector<std::unique_ptr<Expression>> &expressions);
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

    std::vector<std::filesystem::path> includeDirectories;

    CodegenContext(std::string moduleName, llvm::LLVMContext &context);

    bool existsNamed(const std::string &name) const;

    void generate(std::unique_ptr<FulcrumModule> &&fulcrumModule);
    void generate(std::unique_ptr<CModule> &&cModule);

    using NameResolver = std::function<std::string(const std::string &)>;
    LanguageType *getLanguageType(NameResolver resolveNameFn, const std::unique_ptr<ASTType> &type);
    std::unique_ptr<Expression> getExpression(NameResolver resolveNameFn, const std::unique_ptr<ASTNode> &node);

    void emplaceStructType(NameResolver, const std::unique_ptr<StructNode> &);
    void emplaceAliasType(NameResolver, const std::unique_ptr<AliasNode> &);
    void emplaceGlobalVar(NameResolver, const std::unique_ptr<VarDeclarationNode> &);
    void emplaceFulcrumFunction(NameResolver, const std::unique_ptr<FunctionNode> &);
    void fillStructTypeFields(NameResolver, const std::unique_ptr<StructNode> &);

    void emplaceCStructType(NameResolver, const std::unique_ptr<StructNode> &);
    void emplaceCAliasType(NameResolver, const std::unique_ptr<AliasNode> &);
    void emplaceCFunction(NameResolver, const std::unique_ptr<FunctionNode> &);


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
            throw CodegenError(fmt::format("A {} named {} is already defined", T::namedType(), name));
        else
            throw CodegenError(
                fmt::format("Name {} is already defined as a {}", name, maybeResultValue->valueNamedType())
            );
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
