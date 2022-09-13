#include <llvm/IR/Function.h>

#include <fmt/format.h>

#include "codegen_context.hpp"
#include "expressions.hpp"

using llvm::LLVMContext;

Function::Function(CodegenContext &context, llvm::Module &module, const std::string &name, const Args &arguments, LanguageType *returnType, Body &&body, bool isPublic)
    : name(name),
      body(std::move(body)),
      isPublic(isPublic) {
    std::vector<LanguageType *> argumentTypes;
    for (auto &&[nm, tp] : arguments) {
        argumentNames.push_back(nm);
        argumentTypes.push_back(tp);
    }
    type = std::make_unique<FunctionType>(context, argumentTypes, returnType);

    auto funcType = (llvm::FunctionType *)type->llvmType();
    function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);

    for (auto i = 0u; i < function->arg_size(); i++)
        function->getArg(i)->setName(argumentNames[i]);
}

const std::string &Function::getName() const { return name; }
FunctionType *Function::functionType() { return type.get(); }
llvm::Function *Function::llvmFunction() { return function; }

void Function::generateExpressions(ExpressionGenContext &genContext, const std::vector<std::unique_ptr<Expression>> &expressions) {
    std::vector<Expression *> args;
    for (auto &expr : expressions)
        args.push_back(expr.get());
    generateExpressions(genContext, args);
}

void Function::generateBody(ExpressionGenContext &genContext) {
    if (body.size() == 0) return;

    auto &context = genContext.codegenContext;
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context.context, "enter", function);
    genContext.builder.SetInsertPoint(bb);

    // Insert variables for arguments
    genContext.pushScope();
    for (auto i = 0u; i < argumentNames.size(); i++) {
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

std::string Function::dump() {
    std::vector<std::string> argumentDumps, expressionDumps;
    for (auto i = 0u; i < function->arg_size(); i++)
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

CodegenError::CodegenError(std::string message)
    : message(message) {}

std::string CodegenError::indentSpaces(int n) const {
    return fmt::format("{: >{}}", "", n);
}

const char *CodegenError::what() const noexcept {
    return whatIndented(0);
}

const char *CodegenError::whatIndented(int indent) const {
    indentedMessage = fmt::format("{}{}", indentSpaces(indent), message.data());
    return indentedMessage.data();
}

StackedCodegenErrors::StackedCodegenErrors(std::string message, std::vector<std::unique_ptr<CodegenError>> &&errors)
    : CodegenError(message),
      errors(std::move(errors)) {}

const char *StackedCodegenErrors::what() const noexcept {
    return whatIndented(0);
}

const char *StackedCodegenErrors::whatIndented(int indent) const {
    indentedMsg = fmt::format("{}{}\n", indentSpaces(indent), message);
    for (auto &err : errors) {
        indentedMsg += fmt::format("{}\n", err->whatIndented(indent + 2));
    }
    return indentedMsg.data();
}

CodegenContext::CodegenContext(std::string moduleName, llvm::LLVMContext &context)
    : context(context),
      module(moduleName, context) {
    emplaceType<FloatType>("f32", FloatType::Bits::Float);
    emplaceType<FloatType>("f64", FloatType::Bits::Double);
    emplaceType<VoidType>("void");
    emplaceType<BoolType>("bool");
    emplaceType<IntegerType>("i32", 32, true);
    emplaceType<IntegerType>("u32", 32, false);
    emplaceType<CharType>("char");
}

LanguageType *CodegenContext::getType(const std::string &&name) const {
    if (types.contains(name)) {
        return types.at(name).get();
    }

    return nullptr;
}

void CodegenContext::emplaceFn(const std::string &name, const Function::Args &args, LanguageType *returnType, Function::Body &&body, bool isPublic) {
    if (functions.contains(name))
        throw CodegenError(fmt::format("Function {} already defined", name));

    functions.emplace(name, Function(*this, module, name, args, returnType, std::move(body), isPublic));
}
