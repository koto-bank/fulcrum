#include <map>
#include <variant>

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "assert.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"
#include "types.hpp"

VariableDefinition::VariableDefinition(const std::string &name, LanguageType *type)
    : name(name),
      type(type) {}

const VariableDefinition *ExpressionGenContext::lookupVariable(const std::string &name) const {
    for (auto scope = variableScopes.rbegin(); scope != variableScopes.rend(); ++scope) {
        if (scope->contains(name)) return &scope->at(name);
    }
    if (codegenContext.existsNamed(name)) return codegenContext.getNamed<NamedVariableValue>(name);

    return nullptr;
}

VariableDefinition *ExpressionGenContext::insertVariable(const std::string &name, LanguageType *type) {
    if (lookupVariable(name) != nullptr) throw CodegenError(fmt::format("Variable {} already defined", name));

    auto allocated = builder.CreateAlloca(type->llvmType(), 0, name);
    auto emplaced = variableScopes.back().emplace(name, VariableDefinition(name, type));

    auto varDef = &emplaced.first->second;
    varDef->value = allocated;

    return varDef;
}

VariableDefinition *ExpressionGenContext::insertFunctionArgument(const std::string &name, LanguageType *type) {
    if (lookupVariable(name) != nullptr) throw CodegenError(fmt::format("Argument {} already defined", name));

    auto allocated = builder.CreateAlloca(type->llvmTypeAccess(), 0, name);
    auto emplaced = variableScopes.back().emplace(name, VariableDefinition(name, type));

    auto varDef = &emplaced.first->second;
    varDef->value = allocated;

    return varDef;
}

void ExpressionGenContext::pushScope() { variableScopes.push_back({}); }

void ExpressionGenContext::popScope() { variableScopes.pop_back(); }

void Expression::assumeExpression(ExpressionGenContext &genContext, Expression *expr, const std::string &errorMessage) {
    if (expr->languageType(genContext)->actualLanguageType()
        == genContext.codegenContext.getNamed<NamedTypeValue>("void"))
        throw CodegenError(errorMessage);
}

std::string Expression::indentSpaces(int n) { return fmt::format("{: >{}}", "", n); }

Expression::Expression(LanguageType *type)
    : type(type) {}

LanguageType *Expression::languageType(const ExpressionGenContext &) { return type; }

llvm::Type *Expression::llvmType(ExpressionGenContext &genContext) { return languageType(genContext)->llvmType(); }

llvm::Value *Expression::llvmValue(ExpressionGenContext &) { return value; }

bool Expression::isTerminator() { return false; }


llvm::Constant *ConstantExpression::llvmConstant(CodegenContext &) { return (llvm::Constant *)value; }

IntegerConstant::IntegerConstant(IntegerType *type, IsLongInteger auto _constValue)
    : ConstantExpression(type),
      constValue(_constValue) {
    if (!llvm::ConstantInt::isValueValidForType(type->llvmType(), _constValue)) {
        throw CodegenError(fmt::format("Integer {} does not fit into its type", _constValue));
    }

    value = llvm::ConstantInt::get(type->llvmType(), _constValue);
}
template IntegerConstant::IntegerConstant(IntegerType *type, int64_t _constValue);
template IntegerConstant::IntegerConstant(IntegerType *type, uint64_t _constValue);

std::string IntegerConstant::dump(int indent) {
    auto isSigned = static_cast<IntegerType *>(type)->isSigned;

    return isSigned ? fmt::format("{}{}{}", indentSpaces(indent), std::get<int64_t>(constValue), type->signature())
                    : fmt::format("{}{}{}", indentSpaces(indent), std::get<uint64_t>(constValue), type->signature());
}

FloatConstant::FloatConstant(LanguageType *type, IsFloatingPoint auto constValue_)
    : ConstantExpression(type),
      constValue(constValue_) {
    auto apFloat = llvm::APFloat(constValue_);
    if (!llvm::ConstantFP::isValueValidForType(type->llvmType(), apFloat)) {
        throw CodegenError(fmt::format("Float {} does not fit into its type", constValue_));
    }
    value = llvm::ConstantFP::get(type->llvmType(), apFloat);
}
template FloatConstant::FloatConstant(LanguageType *type, float constValue_);
template FloatConstant::FloatConstant(LanguageType *type, double constValue_);

