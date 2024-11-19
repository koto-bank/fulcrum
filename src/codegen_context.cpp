#include <llvm/IR/Function.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "assert.hpp"
#include "ast_name_path.hpp"
#include "ast_nodes.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"
#include "fulcrum_module.hpp"

Function::Function(
    CodegenContext &context,
    llvm::Module &module,
    const std::string &name_,
    const Args &arguments,
    LanguageType *returnType,
    Body &&body,
    bool isPublic,
    bool isVariadic)
    : isPublic(isPublic)
    , name(name_)
    , body(std::move(body)) {
    std::vector<LanguageType *> argumentTypes;
    for (const auto &arg : arguments) {
        argumentNames.push_back(arg.name);
        argumentTypes.push_back(arg.type);
    }

    type = std::make_unique<FunctionType>(context, argumentTypes, returnType, isVariadic);
    std::replace(name.begin(), name.end(), '/', '_');

    auto funcType = static_cast<llvm::FunctionType *>(type->llvmType());
    function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);

    for (auto i = 0u; i < function->arg_size(); i++) {
        function->getArg(i)->setName(argumentNames[i]);
    }
}

const std::string &Function::getName() const { return name; }
FunctionType *Function::functionType() { return type.get(); }
llvm::Function *Function::llvmFunction() { return function; }

void Function::generateExpressions(
    ExpressionGenContext &genContext,
    const std::vector<std::unique_ptr<Expression>> &expressions) {
    std::vector<Expression *> args;
    for (auto &expr : expressions) {
        args.push_back(expr.get());
    }
    generateExpressions(genContext, args);
}

void Function::generateBody(ExpressionGenContext &genContext) {
    if (body.size() == 0) {
        llvm::outs() << "Body for function " << name << " is empty!\n";
        return;
    }

    auto &context = genContext.codegenContext;
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context.context, "enter", function);
    genContext.builder.SetInsertPoint(bb);

    // Insert variables for arguments
    genContext.pushScope();
    for (auto i = 0u; i < argumentNames.size(); i++) {
        auto arg = functionType()->arguments[i];
        auto varDef = genContext.insertFunctionArgument(argumentNames[i], arg);
        if (dynamic_cast<ArrayType *>(arg->actualLanguageType()) != nullptr) {
            genContext.builder.CreateConstGEP2_32(arg->actualLanguageType()->llvmType(), varDef->value, 0, 0);
        } else {
            genContext.builder.CreateStore(function->getArg(i), varDef->value);
        }
    }

    generateExpressions(genContext, body);

    genContext.popScope();

    if (genContext.function->llvmFunction()->back().getTerminator() == nullptr) {
        // If the function is not void, insert unreachable at the end, since the user must return
        // something
        if (genContext.function->functionType()->returnType != context.namedTypes.at("void").get()) {
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

StackedCodegenErrors::StackedCodegenErrors(const std::string &message, std::vector<std::unique_ptr<CodegenError>> &&errors)
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
      module(moduleName, context) {
    emplaceType<FloatType>("f32", FloatType::Bits::Float);
    emplaceType<FloatType>("f64", FloatType::Bits::Double);
    emplaceType<VoidType>("void");
    emplaceType<BoolType>("bool");
    emplaceType<IntegerType>("i8", 8, true);
    emplaceType<IntegerType>("u8", 8, false);
    emplaceType<IntegerType>("i32", 32, true);
    emplaceType<IntegerType>("u32", 32, false);
    emplaceType<IntegerType>("i64", 64, true);
    emplaceType<IntegerType>("u64", 64, false);
    emplaceType<CharType>("char");
    emplaceType<StringType>("str");

    // Why do we need this?
    emplaceType<VAType>(VAType::Signature);
}

CodegenContext::CodegenResult<StructType> CodegenContext::emplaceStructType(StructNode &&structNode) {
    auto name = structNode.name.join();
    if (auto u = dynamic_cast<UnionNode *>(&structNode); u != nullptr) {
        // TODO: private/public unions
        return emplaceType<UnionType>(name, structNode.name, structNode.isPublic);
    } else {
        return emplaceType<StructType>(name, structNode.name, structNode.isPublic);
    }
}

CodegenContext::CodegenResult<StructType> CodegenContext::emplaceCStructType(StructNode &&structNode) {
    auto name = structNode.name.join();
    if (auto u = dynamic_cast<UnionNode *>(&structNode); u != nullptr) {
        // TODO: private/public unions
        return emplaceType<UnionType>(name, structNode.name, u->biggestSize);
    } else {
        return emplaceType<StructType>(name, structNode.name, structNode.isPublic);
    }
}

CodegenContext::CodegenResult<AliasType> CodegenContext::emplaceAliasType(TypeAliasNode &&aliasNode) {
    return emplaceType<AliasType>(aliasNode.name.join(), aliasNode.name, getLanguageType(aliasNode.target).value());
}

CodegenContext::CodegenResult<AliasType> CodegenContext::emplaceCAliasType(TypeAliasNode &&aliasNode) {
    return emplaceType<AliasType>(aliasNode.name.join(), aliasNode.name, getLanguageType(aliasNode.target).value());
}

CodegenContext::CodegenResult<VariableDefinition>
CodegenContext::emplaceGlobalVar(VariableDeclarationNode &&varNode) {
    auto name = varNode.name.join();
    if (globalVars.contains(name)) {
        return std::unexpected(CodegenError(fmt::format("Global variable with name {} already exists", name)));
    }

    auto varDef = std::make_unique<VariableDefinition>(varNode.name.join(), getLanguageType(varNode.type).value());

    if (varNode.initialValue != nullptr) {
        auto expr = getExpression(std::move(varNode.initialValue));
        auto constExpr = dynamic_cast<ConstantExpression *>(expr.get());
        if (constExpr == nullptr) {
            return std::unexpected(CodegenError(fmt::format("Global variable of non-constant type not supported: {}", varNode.name.join())));
        }

        auto constVal = constExpr->llvmConstant(*this);
        auto llvmGlobal = new llvm::GlobalVariable(
            module, constVal->getType(), false, llvm::GlobalVariable::PrivateLinkage, constVal, varNode.name.join()
        );
        varDef->value = llvmGlobal;
    }
    return globalVars.emplace(name, std::move(varDef)).first->second.get();
}

CodegenContext::CodegenResult<Function> CodegenContext::emplaceFulcrumFunction(FunctionNode &&fnNode) {
    // TODO: already existing functions?
    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode.arguments) {
        exprArgs.push_back({ name, getLanguageType(astType).value() });
    }

    Function::Body exprBody;
    for (auto &node : fnNode.body) {
        exprBody.push_back(getExpression(std::move(node)));
    }

    auto funcName = fnNode.name;
    return namedFunctions.emplace(funcName.join(),
                                  std::make_unique<Function>(
                                      *this,
                                      module,
                                      funcName.join(),
                                      exprArgs,
                                      getLanguageType(fnNode.returnType).value(),
                                      std::move(exprBody),
                                      fnNode.isPublic,
                                      fnNode.isVariadic)).first->second.get();
}

CodegenContext::CodegenResult<Function> CodegenContext::emplaceCFunction(FunctionNode &&fnNode) {
    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode.arguments) {
        exprArgs.push_back({ name, getLanguageType(astType).value() });
    }
    fc_assert(fnNode.body.size() == 0);
    // Fixme: we should properly report this error
    fc_assert(fnNode.name.join() != "main");
    auto name = fnNode.name.join();

    if (namedFunctions.contains(name)) {
        return std::unexpected(CodegenError(fmt::format("C function with name {} is already declared",  name)));
    }

    return namedFunctions.emplace(name,
                                  std::make_unique<Function>(
                                      *this,
                                      module,
                                      name,
                                      exprArgs,
                                      getLanguageType(fnNode.returnType).value(),
                                      Function::Body {},
                                      fnNode.isPublic,
                                      fnNode.isVariadic)).first->second.get();
}

