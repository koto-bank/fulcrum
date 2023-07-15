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

Symbol::Symbol(const std::string& symbol)
    : Token(Type::Symbol)
    , symbol(symbol) {}

Keyword::Keyword(const std::string &_name)
    : Token(Type::Keyword)
    , name(_name.substr(1)) {
    fc_assert(_name.length() >= 2);
    fc_assert(_name[0] == ':');
}

BooleanLiteral::BooleanLiteral(bool value)
    : Token(Type::BooleanLiteral)
    , value(value) {}

IntegerLiteral::IntegerLiteral(uint64_t _value, uint32_t bits)
    : Token(Type::IntegerLiteral)
    , value(static_cast<uint64_t>(_value))
    , isSigned(false)
    , bits(bits) {}

IntegerLiteral::IntegerLiteral(int64_t _value, uint32_t bits)
    : Token(Type::IntegerLiteral)
    , value(static_cast<int64_t>(_value))
    , isSigned(true)
    , bits(bits) {}

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

EndOfFile::EndOfFile()
    : Token(Type::EndOfFile) {}

bool operator==(const token::IntegerLiteral &l, const token::IntegerLiteral &r) {
    return l.isSigned == r.isSigned
        && l.bits == r.bits
        && l.value == r.value
        && l.line == r.line
        && l.col == r.col;
}
}

std::ostream &operator<<(std::ostream &os, const token::Type &type) {
    switch (type) {
    case token::Type::Error:
        os << "Error";
        break;
    case token::Type::LParen:
        os << "LParen";
        break;
    case token::Type::RParen:
        os << "RParen";
        break;
    case token::Type::Symbol:
        os << "Symbol";
        break;
    case token::Type::Keyword:
        os << "Keyword";
        break;
    case token::Type::BooleanLiteral:
        os << "BooleanLiteral";
        break;
    case token::Type::IntegerLiteral:
        os << "IntegerLiteral";
        break;
    case token::Type::CharLiteral:
        os << "CharLiteral";
        break;
    case token::Type::FloatLiteral:
        os << "FloatLiteral";
        break;
    case token::Type::StringLiteral:
        os << "StringLiteral";
        break;
    case token::Type::EndOfFile:
        os << "EndOfFile";
        break;
    }
    return os;
}
