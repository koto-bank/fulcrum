#include <iostream>
#include <map>
#include <variant>

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>

#include <fmt/format.h>

#include "codegen_context.hpp"
#include "expressions.hpp"
#include "types.hpp"

VariableDefinition::VariableDefinition(const std::string &name, LanguageType *type)
    : name(name),
      type(type) {}

VariableDefinition *ExpressionGenContext::lookupVariable(const std::string &name) {
    for (auto scope = variableScopes.rbegin(); scope != variableScopes.rend(); ++scope) {
        if (scope->contains(name)) return &scope->at(name);
    }
    if (codegenContext.existsNamed(name)) return codegenContext.getNamed<NamedVariableValue>(name);

    return nullptr;
}

VariableDefinition *
ExpressionGenContext::insertVariable(const std::string &name, LanguageType *type) {
    if (lookupVariable(name) != nullptr)
        throw CodegenError(fmt::format("Variable {} already defined", name));

    auto allocated = builder.CreateAlloca(type->llvmType(), 0, name);
    auto emplaced = variableScopes.back().emplace(name, VariableDefinition(name, type));

    auto varDef = &emplaced.first->second;
    varDef->value = allocated;

    return varDef;
}

void ExpressionGenContext::pushScope() { variableScopes.push_back({}); }

void ExpressionGenContext::popScope() { variableScopes.pop_back(); }

void Expression::assumeExpression(
    ExpressionGenContext &genContext, Expression *expr, const std::string &errorMessage
) {
    if (expr->languageType(genContext)->actualLanguageType()
        == genContext.codegenContext.getNamed<NamedTypeValue>("void"))
        throw CodegenError(errorMessage);
}

std::string Expression::indentSpaces(int n) { return fmt::format("{: >{}}", "", n); }

Expression::Expression(LanguageType *type)
    : type(type) {}

LanguageType *Expression::languageType(ExpressionGenContext &) { return type; }

llvm::Type *Expression::llvmType(ExpressionGenContext &genContext) {
    return languageType(genContext)->llvmType();
}

llvm::Value *Expression::llvmValue(ExpressionGenContext &) { return value; }

bool Expression::isTerminator() { return false; }

IntegerConstant::IntegerConstant(IntegerType *type, IsLongInteger auto _constValue)
    : Expression(type),
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

    return isSigned
        ? fmt::format(
            "{}{}{}", indentSpaces(indent), std::get<int64_t>(constValue), type->signature()
        )
        : fmt::format(
            "{}{}{}", indentSpaces(indent), std::get<uint64_t>(constValue), type->signature()
        );
}

FloatConstant::FloatConstant(LanguageType *type, IsFloatingPoint auto constValue_)
    : Expression(type),
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
        floatbits == FloatType::Bits::Double ? std::get<double>(constValue)
                                             : std::get<float>(constValue),
        type->signature()
    );
}

StringConstant::StringConstant(LanguageType *type, const std::string &constValue)
    : Expression(type),
      constValue(constValue) {}

llvm::Value *StringConstant::llvmValue(ExpressionGenContext &genContext) {
    if (llvmConst == nullptr) { llvmConst = genContext.builder.CreateGlobalString(constValue); }

    auto Zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(type->llvmType()->getContext()), 0);
    llvm::Constant *Indices[] = { Zero, Zero };
    return llvm::ConstantExpr::getInBoundsGetElementPtr(
        llvmConst->getValueType(), llvmConst, Indices
    );
}

std::string StringConstant::dump(int indent) {
    return fmt::format("{}\"{}\"", indentSpaces(indent), constValue);
}

BoolConstant::BoolConstant(LanguageType *type, bool constValue)
    : Expression(type),
      constValue(constValue) {
    value = llvm::ConstantInt::get(type->llvmType(), constValue ? 1 : 0);
}

