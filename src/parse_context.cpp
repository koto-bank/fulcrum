#include "parse_context.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"

ASTBuiltinType::ASTBuiltinType(std::string name)
    : builtinName(name) {}

LanguageType *ASTBuiltinType::languageType(ModuleNode *, CodegenContext &context) {
    assert(context.existsNamed(builtinName));

    return context.getNamed<NamedTypeValue>(builtinName);
};

ASTNamedType::ASTNamedType(std::string name)
    : name(name) {}

LanguageType *ASTNamedType::languageType(ModuleNode *module, CodegenContext &context) {
    auto fullName = resolveName(module, name);

    return context.getNamed<NamedTypeValue>(fullName);
}

ASTPointerType::ASTPointerType(std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType)) {}

LanguageType *ASTPointerType::languageType(ModuleNode *module, CodegenContext &context) {
    auto targetLangType = targetType->languageType(module, context);

    auto pointeeName = targetLangType->signature();
    auto ptrName = pointeeName + "*";
    return context.getOrEmplaceType<PointerType>(ptrName, targetLangType);
};

ASTArrayType::ASTArrayType(std::unique_ptr<ASTType> &&targetType, size_t size)
    : targetType(std::move(targetType)),
      size(size) {}

LanguageType *ASTArrayType::languageType(ModuleNode *module, CodegenContext &context) {
    auto targetLangType = targetType->languageType(module, context);

    auto pointeeName = targetLangType->signature();
    auto arrName = fmt::format("{}[{}]", pointeeName, size);
    return context.getOrEmplaceType<ArrayType>(pointeeName, targetLangType, size);
};

ASTFunctionType::ASTFunctionType(Args &&arguments, std::unique_ptr<ASTType> &&returnType)
    : arguments(std::move(arguments)),
      returnType(std::move(returnType)) {}

LanguageType *ASTFunctionType::languageType(ModuleNode *module, CodegenContext &context) {
    std::vector<LanguageType *> exprArgs;
    for (auto &astType : arguments)
        exprArgs.emplace_back(astType->languageType(module, context));

    return context.getOrEmplaceType<FunctionType>(
        FunctionType::signatureFrom(exprArgs, returnType->languageType(module, context)),
        exprArgs, returnType->languageType(module, context)
    );
}

StructNode::StructNode(std::string name, Fields &&fields, bool isPublic)
    : name(name),
      isPublic(isPublic),
      fields(std::move(fields)) {}

std::unique_ptr<Expression> StructNode::expression(ModuleNode *, CodegenContext &) { return nullptr; }

void StructNode::emplaceStructType(ModuleNode *module, CodegenContext &context) {
    auto fullName = resolveName(module, name);

    context.emplaceType<StructType>(fullName, fullName, isPublic);
}
void StructNode::fillStructTypeFields(ModuleNode *module, CodegenContext &context) {
    StructType::Fields exprFields;
    for (auto &[name, astType] : fields)
        exprFields.emplace_back(name, astType->languageType(module, context));

    auto fullName = resolveName(module, name);
    auto structType = static_cast<StructType *>(context.getNamed<NamedTypeValue>(fullName));
    structType->fillFields(exprFields);
}

AliasNode::AliasNode(std::string name, std::unique_ptr<ASTType> &&target)
    : name(name),
      target(std::move(target)) {}

std::unique_ptr<Expression> AliasNode::expression(ModuleNode *, CodegenContext &) { return nullptr; }

void AliasNode::emplaceAliasType(ModuleNode *module, CodegenContext &context) {
    auto fullName = resolveName(module, name);

    context.emplaceType<AliasType>(fullName, fullName, target->languageType(module, context));
}

FunctionNode::FunctionNode(std::string name, Args &&arguments, std::unique_ptr<ASTType> &&returnType, Body &&, bool isPublic)
    : name(name),
      isPublic(isPublic),
      arguments(std::move(arguments)),
      returnType(std::move(returnType)) {}

std::unique_ptr<Expression> FunctionNode::expression(ModuleNode *, CodegenContext &) { return nullptr; }

void FunctionNode::emplaceFunction(ModuleNode *module, CodegenContext &context) {
    Function::Args exprArgs;
    for (auto &[name, astType] : arguments)
        exprArgs.emplace_back(name, astType->languageType(module, context));
    Function::Body exprBody;
    for (auto &node : body)
        exprBody.push_back(node->expression(module, context));

    auto langName = name == "main" ? name : resolveName(module, name);
    // FIXME: maybe there's a better way, but for now assume functions with no body are C declarations
    auto funcName = (name == "main" || body.size() == 0) ? name : resolveName(module, name);

    context.emplaceFn(langName, funcName, exprArgs, returnType->languageType(module, context), std::move(exprBody), isPublic);
}

ConstantStringNode::ConstantStringNode(std::string value)
    : value(value) {}

std::unique_ptr<Expression> ConstantStringNode::expression(ModuleNode *, CodegenContext &context) {
    return std::make_unique<StringConstant>(context.getNamed<NamedTypeValue>("str"), value);
}

ConstantIntNode::ConstantIntNode(ASTBuiltinType intType, IsLongInteger auto constValue_)
    : intType(intType),
      value(constValue_) {
    isSigned = std::holds_alternative<int64_t>(value);
}
template ConstantIntNode::ConstantIntNode(ASTBuiltinType, int64_t);
template ConstantIntNode::ConstantIntNode(ASTBuiltinType, uint64_t);

std::unique_ptr<Expression> ConstantIntNode::expression(ModuleNode *module, CodegenContext &context) {
    auto tp = (IntegerType *)intType.languageType(module, context);
    return isSigned
        ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(value))
        : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(value));
}

