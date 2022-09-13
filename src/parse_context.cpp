#include "parse_context.hpp"

ASTBuiltinType::ASTBuiltinType(std::string name)
    : builtinName(name) {}

LanguageType *ASTBuiltinType::languageType(CodegenContext &context) {
    assert(context.types.contains(builtinName));

    return context.types.at(builtinName).get();
};

ASTNamedType::ASTNamedType(std::string name)
    : name(name) {}

LanguageType *ASTNamedType::languageType(CodegenContext &context) {
    if (!context.types.contains(name))
        throw CodegenError(fmt::format("Unknown named type {}", name));

    return context.types.at(name).get();
}

ASTPointerType::ASTPointerType(std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType)) {}

LanguageType *ASTPointerType::languageType(CodegenContext &context) {
    auto targetLangType = targetType->languageType(context);

    auto pointeeName = targetLangType->signature();
    auto ptrName = pointeeName + "*";
    return context.getOrEmplaceType<PointerType>(ptrName, targetLangType);
};

ASTArrayType::ASTArrayType(std::unique_ptr<ASTType> &&targetType, size_t size)
    : targetType(std::move(targetType)),
      size(size) {}

LanguageType *ASTArrayType::languageType(CodegenContext &context) {
    auto targetLangType = targetType->languageType(context);

    auto pointeeName = targetLangType->signature();
    auto arrName = fmt::format("{}[{}]", pointeeName, size);
    return context.getOrEmplaceType<ArrayType>(pointeeName, targetLangType, size);
};

ASTFunctionType::ASTFunctionType(Args &&arguments, std::unique_ptr<ASTType> &&returnType)
    : arguments(std::move(arguments)),
      returnType(std::move(returnType)) {}

LanguageType *ASTFunctionType::languageType(CodegenContext &context) {
    std::vector<LanguageType *> exprArgs;
    for (auto &astType : arguments)
        exprArgs.emplace_back(astType->languageType(context));

    return context.getOrEmplaceType<FunctionType>(
        FunctionType::signatureFrom(exprArgs, returnType->languageType(context)),
        exprArgs, returnType->languageType(context)
    );
}

StructNode::StructNode(std::string name, Fields &&fields, bool isPublic)
    : name(name),
      isPublic(isPublic),
      fields(std::move(fields)) {}

std::unique_ptr<Expression> StructNode::expression(CodegenContext &) { return nullptr; }

void StructNode::emplaceStructType(CodegenContext &context) {
    context.emplaceType<StructType>(name, name, isPublic);
}
void StructNode::fillStructTypeFields(CodegenContext &context) {
    StructType::Fields exprFields;
    for (auto &[name, astType] : fields)
        exprFields.emplace_back(name, astType->languageType(context));
}

AliasNode::AliasNode(std::string name, std::unique_ptr<ASTType> &&target)
    : name(name),
      target(std::move(target)) {}

std::unique_ptr<Expression> AliasNode::expression(CodegenContext &) { return nullptr; }

void AliasNode::emplaceAliasType(CodegenContext &context) {
    context.emplaceType<AliasType>(name, name, target->languageType(context));
}

FunctionNode::FunctionNode(std::string name, Args &&arguments, std::unique_ptr<ASTType> &&returnType, Body &&, bool isPublic)
    : name(name),
      isPublic(isPublic),
      arguments(std::move(arguments)),
      returnType(std::move(returnType)) {}

std::unique_ptr<Expression> FunctionNode::expression(CodegenContext &) { return nullptr; }

void FunctionNode::emplaceFunction(CodegenContext &context) {
    Function::Args exprArgs;
    for (auto &[name, astType] : arguments)
        exprArgs.emplace_back(name, astType->languageType(context));
    Function::Body exprBody;
    for (auto &node : body)
        exprBody.push_back(node->expression(context));

    context.emplaceFn(name, exprArgs, returnType->languageType(context), std::move(exprBody), isPublic);
}

ConstantStringNode::ConstantStringNode(std::string value)
    : value(value) {}