std::string FloatConstant::dump(int indent) {
    auto floatbits = ((FloatType *)type)->bits;

    return fmt::format(
        "{}{}{}", indentSpaces(indent),
        floatbits == FloatType::Bits::Double ? std::get<double>(constValue) : std::get<float>(constValue),
        type->signature()
    );
}

StringConstant::StringConstant(LanguageType *type, const std::string &constValue)
    : ConstantExpression(type),
      constValue(constValue) {}

llvm::Value *StringConstant::llvmValue(ExpressionGenContext &genContext) {
    llvmConstant(genContext.codegenContext);

    auto Zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(type->llvmType()->getContext()), 0);
    llvm::Constant *indices[] = { Zero, Zero };
    auto ret = llvm::ConstantExpr::getInBoundsGetElementPtr(
        llvmConst->getValueType(), llvmConstant(genContext.codegenContext), indices
    );
    return ret;
}

llvm::Constant *StringConstant::llvmConstant(CodegenContext &codegenContext) {
    if (llvmConst == nullptr) {
        llvm::Constant *strConstant = llvm::ConstantDataArray::getString(codegenContext.context, constValue);
        llvmConst = new llvm::GlobalVariable(
            codegenContext.module, strConstant->getType(), true, llvm::GlobalValue::PrivateLinkage, strConstant
        );
    }

    return llvmConst;
}

std::string StringConstant::dump(int indent) { return fmt::format("{}\"{}\"", indentSpaces(indent), constValue); }

BoolConstant::BoolConstant(LanguageType *type, bool constValue)
    : ConstantExpression(type),
      constValue(constValue) {
    value = llvm::ConstantInt::get(type->llvmType(), constValue ? 1 : 0);
}

std::string BoolConstant::dump(int indent) { return fmt::format("{}{}", indentSpaces(indent), constValue); }

FunctionCall::FunctionCall(const std::string &name_, Args &&args)
    // Initialize type with nullptr for now, since we don't know the return type yet
    : Expression(nullptr),
      name(name_),
      args(std::move(args)) {
    // De-namespace special functions
    auto namespaceSep = name.find('/');
    auto baseName = name.substr(namespaceSep + 1);
    if (specialFunctions.contains(baseName)) name = baseName;
}

llvm::Value *FunctionCall::returnProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 0 && args.size() != 1) { throw CodegenError("Return must have 0 or 1 arguments"); }

    auto returnType = genContext.function->functionType()->returnType;
    if (args.size() == 0
        && returnType->actualLanguageType() != genContext.codegenContext.getNamed<NamedTypeValue>("void")) {
        throw CodegenError("Only void function can return nothing");
    } else if (args.size() == 1 && returnType->actualLanguageType() != args[0]->languageType(genContext)->actualLanguageType()) {
        throw CodegenError(fmt::format(
            "Function expected to return {}, but returns {}", returnType->signature(),
            args[0]->languageType(genContext)->signature()
        ));
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

    if (args.size() < 2 || args.size() > 3) { throw CodegenError("If must have from 2 to 3 arguments"); }
    if (args[0]->languageType(genContext)->actualLanguageType() != context.getNamed<NamedTypeValue>("bool")) {
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
    args[1]->llvmValue(genContext);
    if (!args[1]->isTerminator()) builder.CreateBr(afterIfBlock);
    genContext.popScope();

    if (args.size() == 3) {
        builder.SetInsertPoint(elseBlock);

        genContext.pushScope();
        args[2]->llvmValue(genContext);
        if (!args[2]->isTerminator()) builder.CreateBr(afterIfBlock);
        genContext.popScope();
    }
    builder.SetInsertPoint(afterIfBlock);

    return nullptr;
}

llvm::Value *FunctionCall::ptrArithmeticsProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 2) { throw CodegenError(fmt::format("Expected exaclty 2 arguments to {}, but got {}", name, args.size())); }

    const auto &targetArg = args[0];
    auto targetType = targetArg->languageType(genContext)->actualLanguageType();
    auto ptrType = dynamic_cast<PointerType *>(targetType);
    if (ptrType == nullptr) {
        throw CodegenError(fmt::format(
            "Expected first argument to {} to be of an integer type, but it was of type {}", name,
            type->signature()
        ));
    }

    const auto &offsetArg = args[1];
    auto offsetType = offsetArg->languageType(genContext)->actualLanguageType();
    auto intType = dynamic_cast<IntegerType *>(offsetType);
    if (intType == nullptr) {
        throw CodegenError(fmt::format(
            "Expected first argument to {} to be of an integer type, but it was of type {}", name,
            type->signature()
        ));
    }

    auto offset = offsetArg->llvmValue(genContext);
    auto &builder = genContext.builder;
    if (name == "ptr-") {
        offset = builder.CreateNeg(offset);
    } else if (name != "ptr+") {
        fc_unreachable();
        return nullptr;
    }
    return builder.CreateGEP(targetArg->llvmType(genContext),
                             targetArg->llvmValue(genContext),
                             offset);
}

