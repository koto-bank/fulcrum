#pragma once

#include <expected>
#include <map>

#include <llvm/IR/Module.h>

#include <fmt/format.h>

#include "ast_name_path.hpp"
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
struct UnionNode;
struct VariableDeclarationNode;

namespace llvm {
class Function;
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
             Args &&arguments,
             LanguageType *returnType,
             Body &&body,
             bool isPublic,
             bool isVariadic);

    FunctionType *functionType();
    llvm::Function *llvmFunction();

    bool generateTerminates = false;
    void generateExpressions(ExpressionGenContext &genContext,
                             const std::vector<std::unique_ptr<Expression>> &expressions);

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

struct CodegenContext {
    llvm::LLVMContext &context;
    llvm::Module module;

    std::unordered_map<std::string, std::unique_ptr<LanguageType>> namedTypes;
    std::unordered_map<std::string, std::unique_ptr<VariableDefinition>> globalVars;
    std::unordered_map<std::string, std::unique_ptr<Function>> namedFunctions;

    CodegenContext(std::string moduleName, llvm::LLVMContext &context);

    void generate(FulcrumModule &&fulcrumModule);

    // uh oh, unused
    void generate(CModule &&cModule);

    template <typename T>
    using CodegenResult = std::expected<T *, CodegenError>;

    CodegenResult<LanguageType> getLanguageType(const ASTType *type);
    std::unique_ptr<Expression> getExpression(std::unique_ptr<ASTNode> &&node);

    CodegenResult<StructType> emplaceStructType(StructNode &&);
    CodegenResult<AliasType> emplaceAliasType(TypeAliasNode &&);
    CodegenResult<VariableDefinition> emplaceGlobalVar(VariableDeclarationNode &&);
    CodegenResult<Function> emplaceFulcrumFunction(FunctionNode &&);
    void fillStructTypeFields(StructNode &&);

    CodegenResult<StructType> emplaceCStructType(StructNode &&);
    CodegenResult<AliasType> emplaceCAliasType(TypeAliasNode &&);
    CodegenResult<Function> emplaceCFunction(FunctionNode &&);

    template <typename T, typename ...Args>
    T * emplaceType(const std::string& name, Args &&...args) {
        return static_cast<T *>(namedTypes.emplace(name, std::make_unique<T>(*this, std::forward<Args>(args)...)).first->second.get());
    }
};
