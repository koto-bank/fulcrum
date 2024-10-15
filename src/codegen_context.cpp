#include <llvm/IR/Function.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "assert.hpp"
#include "ast_nodes.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"
#include "fulcrum_module.hpp"

Function::Function(
    CodegenContext &context, llvm::Module &module, const std::string &name_, const Args &arguments,
    LanguageType *returnType, Body &&body, bool isPublic, bool isVariadic
)
    : isPublic(isPublic),
      name(name_),
      body(std::move(body)) {
    std::vector<LanguageType *> argumentTypes;
    for (const auto &arg : arguments) {
        argumentNames.push_back(arg.name);
        argumentTypes.push_back(arg.type);
    }

    type = std::make_unique<FunctionType>(context, argumentTypes, returnType, isVariadic);
    std::replace(name.begin(), name.end(), '/', '_');

    auto funcType = static_cast<llvm::FunctionType *>(type->llvmType());
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

    // Why do we need this?
    emplaceType<VAType>(VAType::Signature);
}

bool CodegenContext::existsNamed(const std::string &name) const {
    return names.contains(name);
}

void CodegenContext::emplaceStructType(StructNode &&structNode) {
    if (auto u = dynamic_cast<UnionNode *>(&structNode); u != nullptr) {
        // TODO: private/public unions
        emplaceType<UnionType>(structNode.name, structNode.name, u->biggestSize);
    } else {
        emplaceType<StructType>(structNode.name, structNode.name, structNode.isPublic);
    }
}

void CodegenContext::emplaceCStructType(StructNode &&structNode) {
    if (auto u = dynamic_cast<UnionNode *>(&structNode); u != nullptr) {
        // TODO: private/public unions
        ensureType<UnionType>(structNode.name, structNode.name, u->biggestSize);
    } else {
        ensureType<StructType>(structNode.name, structNode.name, structNode.isPublic);
    }
}

void CodegenContext::emplaceAliasType(TypeAliasNode &&aliasNode) {
    emplaceType<AliasType>(aliasNode.name, aliasNode.name, getLanguageType(aliasNode.target));
}

void CodegenContext::emplaceCAliasType(TypeAliasNode &&aliasNode) {
    ensureType<AliasType>(aliasNode.name, aliasNode.name, getLanguageType(aliasNode.target));
}

void CodegenContext::emplaceGlobalVar(VariableDeclarationNode &&varNode) {
    if (names.contains(varNode.name)) return;
    VariableDefinition varDef(varNode.name, getLanguageType(varNode.type));

    if (varNode.initialValue != nullptr) {
        auto expr = getExpression(std::move(varNode.initialValue));
        auto constExpr = dynamic_cast<ConstantExpression *>(expr.get());
        if (constExpr == nullptr)
            throw CodegenError(fmt::format("Global variable of non-constant type not supported: {}", varNode.name));

        auto constVal = constExpr->llvmConstant(*this);
        auto llvmGlobal = new llvm::GlobalVariable(
            module, constVal->getType(), false, llvm::GlobalVariable::PrivateLinkage, constVal, varNode.name
        );
        varDef.value = llvmGlobal;
        emplaceNamed<NamedVariableValue>(varNode.name, varDef);
    }
}

void CodegenContext::emplaceFulcrumFunction(FunctionNode &&fnNode) {
    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode.arguments) {
        exprArgs.push_back({ name, getLanguageType(astType) });
    }
    Function::Body exprBody;
    for (auto &node : fnNode.body) {
        exprBody.push_back(getExpression(std::move(node)));
    }

    auto funcName = fnNode.name;
    emplaceNamed<NamedFunctionValue>(funcName,
                                     *this,
                                     module,
                                     funcName,
                                     exprArgs,
                                     getLanguageType(fnNode.returnType),
                                     std::move(exprBody),
                                     fnNode.isPublic,
                                     fnNode.isVariadic);
}

void CodegenContext::emplaceCFunction(FunctionNode &&fnNode) {
    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode.arguments) {
        exprArgs.push_back({ name, getLanguageType(astType) });
    }
    fc_assert(fnNode.body.size() == 0);
    // Fixme: we should properly report this error
    fc_assert(fnNode.name != "main");
    auto name = fnNode.name;
    // hack... We probably should collect all imports and dedupe them
    // before creating named values
    if (names.contains(name)) {
        return;
    }
    emplaceNamed<NamedFunctionValue>(name,
                                     *this,
                                     module,
                                     name,
                                     exprArgs,
                                     getLanguageType(fnNode.returnType),
                                     Function::Body {},
                                     fnNode.isPublic,
                                     fnNode.isVariadic);
}

