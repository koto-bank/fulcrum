#include <llvm/IR/Function.h>

#include <fmt/format.h>

#include "assert.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"
#include "parse_context.hpp"

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
    for (auto i = 0u; i < argumentNames.size(); i++) {
        auto varDef = genContext.insertVariable(argumentNames[i], functionType()->arguments[i]);
        genContext.builder.CreateStore(function->getArg(i), varDef->value);
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
    emplaceType<VAType>(VAType::Signature);
}

bool CodegenContext::existsNamed(const std::string &name) const {
    return names.contains(name);
}

void CodegenContext::emplaceStructType(NameResolver resolveNameFn,
                                       const std::unique_ptr<StructNode> &structNode) {
    auto fullName = resolveNameFn(structNode->name);
    if (auto u = dynamic_cast<UnionNode *>(structNode.get()); u != nullptr) {
        // TODO: private/public unions
        emplaceType<UnionType>(fullName, fullName, u->biggestSize);
    } else {
        emplaceType<StructType>(fullName, fullName, structNode->isPublic);
    }
}

void CodegenContext::emplaceCStructType(NameResolver resolveNameFn,
                                        const std::unique_ptr<StructNode> &structNode) {
    auto fullName = resolveNameFn(structNode->name);
    if (auto u = dynamic_cast<UnionNode *>(structNode.get()); u != nullptr) {
        // TODO: private/public unions
        ensureType<UnionType>(structNode->name, fullName, u->biggestSize);
    } else {
        ensureType<StructType>(structNode->name, fullName, structNode->isPublic);
    }
}

void CodegenContext::emplaceAliasType(NameResolver resolveNameFn,
                                      const std::unique_ptr<AliasNode> &aliasNode) {
    auto fullName = resolveNameFn(aliasNode->name);
    emplaceType<AliasType>(fullName, fullName, getLanguageType(resolveNameFn, aliasNode->target));
}

void CodegenContext::emplaceCAliasType(NameResolver resolveNameFn, const std::unique_ptr<AliasNode> &aliasNode) {
    auto fullName = resolveNameFn(aliasNode->name);
    ensureType<AliasType>(aliasNode->name, fullName, getLanguageType(resolveNameFn, aliasNode->target));
}

void CodegenContext::emplaceGlobalVar(NameResolver resolveNameFn,
                                      const std::unique_ptr<VarDeclarationNode> &varNode) {
    auto fullName = resolveNameFn(varNode->name);

    VariableDefinition varDef(fullName, getLanguageType(resolveNameFn, varNode->type));

    if (varNode->initialValue != nullptr) {
        auto expr = getExpression(resolveNameFn, varNode->initialValue);
        auto constExpr = dynamic_cast<ConstantExpression *>(expr.get());
        if (constExpr == nullptr)
            throw CodegenError(fmt::format("Global variable of non-constant type not supported: {}", fullName));

        auto constVal = constExpr->llvmConstant(*this);
        auto llvmGlobal = new llvm::GlobalVariable(
            module, constVal->getType(), false, llvm::GlobalVariable::PrivateLinkage, constVal, fullName
        );
        varDef.value = llvmGlobal;
        emplaceNamed<NamedVariableValue>(fullName, varDef);
    }
}

void CodegenContext::emplaceFulcrumFunction(NameResolver resolveNameFn,
                                            const std::unique_ptr<FunctionNode> &fnNode) {
    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode->arguments) {
        auto resolvedName = resolveNameFn(name);
        exprArgs.emplace_back(resolvedName, getLanguageType(resolveNameFn, astType));
    }
    Function::Body exprBody;
    for (auto &node : fnNode->body)
        exprBody.push_back(getExpression(resolveNameFn, node));

    auto langName = fnNode->name == "main" ? fnNode->name : resolveNameFn(fnNode->name);
    auto funcName = fnNode->name == "main" ? fnNode->name : resolveNameFn(fnNode->name);

    emplaceNamed<NamedFunctionValue>(langName,
                                     *this,
                                     module,
                                     funcName,
                                     exprArgs,
                                     getLanguageType(resolveNameFn, fnNode->returnType),
                                     std::move(exprBody),
                                     fnNode->isPublic);
}

