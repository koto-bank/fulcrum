#include "expressions.hpp"
#include "types.hpp"

llvm::Value *FunctionCall::returnProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 0 && args.size() != 1) {
        throw CodegenError("Return must have 0 or 1 arguments");
    }

    auto returnType = genContext.function->functionType()->returnType;
    if (args.size() == 0 && returnType != context.types.at("void").get()) {
        throw CodegenError("Only void function can return nothing");
    } else if (args.size() == 1 && returnType != args[0]->languageType()) {
        throw CodegenError(
            fmt::format(
                "Function expected to return {}, but returns {}",
                returnType->signature(),
                args[0]->languageType()->signature()
            )
        );
    }

    if (args.size() == 0)
        genContext.builder.CreateRetVoid();
    else
        genContext.builder.CreateRet(args[0]->llvmValue(genContext));

    return nullptr;
}

llvm::Value *FunctionCall::doProcessor(ExpressionGenContext &genContext) {
    genContext.function->generateExpressions(genContext, args);

    return nullptr;
}

llvm::Value *FunctionCall::ifProcessor(ExpressionGenContext &genContext) {
    if (args.size() < 2 || args.size() > 3) {
        throw CodegenError("If must have from 2 to 3 arguments");
    }
    if (args[0]->languageType() != context.types.at("bool").get()) {
        throw CodegenError("First argument to if must be boolean");
    }

    auto &builder = genContext.builder;

    auto ifCondition = args[0]->llvmValue(genContext);

    auto thenBlock = llvm::BasicBlock::Create(context.context, "if-then", genContext.function->llvmFunction());
    auto elseBlock = args.size() == 3
        ? llvm::BasicBlock::Create(context.context, "if-else", genContext.function->llvmFunction())
        : nullptr;
    auto afterIfBlock = llvm::BasicBlock::Create(context.context, "after-if", genContext.function->llvmFunction());
    thenBlock->moveAfter(builder.GetInsertBlock());
    if (elseBlock != nullptr) elseBlock->moveAfter(thenBlock);
    afterIfBlock->moveAfter(elseBlock != nullptr ? elseBlock : thenBlock);

    builder.CreateCondBr(ifCondition, thenBlock, elseBlock != nullptr ? elseBlock : afterIfBlock);

    builder.SetInsertPoint(thenBlock);

    if (!genContext.function->generateExpressions(genContext, {args[1].get()})) {
        builder.CreateBr(afterIfBlock);
    }

    if (args.size() == 3) {
        builder.SetInsertPoint(elseBlock);

        if (!genContext.function->generateExpressions(genContext, {args[2].get()})) {
            builder.CreateBr(afterIfBlock);
        }
    }
    builder.SetInsertPoint(afterIfBlock);

    return nullptr;
}

llvm::Value *FunctionCall::arithmeticsProcessor(ExpressionGenContext &genContext) {
    if (args.size() == 0) {
        throw CodegenError(
            fmt::format("Expected at least 1 argument to {}, but got 0", name)
        );
    }

    auto expectedType = args[0]->languageType();
    auto intType = dynamic_cast<IntegerType *>(expectedType);
    auto floatType = intType == nullptr ? nullptr : dynamic_cast<FloatType*>(expectedType);

    if (intType == nullptr && floatType == nullptr) {
        throw CodegenError(
            fmt::format(
            "Expected first argument to {} to be of a numeric type, but it was of type {}",
            name,
            expectedType->signature()
            )
        );
    }

    for (auto i = 0; i< args.size(); i++) {
        auto &arg = args[i];
        if (arg->languageType() != expectedType) {
            throw CodegenError(
                fmt::format(
                    "Expected all arguments to {} to be of type {}, but argument #{} was of type {}",
                    name,
                    expectedType->signature(),
                    i,
                    arg->languageType()->signature()
                )
            );
        }
    }

    using namespace std::placeholders;
    std::function<llvm::Value *(llvm::IRBuilderBase *, llvm::Value *, llvm::Value *)> buildOperation;

    switch (name[0]) {
    case '+':
        if (intType)
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateAdd, _1, _2, _3, "", false, false);
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFAdd, _1, _2, _3, "", nullptr);

        break;
    case '-':
        if (intType)
            if (intType->isSigned)
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateSRem, _1, _2, _3, "");
            else
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateURem, _1, _2, _3, "");
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFRem, _1, _2, _3, "", nullptr);

        break;
    case '/':
        if (intType)
            if (intType->isSigned)
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateSDiv, _1, _2, _3, "", false);
            else
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateUDiv, _1, _2, _3, "", false);
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFDiv, _1, _2, _3, "", nullptr);
        break;
    case '%':
        if (intType)
            if (intType->isSigned)
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateSRem, _1, _2, _3, "");
            else
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateURem, _1, _2, _3, "");
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFRem, _1, _2, _3, "", nullptr);
        break;
    }

    llvm::Value * result = nullptr;
    for (auto &arg : args) {
        if (result == nullptr)
            result = arg->llvmValue(genContext);
        else
            result = buildOperation(&genContext.builder, result, arg->llvmValue(genContext));
    }

    return result;
}

LanguageType *FunctionCall::arithmeticsProcessorType() {
    if (args.size() == 0) {
        throw CodegenError(
            fmt::format("Expected at least 1 argument to {}, but got 0", name)
        );
    }

    return args[0]->languageType();
}

VarAccess::VarAccess(CodegenContext &codegenCont, const std::string& name)
    : Expression(nullptr), context(codegenCont), name(name) { }

std::string VarAccess::dump(int indent) {
    return fmt::format("{}{}", indentSpaces(indent), name);
}

AddrOf::AddrOf(std::unique_ptr<Expression> &&target)
    : Expression(nullptr), target(std::move(target)) { }

std::string AddrOf::dump(int indent) {
    return fmt::format("{}&", indentSpaces(indent));
}

Dereference::Dereference() : Expression(nullptr) { }

std::string Dereference::dump(int indent) {
    return fmt::format("{}@", indentSpaces(indent));
}

