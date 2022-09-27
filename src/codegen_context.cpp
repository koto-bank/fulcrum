#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include <fmt/format.h>

#include "codegen_context.hpp"
#include "expressions.hpp"
#include "parse_context.hpp"
#include "parse_context_macro.h"

using llvm::LLVMContext;

Function::Function(
    CodegenContext &context, llvm::Module &module, const std::string &name_, const Args &arguments,
    LanguageType *returnType, Body &&body, bool isPublic
)
    : name(name_),
      body(std::move(body)),
      isPublic(isPublic) {
    std::vector<LanguageType *> argumentTypes;
    for (auto &&[nm, tp] : arguments) {
        argumentNames.push_back(nm);
        argumentTypes.push_back(tp);
    }
    type = std::make_unique<FunctionType>(context, argumentTypes, returnType);

    std::replace(name.begin(), name.end(), '/', '_');

    auto funcType = (llvm::FunctionType *)type->llvmType();
    function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);

    for (auto i = 0u; i < function->arg_size(); i++)
        function->getArg(i)->setName(argumentNames[i]);
}

const std::string &Function::getName() const { return name; }
FunctionType *Function::functionType() { return type.get(); }
llvm::Function *Function::llvmFunction() { return function; }
void Function::setLLVMFunction(llvm::Function *function) { this->function = function; }

void Function::generateExpressions(
    ExpressionGenContext &genContext, const std::vector<std::unique_ptr<Expression>> &expressions
) {
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

    if (name.find("macro-") != std::string::npos) {
        auto functionArg = function->getArg(0);
        for (auto i = 0u; i < argumentNames.size(); i++) {
            auto argType = functionType()->arguments[i];
            auto varDef = genContext.insertVariable(argumentNames[i], argType);

            auto N = llvm::ConstantInt::get(llvm::Type::getInt32Ty(type->llvmType()->getContext()), i);

            auto argPointer = genContext.builder.CreateGEP(functionArg->getType(), functionArg, { N });
            auto loadArg = genContext.builder.CreateLoad(argType->llvmType(), argPointer);

            genContext.builder.CreateStore(loadArg, varDef->value);
        }
    } else {
        for (auto i = 0u; i < argumentNames.size(); i++) {
            auto varDef = genContext.insertVariable(argumentNames[i], functionType()->arguments[i]);
            genContext.builder.CreateStore(function->getArg(i), varDef->value);
        }
    }

    generateExpressions(genContext, body);

    genContext.popScope();

    if (genContext.function->llvmFunction()->back().getTerminator() == nullptr) {
        // If the function is not void, insert unreachable at the end, since the user must return
        // something
        if (genContext.function->functionType()->returnType != context.getNamed<NamedTypeValue>("void")) {
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
        if (expr->isTerminator()) return;
    }
}

std::string Function::dump() {
    std::vector<std::string> argumentDumps, expressionDumps;
    for (auto i = 0u; i < function->arg_size(); i++)
        argumentDumps.push_back(fmt::format("({} {})", argumentNames[i], type->arguments[i]->signature()));

    for (auto &expr : body) {
        // Indent by 4
        expressionDumps.push_back(expr->dump(4));
    }

    return fmt::format(
        "({} {} {} ({})\n{})", isPublic ? "fn" : "fn-", name, type->returnType->signature(),
        fmt::join(argumentDumps, " "), fmt::join(expressionDumps, "\n")
    );
}

CodegenError::CodegenError(std::string message)
    : message(message) {}

std::string CodegenError::indentSpaces(int n) const { return fmt::format("{: >{}}", "", n); }

const char *CodegenError::what() const noexcept { return whatIndented(0); }

const char *CodegenError::whatIndented(int indent) const {
    indentedMessage = fmt::format("{}{}", indentSpaces(indent), message.data());
    return indentedMessage.data();
}

StackedCodegenErrors::StackedCodegenErrors(std::string message, std::vector<std::unique_ptr<CodegenError>> &&errors)
    : CodegenError(message),
      errors(std::move(errors)) {}

const char *StackedCodegenErrors::what() const noexcept { return whatIndented(0); }

const char *StackedCodegenErrors::whatIndented(int indent) const {
    indentedMsg = fmt::format("{}{}\n", indentSpaces(indent), message);
    for (auto &err : errors) {
        indentedMsg += fmt::format("{}\n", err->whatIndented(indent + 2));
    }
    return indentedMsg.data();
}

CodegenContext::CodegenContext(std::string moduleName, llvm::LLVMContext &context)
    : context(context),
      module(std::make_unique<llvm::Module>(moduleName, context)) {
    {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        auto targetTriple = llvm::sys::getDefaultTargetTriple();
        std::string err;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, err);
        if (!target) {
            llvm::errs() << err;
            return;
        }

        llvm::TargetOptions options;
        auto rm = llvm::Optional<llvm::Reloc::Model>();
        targetMachine = target->createTargetMachine(targetTriple, "generic", "", options, rm);
        module->setDataLayout(targetMachine->createDataLayout());
        module->setTargetTriple(targetTriple);
    }

    emplaceType<FloatType>("f32", FloatType::Bits::Float);
    emplaceType<FloatType>("f64", FloatType::Bits::Double);
    emplaceType<VoidType>("void");
    emplaceType<BoolType>("bool");
    emplaceType<IntegerType>("i32", 32, true);
    emplaceType<IntegerType>("u32", 32, false);
    emplaceType<CharType>("char");
    emplaceType<StringType>("str");

    lljit = llvm::cantFail(llvm::orc::LLJITBuilder().create());
}

