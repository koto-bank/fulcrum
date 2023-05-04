#include "token.hpp"

#include <array>

namespace token {
namespace {
    constexpr std::array<const char *, Error::Count> errorDescriptions = {
        "EOF inside a string",
        "Encountered line break during string parsing",

        "EOF inside a char literal",
        "Encountered line break during char literal parsing",
        "Char literal length > 1",

        "Integer literal exceeds 64-bit limit",
        "Ill-formed integer literal",
        "Bad integer bit size",

        "Float literal exceeds 64-bit limit",
        "Ill-formed float literal",
        "Bad float bit size",

        "Failed to parse numeric value",
    };
    static_assert(std::size(errorDescriptions) == Error::Count);

    constexpr const char *getErrorDescription(uint32_t code) {
        fc_assert(code < Error::Count);
        return errorDescriptions[code];
    }
}

Token::Token(Type type)
    : type(type) {}

Error::Error(uint32_t code, std::unique_ptr<Token> &&originalToken)
    : Token(Type::Error)
    , code(code)
    , what(getErrorDescription(code))
    , originalToken(std::move(originalToken)) {
}

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

CharLiteral::CharLiteral(uint8_t value)
    : Token(Type::CharLiteral)
    , value(value) {}

FloatLiteral::FloatLiteral(double value, uint32_t bits)
    : Token(Type::FloatLiteral)
    , value(value)
    , bits(bits) {}

StringLiteral::StringLiteral(const std::string& contents)
    : Token(Type::StringLiteral)
    , contents(contents) {}
}
