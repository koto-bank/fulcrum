#pragma once

#include <map>

#include "llvm/IR/Function.h"

#include "types.hpp"

struct Expression;
struct ExpressionGenContext;
struct CodegenContext;

class Function {
    CodegenContext &context;
    llvm::Module &module;
    llvm::Function *function;

    std::unique_ptr<FunctionType> type;
    std::string name;
    std::vector<std::string> argumentNames;
    std::vector<std::unique_ptr<Expression>> body;
public:
    bool isPublic;

    using Args = std::vector<std::tuple<std::string, LanguageType *>>;
    using Body = std::vector<std::unique_ptr<Expression>>;

    Function(CodegenContext &context, llvm::Module &module,
             const std::string &name, const Args &arguments,
             LanguageType *returnType, Body &&body,
             bool isPublic);

    const std::string &getName() const { return name; }
    FunctionType *functionType() { return type.get(); }
    llvm::Function *llvmFunction() { return function; }


    bool generateExpressions(ExpressionGenContext &genContext, const std::vector<std::unique_ptr<Expression>> &expressions) {
        std::vector<Expression *> args;
        for (auto &expr : expressions)
            args.push_back(expr.get());
        return generateExpressions(genContext, args);
    }
    // Returns true if the function returned early because of "return"
    bool generateExpressions(ExpressionGenContext &genContext, std::vector<Expression *> expressions);
    void generateBody(ExpressionGenContext &builder);

    std::string dump();
};

struct Import {
    std::string target; // C header or Fulcrum module name
    std::vector<std::string> keywords;
};

struct CodegenContext {
    LLVMContext &context;
    llvm::Module &module;

    std::string moduleName;
    std::vector<Import> imports;

    template <typename Type, typename ...Args>
    Type *getOrEmplaceType(const std::string& name, Args &&...args) {
        if (!types.contains(name)) {
            types.emplace(name, std::make_unique<Type>(context, std::forward<Args>(args)...));
        }
        return static_cast<Type *>(types.at(name).get());
    }

    template <typename Type, typename ...Args>
    void emplaceType(const std::string& name, Args &&...args) {
        assert(!types.contains(name));
        types.emplace(name, std::make_unique<Type>(context, std::forward<Args>(args)...));
    }

    template <typename Expr, typename Type, typename ...Args>
    std::unique_ptr<Expr> makeExpression(Type *type, Args &&...args) {
        return std::make_unique<Expr>(context, type, std::forward<Args>(args)...);
    }

    LanguageType *getType(const std::string &&name) const;

    std::map<std::string, std::unique_ptr<LanguageType>> types;
    std::map<std::string, std::unique_ptr<CustomType>> unresolvedCustomTypes;

    void emplaceFn(const std::string &name, const Function::Args &args,
                   LanguageType *returnType, Function::Body &&body,
                   bool isPublic) {
        functions.emplace(name, Function(*this, module, name, args, returnType,
                                         std::move(body), isPublic));
    }

    std::map<std::string, Function> functions;
};
