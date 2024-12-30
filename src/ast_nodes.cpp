#include "assert.hpp"
#include "ast_nodes.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"

ASTType::~ASTType() = default;

ASTBuiltinType::ASTBuiltinType(const std::string &name)
    : builtinName(name) {}

std::string ASTBuiltinType::signature() const {
    return builtinName;
}

ASTIntegerType::ASTIntegerType(bool isSigned, uint32_t bits)
    : ASTBuiltinType(fmt::format("{}{}", isSigned ? 'i' : 'u', bits))
    , bits(bits)
    , isSigned(isSigned) {}

std::string ASTIntegerType::signature() const {
    return fmt::format("{}{}", isSigned ? "i" : "u", bits);
}

ASTFloatType::ASTFloatType(uint32_t bits)
    : ASTBuiltinType(fmt::format("f{}", bits))
    , bits(bits) {}

std::string ASTFloatType::signature() const {
    return fmt::format("f{}", bits);
}

ASTBoolType::ASTBoolType()
    : ASTBuiltinType("bool") {}

std::string ASTBoolType::signature() const {
    return "bool";
}

ASTVoidType::ASTVoidType()
    : ASTBuiltinType("void") {}

std::string ASTVoidType::signature() const {
    return "void";
}

ASTNamedType::ASTNamedType(const std::string &name)
    : name(name) {}

std::string ASTNamedType::signature() const {
    return name;
}

ASTPointerType::ASTPointerType(ASTType *targetType)
    : targetType(targetType) {}

std::string ASTPointerType::signature() const {
    return fmt::format("{}*", targetType->signature());
}

ASTArrayType::ASTArrayType(ASTType *targetType, size_t size)
    : targetType(targetType),
      size(size) {}

std::string ASTArrayType::signature() const {
    return fmt::format("{}[]", targetType->signature());
}

ASTFunctionType::ASTFunctionType(ArgTypes &&argTypes, ASTType *returnType)
    : argumentTypes(std::move(argTypes)),
      returnType(returnType) {}

std::string ASTFunctionType::signature() const {
    // TODO: do we need function type at all?
    std::string argTypes = "(";
    for (auto a : argumentTypes) {
        argTypes += a->signature();
        argTypes += ", ";
    }
    if (argTypes.size() > 1) {
        argTypes.pop_back(); // -' '
        argTypes.pop_back(); // -','
    }
    argTypes += ")";
    return fmt::format("{}{}", returnType->signature(), argTypes);
}

ASTVectorType::ASTVectorType(const ASTType *elementType, size_t elementCount, bool isScalable)
    : elementType(elementType)
    , elementCount(elementCount)
    , isScalable(isScalable) {}

std::string ASTVectorType::signature() const {
    // TODO: format should be m<bit width><element type> Thus, get bit
    // width of target type, multiply by element count, and then
    // decide on type suffix
    return fmt::format("m{}??", elementCount);
}

StructNode::StructNode(const std::string &name, Fields &&fields, bool isPublic)
    : name(name),
      isPublic(isPublic),
      fields(std::move(fields)) {}

TypeAliasNode::TypeAliasNode(const std::string &name, ASTType *target)
    : name(name),
      target(target) {}

FunctionNode::FunctionNode(
    const std::string &name, ArgList &&arguments, ASTType *returnType, Body &&body, bool isPublic, bool isVariadic)
    : name(name)
    , isPublic(isPublic)
    , arguments(arguments)
    , returnType(returnType)
    , body(std::move(body))
    , isVariadic(isVariadic) {}

std::string FunctionNode::signature() const {
    std::string argTypes = "(";
    for (auto &[_, a] : arguments) {
        argTypes += a->signature();
        argTypes += ", ";
    }
    if (argTypes.size() > 1) {
        if (isVariadic) {
            argTypes += "...";
        } else {
            argTypes.pop_back(); // remove ' '
            argTypes.pop_back(); // remove ','
        }
    }
    argTypes += ")";
    return fmt::format("{} {}{}", returnType->signature(), name, argTypes);
}

ConstantStringNode::ConstantStringNode(std::string value)
    : value(value) {}

ConstantIntNode::ConstantIntNode(ASTType *intType, IsLongInteger auto constValue)
    : intType(intType),
      value(constValue) {
    isSigned = std::holds_alternative<int64_t>(value);
}
template ConstantIntNode::ConstantIntNode(ASTType *, int64_t);
template ConstantIntNode::ConstantIntNode(ASTType *, uint64_t);

ConstantFloatNode::ConstantFloatNode(ASTType *floatType, double constValue)
    : floatType(floatType)
    , value(constValue) {}

ConstantBoolNode::ConstantBoolNode(bool value)
    : value(value) {}

FunctionCallNode::FunctionCallNode(const std::string &name)
    : name(name) {}

SymbolNode::SymbolNode(const std::string &name)
    : name(name) {}

VariableDeclarationNode::VariableDeclarationNode(const std::string &name, ASTType *type)
    : type(type)
    , name(name) {}

SizeofNode::SizeofNode(ASTType *targetType)
    : targetType(std::move(targetType)) {}

DereferenceNode::DereferenceNode(std::unique_ptr<ASTNode> &&target)
    : target(std::move(target)) {}

AtNode::AtNode(std::unique_ptr<ASTNode> &&target, std::unique_ptr<ASTNode> &&subscript)
    : target(std::move(target))
    , subscript(std::move(subscript)) {}

FieldAccessNode::FieldAccessNode(std::unique_ptr<ASTNode> &&target, std::unique_ptr<ASTNode> &&subscript)
    : target(std::move(target))
    , subscript(std::move(subscript)) {}

CastNode::CastNode(std::unique_ptr<ASTNode> &&target, ASTType *targetType)
    : targetType(targetType)
    , targetExpression(std::move(target)) {}