llvm::Value *FunctionCall::arithmeticsProcessor(ExpressionGenContext &genContext) {
    if (args.size() == 0) { throw CodegenError(fmt::format("Expected at least 1 argument to {}, but got 0", name)); }
    if ((name == "=" || name[0] == '>' || name[0] == '<') && args.size() != 2)
        throw CodegenError(fmt::format("Expected exactly 2 argument to {}, but got {}", name, args.size()));

    auto expectedType = args[0]->languageType(genContext)->actualLanguageType();
    auto intType = dynamic_cast<IntegerType *>(expectedType);
    auto floatType = intType == nullptr ? dynamic_cast<FloatType *>(expectedType) : nullptr;

    if (intType == nullptr && floatType == nullptr) {
        throw CodegenError(fmt::format(
            "Expected first argument to {} to be of a numeric type, but it was of type {}", name,
            expectedType->signature()
        ));
    }

    for (auto i = 0u; i < args.size(); i++) {
        auto &arg = args[i];
        if (arg->languageType(genContext)->actualLanguageType() != expectedType) {
            throw CodegenError(fmt::format(
                "Expected all arguments to {} to be of type {}, but argument #{} was of type {}", name,
                expectedType->signature(), i, arg->languageType(genContext)->signature()
            ));
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
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateSub, _1, _2, _3, "", false, false);
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFSub, _1, _2, _3, "", nullptr);
        break;
    case '*':
        if (intType)
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateMul, _1, _2, _3, "", false, false);
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFMul, _1, _2, _3, "", nullptr);
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
    case '!': {
        fc_assert(name[1] == '=');
        if (intType)
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpNE, _1, _2, _3, "");
        else
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateFCmpONE, _1, _2, _3, "", nullptr);
        break;
    }
    case '>': {
        auto orEquals = name.size() > 1 && name[1] == '=';
        if (intType) {
            if (intType->isSigned) {
                if (orEquals) {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpSGE, _1, _2, _3, "");
                } else {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpSGT, _1, _2, _3, "");
                }
            } else {
                if (orEquals) {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpUGE, _1, _2, _3, "");
                } else {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpUGT, _1, _2, _3, "");
                }
            }
        } else {
            if (orEquals) {
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateFCmpOGE, _1, _2, _3, "", nullptr);
            } else {
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateFCmpOGT, _1, _2, _3, "", nullptr);
            }
        }

        break;
    }
    case '<': {
        auto orEquals = name.size() > 1 && name[1] == '=';
        if (intType) {
            if (intType->isSigned) {
                if (orEquals) {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpSLE, _1, _2, _3, "");
                } else {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpSLT, _1, _2, _3, "");
                }
            } else {
                if (orEquals) {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpULE, _1, _2, _3, "");
                } else {
                    buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpULT, _1, _2, _3, "");
                }
            }
        } else {
            if (orEquals) {
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateFCmpOLE, _1, _2, _3, "", nullptr);
            } else {
                buildOperation = std::bind(&llvm::IRBuilderBase::CreateFCmpOLT, _1, _2, _3, "", nullptr);
            }
        }

        break;
    }
    }

    llvm::Value *result = nullptr;
    for (auto &arg : args) {
        if (result == nullptr)
            result = arg->llvmValue(genContext);
        else
            result = buildOperation(&genContext.builder, result, arg->llvmValue(genContext));
    }

    return result;
}

LanguageType *FunctionCall::voidProcessorType(const ExpressionGenContext &genContext) const {
    return genContext.codegenContext.getNamed<NamedTypeValue>("void");
}

LanguageType *FunctionCall::boolProcessorType(const ExpressionGenContext &genContext) const {
    return genContext.codegenContext.getNamed<NamedTypeValue>("bool");
}

