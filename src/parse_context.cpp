#include "assert.hpp"
#include "parse_context.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"

ASTBuiltinType::ASTBuiltinType(const std::string& name)
    : builtinName(name) {}

ASTIntegerType::ASTIntegerType(bool isSigned, uint32_t bits)
    : ASTBuiltinType(fmt::format("{}{}", isSigned ? 'i' : 'u', bits))
    , bits(bits)
    , isSigned(isSigned) {}

ASTFloatType::ASTFloatType(uint32_t bits)
    : ASTBuiltinType(fmt::format("f{}", bits))
    , bits(bits) {}

ASTNamedType::ASTNamedType(const std::string &name)
    : name(name) {}

ASTPointerType::ASTPointerType(std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType)) {}

ASTArrayType::ASTArrayType(std::unique_ptr<ASTType> &&targetType, size_t size)
    : targetType(std::move(targetType)),
      size(size) {}

ASTFunctionType::ASTFunctionType(Args &&arguments, std::unique_ptr<ASTType> &&returnType)
    : arguments(std::move(arguments)),
      returnType(std::move(returnType)) {}

StructNode::StructNode(std::string name, Fields &&fields, bool isPublic)
    : name(name),
      isPublic(isPublic),
      fields(std::move(fields)) {}

AliasNode::AliasNode(std::string name, std::unique_ptr<ASTType> &&target)
    : name(name),
      target(std::move(target)) {}

FunctionNode::FunctionNode(
    std::string name, ArgList &&arguments, std::unique_ptr<ASTType> &&returnType, Body &&, bool isPublic, bool isVariadic
)
    : name(name),
      isPublic(isPublic),
      arguments(std::move(arguments)),
      returnType(std::move(returnType)),
      isVariadic(isVariadic) {}

ConstantStringNode::ConstantStringNode(std::string value)
    : value(value) {}

ConstantIntNode::ConstantIntNode(std::unique_ptr<ASTType> &&intType, IsLongInteger auto constValue)
    : intType(std::move(intType)),
      value(constValue) {
    isSigned = std::holds_alternative<int64_t>(value);
}
template ConstantIntNode::ConstantIntNode(std::unique_ptr<ASTType> &&, int64_t);
template ConstantIntNode::ConstantIntNode(std::unique_ptr<ASTType> &&, uint64_t);

ConstantFloatNode::ConstantFloatNode(std::unique_ptr<ASTType> &&floatType,double constValue)
    : floatType(std::move(floatType))
    , value(constValue) {}

ConstantBoolNode::ConstantBoolNode(bool value)
    : value(value) {}

FunctionCallNode::FunctionCallNode(std::string name)
    : name(name) {}

VarAccessNode::VarAccessNode(std::string name)
    : name(name) {}

VarDeclarationNode::VarDeclarationNode(const std::string &name, std::unique_ptr<ASTType> &&type)
    : type(std::move(type))
    , name(name) {}

SizeofNode::SizeofNode(std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType)) {}

DereferenceNode::DereferenceNode(std::unique_ptr<ASTNode> &&target)
    : target(std::move(target)) {}

ArrayNthNode::ArrayNthNode(std::unique_ptr<ASTNode> &&array, std::unique_ptr<ASTNode> &&subscript)
    : array(std::move(array))
    , subscript(std::move(subscript)) {}

CastNode::CastNode(std::unique_ptr<ASTNode> &&target, std::unique_ptr<ASTType> &&targetType)
    : targetType(std::move(targetType))
    , targetExpression(std::move(target)) {}

FulcrumModule::FulcrumModule(const std::string &name)
    : name(name) {};

std::string FulcrumModule::resolveName(const std::string &targetName) const {
    auto isTargetNamespaced = targetName != "/" && targetName.find('/') != targetName.npos;

    std::string fullName;
    if (isTargetNamespaced) {
        fullName = targetName;
    } else if (importedNames.contains(targetName)) {
        fullName = importedNames.at(targetName);
    } else {
        fullName = fmt::format("{}/{}", name, targetName);
    }

    return fullName;
}

FulcrumModule::IdFullNameMap FulcrumModule::allNames() const {
    IdFullNameMap result;
    for (const auto &moduleStruct : structs)
        result.emplace(moduleStruct->name, resolveName(moduleStruct->name));
    for (const auto &moduleAlias : aliases)
        result.emplace(moduleAlias->name, resolveName(moduleAlias->name));
    for (const auto &globalVar : globalVariables)
        result.emplace(globalVar->name, resolveName(globalVar->name));
    for (const auto &function : functions)
        result.emplace(function->name, resolveName(function->name));

    return result;
}

void FulcrumModule::importName(const std::string &baseName, const std::string &fullName) {
    if (importedNames.contains(baseName)) {
        if (importedNames[baseName] != fullName)
            throw CodegenError(fmt::format("{} already imported as {}", baseName, importedNames[baseName]));
    }

    importedNames[baseName] = fullName;
}

CModule::IdFullNameMap CModule::allNames() const {
    auto resolveName = [](const std::string &fileName, const std::string &name) {
        return fmt::format("{}/{}", fileName, name);
    };
    IdFullNameMap result;
    for (const auto &[fileName, moduleStruct] : structs)
        result.emplace(moduleStruct->name, resolveName(fileName, moduleStruct->name));
    for (const auto &[fileName, moduleAlias] : aliases)
        result.emplace(moduleAlias->name, resolveName(fileName, moduleAlias->name));
    for (const auto &[fileName, globalVar] : globalVariables)
        result.emplace(globalVar->name, resolveName(fileName, globalVar->name));
    for (const auto &[fileName, function] : functions)
        result.emplace(function->name, resolveName(fileName, function->name));
    return result;
}