std::unique_ptr<Expression> ConstantStringNode::expression(CodegenContext &context) {
    return std::make_unique<StringConstant>(context.getType("str"), value);
}

ConstantIntNode::ConstantIntNode(ASTBuiltinType intType, IsLongInteger auto constValue_)
    : intType(intType),
      value(constValue_) {
    isSigned = std::holds_alternative<int64_t>(value);
}
template ConstantIntNode::ConstantIntNode(ASTBuiltinType, int64_t);
template ConstantIntNode::ConstantIntNode(ASTBuiltinType, uint64_t);

std::unique_ptr<Expression> ConstantIntNode::expression(CodegenContext &context) {
    auto tp = (IntegerType *)intType.languageType(context);
    return isSigned
        ? std::make_unique<IntegerConstant>(tp, std::get<int64_t>(value))
        : std::make_unique<IntegerConstant>(tp, std::get<uint64_t>(value));
}

ConstantBoolNode::ConstantBoolNode(bool value)
    : value(value) {}

std::unique_ptr<Expression> ConstantBoolNode::expression(CodegenContext &context) {
    return std::make_unique<BoolConstant>(context.getType("bool"), value);
}

FunctionCallNode::FunctionCallNode(std::string name)
    : name(name) {}

std::unique_ptr<Expression> FunctionCallNode::expression(CodegenContext &context) {
    FunctionCall::Args argsExprs;
    for (auto &arg : args)
        argsExprs.push_back(arg->expression(context));

    return std::make_unique<FunctionCall>(name, std::move(argsExprs));
}

VarAccessNode::VarAccessNode(std::string name)
    : name(name) {}

std::unique_ptr<Expression> VarAccessNode::expression(CodegenContext &) {
    return std::make_unique<VarAccess>(name);
}

AddrOfNode::AddrOfNode(std::unique_ptr<ASTNode> &&target)
    : target(std::move(target)) {}

std::unique_ptr<Expression> AddrOfNode::expression(CodegenContext &context) {
    return std::make_unique<AddrOf>(target->expression(context));
}

std::unique_ptr<Expression> DereferenceNode::expression(CodegenContext &context) {
    return std::make_unique<Dereference>(target->expression(context));
}

VariableDeclarationNode::VariableDeclarationNode(std::string name)
    : name(name) {}

std::unique_ptr<Expression> VariableDeclarationNode::expression(CodegenContext &context) {
    return std::make_unique<VariableDeclaration>(
        name, type->languageType(context),
        initialValue ? initialValue->expression(context) : nullptr
    );
}

void VariableDeclarationNode::emplaceGlobalVar(CodegenContext &context) {
    VariableDefinition varDef(name, type->languageType(context));
    auto varType = type->languageType(context);

    if (initialValue != nullptr) {
        auto expr = initialValue->expression(context);

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
                name
            );
            varDef.value = llvmGlobal;
            context.globalVariables.emplace(name, varDef);
        } else {
            throw CodegenError(fmt::format("Global variables of type {} are not supported", varType->signature()));
        }
    }
}

SizeofNode::SizeofNode(std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType)) {}

std::unique_ptr<Expression> SizeofNode::expression(CodegenContext &context) {
    return std::make_unique<Sizeof>(context, targetType->languageType(context));
}

ModuleNode::ModuleNode(std::string name)
    : name(name){};

std::unique_ptr<Expression> ModuleNode::expression(CodegenContext &) { return nullptr; }

void ModuleNode::generate(CodegenContext &codegenContext) {
    // First insert all the structure types
    for (auto &moduleStruct : structs)
        moduleStruct->emplaceStructType(codegenContext);
    // Now insert all alias types
    for (auto &moduleAlias : aliases)
        moduleAlias->emplaceAliasType(codegenContext);

    // Now fill structure type fields, which could possibly refer
    // to other structures or aliases
    for (auto &moduleStruct : structs)
        moduleStruct->fillStructTypeFields(codegenContext);


    for (auto &globalVar : globalVariables)
        globalVar->emplaceGlobalVar(codegenContext);

    for (auto &functions : functions)
        functions->emplaceFunction(codegenContext);
}