void CodegenContext::emplaceCFunction(NameResolver resolveNameFn,
                                     const std::unique_ptr<FunctionNode> &fnNode) {
    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode->arguments) {
        auto resolvedName = resolveNameFn(name);
        exprArgs.emplace_back(resolvedName, getLanguageType(resolveNameFn, astType));
    }
    fc_assert(fnNode->body.size() == 0);
    // Fixme: we should properly report this error
    fc_assert(fnNode->name != "main");
    auto langName = fnNode->name;
    auto funcName = resolveNameFn(fnNode->name);

    emplaceNamed<NamedFunctionValue>(langName,
                                     *this,
                                     module,
                                     funcName,
                                     exprArgs,
                                     getLanguageType(resolveNameFn, fnNode->returnType),
                                     Function::Body {},
                                     fnNode->isPublic);
}

void CodegenContext::fillStructTypeFields(NameResolver resolveNameFn,
                                          const std::unique_ptr<StructNode> &structNode) {
    StructType::Fields exprFields;
    for (auto &[name, astType] : structNode->fields)
        exprFields.emplace_back(name, getLanguageType(resolveNameFn, astType));

    auto fullName = resolveNameFn(structNode->name);

    auto structType = static_cast<StructType *>(getNamed<NamedTypeValue>(fullName));
    structType->fillFields(exprFields);
}

void CodegenContext::generate(std::unique_ptr<FulcrumModule> &&fulcrumModule) {
    // First insert all the structure types
    auto nameResolver = [&fulcrumModule](const std::string &name) {
        return fulcrumModule->resolveName(name);
    };

    for (const auto &structNode : fulcrumModule->structs)
        emplaceStructType(nameResolver, structNode);

    // Now insert all alias types
    for (auto &aliasNode : fulcrumModule->aliases)
        emplaceAliasType(nameResolver, aliasNode);

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    for (auto &structNode : fulcrumModule->structs)
        fillStructTypeFields(nameResolver, structNode);

    for (auto &globalVar : fulcrumModule->globalVariables)
        emplaceGlobalVar(nameResolver, globalVar);

    for (auto &function : fulcrumModule->functions)
        emplaceFulcrumFunction(nameResolver, function);
}

void CodegenContext::generate(std::unique_ptr<CModule> &&cModule) {
    for (const auto &[header, structNode] : cModule->structs) {
        auto nameResolver = [](const std::string &name) {
            return name;
        };
        emplaceCStructType(nameResolver, structNode);
    }

    // Now insert all alias types
    for (const auto &[header, aliasNode] : cModule->aliases) {
        auto nameResolver = [](const std::string &name) {
            return name;
        };
        emplaceCAliasType(nameResolver, aliasNode);
    }

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    for (const auto &[header, structNode] : cModule->structs) {
        auto nameResolver = [](const std::string &name) {
            return name;
        };
        fillStructTypeFields(nameResolver, structNode);
    }

    for (const auto &[header, globalVar] : cModule->globalVariables) {
        auto nameResolver = [](const std::string &name) {
            return name;
        };
        emplaceGlobalVar(nameResolver, globalVar);
    }

    for (const auto &[header, function] : cModule->functions) {
        auto nameResolver = [](const std::string &name) {
            return name;
        };
        emplaceCFunction(nameResolver, function);
    }
}


