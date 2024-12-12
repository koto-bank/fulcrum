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

VariableDefinition::VariableDefinition(const std::string &name, const LanguageType *type)
    : name(name),
      type(type) {}

const VariableDefinition *ExpressionGenContext::lookupVariable(const std::string &name) const {
    for (auto scope = variableScopes.rbegin(); scope != variableScopes.rend(); ++scope) {
        if (scope->contains(name)) return &scope->at(name);
    }
    if (codegenContext.globalVars.contains(name)) return codegenContext.globalVars.at(name).get();

    return nullptr;
}

VariableDefinition *ExpressionGenContext::insertVariable(const std::string &name, const LanguageType *type) {
    if (lookupVariable(name) != nullptr) throw CodegenError(fmt::format("Variable {} already defined", name));

    auto allocated = builder.CreateAlloca(type->llvmType(), 0, name);
    auto emplaced = variableScopes.back().emplace(name, VariableDefinition(name, type));

    auto varDef = &emplaced.first->second;
    varDef->value = allocated;

    return varDef;
}

VariableDefinition *ExpressionGenContext::insertFunctionArgument(const std::string &name, const LanguageType *type) {
    if (lookupVariable(name) != nullptr) {
        throw CodegenError(fmt::format("Argument {} already defined", name));
    }

    auto allocated = builder.CreateAlloca(type->llvmType(), 0, name + ".addr");
    auto emplaced = variableScopes.back().emplace(name, VariableDefinition(name, type));

    auto varDef = &emplaced.first->second;
    varDef->value = allocated;

    return varDef;
}

void ExpressionGenContext::pushScope() { variableScopes.push_back({}); }

void ExpressionGenContext::popScope() { variableScopes.pop_back(); }

void Expression::assumeExpression(ExpressionGenContext &genContext, Expression *expr, const std::string &errorMessage) {
    if (expr->languageType(genContext)
        == genContext.codegenContext.voidType)
        throw CodegenError(errorMessage);
}

std::string Expression::indentSpaces(int n) const { return fmt::format("{: >{}}", "", n); }

Expression::Expression(const LanguageType *type)
    : type(type) {}

const LanguageType *Expression::languageType(const ExpressionGenContext &) { return type; }

llvm::Type *Expression::llvmType(ExpressionGenContext &genContext) { return languageType(genContext)->llvmType(); }

llvm::Value *Expression::llvmValue(ExpressionGenContext &) { return value; }

bool Expression::isTerminator() { return false; }


llvm::Constant *ConstantExpression::llvmConstant(CodegenContext &) { return (llvm::Constant *)value; }

IntegerConstant::IntegerConstant(const IntegerType *type, IsLongInteger auto _constValue)
    : ConstantExpression(type),
      constValue(_constValue) {
    if (!llvm::ConstantInt::isValueValidForType(type->llvmType(), _constValue)) {
        throw CodegenError(fmt::format("Integer {} does not fit into its type", _constValue));
    }

    value = llvm::ConstantInt::get(type->llvmType(), _constValue);
}
template IntegerConstant::IntegerConstant(const IntegerType *type, int64_t _constValue);
template IntegerConstant::IntegerConstant(const IntegerType *type, uint64_t _constValue);

std::string IntegerConstant::dump(int indent) const {
    auto isSigned = static_cast<const IntegerType *>(type)->isSigned;

    return isSigned ? fmt::format("{}{}{}", indentSpaces(indent), std::get<int64_t>(constValue), type->signature())
                    : fmt::format("{}{}{}", indentSpaces(indent), std::get<uint64_t>(constValue), type->signature());
}

FloatConstant::FloatConstant(const FloatType *type, IsFloatingPoint auto constValue_)
    : ConstantExpression(type),
      constValue(constValue_) {
    auto apFloat = llvm::APFloat(constValue_);
    if (!llvm::ConstantFP::isValueValidForType(type->llvmType(), apFloat)) {
        throw CodegenError(fmt::format("Float {} does not fit into its type", constValue_));
    }
    value = llvm::ConstantFP::get(type->llvmType(), apFloat);
}
template FloatConstant::FloatConstant(const FloatType *type, float constValue_);
template FloatConstant::FloatConstant(const FloatType *type, double constValue_);

