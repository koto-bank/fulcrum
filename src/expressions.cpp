#include "expressions.hpp"

#include "codegen_context.hpp"
#include "types.hpp"

#include "fmt/format.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>

#include <iostream>
#include <map>
#include <variant>

VariableDefinition *ExpressionGenContext::lookupVariable(std::string name) {
    for (auto scope = variableScopes.rbegin(); scope != variableScopes.rend(); ++scope) {
        if (scope->contains(name))
            return &scope->at(name);
    }
    if (codegenContext.globalVariables.contains(name))
        return &codegenContext.globalVariables.at(name);

    return nullptr;
}

VariableDefinition *ExpressionGenContext::insertVariable(std::string name, LanguageType *type) {
    if (lookupVariable(name) != nullptr)
        throw CodegenError(fmt::format("Variable {} already defined", name));

    auto allocated = builder.CreateAlloca(type->llvmType(), 0, name);
    auto emplaced = variableScopes.back().emplace(name, VariableDefinition(name, type));

    auto varDef = &emplaced.first->second;
    varDef->value = allocated;

    return varDef;
}

void ExpressionGenContext::variableSet(VariableDefinition *var, llvm::Value *value) {
    builder.CreateStore(value, var->value);
}

void Expression::assumeExpression(ExpressionGenContext &genContext, Expression *expr, std::string errorMessage) {
    if (expr->llvmType(genContext) == genContext.codegenContext.getType("void")->llvmType())
        throw CodegenError(errorMessage);
}

IntegerConstant::IntegerConstant(IntegerType *type, IsLongInteger auto _constValue) : Expression(type), constValue(_constValue) {
    if (!llvm::ConstantInt::isValueValidForType(type->llvmType(), _constValue)) {
        throw CodegenError(fmt::format("Integer {} does not fit into its type", _constValue));
    }

    value = llvm::ConstantInt::get(type->llvmType(), _constValue);
}
template IntegerConstant::IntegerConstant(IntegerType *type, int64_t _constValue);
template IntegerConstant::IntegerConstant(IntegerType *type, uint64_t _constValue);

FloatConstant::FloatConstant(LanguageType *type, IsFloatingPoint auto constValue_) : Expression(type), constValue(constValue_) {
    auto apFloat = llvm::APFloat(constValue_);
    if (!llvm::ConstantFP::isValueValidForType(type->llvmType(), apFloat)) {
        throw CodegenError(fmt::format("Float {} does not fit into its type", constValue_));
    }
    value = llvm::ConstantFP::get(type->llvmType(), apFloat);
}
template FloatConstant::FloatConstant(LanguageType *type, float constValue_);
template FloatConstant::FloatConstant(LanguageType *type, double constValue_);

