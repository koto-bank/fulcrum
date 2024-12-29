#include <llvm/IR/Function.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <spdlog/spdlog.h>

#include "assert.hpp"
#include "ast_nodes.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"
#include "fulcrum_module.hpp"

Function::Function(
    CodegenContext &context,
    llvm::Module &module,
    const std::string &name_,
    Args &&arguments,
    const LanguageType *returnType,
    Body &&body,
    bool isPublic,
    bool isVariadic)
    : isPublic(isPublic)
    , name(name_)
    , body(std::move(body)) {
    std::vector<const LanguageType *> argumentTypes;
    for (auto &&arg : arguments) {
        argumentNames.push_back(arg.name);
        if (auto at = dynamic_cast<const ArrayType *>(arg.type); at != nullptr) {
            // decay to pointer right away
            auto ptrType = at->decay();
            argumentTypes.push_back(ptrType);
        } else {
            argumentTypes.push_back(arg.type);
        }
    }

    type = std::make_unique<FunctionType>(context, argumentTypes, returnType, isVariadic);

    auto funcType = static_cast<llvm::FunctionType *>(type->llvmType());
    function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);

    for (auto i = 0u; i < function->arg_size(); i++) {
        function->getArg(i)->setName(argumentNames[i]);
    }
}

FunctionType *Function::functionType() { return type.get(); }
llvm::Function *Function::llvmFunction() { return function; }

void Function::generateExpressions(ExpressionGenContext &genContext,
                                   const std::vector<std::unique_ptr<Expression>> &expressions) {
    for (auto i = 0u; i < expressions.size(); i++) {
        expressions[i]->llvmValue(genContext);
        if (expressions[i]->isTerminator()) {
            if (i != expressions.size() - 1) {
                // TODO: more detailed message
                spdlog::warn("Unreachable code");
            }
            return;
        }
    }
}

