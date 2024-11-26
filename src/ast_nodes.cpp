#include "assert.hpp"
#include "ast_nodes.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"

ASTBuiltinType::ASTBuiltinType(std::string&& name)
    : builtinName(name) {}

ASTIntegerType::ASTIntegerType(bool isSigned, uint32_t bits)
    : ASTBuiltinType(fmt::format("{}{}", isSigned ? 'i' : 'u', bits))
    , bits(bits)
    , isSigned(isSigned) {}

ASTFloatType::ASTFloatType(uint32_t bits)
    : ASTBuiltinType(fmt::format("f{}", bits))
    , bits(bits) {}

ASTBoolType::ASTBoolType()
    : ASTBuiltinType("bool") {}

ASTVoidType::ASTVoidType()
    : ASTBuiltinType("void") {}

ASTNamedType::ASTNamedType(NamePath &&name)
    : name(std::move(name)) {}

ASTNamedType::ASTNamedType(const std::string &name)
    : name(NamePath::create(name).value()) {}

ASTPointerType::ASTPointerType(ASTType *targetType)
    : targetType(targetType) {}

ASTArrayType::ASTArrayType(ASTType *targetType, size_t size)
    : targetType(targetType),
      size(size) {}

ASTFunctionType::ASTFunctionType(ArgTypes &&argTypes, ASTType *returnType)
    : argumentTypes(std::move(argTypes)),
      returnType(returnType) {}

StructNode::StructNode(NamePath &&name, Fields &&fields, bool isPublic)
    : name(name),
      isPublic(isPublic),
      fields(std::move(fields)) {}

TypeAliasNode::TypeAliasNode(NamePath &&name, ASTType *target)
    : name(std::move(name)),
      target(target) {}

FunctionNode::FunctionNode(
    NamePath &&name, ArgList &&arguments, ASTType *returnType, Body &&body, bool isPublic, bool isVariadic)
    : name(std::move(name))
    , isPublic(isPublic)
    , arguments(arguments)
    , returnType(returnType)
    , body(std::move(body))
    , isVariadic(isVariadic) {}

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

FunctionCallNode::FunctionCallNode(NamePath &&name)
    : name(std::move(name)) {}

VariableAccessNode::VariableAccessNode(NamePath &&name)
    : name(std::move(name)) {}

VariableDeclarationNode::VariableDeclarationNode(NamePath &&name, ASTType *type)
    : type(type)
    , name(name) {}

SizeofNode::SizeofNode(ASTType *targetType)
    : targetType(std::move(targetType)) {}

DereferenceNode::DereferenceNode(std::unique_ptr<ASTNode> &&target)
    : target(std::move(target)) {}

ArrayNthNode::ArrayNthNode(std::unique_ptr<ASTNode> &&array, std::unique_ptr<ASTNode> &&subscript)
    : array(std::move(array))
    , subscript(std::move(subscript)) {}

CastNode::CastNode(std::unique_ptr<ASTNode> &&target, ASTType *targetType)
    : targetType(targetType)
    , targetExpression(std::move(target)) {}