LanguageType *FunctionCall::addrOfProcessorType(const ExpressionGenContext &genContext) const {
    if (args.size() != 1) { throw CodegenError(fmt::format("Expected exactly 1 argument to {}, but got {}", name, args.size())); }
    const auto &target = args[0];
    auto maybeVar = dynamic_cast<VariableAccess *>(target.get());
    if (maybeVar == nullptr) {
        throw CodegenError(fmt::format("Expected a variable to take an address of, got {}", target->dump()));
    }
    auto varType = maybeVar->languageType(genContext);
    auto ptrTypeName = varType->signature() + "*";
    auto ret = genContext.codegenContext.getOrEmplaceType<PointerType>(ptrTypeName, varType);
    return ret;
}

LanguageType *FunctionCall::arithmeticsProcessorType(const ExpressionGenContext &genCont) const {
    if (args.size() == 0) { throw CodegenError(fmt::format("Expected at least 1 argument to {}, but got 0", name)); }
    if (name == "=" || name == "!=" || name[0] == '>' || name[0] == '<')
        return genCont.codegenContext.getNamed<NamedTypeValue>("bool");
    // TODO: move type processing to separate step
    return args[0]->languageType(genCont);
}

LanguageType *FunctionCall::ptrArithmeticsProcessorType(const ExpressionGenContext &genCont) const {
    if (args.size() != 2) { throw CodegenError(fmt::format("Expected exactly 2 arguments to {}, but got {}", name, args.size())); }
    return args[0]->languageType(genCont);
}

llvm::Value *FunctionCall::setProcessor(ExpressionGenContext &genContext) {
    if (args.size() % 2 != 0) {
        throw CodegenError(fmt::format("Expected an even number of arguments to set, but got {}", args.size()));
    }

    for (auto i = 0u; i < args.size(); i++) {
        LanguageType *variableType = nullptr;
        llvm::Value *varAddress = nullptr;

        auto maybeDeref = dynamic_cast<Dereference *>(args[i].get());
        if (maybeDeref != nullptr) {
            variableType = maybeDeref->languageType(genContext);

            auto *derefTarget = maybeDeref->target.get();
            auto ptrType = dynamic_cast<PointerType *>(derefTarget->languageType(genContext));
            if (ptrType == nullptr)
                throw CodegenError(fmt::format("Dereferencing a non-pointer type {}", ptrType->signature()));
            varAddress = derefTarget->llvmValue(genContext);
        }
        auto maybeArraySubscription = dynamic_cast<ArraySubscription *>(args[i].get());
        if (maybeArraySubscription != nullptr) {
            variableType = maybeArraySubscription->languageType(genContext);
            varAddress = maybeArraySubscription->getElementPtr(genContext);
        }
        if (variableType == nullptr) {
            auto maybeVarExpr = dynamic_cast<VariableAccess *>(args[i].get());
            if (maybeVarExpr != nullptr) {
                variableType = maybeVarExpr->languageType(genContext);
                varAddress = maybeVarExpr->varAddress(genContext);
            }
        }

        if (variableType == nullptr) {
            throw CodegenError(fmt::format(
                "Expected argument #{} to set to be a variable name or pointer dereference, but "
                "got {}",
                i, args[i]->dump()
            ));
        }

        auto newValue = args[++i].get();
        assumeExpression(
            genContext, newValue,
            fmt::format("Expected argument #{} to set to be an expression, but it's a statement", i)
        );

        auto newValType = newValue->languageType(genContext)->actualLanguageType();
        if (variableType->actualLanguageType() != newValType)
            throw CodegenError(fmt::format(
                "Variable {} is of type {}, argument to set is of type {}", args[i]->dump(), variableType->signature(),
                newValType->signature()
            ));

        genContext.builder.CreateStore(newValue->llvmValue(genContext), varAddress);
    }

    return nullptr;
}