void CodegenContext::fillStructTypeFields(StructNode &&structNode) {
    StructType::Fields exprFields;
    for (auto &[name, astType] : structNode.fields)
        exprFields.emplace_back(name, getLanguageType(astType));

    auto structType = static_cast<StructType *>(getNamed<NamedTypeValue>(structNode.name));
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

LanguageType *CodegenContext::getLanguageType(const ASTType *type) {
    if (auto t = dynamic_cast<const ASTBuiltinType *>(type); t != nullptr) {
        fc_assert(existsNamed(t->builtinName));
        return getNamed<NamedTypeValue>(t->builtinName);
    } else if (auto t = dynamic_cast<const ASTIntegerType *>(type); t != nullptr) {
        return getNamed<NamedTypeValue>(t->builtinName);
    } else if (auto t = dynamic_cast<const ASTFloatType *>(type); t != nullptr) {
        return getNamed<NamedTypeValue>(t->builtinName);
    } else if (auto t = dynamic_cast<const ASTNamedType *>(type); t != nullptr) {
        return getNamed<NamedTypeValue>(t->name);
    } else if (auto t = dynamic_cast<const ASTPointerType *>(type); t != nullptr) {
        auto targetLangType = getLanguageType(t->targetType);
        auto pointeeName = targetLangType->signature();
        auto ptrName = pointeeName + "*";
        return getOrEmplaceType<PointerType>(ptrName, targetLangType);
    } else if (auto t = dynamic_cast<const ASTArrayType *>(type); t != nullptr) {
        auto targetLangType = getLanguageType(t->targetType);
        auto pointeeName = targetLangType->signature();
        auto arrName = fmt::format("{}[{}]", pointeeName, t->size);
        return getOrEmplaceType<ArrayType>(arrName, targetLangType, t->size);
    } else if (auto t = dynamic_cast<const ASTFunctionType *>(type); t != nullptr) {
        std::vector<LanguageType *> exprArgs;
        for (const auto &astType : t->argumentTypes) {
            exprArgs.emplace_back(getLanguageType(astType));
        }
        auto retType = getLanguageType(t->returnType);
        return getOrEmplaceType<FunctionType>(
            FunctionType::signatureFrom(exprArgs, retType),
            exprArgs, retType, false);
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
        return std::make_unique<StringConstant>(getNamed<NamedTypeValue>("str"), t->value);
    } else if (auto t = dynamic_cast<const ConstantIntNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<IntegerType *>(getLanguageType(t->intType));
        fc_assert(tp != nullptr);
        return t->isSigned
            ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(t->value))
            : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(t->value));
    } else if (auto t = dynamic_cast<const ConstantFloatNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<FloatType *>(getLanguageType(t->floatType));
        if (tp->bits == FloatType::Bits::Float) {
            return std::make_unique<FloatConstant>(tp, static_cast<float>(t->value));
        } else {
            return std::make_unique<FloatConstant>(tp, t->value);
        }
    } else if (auto t = dynamic_cast<const ConstantBoolNode *>(node.get()); t != nullptr) {
        return std::make_unique<BoolConstant>(getNamed<NamedTypeValue>("bool"), t->value);
    } else if (auto t = dynamic_cast<FunctionCallNode *>(node.get()); t != nullptr) {
        FunctionCall::Args argsExprs;
        for (auto &arg : t->args) {
            argsExprs.push_back(getExpression(std::move(arg)));
        }
        for (const auto &a : argsExprs) {
            a->dump();
        }
        std::string fullName = t->name == "main" ? t->name : (t->name);
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
            t->name,
            getLanguageType(t->type),
            t->initialValue != nullptr
            ? getExpression(std::move(t->initialValue))
            : nullptr);
    } else if (auto t = dynamic_cast<const SizeofNode *>(node.get()); t != nullptr) {
        return std::make_unique<Sizeof>(*this, getLanguageType(t->targetType));
    } else if (auto t = dynamic_cast<CastNode *>(node.get()); t != nullptr) {
        return std::make_unique<Cast>(
            *this,
            getLanguageType(t->targetType),
            getExpression(std::move(t->targetExpression)));
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
