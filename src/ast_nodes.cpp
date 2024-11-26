#include "assert.hpp"
#include "ast_nodes.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"

ASTType::~ASTType() = default;

ASTBuiltinType::ASTBuiltinType(std::string&& name)
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

ASTNamedType::ASTNamedType(NamePath &&name)
    : name(std::move(name)) {}

ASTNamedType::ASTNamedType(const std::string &name)
    : name(NamePath::create(name).value()) {}

std::string ASTNamedType::signature() const {
    return name.join();
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
    return fmt::format("{} {}{}", returnType->signature(), name.join(), argTypes);
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