llvm::Value *FunctionCall::whileProcessor(ExpressionGenContext &genContext) {
    if (args.size() < 1) { throw CodegenError(fmt::format("Expected at least 1 argument to while")); }
    if (args[0]->languageType(genContext)->actualLanguageType()
        != genContext.codegenContext.getNamed<NamedTypeValue>("bool")) {
        throw CodegenError("First argument to while must be boolean");
    }

    auto &llContext = genContext.codegenContext.context;
    auto whileCondBlock = llvm::BasicBlock::Create(llContext, "while-cond", genContext.function->llvmFunction());
    genContext.builder.CreateBr(whileCondBlock);
    genContext.builder.SetInsertPoint(whileCondBlock);
    auto condValue = args[0]->llvmValue(genContext);

    auto condTrueBlock = llvm::BasicBlock::Create(llContext, "while-true", genContext.function->llvmFunction());

    genContext.builder.SetInsertPoint(condTrueBlock);

    genContext.pushScope();
    bool terminatorPresent = false;
    for (auto &arg : args ) {
        arg->llvmValue(genContext);
        if (arg->isTerminator()) {
            terminatorPresent = true;
        }
    }

    if (!terminatorPresent) {
        genContext.builder.CreateBr(whileCondBlock);
    }

    genContext.popScope();

    auto condAfterBlock = llvm::BasicBlock::Create(llContext, "while-after", genContext.function->llvmFunction());
    genContext.builder.SetInsertPoint(whileCondBlock);
    // Actually add the conditional jump, now that while-after has been createad at the very end
    genContext.builder.CreateCondBr(condValue, condTrueBlock, condAfterBlock);
    // Continue inserting after the while
    genContext.builder.SetInsertPoint(condAfterBlock);

    return nullptr;
}

llvm::Value *FunctionCall::boolProcessor(ExpressionGenContext &genContext) {
    if (name == "not") {
        if (args.size() != 1
            || args[0]->languageType(genContext)->actualLanguageType()
            != genContext.codegenContext.getNamed<NamedTypeValue>("bool"))
            throw CodegenError(fmt::format("'not' expects exactly one argument of type bool", name));

        assumeExpression(
            genContext, args[0].get(), fmt::format("Expected argument to '{}' to to be an expression", name)
            );
        return genContext.builder.CreateNot(args[0]->llvmValue(genContext));
    }

    if (args.size() != 2
        || args[0]->languageType(genContext)->actualLanguageType()
            != genContext.codegenContext.getNamed<NamedTypeValue>("bool")
        || args[1]->languageType(genContext)->actualLanguageType()
            != genContext.codegenContext.getNamed<NamedTypeValue>("bool"))
        throw CodegenError(fmt::format("'{}' expects exactly two arguments of type bool", name));

    assumeExpression(
        genContext, args[0].get(), fmt::format("Expected argument 1 to '{}' to to be an expression", name)
    );
    assumeExpression(
        genContext, args[1].get(), fmt::format("Expected argument 2 to '{}' to to be an expression", name)
    );

    if (name == "and") {
        return genContext.builder.CreateAnd({ args[0]->llvmValue(genContext), args[1]->llvmValue(genContext) });
    } else if (name == "or") {
        return genContext.builder.CreateOr({ args[0]->llvmValue(genContext), args[1]->llvmValue(genContext) });
    } else if (name == "xor") {
        return genContext.builder.CreateXor(args[0]->llvmValue(genContext), args[1]->llvmValue(genContext));
    }
    fc_unreachable();
    return nullptr;
}

LanguageType *FunctionCall::languageType(const ExpressionGenContext &genContext) {
    if (type == nullptr) {
        if (specialFunctions.contains(name)) {
            type = specialFunctions.at(name).second(this, genContext);
        } else {
            type = genContext.codegenContext.getNamed<NamedFunctionValue>(name)->functionType()->returnType;
        }
    }

    return type;
}

llvm::Value *FunctionCall::addrOfProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 1) {
        throw CodegenError("'addr-of' expects exactly one argument");
    }
    const auto &target = args[0];
    auto maybeVar = dynamic_cast<VariableAccess *>(target.get());
    if (maybeVar == nullptr) {
        throw CodegenError(fmt::format("Expected a variable to take an address of, got {}", target->dump()));
    }
    return maybeVar->varAddress(genContext);
}

