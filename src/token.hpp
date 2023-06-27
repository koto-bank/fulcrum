#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <variant>

#include "assert.hpp"

namespace token {
enum class Type : uint32_t {
    Error,

    LParen,
    RParen,
    Id,
    Keyword,

    BooleanLiteral,
    IntegerLiteral,
    CharLiteral,
    FloatLiteral,
    StringLiteral,

    EndOfFile,
};

struct Token {
    const Type type = Type::Error;

    Token(Type type);
    Token() = delete;

    virtual ~Token() = default;

    template <typename T>
    T *as() {
#if defined(FC_DEBUG)
        auto r = dynamic_cast<T *>(this);
        fc_assert(r != nullptr);
        return r;
#else
        return static_cast<T *>(this);
#endif
    }
};

struct Error final : public Token {
    enum {
        EOFInString,
        LineBreakInString,

        EOFInChar,
        LineBreakInChar,
        CharLiteralTooLong,

        IntegerOverflow,
        IllFormedInteger,
        IntegerBadBitSize,

        FloatOverflow,
        IllFormedFloat,
        FloatBadBitSize,

        NumericValueParsingFailed, // ...for some reason

        Count
    };

    Error(uint32_t code, std::unique_ptr<Token> &&originalToken);

    const uint32_t code;
    const std::string what;
    std::unique_ptr<Token> originalToken;
};

struct LParen final : public Token {
    LParen();
};

struct RParen final : public Token {
    RParen();
};

struct Id final : public Token {
    Id(const std::string &id);
    const std::string id;
};

struct Keyword final : public Token {
    Keyword(const std::string &name);
    const std::string name;
};

struct BooleanLiteral final : public Token {
    BooleanLiteral(bool value);

    const bool value;
};

struct IntegerLiteral final : public Token {
    IntegerLiteral(int64_t value, uint32_t bits);
    IntegerLiteral(uint64_t value, uint32_t bits);

    const std::variant<uint64_t, int64_t> value;
    const bool isSigned;
    const uint32_t bits;

    template <typename T>
    T get() {
        if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(std::get<int64_t>(value));
        } else {
            return static_cast<T>(std::get<uint64_t>(value));
        }
    }
};

struct CharLiteral final : public Token {
    CharLiteral(uint8_t value);

    const uint8_t value;
};

struct FloatLiteral final : public Token {
    FloatLiteral(double value, uint32_t bits);

    const double value;
    const uint32_t bits;

    template <typename T>
    T get() {
        return static_cast<T>(value);
    }
};

struct StringLiteral final : Token {
    StringLiteral(const std::string &contents);
    const std::string contents;
};

struct EndOfFile final : Token {
    EndOfFile();
};
}
std::ostream &operator<<(std::ostream &os, const token::Type &);