ConstantBoolNode::ConstantBoolNode(bool value)
    : value(value) {}

std::unique_ptr<Expression> ConstantBoolNode::expression(ModuleNode *, CodegenContext &context) {
    return std::make_unique<BoolConstant>(context.getNamed<NamedTypeValue>("bool"), value);
}

FunctionCallNode::FunctionCallNode(std::string name)
    : name(name) {}

std::unique_ptr<Expression> FunctionCallNode::expression(ModuleNode *module, CodegenContext &context) {
    FunctionCall::Args argsExprs;
    for (auto &arg : args)
        argsExprs.push_back(arg->expression(module, context));

    std::string fullName = name == "main" ? name : resolveName(module, name);

    return std::make_unique<FunctionCall>(fullName, std::move(argsExprs));
}

VarAccessNode::VarAccessNode(std::string name)
    : name(name) {}

std::unique_ptr<Expression> VarAccessNode::expression(ModuleNode *, CodegenContext &) {
    return std::make_unique<VarAccess>(name);
}

AddrOfNode::AddrOfNode(std::unique_ptr<ASTNode> &&target)
    : target(std::move(target)) {}

std::unique_ptr<Expression> AddrOfNode::expression(ModuleNode *module, CodegenContext &context) {
    return std::make_unique<AddrOf>(target->expression(module, context));
}

std::unique_ptr<Expression> DereferenceNode::expression(ModuleNode *module, CodegenContext &context) {
    return std::make_unique<Dereference>(target->expression(module, context));
}

VariableDeclarationNode::VariableDeclarationNode(std::string name)
    : name(name) {}

std::unique_ptr<Expression> VariableDeclarationNode::expression(ModuleNode *module, CodegenContext &context) {
    return std::make_unique<VariableDeclaration>(
        name, type->languageType(module, context),
        initialValue ? initialValue->expression(module, context) : nullptr
    );
}

void VariableDeclarationNode::emplaceGlobalVar(ModuleNode *module, CodegenContext &context) {
    auto fullName = resolveName(module, name);
    VariableDefinition varDef(fullName, type->languageType(module, context));
    auto varType = type->languageType(module, context);

    if (initialValue != nullptr) {
        auto expr = initialValue->expression(module, context);

        // FIXME: This is a mess
        auto maybeInt = dynamic_cast<IntegerConstant *>(expr.get());
        if (maybeInt != nullptr) {
            auto isSigned = std::holds_alternative<int64_t>(maybeInt->constValue);
            llvm::Constant *numberConstant = isSigned
                ? llvm::ConstantInt::getSigned(varType->llvmType(), std::get<int64_t>(maybeInt->constValue))
                : llvm::ConstantInt::get(varType->llvmType(), std::get<uint64_t>(maybeInt->constValue));

            auto llvmGlobal = new llvm::GlobalVariable(
                context.module,
                varType->llvmType(),
                false,
                llvm::GlobalVariable::PrivateLinkage,
                numberConstant,
                fullName
            );
            varDef.value = llvmGlobal;
            context.emplaceNamed<NamedVariableValue>(fullName, varDef);
        } else {
            throw CodegenError(fmt::format("Global variables of type {} are not supported", varType->signature()));
        }
    }
}

SizeofNode::SizeofNode(std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType)) {}

std::unique_ptr<Expression> SizeofNode::expression(ModuleNode *module, CodegenContext &context) {
    return std::make_unique<Sizeof>(context, targetType->languageType(module, context));
}

ModuleNode::ModuleNode(std::string name)
    : name(name){};

std::unique_ptr<Expression> ModuleNode::expression(ModuleNode *, CodegenContext &) { return nullptr; }

void ModuleNode::generate(CodegenContext &codegenContext) {
    // First insert all the structure types
    for (auto &moduleStruct : structs)
        moduleStruct->emplaceStructType(this, codegenContext);

    // Now insert all alias types
    for (auto &moduleAlias : aliases)
        moduleAlias->emplaceAliasType(this, codegenContext);

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    for (auto &moduleStruct : structs)
        moduleStruct->fillStructTypeFields(this, codegenContext);

    for (auto &globalVar : globalVariables)
        globalVar->emplaceGlobalVar(this, codegenContext);

    for (auto &function : functions)
        function->emplaceFunction(this, codegenContext);
}

std::map<std::string, std::string> ModuleNode::allNames() {
    std::map<std::string, std::string> result;
    for (auto &moduleStruct : structs)
        result.emplace(moduleStruct->name, resolveName(this, moduleStruct->name));
    for (auto &moduleAlias : aliases)
        result.emplace(moduleAlias->name, resolveName(this, moduleAlias->name));
    for (auto &globalVar : globalVariables)
        result.emplace(globalVar->name, resolveName(this, globalVar->name));
    for (auto &function : functions)
        result.emplace(function->name, resolveName(this, function->name));

    return result;
}

void ModuleNode::importName(const std::string &baseName, const std::string &fullName) {
    if (importedNames.contains(baseName)) {
        if (importedNames[baseName] != fullName)
            throw CodegenError(fmt::format("{} already imported as {}", baseName, importedNames[baseName]));
    }

    importedNames[baseName] = fullName;
}

std::string resolveName(ModuleNode *module, const std::string &name) {
    auto isNamespaced = name != "/" && name.find('/') != name.npos;

    std::string fullName;
    if (isNamespaced) {
        fullName = name;
    } else if (module->importedNames.contains(name)) {
        fullName = module->importedNames[name];
    } else {
        fullName = fmt::format("{}/{}", module->name, name);
    }

    return fullName;
}