llvm::Value *FunctionCall::llvmValue(ExpressionGenContext &genContext) {
    if (specialFunctions.contains(name)) return specialFunctions.at(name).first(this, genContext);

    auto calledFunction = genContext.codegenContext.getNamed<NamedFunctionValue>(name);

    std::vector<llvm::Value *> argValues;
    auto fnType = calledFunction->functionType();
    for (auto i = 0u; i < args.size(); i++) {
        LanguageType *argType = args[i]->languageType(genContext);

        if (!fnType->isVariadic || i < fnType->arguments.size()) {
            LanguageType *expectedType = fnType->arguments[i];
            if (argType->actualLanguageType() != expectedType->actualLanguageType()) {
                throw CodegenError(fmt::format(
                                       "Incompatible argument type in {}: for argument #{}"
                                       " expected {}, but received {}",
                                       name, i, expectedType->signature(), argType->signature()
                                       ));
            }
        }
        argValues.push_back(args[i]->llvmValue(genContext));
    }

//    llvm::outs() << "Compiling a function call!\n";
//    llvm::outs() << "Name: " << name << "\n";
//    llvm::outs() << "Args with types: >>>>>\n";
//    for (uint32_t i = 0u; i < args.size(); i++) {
//        args[i]->languageType(genContext)->llvmType()->print(llvm::outs());
//        args[i]->llvmValue(genContext)->print(llvm::outs() << " -- ");
//        args[i]->llvmValue(genContext)->getType()->print(llvm::outs() << " -- Actual value type: ");
//        llvm::outs() << "\n<<<<<\n";
//    }
//
    auto ret = genContext.builder.CreateCall(calledFunction->llvmFunction(), argValues);
//    llvm::outs() << "Resulting call: ";
//    ret->print(llvm::outs());
//    llvm::outs() << "\n";
    return ret;
}

bool FunctionCall::isTerminator() {
    if (name == "return") return true;
    if (name == "do") {
        for (auto &arg : args)
            if (arg->isTerminator()) return true;
    }

    return false;
}

std::string FunctionCall::dump(int indent) {
    std::vector<std::string> argDumps;
    for (auto &arg : args) {
        if (arg != nullptr) {
            argDumps.push_back(arg->dump());
        } else {
            argDumps.push_back("nullptr");
        }
    }

    return fmt::format("{}($call {} {})", indentSpaces(indent), name, fmt::join(argDumps, " "));
}

VariableAccess::VariableAccess(const std::string &name)
    : Expression(nullptr),
      name(name) {
    if (varPath.size() == 0) {
        if (std::find(name.begin(), name.end(), '.') != name.end()) {
            std::string currentName;

            for (auto i = 0u; i < name.size(); i++) {
                if (name[i] == '.') {
                    varPath.push_back(currentName);
                    currentName = "";
                } else {
                    currentName += name[i];
                }
            }
            varPath.push_back(currentName);
        } else {
            varPath.push_back(name);
        }
    }
}

LanguageType *VariableAccess::languageType(const ExpressionGenContext &genCont) {
    auto pathName = path();

    if (pathName.size() > 1) {
        LanguageType *currentType = nullptr;
        for (auto &currentName : pathName) {
            if (currentType == nullptr) {
                auto foundVar = genCont.lookupVariable(currentName);
                if (foundVar == nullptr) throw CodegenError(fmt::format("Variable {} not defined", currentName));

                currentType = foundVar->type;
            } else {
                // This works both for unions and structs
                auto *structType = dynamic_cast<StructType *>(currentType);
                if (structType == nullptr)
                    throw CodegenError(fmt::format("Expected {} to be a structure type", currentType->signature()));
                auto fieldIndex = structType->fieldIndex(currentName);
                currentType = std::get<1>(structType->fields[fieldIndex]);
            }
        }

        return currentType;
    } else {
        auto var = genCont.lookupVariable(name);
        if (var == nullptr) throw CodegenError(fmt::format("Variable {} not defined", name));
        return var->type;
    }
}

const std::vector<std::string> &VariableAccess::path() const {
    return varPath;
}

llvm::Value *VariableAccess::varAddress(ExpressionGenContext &genCont) {
    auto pathName = this->path();

    llvm::Value *currentValue = nullptr;
    LanguageType *currentType = nullptr;

    auto loadStructField = [&](const std::string &currentName) {
        if (currentValue == nullptr) {
            auto var = genCont.lookupVariable(currentName);
            currentValue = var->value;
            currentType = var->type;
        } else {
            auto structType = dynamic_cast<StructType *>(currentType);
            if (structType == nullptr)
                throw CodegenError(
                    fmt::format("Expected {} to be a structure or a union type", currentType->signature())
                );
            auto fieldIndex = structType->fieldIndex(currentName);

            currentType = std::get<1>(structType->fields[fieldIndex]);

            if (dynamic_cast<UnionType *>(structType) != nullptr) {
                // Don't do anything, loading with current type will produce the right value
            } else {
                currentValue = genCont.builder.CreateStructGEP(structType->llvmType(), currentValue, fieldIndex);
            }
        }
    };

    if (pathName.size() > 1) {
        for (auto &currentName : pathName) {
            loadStructField(currentName);
        }
        return currentValue;
    } else {
        auto var = genCont.lookupVariable(name);
        if (var == nullptr) throw CodegenError(fmt::format("Variable {} not defined", name));
        return var->value;
    }
}