std::string FloatConstant::dump(int indent) const {
    auto floatbits = ((FloatType *)type)->bits;

    return fmt::format(
        "{}{}{}", indentSpaces(indent),
        floatbits == FloatType::Bits::Double ? std::get<double>(constValue) : std::get<float>(constValue),
        type->signature()
    );
}

StringConstant::StringConstant(const LanguageType *type, const std::string &constValue)
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

std::string StringConstant::dump(int indent) const { return fmt::format("{}\"{}\"", indentSpaces(indent), constValue); }

BoolConstant::BoolConstant(const LanguageType *type, bool constValue)
    : ConstantExpression(type),
      constValue(constValue) {
    value = llvm::ConstantInt::get(type->llvmType(), constValue ? 1 : 0);
}

std::string BoolConstant::dump(int indent) const { return fmt::format("{}{}", indentSpaces(indent), constValue); }

FunctionCall::FunctionCall(const std::string &name, Args &&args)
    // Initialize type with nullptr for now, since we don't know the return type yet
    : Expression(nullptr),
      name(name),
      args(std::move(args)) {}

llvm::Value *FunctionCall::returnProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 0 && args.size() != 1) { throw CodegenError("Return must have 0 or 1 arguments"); }

    auto returnType = genContext.function->functionType()->returnType;
    if (args.size() == 0
        && returnType != genContext.codegenContext.voidType) {
        throw CodegenError("Only void function can return nothing");
    } else if (args.size() == 1
               && returnType != args[0]->languageType(genContext)) {
        throw CodegenError(fmt::format(
            "Function {} expected to return {}, but returns {}", name, returnType->signature(),
            args[0]->languageType(genContext)->signature()
        ));
    }

    if (args.size() == 0) {
        genContext.builder.CreateRetVoid();
    } else {
        genContext.builder.CreateRet(args[0]->llvmValue(genContext));
    }
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
    if (args[0]->languageType(genContext) != context.boolType) {
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
    auto targetType = targetArg->languageType(genContext);
    auto ptrType = dynamic_cast<const PointerType *>(targetType);
    if (ptrType == nullptr) {
        throw CodegenError(fmt::format(
            "Expected first argument to {} to be of an integer type, but it was of type {}", name,
            type->signature()
        ));
    }

    const auto &offsetArg = args[1];
    auto offsetType = offsetArg->languageType(genContext);
    auto intType = dynamic_cast<const IntegerType *>(offsetType);
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
    } else if (name != "ptr+") { // != here! ptr+ _is_ supported!
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

    auto expectedType = args[0]->languageType(genContext);
    auto intType = dynamic_cast<const IntegerType *>(expectedType);
    auto floatType = intType == nullptr ? dynamic_cast<const FloatType *>(expectedType) : nullptr;

    if (intType == nullptr && floatType == nullptr) {
        throw CodegenError(fmt::format(
            "Expected first argument to {} to be of a numeric type, but it was of type {}", name,
            expectedType->signature()
        ));
    }

    // TODO: auto casts for signed/unsigned and various bit width
/*
    for (auto i = 0u; i < args.size(); i++) {
        auto &arg = args[i];

        if (arg->languageType(genContext) != expectedType) {
            throw CodegenError(fmt::format(
                "Expected all arguments to {} to be of type {}, but argument #{} was of type {}", name,
                expectedType->signature(), i, arg->languageType(genContext)->signature()
            ));
        }
    }
*/
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

const LanguageType *FunctionCall::voidProcessorType(const ExpressionGenContext &genContext) const {
    return genContext.codegenContext.voidType;
}

const LanguageType *FunctionCall::boolProcessorType(const ExpressionGenContext &genContext) const {
    return genContext.codegenContext.boolType;
}

const LanguageType *FunctionCall::addrOfProcessorType(const ExpressionGenContext &genContext) const {
    if (args.size() != 1) { throw CodegenError(fmt::format("Expected exactly 1 argument to {}, but got {}", name, args.size())); }
    const auto &target = args[0];
    auto maybeVar = dynamic_cast<SymbolAccess *>(target.get());
    if (maybeVar == nullptr) {
        throw CodegenError(fmt::format("Expected a variable to take an address of, got {}", target->dump()));
    }
    auto varType = maybeVar->languageType(genContext);
    auto ret = genContext.codegenContext.emplaceType<PointerType>(varType);
    return ret;
}

const LanguageType *FunctionCall::prognType(const ExpressionGenContext &genContext) const {
    if (args.empty()) {
        return genContext.codegenContext.voidType;
    }
    const LanguageType *retType;
    for (auto &arg : args) {
        retType = arg->languageType(genContext);
    }

    fc_assert(retType != nullptr);
    return retType;
}

const LanguageType *FunctionCall::arithmeticsProcessorType(const ExpressionGenContext &genCont) const {
    if (args.size() == 0) { throw CodegenError(fmt::format("Expected at least 1 argument to {}, but got 0", name)); }
    if (name == "=" || name == "!=" || name[0] == '>' || name[0] == '<')
        return genCont.codegenContext.boolType;
    // TODO: move type processing to separate step
    return args[0]->languageType(genCont);
}

const LanguageType *FunctionCall::ptrArithmeticsProcessorType(const ExpressionGenContext &genCont) const {
    if (args.size() != 2) { throw CodegenError(fmt::format("Expected exactly 2 arguments to {}, but got {}", name, args.size())); }
    return args[0]->languageType(genCont);
}

llvm::Value *FunctionCall::setProcessor(ExpressionGenContext &genContext) {
    if (args.size() % 2 != 0) {
        throw CodegenError(fmt::format("Expected an even number of arguments to set, but got {}", args.size()));
    }

    for (auto i = 0u; i < args.size(); i++) {
        const LanguageType *variableType = nullptr;
        llvm::Value *varAddress = nullptr;

        auto maybeDeref = dynamic_cast<Dereference *>(args[i].get());
        if (maybeDeref != nullptr) {
            variableType = maybeDeref->languageType(genContext);

            auto *derefTarget = maybeDeref->target.get();
            auto ptrType = dynamic_cast<const PointerType *>(derefTarget->languageType(genContext));
            if (ptrType == nullptr)
                throw CodegenError(fmt::format("Dereferencing a non-pointer type {}", ptrType->signature()));
            varAddress = derefTarget->llvmValue(genContext);
        }
        auto maybeSubscription = dynamic_cast<InboundAccess *>(args[i].get());
        if (maybeSubscription != nullptr) {
            variableType = maybeSubscription->languageType(genContext);
            varAddress = maybeSubscription->getElementPtr(genContext);
        }
        if (variableType == nullptr) {
            auto maybeVarExpr = dynamic_cast<SymbolAccess *>(args[i].get());
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

        auto newValType = newValue->languageType(genContext);
        if (!LanguageType::assignable(variableType, newValType))
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
    if (args[0]->languageType(genContext)
        != genContext.codegenContext.boolType) {
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
            || args[0]->languageType(genContext)
            != genContext.codegenContext.boolType)
            throw CodegenError(fmt::format("'not' expects exactly one argument of type bool", name));

        assumeExpression(
            genContext, args[0].get(), fmt::format("Expected argument to '{}' to to be an expression", name)
            );
        return genContext.builder.CreateNot(args[0]->llvmValue(genContext));
    }

    if (args.size() != 2
        || args[0]->languageType(genContext)
            != genContext.codegenContext.boolType
        || args[1]->languageType(genContext)
            != genContext.codegenContext.boolType) {
        throw CodegenError(fmt::format("'{}' expects exactly two arguments of type bool", name));
    }

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

const LanguageType *FunctionCall::languageType(const ExpressionGenContext &genContext) {
    if (type == nullptr) {
        if (specialFunctions.contains(name)) {
            type = specialFunctions.at(name).second(this, genContext);
        } else {
            if (genContext.codegenContext.namedFunctions.find(name) == genContext.codegenContext.namedFunctions.end()) {
                throw CodegenError(fmt::format("Function {} is undefined", name));
            }
            type = genContext.codegenContext.namedFunctions.at(name)->functionType()->returnType;
        }
    }

    return type;
}

llvm::Value *FunctionCall::addrOfProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 1) {
        throw CodegenError("'addr-of' expects exactly one argument");
    }
    const auto &target = args[0];
    auto maybeVar = dynamic_cast<SymbolAccess *>(target.get());
    if (maybeVar == nullptr) {
        throw CodegenError(fmt::format("Expected a variable to take an address of, got {}", target->dump()));
    }
    return maybeVar->varAddress(genContext);
}

llvm::Value *FunctionCall::prognValue(ExpressionGenContext &genContext) {
    if (args.empty()) {
        return nullptr;
    }
    llvm::Value *val;
    for (auto &a : args) {
        val = a->llvmValue(genContext);
    }
    return val;
}

llvm::Value *FunctionCall::llvmValue(ExpressionGenContext &genContext) {
    if (specialFunctions.contains(name)) {
        return specialFunctions.at(name).first(this, genContext);
    }

    if (genContext.codegenContext.namedFunctions.find(name) == genContext.codegenContext.namedFunctions.end()) {
        throw CodegenError(fmt::format("Function {} is undefined", name));
    }
    auto calledFunction = genContext.codegenContext.namedFunctions.at(name).get();

    std::vector<llvm::Value *> argValues;
    auto fnType = calledFunction->functionType();
    for (auto i = 0u; i < args.size(); i++) {
        const LanguageType *argType = args[i]->languageType(genContext);
        if (!fnType->isVariadic || i < fnType->arguments.size()) {
            const LanguageType *expectedType = fnType->arguments[i];
            if (!LanguageType::assignable(expectedType, argType)) {
                throw CodegenError(fmt::format(
                                       "Incompatible argument type in {}: for argument #{}"
                                       " expected {}, but received {}",
                                       name, i, expectedType->signature(), argType->signature()
                                       ));
            }
        }
        llvm::Value *value = args[i]->llvmValue(genContext);
        // we have to decay array to pointer when passing it to a function, instead of storing its value
        if (dynamic_cast<const ArrayType *>(argType) != nullptr) {
            value = genContext.builder.CreateConstInBoundsGEP2_64(args[i]->llvmType(genContext), value, 0u, 0u);
            value->setName("array.decay");
        }
        argValues.push_back(value);
    }

    auto ret = genContext.builder.CreateCall(calledFunction->llvmFunction(), argValues);
    return ret;
}

bool FunctionCall::isTerminator() {
    if (name == "return") {
        return true;
    }
    if (name == "do") {
        for (auto &arg : args) {
            if (arg->isTerminator()) {
                return true;
            }
        }
    }

    if (name == "while") {
        for (auto &arg : args) {
            if (arg->isTerminator()) {
                return true;
            }
        }
    }

    return false;
}

std::string FunctionCall::dump(int indent) const {
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

SymbolAccess::SymbolAccess(const std::string &path)
    : Expression(nullptr),
      name(path) {}

const LanguageType *SymbolAccess::languageType(const ExpressionGenContext &genCont) {
    auto var = genCont.lookupVariable(name);
    if (var == nullptr) {
        throw CodegenError(fmt::format("Variable {} not defined", name));
    }
    if (auto at = dynamic_cast<const ArrayType *>(var->type); at != nullptr) {
        return at->decay();
    }
    return var->type;
}

llvm::Value *SymbolAccess::varAddress(ExpressionGenContext &genCont) {
    auto var = genCont.lookupVariable(name);
    if (var == nullptr) throw CodegenError(fmt::format("Variable {} not defined", name));
    return var->value;
}

llvm::Value *SymbolAccess::llvmValue(ExpressionGenContext &genContext) {
    auto var = genContext.lookupVariable(name);
    if (dynamic_cast<const ArrayType *>(var->type) != nullptr) {
        // no need to store arrays, since they are alloca'd and thus immutable
        return var->value;
    }
    if (dynamic_cast<const StructType *>(var->type) != nullptr) {
        // alloca'd structs don't need to be loaded also
        return var->value;
    }
    return genContext.builder.CreateLoad(llvmType(genContext), varAddress(genContext));
}

std::string SymbolAccess::dump(int indent) const { return fmt::format("{}{}", indentSpaces(indent), name); }

Dereference::Dereference(std::unique_ptr<Expression> &&target)
    : Expression(nullptr),
      target(std::move(target)) {}

const LanguageType *Dereference::languageType(const ExpressionGenContext &genCont) {
    auto derefing = target->languageType(genCont);
    auto ptrType = dynamic_cast<const PointerType *>(derefing);
    if (ptrType == nullptr)
        throw CodegenError(fmt::format("Dereferencing a non-pointer type {}", derefing->signature()));

    return ptrType->targetType;
}

llvm::Value *Dereference::llvmValue(ExpressionGenContext &genCont) {
    return genCont.builder.CreateLoad(languageType(genCont)->llvmType(), target->llvmValue(genCont));
}

std::string Dereference::dump(int indent) const { return fmt::format("{}@{}", indentSpaces(indent), target->dump(0)); }

InboundAccess::InboundAccess(std::unique_ptr<Expression> &&target, std::unique_ptr<Expression> &&subscript)
    : Expression(nullptr)
    , target(std::move(target))
    , subscript(std::move(subscript)) {}

const LanguageType *InboundAccess::languageType(const ExpressionGenContext &genContext) {
    targetType = target->languageType(genContext);
    auto structType = dynamic_cast<const StructType *>(targetType);
    if (structType == nullptr) {
        auto arrayType = dynamic_cast<const ArrayType *>(targetType);
        auto ptrType = dynamic_cast<const PointerType *>(targetType);
        if (arrayType == nullptr
            && ptrType == nullptr) {
            fc_assert(targetType != nullptr);
            throw CodegenError(fmt::format("Subscripting a non-subscriptable type {}", targetType->signature()));
        }

        auto subscriptType = subscript->languageType(genContext);
        auto intType = dynamic_cast<const IntegerType *>(subscriptType);
        if (intType == nullptr) {
            throw CodegenError(fmt::format("Subscripting with a value of non-integer type {}", subscriptType->signature()));
        }
        return arrayType != nullptr ? arrayType->targetType : ptrType->targetType;
    } else {
        auto sa = dynamic_cast<SymbolAccess *>(subscript.get());
        if (sa == nullptr) {
            throw CodegenError(fmt::format("Expected symbol for field access, got {}", subscript->languageType(genContext)->signature()));
        }
        return structType->fieldType(sa->name);
    }
}

llvm::Value *InboundAccess::getElementPtr(ExpressionGenContext &genContext) {
    // TODO: fix this profound skill issue
    if (targetType == nullptr) {
        languageType(genContext);
    }
    auto structType = dynamic_cast<const StructType *>(targetType);
    if (structType == nullptr) {
        auto idx = subscript->llvmValue(genContext);
        fc_assert(targetType != nullptr);
        return genContext.builder.CreateInBoundsGEP(
            target->llvmType(genContext),
            target->llvmValue(genContext),
            idx);
    } else {
        auto structType = dynamic_cast<const StructType *>(targetType);
        auto sa = dynamic_cast<SymbolAccess *>(subscript.get());
        fc_assert(sa != nullptr);
        auto fieldIndex = structType->fieldIndex(sa->name);
        if (auto unionType = dynamic_cast<const UnionType *>(structType); unionType != nullptr) {
            auto targetType = unionType->fieldType(sa->name);
            if (dynamic_cast<const StructType *>(targetType) != nullptr) {
                return genContext.builder.CreateStructGEP(
                    targetType->llvmType(),
                    target->llvmValue(genContext),
                    fieldIndex,
                    sa->name);
            } else {
                // basic types in union don't require GEP
                return target->llvmValue(genContext);
            }
        } else {
            return genContext.builder.CreateStructGEP(
                target->llvmType(genContext),
                target->llvmValue(genContext),
                fieldIndex,
                sa->name);
        }
    }
}

llvm::Value *InboundAccess::llvmValue(ExpressionGenContext &genContext) {
    auto gep = getElementPtr(genContext);
    return genContext.builder.CreateLoad(llvmType(genContext), gep);
}

std::string InboundAccess::dump(int indent) const {
    auto structType = dynamic_cast<const StructType *>(targetType);
    if (structType == nullptr) {
        return fmt::format("{}{}[{}]", indentSpaces(indent), target->dump(0), subscript->dump(0));
    } else {
        auto sa = dynamic_cast<SymbolAccess *>(subscript.get());
        fc_assert(sa != nullptr);
        return fmt::format("{}.{}", indentSpaces(indent), target->dump(0), sa->name);
    }
}

VariableDeclaration::VariableDeclaration(const std::string &name,
                                         const LanguageType *type,
                                         std::unique_ptr<Expression> &&initialValue)
    : Expression(type),
      initialValue(std::move(initialValue)),
      name(name) {}

llvm::Value *VariableDeclaration::llvmValue(ExpressionGenContext &genContext) {
    auto varDef = genContext.insertVariable(name, type);
    if (initialValue != nullptr) {
        auto initialValType = initialValue->languageType(genContext);
        if (initialValType != type) {
            throw CodegenError(fmt::format(
                "Tried to assign a value ot type {} to {}, which is a variable of type {}", initialValType->signature(),
                name, type->signature()
            ));
        }

        assumeExpression(
            genContext, initialValue.get(),
            fmt::format("Expected value for #{} to to be an expression, but it's a statement", name)
        );

        auto value = initialValue->llvmValue(genContext);
        if (dynamic_cast<const ArrayType *>(initialValType) != nullptr) {
            // decay first, then store
            value = genContext.builder.CreateConstInBoundsGEP2_64(initialValue->llvmType(genContext), value, 0u, 0u);
        }

        genContext.builder.CreateStore(value, varDef->value);
    }

    return nullptr;
}

llvm::Type *VariableDeclaration::llvmType(ExpressionGenContext &) { return nullptr; }

std::string VariableDeclaration::dump(int indent) const {
    return fmt::format(
        "{}($var {} {}", indentSpaces(indent), name,
        type->signature() + (initialValue == nullptr ? ")" : fmt::format(" {})", initialValue->dump(0)))
    );
}

Sizeof::Sizeof(CodegenContext &context, const LanguageType *targetType)
    : Expression(context.namedTypes.at("u32").get()),
      targetType(targetType) {
    value = llvm::ConstantInt::get(
        type->llvmType(), context.module.getDataLayout().getTypeAllocSize(targetType->llvmType())
    );
}

std::string Sizeof::dump(int indent) const {
    return fmt::format("{}($sizeof {})", indentSpaces(indent), targetType->signature());
}

Cast::Cast(CodegenContext &, const LanguageType *targetType, std::unique_ptr<Expression> &&targetExpression)
    : Expression(targetType),
      targetExpression(std::move(targetExpression)) {}

std::string Cast::dump(int indent) const {
    return fmt::format("{}($cast {} {})", indentSpaces(indent), type->signature(), targetExpression->dump());
}


llvm::Value *Cast::llvmValue(ExpressionGenContext &genContext) {
    const auto targetValue = targetExpression->llvmValue(genContext);
    const auto targetType = type;
    const auto targetLlvmType = targetType->llvmType();
    const auto previousType = targetExpression->languageType(genContext);
    const auto previousLlvmType = previousType->llvmType();

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
            auto intType = dynamic_cast<const IntegerType *>(targetType);
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
