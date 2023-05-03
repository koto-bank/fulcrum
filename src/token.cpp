#include "token.hpp"

namespace token {
Token::Token(Type type)
    : type(type) {}

Error::Error(const std::string &what, std::unique_ptr<Token> &&originalToken)
    : Token(Type::Error)
    , what(what)
    , originalToken(std::move(originalToken)) {}

LParen::LParen()
    : Token(Type::LParen) {}

RParen::RParen()
    : Token(Type::RParen) {}

Id::Id(const std::string& id)
    : Token(Type::Id)
    , id(id) {}

BooleanLiteral::BooleanLiteral(bool value)
    : Token(Type::BooleanLiteral)
    , value(value) {}

IntegerLiteral::IntegerLiteral(uint64_t _value, bool isSigned, uint32_t bits)
    : Token(Type::IntegerLiteral)
    , isSigned(isSigned)
    , bits(bits) {
    if (isSigned) {
        value = static_cast<int64_t>(_value);
    } else {
        value = static_cast<uint64_t>(_value);
    }
}

FloatLiteral::FloatLiteral(double value, uint32_t bits)
    : Token(Type::FloatLiteral)
    , value(value)
    , bits(bits) {}

StringLiteral::StringLiteral(const std::string& contents)
    : Token(Type::StringLiteral)
    , contents(contents) {}
}