llvm::Value *VariableAccess::llvmValue(ExpressionGenContext &genContext) {
    if (path().size() > 1) { // TODO?
        llvm::outs() << "Multi-path variable: ";
        for (auto &s : path()) { llvm::outs() << s; }
        llvm::outs() << '\n';
        return genContext.builder.CreateLoad(llvmType(genContext), varAddress(genContext));
    }
    auto maybeArray = dynamic_cast<ArrayType *>(languageType(genContext));
    if (maybeArray != nullptr) {
//        llvm::outs() << "Got variable access to array type\n";
//        llvmType(genContext)->print(llvm::outs());
//        llvm::outs() << "\nAccess value:\n";
//        varAddress(genContext)->print(llvm::outs());
//        llvm::outs() << '\n';
        //llvm::outs() << "Array name: " << name << "\n";
        return varAddress(genContext);
    }
    return genContext.builder.CreateLoad(llvmType(genContext), varAddress(genContext));
}

std::string VariableAccess::dump(int indent) { return fmt::format("{}{}", indentSpaces(indent), name); }

Dereference::Dereference(std::unique_ptr<Expression> &&target)
    : Expression(nullptr),
      target(std::move(target)) {}

LanguageType *Dereference::languageType(const ExpressionGenContext &genCont) {
    auto derefing = target->languageType(genCont);
    auto ptrType = dynamic_cast<PointerType *>(derefing);
    if (ptrType == nullptr)
        throw CodegenError(fmt::format("Dereferencing a non-pointer type {}", derefing->signature()));

    return ptrType->pointerTo;
}

llvm::Value *Dereference::llvmValue(ExpressionGenContext &genCont) {
    return genCont.builder.CreateLoad(languageType(genCont)->llvmType(), target->llvmValue(genCont));
}

std::string Dereference::dump(int indent) { return fmt::format("{}@{}", indentSpaces(indent), target->dump(0)); }

ArraySubscription::ArraySubscription(std::unique_ptr<Expression> &&array, std::unique_ptr<Expression> &&subscript)
    : Expression(nullptr)
    , array(std::move(array))
    , subscript(std::move(subscript)) {}

LanguageType *ArraySubscription::languageType(const ExpressionGenContext &genContext) {
    auto maybeArrayType = array->languageType(genContext);
    auto arrayType = dynamic_cast<ArrayType *>(maybeArrayType);
    if (arrayType == nullptr) {
        throw CodegenError(fmt::format("Subscripting a non-array type {}", maybeArrayType->signature()));
    }

    auto subscriptType = subscript->languageType(genContext);
    auto intType = dynamic_cast<IntegerType *>(subscriptType);
    if (intType == nullptr) {
        throw CodegenError(fmt::format("Subscripting array with a value of non-integer type {}", subscriptType->signature()));
    }
    return arrayType->targetType;
}

llvm::Value *ArraySubscription::getElementPtr(ExpressionGenContext &genContext) {
    auto zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(genContext.codegenContext.context), 0);
    auto idx = subscript->llvmValue(genContext);
    return genContext.builder.CreateGEP(
        array->llvmType(genContext),
        array->llvmValue(genContext),
        { zero, idx });
}

llvm::Value *ArraySubscription::llvmValue(ExpressionGenContext &genContext) {
    // TODO: this doesn't work at all
//    array->llvmType(genContext)->print(llvm::outs());
//    llvm::outs() << '\n';
//    array->llvmValue(genContext)->print(llvm::outs());
//    llvm::outs() << '\n';
//    array->llvmValue(genContext)->getType()->print(llvm::outs());
//    llvm::outs() << '\n';
    auto gep = getElementPtr(genContext);
    return genContext.builder.CreateLoad(llvmType(genContext), gep);
}

