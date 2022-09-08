#include <fmt/format.h>

#include "codegen_context.hpp"
#include "expressions.hpp"

Function::Function(CodegenContext &context, llvm::Module &module,
         const std::string &name, const Args &arguments,
         LanguageType *returnType, Body &&body,
         bool isPublic)
    : name(name), body(std::move(body)), isPublic(isPublic) {
    std::vector<LanguageType *> argumentTypes;
    for (auto &&[nm, tp] : arguments) {
        argumentNames.push_back(nm);
        argumentTypes.push_back(tp);
    }
    type = std::make_unique<FunctionType>(context, argumentTypes, returnType);

    auto funcType = (llvm::FunctionType*)type->llvmType();
    function =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);

    for (auto i = 0; i < function->arg_size(); i++)
        function->getArg(i)->setName(argumentNames[i]);
}

std::string Function::dump() {
        std::vector<std::string> argumentDumps, expressionDumps;
        for (auto i = 0; i < function->arg_size(); i++)
            argumentDumps.push_back(
                fmt::format("({} {})", argumentNames[i], type->arguments[i]->signature())
            );

        for (auto &expr : body) {
            // Indent by 4
            expressionDumps.push_back(expr->dump(4));
        }

        return fmt::format(
            "({} {} {} ({})\n{})",
            isPublic ? "fn" : "fn-",
            name,
            type->returnType->signature(),
            fmt::join(argumentDumps, " "),
            fmt::join(expressionDumps, "\n")
        );
}

LanguageType *CodegenContext::getType(const std::string &&name) const {
    if (types.contains(name)) {
        return types.at(name).get();
    }

    return nullptr;
}

void Function::generateBody(ExpressionGenContext &genContext) {
    if (body.size() == 0) return;

    auto &context = genContext.codegenContext;
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context.context, "enter", function);
    genContext.builder.SetInsertPoint(bb);

    // Insert variables for arguments
    genContext.pushScope();
    for (auto i = 0; i < argumentNames.size(); i++) {
        auto varDef = genContext.insertVariable(argumentNames[i], functionType()->arguments[i]);
        genContext.variableSet(varDef, function->getArg(i));
    }

    generateExpressions(genContext, body);

    genContext.popScope();

    if (genContext.function->llvmFunction()->back().getTerminator() == nullptr) {
        // If the function is not void, insert unreachable at the end, since the user must return something
        if (genContext.function->functionType()->returnType != context.getType("void")) {
            genContext.builder.CreateUnreachable();
        } else {
            // Otherwise, return void automatically
            genContext.builder.CreateRetVoid();
        }
    }
}

void Function::generateExpressions(ExpressionGenContext &genContext, std::vector<Expression *> expressions) {
    for (auto &expr : expressions) {
        expr->llvmValue(genContext);
        if (expr->isTerminator())
            return;
    }
}