int64_t macroAstIntegerValue(ASTNodeMacro node) {
    auto integer = (IntegerConstant *)node.data;

    std::cout << "AA" << std::endl;
    std::cout << integer->dump(0) << std::endl;

    if (std::holds_alternative<int64_t>(integer->constValue))
        return std::get<int64_t>(integer->constValue);
    else
        return (int64_t)std::get<uint64_t>(integer->constValue);
};

std::unique_ptr<Expression>
CodegenContext::evaluateMacro(ExpressionGenContext &genContext, Function *macroFunc, FunctionCall::Args &callArgs) {
    macroFunc->llvmFunction()->removeFromParent();

    llvm::ValueToValueMapTy valueMap;
    auto calledFrom = genContext.function->llvmFunction()->getName();

    // auto astNodeMacro = getNamed<NamedTypeValue>("___src_parse_context_macro_h/ASTNodeMacro");

    // auto argN = macroFunc->functionType()->arguments.size();
    auto argArray = llvm::PointerType::get(context, 0);

    std::vector<llvm::Type *> argTypes{ argArray };
    auto funcType = llvm::FunctionType::get(macroFunc->functionType()->returnType->llvmType(), argTypes, false);
    llvm::Function *function;

    function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, macroFunc->getName(), *module);

    macroFunc->setLLVMFunction(function);

    auto previousFunc = genContext.function;
    auto prevInsertPoint = genContext.builder.GetInsertBlock();
    genContext.function = macroFunc;
    macroFunc->generateBody(genContext);
    genContext.function = previousFunc;
    genContext.builder.SetInsertPoint(prevInsertPoint);

    auto &dylib = lljit->getMainJITDylib();

    llvm::orc::MangleAndInterner mangle(lljit->getExecutionSession(), lljit->getDataLayout());
    llvm::orc::SymbolMap symbolMap;
    symbolMap[mangle("ast-integer-value")] = llvm::JITEvaluatedSymbol(
        llvm::pointerToJITTargetAddress(&macroAstIntegerValue), llvm::JITSymbolFlags::Exported
    );

    llvm::cantFail(dylib.define(llvm::orc::absoluteSymbols(symbolMap)));

    if (llvm::verifyModule(*module, &llvm::errs()))
        ;

    auto macroModule = llvm::CloneModule(*module, valueMap, [&calledFrom](const llvm::GlobalValue *val) {
        return val->getName() != calledFrom;
    });
    llvm::cantFail(
        lljit->addIRModule(llvm::orc::ThreadSafeModule(std::move(macroModule), std::make_unique<LLVMContext>()))
    );

    std::vector<std::unique_ptr<Expression>> argCopies;
    std::vector<ASTNodeMacro> macroArgs;
    for (auto &arg : callArgs)
        argCopies.push_back(arg->clone());
    for (auto &arg : argCopies)
        macroArgs.emplace_back<ASTNodeMacro>({ arg.get() });

    auto func = llvm::cantFail(lljit->lookup(macroFunc->getName())).toPtr<ASTNodeMacro (*)(ASTNodeMacro *)>();

    auto result = func(macroArgs.data());
    Expression *resultExpr = (Expression *)result.data;
    auto resultCopy = resultExpr->clone();

    if (std::find_if(argCopies.begin(), argCopies.end(), [&](auto &arg) { return arg.get() == resultExpr; })
        == argCopies.end()) {
        // Returned expression is not one of the arguments passed in, so it has to be freed manually
        delete resultExpr;
    }

    return resultCopy;
}

void CodegenContext::emplaceFn(
    const std::string &langName, const std::string &funcName, const Function::Args &args, LanguageType *returnType,
    Function::Body &&body, bool isPublic
) {
    emplaceNamed<NamedFunctionValue>(langName, *this, *module, funcName, args, returnType, std::move(body), isPublic);
}

std::string NamedFunctionValue::namedType() { return "function"; }
std::string NamedFunctionValue::valueNamedType() const { return namedType(); };

std::string NamedVariableValue::namedType() { return "variable"; }
std::string NamedVariableValue::valueNamedType() const { return namedType(); };
NamedVariableValue::NamedVariableValue(VariableDefinition varDef)
    : value(std::make_unique<ValueType>(varDef)) {}

std::string NamedTypeValue::namedType() { return "type"; }
std::string NamedTypeValue::valueNamedType() const { return namedType(); };
NamedTypeValue::NamedTypeValue(std::unique_ptr<ValueType> &&value)
    : value(std::move(value)) {}