llvm::Value *FunctionCall::returnProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 0 && args.size() != 1) {
        throw CodegenError("Return must have 0 or 1 arguments");
    }

    auto returnType = genContext.function->functionType()->returnType;
    if (args.size() == 0 && returnType->llvmType() != genContext.codegenContext.types.at("void")->llvmType()) {
        throw CodegenError("Only void function can return nothing");
    } else if (args.size() == 1 && returnType->llvmType() != args[0]->languageType(genContext)->llvmType()) {
        throw CodegenError(
            fmt::format(
                "Function expected to return {}, but returns {}",
                returnType->signature(),
                args[0]->languageType(genContext)->signature()
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
    genContext.pushScope();
    genContext.function->generateExpressions(genContext, args);
    genContext.popScope();

    return nullptr;
}

llvm::Value *FunctionCall::ifProcessor(ExpressionGenContext &genContext) {
    auto &context = genContext.codegenContext;

    if (args.size() < 2 || args.size() > 3) {
        throw CodegenError("If must have from 2 to 3 arguments");
    }
    if (args[0]->languageType(genContext)->llvmType() != context.types.at("bool")->llvmType()) {
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

    genContext.pushScope();
    genContext.function->generateExpressions(genContext, {args[1].get()});
    if (!args[1]->isTerminator())
        builder.CreateBr(afterIfBlock);
    genContext.popScope();

    if (args.size() == 3) {
        builder.SetInsertPoint(elseBlock);

        genContext.pushScope();
        genContext.function->generateExpressions(genContext, {args[2].get()});
        if (!args[2]->isTerminator())
            builder.CreateBr(afterIfBlock);
        genContext.popScope();
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
    if (name == "=" && args.size() != 2)
        throw CodegenError(
            fmt::format("Expected exactly 2 argument to =, but got {}", args.size())
        );

    auto expectedType = args[0]->languageType(genContext);
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
        if (arg->languageType(genContext)->llvmType() != expectedType->llvmType()) {
            throw CodegenError(
                fmt::format(
                    "Expected all arguments to {} to be of type {}, but argument #{} was of type {}",
                    name,
                    expectedType->signature(),
                    i,
                    arg->languageType(genContext)->signature()
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
    case '=':
        if (intType)
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpEQ, _1, _2, _3, "");
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFCmpOEQ, _1, _2, _3, "", nullptr);
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

LanguageType *FunctionCall::voidProcessorType(ExpressionGenContext &genContext) {
    return genContext.codegenContext.getType("void");
}

LanguageType *FunctionCall::arithmeticsProcessorType(ExpressionGenContext &genCont) {
    if (args.size() == 0) {
        throw CodegenError(
            fmt::format("Expected at least 1 argument to {}, but got 0", name)
        );
    }
    if (name == "=")
        return genCont.codegenContext.getType("bool");

    return args[0]->languageType(genCont);
}

llvm::Value *FunctionCall::setProcessor(ExpressionGenContext &genContext) {
    if (args.size() % 2 != 0) {
        throw CodegenError(
            fmt::format("Expected an even number of arguments to set, but got {}", args.size())
        );
    }

    for (auto i = 0; i < args.size(); i++) {
        auto varExpr = dynamic_cast<VarAccess *>(args[i].get());
        if (varExpr == nullptr)
            throw CodegenError(
                fmt::format("Expected argument #{} to set to be a variable name, but got {}", i, args[i]->dump())
            );
        auto newValue = args[++i].get();
        assumeExpression(genContext, newValue, fmt::format("Expected argument #{} to set to be an expression, but it's a statement", i));

        auto declaredVar = genContext.lookupVariable(varExpr->name);
        if (declaredVar == nullptr)
            throw CodegenError(fmt::format("Variable {} not defined", varExpr->name));

        auto declType = declaredVar->type;
        auto newValType = newValue->languageType(genContext);
        if (declType->llvmType() != newValType->llvmType())
            throw CodegenError(
                fmt::format("Variable {} is of type {}, argument to set is of type {}",
                            varExpr->name, declType->signature(), newValType->signature()));

        genContext.variableSet(declaredVar, newValue->llvmValue(genContext));
    }

    return nullptr;
}

LanguageType *FunctionCall::languageType(ExpressionGenContext &genContext) {
    if (type == nullptr) {
        if (specialFunctions.contains(name)) {
            type = specialFunctions[name].second(this, genContext);
        } else {
            if (!genContext.codegenContext.functions.contains(name)) {
                throw CodegenError(fmt::format("Undefined function {}", name));
            }

            type = genContext.codegenContext.functions.at(name).functionType()->returnType;
        }
    }

    return type;
}

llvm::Value *FunctionCall::llvmValue(ExpressionGenContext &genContext) {
    if (specialFunctions.contains(name))
        return specialFunctions[name].first(this, genContext);

    if (!genContext.codegenContext.functions.contains(name)) {
        throw CodegenError(fmt::format("Undefined function {}", name));
    }
    auto &calledFunction = genContext.codegenContext.functions.at(name);

    std::vector<llvm::Value *> argValues;
    for (auto i = 0; i < args.size(); i++) {
        LanguageType *argType = args[i]->languageType(genContext);
        LanguageType *expectedType = calledFunction.functionType()->arguments[i];
        if (argType->llvmType() != expectedType->llvmType()) {
            throw CodegenError(
                fmt::format("Incompatible argument type in {}: for argument #{}"
                            " expected {}, but received {}", name, i, expectedType->signature(), argType->signature())
            );
        }
        argValues.push_back(args[i]->llvmValue(genContext));
    }

    return genContext.builder.CreateCall(calledFunction.llvmFunction(), argValues);
}

VarAccess::VarAccess(const std::string& name)
    : Expression(nullptr), name(name) { }

LanguageType *VarAccess::languageType(ExpressionGenContext &genCont) {
    auto var = genCont.lookupVariable(name);
    if (var == nullptr)
        throw CodegenError(fmt::format("Variable {} not defined", name));

    return var->type;
}

llvm::Value *VarAccess::llvmValue(ExpressionGenContext &genCont) {
    auto var = genCont.lookupVariable(name);
    if (var == nullptr)
        throw CodegenError(fmt::format("Variable {} not defined", name));

    return genCont.builder.CreateLoad(var->type->llvmType(), var->value);
}

std::string VarAccess::dump(int indent) {
    return fmt::format("{}{}", indentSpaces(indent), name);
}

AddrOf::AddrOf(std::unique_ptr<Expression> &&target)
    : Expression(nullptr), target(std::move(target)) { }

LanguageType *AddrOf::languageType(ExpressionGenContext &genCont) {
    auto maybeVar = dynamic_cast<VarAccess *>(target.get());
    if (maybeVar == nullptr)
        throw CodegenError(fmt::format("Expected a variable to take an address of, got {}", target->dump()));

    auto varEntry = genCont.lookupVariable(maybeVar->name);
    if (varEntry == nullptr)
        throw CodegenError(fmt::format("Variable {} not defined", varEntry->name));

    auto ptrTypeName = varEntry->type->signature() + "*";
    return genCont.codegenContext.getOrEmplaceType<PointerType>(ptrTypeName, varEntry->type);
}

llvm::Value *AddrOf::llvmValue(ExpressionGenContext &genCont) {
    auto maybeVar = dynamic_cast<VarAccess *>(target.get());
    if (maybeVar == nullptr)
        throw CodegenError(fmt::format("Expected a variable to take an address of, got {}", target->dump()));

    auto varEntry = genCont.lookupVariable(maybeVar->name);
    if (varEntry == nullptr)
        throw CodegenError(fmt::format("Variable {} not defined", varEntry->name));

    return varEntry->value;
}

std::string AddrOf::dump(int indent) {
    return fmt::format("{}&{}", indentSpaces(indent), target->dump(0));
}

LanguageType *Dereference::languageType(ExpressionGenContext &genCont) {
    auto derefing = target->languageType(genCont);
    auto ptrType = dynamic_cast<PointerType *>(derefing);
    if (ptrType == nullptr)
        throw CodegenError(fmt::format("Dereferencing a non-pointer type {}", derefing->signature()));

    return ptrType->pointerTo;
}

llvm::Value *Dereference::llvmValue(ExpressionGenContext &genCont) {
    return genCont.builder.CreateLoad(languageType(genCont)->llvmType(), target->llvmValue(genCont));
}

std::string Dereference::dump(int indent) {
    return fmt::format("{}@{}", indentSpaces(indent), target->dump(0));
}

llvm::Value *VariableDeclaration::llvmValue(ExpressionGenContext &genContext) {
    auto varDef = genContext.insertVariable(name, type);
    if (initialValue != nullptr) {
        auto initialValType = initialValue->languageType(genContext);
        if (initialValType->llvmType() != type->llvmType()) {
            throw CodegenError(
                fmt::format(
                    "Tried to assign a value ot type {} to {}, which is a variable of type {}",
                    initialValType->signature(),
                    name,
                    type->signature()
                ));
        }

        assumeExpression(genContext, initialValue.get(),
                         fmt::format("Expected value for #{} to to be an expression, but it's a statement", name));
        genContext.variableSet(varDef, initialValue->llvmValue(genContext));
    }

    return nullptr;
}

std::string VariableDeclaration::dump(int indent) {
    return fmt::format("{}($var {} {}",
                       indentSpaces(indent),
                       name,
                       type->signature() +
                       (initialValue == nullptr
                        ? ")"
                        : fmt::format(" {})", initialValue->dump(0))));
}

Sizeof::Sizeof(CodegenContext &context, LanguageType *targetType)
    : Expression(context.getType("u32"))
    , targetType(targetType) {

    value = llvm::ConstantInt::get(
        type->llvmType(),
        context.module.getDataLayout().getTypeAllocSize(targetType->llvmType())
    );
}

std::string Sizeof::dump(int indent) {
    return fmt::format("{}($sizeof {})", indentSpaces(indent), targetType->signature());
}