std::string BoolConstant::dump(int indent) {
    return fmt::format("{}{}", indentSpaces(indent), constValue);
}

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
    if (args.size() != 0 && args.size() != 1) {
        throw CodegenError("Return must have 0 or 1 arguments");
    }

    auto returnType = genContext.function->functionType()->returnType;
    if (args.size() == 0
        && returnType->actualLanguageType()
            != genContext.codegenContext.getNamed<NamedTypeValue>("void")) {
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

    if (args.size() < 2 || args.size() > 3) {
        throw CodegenError("If must have from 2 to 3 arguments");
    }
    if (args[0]->languageType(genContext)->actualLanguageType()
        != context.getNamed<NamedTypeValue>("bool")) {
        throw CodegenError("First argument to if must be boolean");
    }

    auto &builder = genContext.builder;

    auto ifCondition = args[0]->llvmValue(genContext);

    auto thenBlock
        = llvm::BasicBlock::Create(context.context, "if-then", genContext.function->llvmFunction());
    auto elseBlock = args.size() == 3
        ? llvm::BasicBlock::Create(context.context, "if-else", genContext.function->llvmFunction())
        : nullptr;
    auto afterIfBlock = llvm::BasicBlock::Create(
        context.context, "after-if", genContext.function->llvmFunction()
    );
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

llvm::Value *FunctionCall::arithmeticsProcessor(ExpressionGenContext &genContext) {
    if (args.size() == 0) {
        throw CodegenError(fmt::format("Expected at least 1 argument to {}, but got 0", name));
    }
    if ((name == "=" || name[0] == '>' || name[0] == '<') && args.size() != 2)
        throw CodegenError(
            fmt::format("Expected exactly 2 argument to {}, but got {}", name, args.size())
        );

    auto expectedType = args[0]->languageType(genContext)->actualLanguageType();
    auto intType = dynamic_cast<IntegerType *>(expectedType);
    auto floatType = intType == nullptr ? nullptr : dynamic_cast<FloatType *>(expectedType);

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
                "Expected all arguments to {} to be of type {}, but argument #{} was of type {}",
                name, expectedType->signature(), i, arg->languageType(genContext)->signature()
            ));
        }
    }

    using namespace std::placeholders;
    std::function<llvm::Value *(llvm::IRBuilderBase *, llvm::Value *, llvm::Value *)>
        buildOperation;

    switch (name[0]) {
    case '+':
        if (intType)
            buildOperation
                = std::bind(&llvm::IRBuilderBase::CreateAdd, _1, _2, _3, "", false, false);
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
            buildOperation
                = std::bind(&llvm::IRBuilderBase::CreateFCmpOEQ, _1, _2, _3, "", nullptr);
        break;
    case '!': {
        assert(name[1] == '=');
        if (intType)
            buildOperation = std::bind(&llvm::IRBuilderBase::CreateICmpNE, _1, _2, _3, "");
        else
            buildOperation
                = std::bind(&llvm::IRBuilderBase::CreateFCmpONE, _1, _2, _3, "", nullptr);
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
                buildOperation
                    = std::bind(&llvm::IRBuilderBase::CreateFCmpOGE, _1, _2, _3, "", nullptr);
            } else {
                buildOperation
                    = std::bind(&llvm::IRBuilderBase::CreateFCmpOGT, _1, _2, _3, "", nullptr);
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
                buildOperation
                    = std::bind(&llvm::IRBuilderBase::CreateFCmpOLE, _1, _2, _3, "", nullptr);
            } else {
                buildOperation
                    = std::bind(&llvm::IRBuilderBase::CreateFCmpOLT, _1, _2, _3, "", nullptr);
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

LanguageType *FunctionCall::voidProcessorType(ExpressionGenContext &genContext) {
    return genContext.codegenContext.getNamed<NamedTypeValue>("void");
}

LanguageType *FunctionCall::arithmeticsProcessorType(ExpressionGenContext &genCont) {
    if (args.size() == 0) {
        throw CodegenError(fmt::format("Expected at least 1 argument to {}, but got 0", name));
    }
    if (name == "=" || name[0] == '>' || name[0] == '<')
        return genCont.codegenContext.getNamed<NamedTypeValue>("bool");

    return args[0]->languageType(genCont);
}

llvm::Value *FunctionCall::setProcessor(ExpressionGenContext &genContext) {
    if (args.size() % 2 != 0) {
        throw CodegenError(
            fmt::format("Expected an even number of arguments to set, but got {}", args.size())
        );
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
                throw CodegenError(
                    fmt::format("Dereferencing a non-pointer type {}", ptrType->signature())
                );
            varAddress = derefTarget->llvmValue(genContext);
        }
        if (variableType == nullptr) {
            auto maybeVarExpr = dynamic_cast<VarAccess *>(args[i].get());
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
                "Variable {} is of type {}, argument to set is of type {}", args[i]->dump(),
                variableType->signature(), newValType->signature()
            ));

        genContext.builder.CreateStore(newValue->llvmValue(genContext), varAddress);
    }

    return nullptr;
}