void Function::generateBody(ExpressionGenContext &genContext) {
    if (body.size() == 0) {
        // it's a declaration. Probably we need a separate flag for this case.
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
        genContext.builder.CreateStore(function->getArg(i), varDef->value);
    }

    generateExpressions(genContext, body);

    genContext.popScope();

    if (genContext.function->llvmFunction()->back().getTerminator() == nullptr) {
        // If the function is not void, insert unreachable at the end, since the user must return
        // something
        if (genContext.function->functionType()->returnType != genContext.codegenContext.voidType) {
            genContext.builder.CreateUnreachable();
        } else {
            // Otherwise, return void automatically
            genContext.builder.CreateRetVoid();
        }
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

CodegenContext::CodegenContext(const std::string &moduleName, llvm::LLVMContext &context)
    : context(context)
    , module(moduleName, context)
    , dataLayout (&module) {

    emplaceType<FloatType>(FloatType::Bits::Half);
    emplaceType<FloatType>(FloatType::Bits::Float);
    emplaceType<FloatType>(FloatType::Bits::Double);
    emplaceType<FloatType>(FloatType::Bits::Quad);
    voidType = emplaceType<VoidType>();
    boolType = emplaceType<BoolType>();
    auto i8 = emplaceType<IntegerType>(8, true);
    emplaceType<IntegerType>(8, false);
    emplaceType<IntegerType>(16, true);
    emplaceType<IntegerType>(16, false);
    emplaceType<IntegerType>(32, true);
    emplaceType<IntegerType>(32, false);
    emplaceType<IntegerType>(64, true);
    emplaceType<IntegerType>(64, false);
    auto str = emplaceType<PointerType>(i8); // for C strings

    // u128 for __darwin_arm_neon_state64
    emplaceType<IntegerType>(128, false);

    // Why do we need this?
    emplaceType<VAType>();

    strType = emplaceType<AliasType>("str", str);
    voidPtrType = emplaceType<PointerType>(voidType);

    emplaceType<AliasType>("__va_list_tag", voidPtrType);
}

CodegenContext::CodegenResult<StructType> CodegenContext::emplaceStructType(const StructNode &structNode) {
    auto name = structNode.name;
    if (structNode.isUnion) {
        return emplaceType<UnionType>(structNode.name, structNode.isPublic);
    } else {
        return emplaceType<StructType>(structNode.name, structNode.isPublic);
    }
}

CodegenContext::CodegenResult<AliasType> CodegenContext::emplaceAliasType(TypeAliasNode &&aliasNode) {
    return emplaceType<AliasType>(aliasNode.name, getLanguageType(aliasNode.target).value());
}

CodegenContext::CodegenResult<VariableDefinition>
CodegenContext::emplaceGlobalVar(VariableDeclarationNode &&varNode) {
    auto name = varNode.name;
    if (globalVars.contains(name)) {
        return std::unexpected(CodegenError(fmt::format("Global variable with name {} already exists", name)));
    }

    auto type = getLanguageType(varNode.type);
    if (!type) {
        return std::unexpected(type.error());
    }
    auto varDef = std::make_unique<VariableDefinition>(varNode.name, *type);

    if (varNode.initialValue != nullptr) {
        auto expr = getExpression(std::move(varNode.initialValue));
        auto constExpr = dynamic_cast<ConstantExpression *>(expr.get());
        if (constExpr == nullptr) {
            return std::unexpected(CodegenError(fmt::format("Global variable of non-constant type not supported: {}", varNode.name)));
        }

        auto constVal = constExpr->llvmConstant(*this);
        auto llvmGlobal = new llvm::GlobalVariable(
            module, constVal->getType(), false, llvm::GlobalVariable::PrivateLinkage, constVal, varNode.name
        );
        varDef->value = llvmGlobal;
    }
    return globalVars.emplace(name, std::move(varDef)).first->second.get();
}

CodegenContext::CodegenResult<Function> CodegenContext::emplaceFulcrumFunction(FunctionNode &&fnNode) {
    // TODO: already existing functions?
    auto funcName = fnNode.name;
    if (namedFunctions.contains(funcName)) {
        return namedFunctions.at(funcName).get();
    }

    Function::Args exprArgs;
    for (auto &[name, astType] : fnNode.arguments) {
        auto type = getLanguageType(astType);
        if (!type) {
            return std::unexpected(type.error());
        }
        exprArgs.push_back({ name, *type });
    }

    Function::Body exprBody;
    for (auto &node : fnNode.body) {
        exprBody.push_back(getExpression(std::move(node)));
    }

    return namedFunctions.emplace(funcName,
                                  std::make_unique<Function>(
                                      *this,
                                      module,
                                      funcName,
                                      std::move(exprArgs),
                                      getLanguageType(fnNode.returnType).value(),
                                      std::move(exprBody),
                                      fnNode.isPublic,
                                      fnNode.isVariadic)).first->second.get();
}

bool CodegenContext::fillStructTypeFields(const StructNode &structNode, std::vector<StructNode> &unprocessed) {
    StructType::Fields exprFields;
    for (auto &[name, astType] : structNode.fields) {
        exprFields.push_back({ name, getLanguageType(astType).value() });
    }

    auto structType = dynamic_cast<StructType *>(namedTypes.at(structNode.name).get());
    fc_assert(structType != nullptr);
    // fragile processing of nested structures.
    // dep graph is needed for proper solution.
    if (structType->fillFields(exprFields, dataLayout) == false) {
        unprocessed.push_back(structNode);
        return false;
    }
    return true;
}

void CodegenContext::generate(FulcrumModule &&fulcrumModule) {
    // First insert all the structure types
    for (auto &structNode : fulcrumModule.structs) {
        emplaceStructType(structNode);
    }

    // Now insert all alias types
    for (auto &aliasNode : fulcrumModule.typeAliases) {
        spdlog::info("Emplacing type alias {} = {}", aliasNode.name, aliasNode.target->signature());
        emplaceAliasType(std::move(aliasNode));
    }

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    // May require more than one pass since structures can be nested and mutually
    // dependent. Detect circular deps though
    std::vector<StructNode> unprocessedNodes;
    for (auto &s : fulcrumModule.structs) {
        fillStructTypeFields(s, unprocessedNodes);
    }

    while (unprocessedNodes.empty() == false) {
        bool anyProcessed = false;
        std::vector<StructNode> anotherUnprocessedNodes;
        for (auto &s : unprocessedNodes) {
            anyProcessed = fillStructTypeFields(s, anotherUnprocessedNodes) || anyProcessed;
        }
        unprocessedNodes = std::move(anotherUnprocessedNodes);
        if (anyProcessed == false) {
            throw CodegenError(fmt::format("Failed to resolve field types. Circular dependency?"));
        }
    }

    for (auto &globalVar : fulcrumModule.globalVariables) {
        emplaceGlobalVar(std::move(globalVar));
    }

    for (auto &function : fulcrumModule.functions) {
        emplaceFulcrumFunction(std::move(function));
    }
}

CodegenContext::CodegenResult<LanguageType> CodegenContext::getNamedType(const std::string &name) const {
    if (!namedTypes.contains(name)) {
        spdlog::error("Can't find type with name {}", name);
        return std::unexpected(CodegenError(fmt::format("Can't find type with name {}", name)));
    }
    return namedTypes.at(name).get();
}

CodegenContext::CodegenResult<LanguageType> CodegenContext::getLanguageType(const ASTType *type) {
    if (auto t = dynamic_cast<const ASTBuiltinType *>(type); t != nullptr) {
        auto ret = getNamedType(t->builtinName);
        fc_assert(ret != nullptr);
        return ret;
    } else if (auto t = dynamic_cast<const ASTIntegerType *>(type); t != nullptr) {
        auto ret = getNamedType(t->builtinName);
        fc_assert(ret != nullptr);
        return ret;
    } else if (auto t = dynamic_cast<const ASTFloatType *>(type); t != nullptr) {
        auto ret = getNamedType(t->builtinName);
        fc_assert(ret != nullptr);
        return ret;
    } else if (auto t = dynamic_cast<const ASTNamedType *>(type); t != nullptr) {
        auto ret = getNamedType(t->name);
        fc_assert(ret != nullptr);
        return ret;
    } else if (auto t = dynamic_cast<const ASTBoolType *>(type); t != nullptr) {
        auto ret = getNamedType(t->builtinName);
        fc_assert(ret != nullptr);
        return ret;
    } else if (auto t = dynamic_cast<const ASTVoidType *>(type); t != nullptr) {
        auto ret = getNamedType(t->builtinName);
        fc_assert(ret != nullptr);
        return ret;
    } else if (auto t = dynamic_cast<const ASTPointerType *>(type); t != nullptr) {
        auto targetLangTypeOrError = getLanguageType(t->targetType);
        if (!targetLangTypeOrError) {
            return targetLangTypeOrError;
        }
        return emplaceType<PointerType>(targetLangTypeOrError.value());
    } else if (auto t = dynamic_cast<const ASTArrayType *>(type); t != nullptr) {
        auto targetLangTypeOrError = getLanguageType(t->targetType);
        if (!targetLangTypeOrError) {
            return targetLangTypeOrError;
        }
        return emplaceType<ArrayType>(targetLangTypeOrError.value(), t->size);
    } else if (auto t = dynamic_cast<const ASTFunctionType *>(type); t != nullptr) {
        std::vector<const LanguageType *> exprArgs;
        for (const auto &astType : t->argumentTypes) {
            exprArgs.emplace_back(getLanguageType(astType).value());
        }
        auto retType = getLanguageType(t->returnType);
        return emplaceType<FunctionType>(exprArgs, retType.value(), false);
    } else if (auto t = dynamic_cast<const ASTVectorType *>(type); t != nullptr) {
        auto elemType = getLanguageType(t->elementType);
        return emplaceType<VectorType>(elemType.value(), t->elementCount, t->isScalable);
    } else {
        // new type, unsupported above?
        fc_unreachable();
        return nullptr;
    }
}

std::unique_ptr<Expression> CodegenContext::getExpression(std::unique_ptr<ASTNode> &&node) {
    if (auto t = dynamic_cast<const StructNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const TypeAliasNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const FunctionNode *>(node.get()); t != nullptr) {
        return nullptr;
    } else if (auto t = dynamic_cast<const ConstantStringNode *>(node.get()); t != nullptr) {
        return std::make_unique<StringConstant>(strType, t->value);
    } else if (auto t = dynamic_cast<const ConstantIntNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<const IntegerType *>(getLanguageType(t->intType).value());
        fc_assert(tp != nullptr);
        return t->isSigned
            ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(t->value))
            : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(t->value));
    } else if (auto t = dynamic_cast<const ConstantFloatNode *>(node.get()); t != nullptr) {
        auto tp = dynamic_cast<const FloatType *>(getLanguageType(t->floatType).value());
        if (tp->bits == FloatType::Bits::Float) {
            return std::make_unique<FloatConstant>(tp, static_cast<float>(t->value));
        } else {
            return std::make_unique<FloatConstant>(tp, t->value);
        }
    } else if (auto t = dynamic_cast<const ConstantBoolNode *>(node.get()); t != nullptr) {
        return std::make_unique<BoolConstant>(boolType, t->value);
    } else if (auto t = dynamic_cast<FunctionCallNode *>(node.get()); t != nullptr) {
        FunctionCall::Args argsExprs;
        for (auto &arg : t->args) {
            argsExprs.push_back(getExpression(std::move(arg)));
        }
        std::string fullName = t->name;
        return std::make_unique<FunctionCall>(fullName, std::move(argsExprs));
    } else if (auto t = dynamic_cast<const SymbolNode *>(node.get()); t != nullptr) {
        // Most probably variable access
        return std::make_unique<SymbolAccess>(t->name);
    } else if (auto t = dynamic_cast<DereferenceNode *>(node.get()); t != nullptr) {
        return std::make_unique<Dereference>(getExpression(std::move(t->target)));
    } else if (auto t = dynamic_cast<AtNode *>(node.get()); t != nullptr) {
        auto expr = std::make_unique<InboundAccess>(getExpression(std::move(t->target)),
                                                    getExpression(std::move(t->subscript)));
        return expr;
    } else if (auto t = dynamic_cast<VariableDeclarationNode *>(node.get()); t != nullptr) {
        return std::make_unique<VariableDeclaration>(
            t->name,
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