void CodegenContext::fillStructTypeFields(StructNode &&structNode) {
    StructType::Fields exprFields;
    for (auto &[name, astType] : structNode.fields) {
        exprFields.emplace_back(name, getLanguageType(astType).value());
    }

    auto structType = static_cast<StructType *>(namedTypes[structNode.name.join()].get());
    structType->fillFields(exprFields);
}

void CodegenContext::generate(FulcrumModule &&fulcrumModule) {
    // First insert all the structure types
    for (auto &structNode : fulcrumModule.structs) {
        emplaceStructType(std::move(structNode));
    }

    // Now insert all alias types
    for (auto &aliasNode : fulcrumModule.typeAliases)
        emplaceAliasType(std::move(aliasNode));

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    for (auto &structNode : fulcrumModule.structs)
        fillStructTypeFields(std::move(structNode));

    for (auto &globalVar : fulcrumModule.globalVariables)
        emplaceGlobalVar(std::move(globalVar));

    for (auto &function : fulcrumModule.functions)
        emplaceFulcrumFunction(std::move(function));
}

void CodegenContext::generate(CModule &&cModule) {
    for (auto &structNode : cModule.structs) {
        emplaceCStructType(std::move(structNode));
    }

    // Now insert all alias types
    for (auto &aliasNode : cModule.typeAliases) {
        emplaceCAliasType(std::move(aliasNode));
    }

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    for (auto &structNode : cModule.structs) {
        fillStructTypeFields(std::move(structNode));
    }

    for (auto &globalVar : cModule.globalVariables) {
        emplaceGlobalVar(std::move(globalVar));
    }

    for (auto &function : cModule.functions) {
        emplaceCFunction(std::move(function));
    }
}