llvm::Value *FunctionCall::whileProcessor(ExpressionGenContext &genContext) {
    if (args.size() != 2) {
        throw CodegenError(fmt::format("Expected 2 arguments to while, but got {}", args.size()));
    }
    if (args[0]->languageType(genContext)->actualLanguageType()
        != genContext.codegenContext.getNamed<NamedTypeValue>("bool")) {
        throw CodegenError("First argument to while must be boolean");
    }

    auto &llContext = genContext.codegenContext.context;
    auto whileCondBlock
        = llvm::BasicBlock::Create(llContext, "while-cond", genContext.function->llvmFunction());
    genContext.builder.CreateBr(whileCondBlock);
    genContext.builder.SetInsertPoint(whileCondBlock);
    auto condValue = args[0]->llvmValue(genContext);

    auto condTrueBlock
        = llvm::BasicBlock::Create(llContext, "while-true", genContext.function->llvmFunction());
    auto condAfterBlock
        = llvm::BasicBlock::Create(llContext, "while-after", genContext.function->llvmFunction());

    genContext.builder.CreateCondBr(condValue, condTrueBlock, condAfterBlock);
    genContext.builder.SetInsertPoint(condTrueBlock);

    genContext.pushScope();
    args[1]->llvmValue(genContext);
    if (!args[1]->isTerminator()) { genContext.builder.CreateBr(whileCondBlock); }
    genContext.popScope();

    genContext.builder.SetInsertPoint(condAfterBlock);

    return nullptr;
}

LanguageType *FunctionCall::languageType(ExpressionGenContext &genContext) {
    if (type == nullptr) {
        if (specialFunctions.contains(name)) {
            type = specialFunctions[name].second(this, genContext);
        } else {
            type = genContext.codegenContext.getNamed<NamedFunctionValue>(name)
                       ->functionType()
                       ->returnType;
        }
    }

    return type;
}

llvm::Value *FunctionCall::llvmValue(ExpressionGenContext &genContext) {
    if (specialFunctions.contains(name)) return specialFunctions[name].first(this, genContext);

    auto calledFunction = genContext.codegenContext.getNamed<NamedFunctionValue>(name);

    std::vector<llvm::Value *> argValues;
    for (auto i = 0u; i < args.size(); i++) {
        LanguageType *argType = args[i]->languageType(genContext);
        LanguageType *expectedType = calledFunction->functionType()->arguments[i];
        if (argType->actualLanguageType() != expectedType->actualLanguageType()) {
            throw CodegenError(fmt::format(
                "Incompatible argument type in {}: for argument #{}"
                " expected {}, but received {}",
                name, i, expectedType->signature(), argType->signature()
            ));
        }
        argValues.push_back(args[i]->llvmValue(genContext));
    }

    return genContext.builder.CreateCall(calledFunction->llvmFunction(), argValues);
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

    return fmt::format("{}({} {})", indentSpaces(indent), name, fmt::join(argDumps, " "));
}

VarAccess::VarAccess(const std::string &name)
    : Expression(nullptr),
      name(name) {}