LanguageType *CodegenContext::getLanguageType(NameResolver resolveNameFn,
                                              const std::unique_ptr<ASTType> &type) {
    if (auto t = dynamic_cast<const ASTBuiltinType *>(type.get()); t != nullptr) {
        fc_assert(existsNamed(t->builtinName));
        return getNamed<NamedTypeValue>(t->builtinName);
    } else if (auto t = dynamic_cast<const ASTIntegerType *>(type.get()); t != nullptr) {
        return getNamed<NamedTypeValue>(t->builtinName);
    } else if (auto t = dynamic_cast<const ASTNamedType *>(type.get()); t != nullptr) {
        auto fullName = resolveNameFn(t->name);
        return getNamed<NamedTypeValue>(fullName);
    } else if (auto t = dynamic_cast<const ASTPointerType *>(type.get()); t != nullptr) {
        auto targetLangType = getLanguageType(resolveNameFn, t->targetType);
        auto pointeeName = targetLangType->signature();
        auto ptrName = pointeeName + "*";
        return getOrEmplaceType<PointerType>(ptrName, targetLangType);
    } else if (auto t = dynamic_cast<const ASTArrayType *>(type.get()); t != nullptr) {
        auto targetLangType = getLanguageType(resolveNameFn, t->targetType);
        auto pointeeName = targetLangType->signature();
        auto arrName = fmt::format("{}[{}]", pointeeName, t->size);
        return getOrEmplaceType<ArrayType>(arrName, targetLangType, t->size);
    } else if (auto t = dynamic_cast<const ASTFunctionType *>(type.get()); t != nullptr) {
        std::vector<LanguageType *> exprArgs;
        for (const auto &astType : t->arguments) {
            exprArgs.emplace_back(getLanguageType(resolveNameFn, astType));
        }
        auto retType = getLanguageType(resolveNameFn, t->returnType);
        return getOrEmplaceType<FunctionType>(
            FunctionType::signatureFrom(exprArgs, retType),
            exprArgs, retType);
    } else {
        // new type, unsupported above?
        fc_unreachable();
        return nullptr;
    }
}

std::unique_ptr<Expression> CodegenContext::getExpression(NameResolver resolveNameFn,
                                                          const std::unique_ptr<ASTNode> &node) {
    if (auto t = dynamic_cast<const StructNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const UnionNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const AliasNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const FunctionNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const ConstantStringNode *>(node.get()); t != nullptr) {
        return std::make_unique<StringConstant>(getNamed<NamedTypeValue>("str"), t->value);
    } else if (auto t = dynamic_cast<const ConstantIntNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<IntegerType *>(getLanguageType(resolveNameFn, t->intType));
        fc_assert(tp != nullptr);
        return t->isSigned
            ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(t->value))
            : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(t->value));
    } else if (auto t = dynamic_cast<const ConstantBoolNode *>(node.get()); t != nullptr) {
        return std::make_unique<BoolConstant>(getNamed<NamedTypeValue>("bool"), t->value);
    } else if (auto t = dynamic_cast<const FunctionCallNode *>(node.get()); t != nullptr) {
        FunctionCall::Args argsExprs;
        for (const auto &arg : t->args) {
            argsExprs.push_back(getExpression(resolveNameFn, arg));
        }
        std::string fullName = t->name == "main" ? t->name : (t->name);
        return std::make_unique<FunctionCall>(fullName, std::move(argsExprs));
    } else if (auto t = dynamic_cast<const VarAccessNode *>(node.get()); t != nullptr) {
        return std::make_unique<VarAccess>(resolveNameFn(t->name));
    } else if (auto t = dynamic_cast<const DereferenceNode *>(node.get()); t != nullptr) {
        return std::make_unique<Dereference>(getExpression(resolveNameFn, t->target));
    } else if (auto t = dynamic_cast<const ArrayNthNode *>(node.get()); t != nullptr) {
        return std::make_unique<ArraySubscription>(getExpression(resolveNameFn, t->array),
                                                   getExpression(resolveNameFn, t->subscript));
    } else if (auto t = dynamic_cast<const VarDeclarationNode *>(node.get()); t != nullptr) {
        return std::make_unique<VariableDeclaration>(
            resolveNameFn(t->name),
            getLanguageType(resolveNameFn, t->type),
            t->initialValue != nullptr
            ? getExpression(resolveNameFn, t->initialValue)
            : nullptr);
    } else if (auto t = dynamic_cast<const SizeofNode *>(node.get()); t != nullptr) {
        return std::make_unique<Sizeof>(*this, getLanguageType(resolveNameFn, t->targetType));
    } else if (auto t = dynamic_cast<const CastNode *>(node.get()); t != nullptr) {
        return std::make_unique<Cast>(
            *this,
            getLanguageType(resolveNameFn, t->targetType),
            getExpression(resolveNameFn, t->targetExpression));
    } else {
        // new node, unsupported above?
        fc_unreachable();
        return nullptr;
    }
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