CodegenContext::CodegenResult<LanguageType> CodegenContext::getLanguageType(const ASTType *type) {
    if (auto t = dynamic_cast<const ASTBuiltinType *>(type); t != nullptr) {
        fc_assert(namedTypes.contains(t->builtinName));
        return namedTypes[t->builtinName].get();
    } else if (auto t = dynamic_cast<const ASTIntegerType *>(type); t != nullptr) {
        return namedTypes[t->builtinName].get();
    } else if (auto t = dynamic_cast<const ASTFloatType *>(type); t != nullptr) {
        return namedTypes[t->builtinName].get();
    } else if (auto t = dynamic_cast<const ASTNamedType *>(type); t != nullptr) {
        return namedTypes[t->name.join()].get();
    } else if (auto t = dynamic_cast<const ASTPointerType *>(type); t != nullptr) {
        auto targetLangTypeOrError = getLanguageType(t->targetType);
        if (!targetLangTypeOrError) {
            return targetLangTypeOrError;
        }
        auto pointeeName = targetLangTypeOrError.value()->signature();
        auto ptrName = pointeeName + "*";
        return emplaceType<PointerType>(ptrName, targetLangTypeOrError.value());
    } else if (auto t = dynamic_cast<const ASTArrayType *>(type); t != nullptr) {
        auto targetLangTypeOrError = getLanguageType(t->targetType);
        if (!targetLangTypeOrError) {
            return targetLangTypeOrError;
        }
        auto pointeeName = targetLangTypeOrError.value()->signature();
        auto arrName = fmt::format("{}[{}]", pointeeName, t->size);
        return emplaceType<ArrayType>(arrName, targetLangTypeOrError.value(), t->size);
    } else if (auto t = dynamic_cast<const ASTFunctionType *>(type); t != nullptr) {
        std::vector<LanguageType *> exprArgs;
        for (const auto &astType : t->argumentTypes) {
            exprArgs.emplace_back(getLanguageType(astType).value());
        }
        auto retType = getLanguageType(t->returnType);
        return emplaceType<FunctionType>(
            FunctionType::signatureFrom(exprArgs, retType.value()),
            exprArgs, retType.value(), false);
    } else {
        // new type, unsupported above?
        fc_unreachable();
        return nullptr;
    }
}

std::unique_ptr<Expression> CodegenContext::getExpression(std::unique_ptr<ASTNode> &&node) {
    if (auto t = dynamic_cast<const StructNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const UnionNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const TypeAliasNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const FunctionNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const ConstantStringNode *>(node.get()); t != nullptr) {
        return std::make_unique<StringConstant>(namedTypes.at("str").get(), t->value);
    } else if (auto t = dynamic_cast<const ConstantIntNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<IntegerType *>(getLanguageType(t->intType).value());
        fc_assert(tp != nullptr);
        return t->isSigned
            ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(t->value))
            : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(t->value));
    } else if (auto t = dynamic_cast<const ConstantFloatNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<FloatType *>(getLanguageType(t->floatType).value());
        if (tp->bits == FloatType::Bits::Float) {
            return std::make_unique<FloatConstant>(tp, static_cast<float>(t->value));
        } else {
            return std::make_unique<FloatConstant>(tp, t->value);
        }
    } else if (auto t = dynamic_cast<const ConstantBoolNode *>(node.get()); t != nullptr) {
        return std::make_unique<BoolConstant>(namedTypes.at("bool").get(), t->value);
    } else if (auto t = dynamic_cast<FunctionCallNode *>(node.get()); t != nullptr) {
        FunctionCall::Args argsExprs;
        for (auto &arg : t->args) {
            argsExprs.push_back(getExpression(std::move(arg)));
        }
        for (const auto &a : argsExprs) {
            a->dump();
        }
        std::string fullName = t->name.join();
        return std::make_unique<FunctionCall>(fullName, std::move(argsExprs));
    } else if (auto t = dynamic_cast<const VariableAccessNode *>(node.get()); t != nullptr) {
        return std::make_unique<VariableAccess>(t->name);
    } else if (auto t = dynamic_cast<DereferenceNode *>(node.get()); t != nullptr) {
        return std::make_unique<Dereference>(getExpression(std::move(t->target)));
    } else if (auto t = dynamic_cast<ArrayNthNode *>(node.get()); t != nullptr) {
        return std::make_unique<ArraySubscription>(getExpression(std::move(t->array)),
                                                   getExpression(std::move(t->subscript)));
    } else if (auto t = dynamic_cast<VariableDeclarationNode *>(node.get()); t != nullptr) {
        return std::make_unique<VariableDeclaration>(
            t->name.join(),
            getLanguageType(t->type).value(),
            t->initialValue != nullptr
            ? getExpression(std::move(t->initialValue))
            : nullptr);
    } else if (auto t = dynamic_cast<const SizeofNode *>(node.get()); t != nullptr) {
        return std::make_unique<Sizeof>(*this, getLanguageType(t->targetType).value());
    } else if (auto t = dynamic_cast<CastNode *>(node.get()); t != nullptr) {
        return std::make_unique<Cast>(
            *this,
            getLanguageType(t->targetType).value(),
            getExpression(std::move(t->targetExpression)));
    } else {
        // new node, unsupported above?
        fc_unreachable();
        return nullptr;
    }
}
