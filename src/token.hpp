#pragma once

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

    BooleanLiteral,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
};

struct Token {
    const Type type = Type::Error;

    Token(Type type);
    Token() = delete;

    virtual ~Token() = default;

    template <typename T>
    T* as() {
#if defined(FC_DEBUG)
        auto r = dynamic_cast<T*>(this);
        fc_assert(r != nullptr);
        return r;
#else
        return static_cast<T*>(this);
#endif
    }
};

struct Error final : public Token {
    Error(const std::string &what, std::unique_ptr<Token> &&originalToken);

    std::string what;
    std::unique_ptr<Token> originalToken;
};

struct LParen final : public Token {
    LParen();
};

struct RParen final : public Token {
    RParen();
};

struct Id final : public Token {
    Id(const std::string& id);
    std::string id;
};

struct BooleanLiteral final : public Token {
    BooleanLiteral(bool value);

    bool value;
};

struct IntegerLiteral final : public Token {
    IntegerLiteral(uint64_t value, bool isSigned, uint32_t bits);

    std::variant<uint64_t, int64_t> value;
    bool isSigned;
    uint32_t bits;

    template <typename T>
    T get() {
        if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(std::get<int64_t>(value));
        } else {
            return static_cast<T>(std::get<uint64_t>(value));
        }
    }
};

struct FloatLiteral final : public Token {
    FloatLiteral(double value, uint32_t bits);

    double value;
    uint32_t bits;

    template <typename T>
    T get() {
        return static_cast<T>(value);
    }
};

struct StringLiteral final : Token {
    StringLiteral(const std::string& contents);
    std::string contents;
};
}