LanguageType *VarAccess::languageType(ExpressionGenContext &genCont) {
    auto pathName = path();

    if (pathName.size() > 1) {
        LanguageType *currentType = nullptr;
        for (auto &currentName : pathName) {
            if (currentType == nullptr) {
                currentType = genCont.lookupVariable(currentName)->type;
            } else {
                auto *structType = dynamic_cast<StructType *>(currentType);
                if (structType == nullptr)
                    throw CodegenError(
                        fmt::format("Expected {} to be a structure type", currentType->signature())
                    );
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

const std::vector<std::string> &VarAccess::path() {
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

    return varPath;
}

llvm::Value *VarAccess::varAddress(ExpressionGenContext &genCont) {
    auto pathName = this->path();

    llvm::Value *currentValue = nullptr;
    LanguageType *currentType = nullptr;

    auto loadStructField = [&](const std::string &currentName) {
        if (currentValue == nullptr) {
            auto var = genCont.lookupVariable(currentName);
            currentValue = var->value;
            currentType = var->type;
        } else {
            auto *structType = dynamic_cast<StructType *>(currentType);
            if (structType == nullptr)
                throw CodegenError(
                    fmt::format("Expected {} to be a structure type", currentType->signature())
                );
            auto fieldIndex = structType->fieldIndex(currentName);

            currentValue
                = genCont.builder.CreateStructGEP(structType->llvmType(), currentValue, fieldIndex);
            currentType = std::get<1>(structType->fields[fieldIndex]);
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

llvm::Value *VarAccess::llvmValue(ExpressionGenContext &genCont) {
    if (path().size() > 1)
        return genCont.builder.CreateLoad(llvmType(genCont), varAddress(genCont));

    return genCont.builder.CreateLoad(llvmType(genCont), varAddress(genCont));
}

std::string VarAccess::dump(int indent) { return fmt::format("{}{}", indentSpaces(indent), name); }

AddrOf::AddrOf(std::unique_ptr<Expression> &&target)
    : Expression(nullptr),
      target(std::move(target)) {}

LanguageType *AddrOf::languageType(ExpressionGenContext &genCont) {
    auto maybeVar = dynamic_cast<VarAccess *>(target.get());
    if (maybeVar == nullptr)
        throw CodegenError(
            fmt::format("Expected a variable to take an address of, got {}", target->dump())
        );

    auto varType = maybeVar->languageType(genCont);
    auto ptrTypeName = varType->signature() + "*";
    return genCont.codegenContext.getOrEmplaceType<PointerType>(ptrTypeName, varType);
}

llvm::Value *AddrOf::llvmValue(ExpressionGenContext &genCont) {
    auto maybeVar = dynamic_cast<VarAccess *>(target.get());
    if (maybeVar == nullptr)
        throw CodegenError(
            fmt::format("Expected a variable to take an address of, got {}", target->dump())
        );

    return maybeVar->varAddress(genCont);
}

std::string AddrOf::dump(int indent) {
    return fmt::format("{}&{}", indentSpaces(indent), target->dump(0));
}

Dereference::Dereference(std::unique_ptr<Expression> &&target)
    : Expression(nullptr),
      target(std::move(target)) {}

LanguageType *Dereference::languageType(ExpressionGenContext &genCont) {
    auto derefing = target->languageType(genCont);
    auto ptrType = dynamic_cast<PointerType *>(derefing);
    if (ptrType == nullptr)
        throw CodegenError(fmt::format("Dereferencing a non-pointer type {}", derefing->signature())
        );

    return ptrType->pointerTo;
}

llvm::Value *Dereference::llvmValue(ExpressionGenContext &genCont) {
    return genCont.builder.CreateLoad(
        languageType(genCont)->llvmType(), target->llvmValue(genCont)
    );
}

std::string Dereference::dump(int indent) {
    return fmt::format("{}@{}", indentSpaces(indent), target->dump(0));
}

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
                "Tried to assign a value ot type {} to {}, which is a variable of type {}",
                initialValType->signature(), name, type->signature()
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
        type->signature()
            + (initialValue == nullptr ? ")" : fmt::format(" {})", initialValue->dump(0)))
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