std::string ArraySubscription::dump(int indent) { return fmt::format("{}{}[{}]", indentSpaces(indent), array->dump(0), subscript->dump(0)); }

VariableDeclaration::VariableDeclaration(
    const std::string &name, LanguageType *type, std::unique_ptr<Expression> &&initialValue
)
    : Expression(type),
      initialValue(std::move(initialValue)),
      name(name) {}

llvm::Value *VariableDeclaration::llvmValue(ExpressionGenContext &genContext) {
    auto varDef = genContext.insertVariable(name, type);
    if (initialValue != nullptr) {
        auto initialValType = initialValue->languageType(genContext);
        if (initialValType->actualLanguageType() != type->actualLanguageType()) {
            throw CodegenError(fmt::format(
                "Tried to assign a value ot type {} to {}, which is a variable of type {}", initialValType->signature(),
                name, type->signature()
            ));
        }

        assumeExpression(
            genContext, initialValue.get(),
            fmt::format("Expected value for #{} to to be an expression, but it's a statement", name)
        );
        genContext.builder.CreateStore(initialValue->llvmValue(genContext), varDef->value);
    }

    return nullptr;
}

llvm::Type *VariableDeclaration::llvmType(ExpressionGenContext &) { return nullptr; }

std::string VariableDeclaration::dump(int indent) {
    return fmt::format(
        "{}($var {} {}", indentSpaces(indent), name,
        type->signature() + (initialValue == nullptr ? ")" : fmt::format(" {})", initialValue->dump(0)))
    );
}

Sizeof::Sizeof(CodegenContext &context, LanguageType *targetType)
    : Expression(context.getNamed<NamedTypeValue>("u32")),
      targetType(targetType) {
    value = llvm::ConstantInt::get(
        type->llvmType(), context.module.getDataLayout().getTypeAllocSize(targetType->llvmType())
    );
}

std::string Sizeof::dump(int indent) {
    return fmt::format("{}($sizeof {})", indentSpaces(indent), targetType->signature());
}

Cast::Cast(CodegenContext &, LanguageType *targetType, std::unique_ptr<Expression> &&targetExpression)
    : Expression(targetType),
      targetExpression(std::move(targetExpression)) {}

std::string Cast::dump(int indent) {
    return fmt::format("{}($cast {} {})", indentSpaces(indent), type->signature(), targetExpression->dump());
}


llvm::Value *Cast::llvmValue(ExpressionGenContext &genContext) {
    auto targetValue = targetExpression->llvmValue(genContext);
    auto targetType = type->actualLanguageType();
    auto targetLlvmType = targetType->llvmType();
    auto previousType = targetExpression->languageType(genContext)->actualLanguageType();
    auto previousLlvmType = previousType->llvmType();

    auto isNumericType
        = [](llvm::Type *t) { return !t->isPointerTy() && (t->isIntegerTy() || !t->isFloatingPointTy()); };

    if (!(isNumericType(previousLlvmType) && isNumericType(targetLlvmType))
        && !(previousLlvmType->isPointerTy() && targetLlvmType->isPointerTy())
        && !(previousLlvmType->isPointerTy() && targetLlvmType->isIntegerTy())
        && !(previousLlvmType->isIntegerTy() && targetLlvmType->isPointerTy()))
        throw CodegenError(fmt::format(
            "Can only cast from: numeric types to other numeric types, from one pointer to another, from pointer to "
            "integer and back, tried to cast from {} to {}",
            targetExpression->languageType(genContext)->signature(), type->signature()
        ));

    if (targetLlvmType->isIntegerTy()) {
        if (previousLlvmType->isPointerTy()) {
            return genContext.builder.CreatePtrToInt(targetValue, targetLlvmType);
        } else {
            auto intType = dynamic_cast<IntegerType *>(targetType);
            return genContext.builder.CreateIntCast(targetValue, targetLlvmType, intType->isSigned);
        }
    } else if (targetLlvmType->isFloatingPointTy()) {
        return genContext.builder.CreateFPCast(targetValue, targetLlvmType);
    } else if (targetLlvmType->isPointerTy()) {
        if (previousLlvmType->isIntegerTy()) {
            return genContext.builder.CreateIntToPtr(targetValue, targetLlvmType);
        } else {
            return genContext.builder.CreatePointerCast(targetValue, targetLlvmType);
        }
    }

    throw CodegenError("Unknown cast type reached");
}
