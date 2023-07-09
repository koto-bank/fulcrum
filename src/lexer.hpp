#pragma once

#include <memory>
#include <string>
#include <vector>
#include <istream>

#include "token.hpp"

struct Lexer {
    using Tokens = std::vector<std::unique_ptr<token::Token>>;
    Tokens lex(std::istream *stream);

private:
    bool inComment = false;
    bool inString = false;
    bool inCharLiteral = false;
    bool readingToken = false;
    bool escape = false;

    std::string currentTokenStr;

    size_t line = 1;
    size_t col = 0;

    void pushToken();

    Tokens tokens;

    std::unique_ptr<token::Token> readNumber();
    std::unique_ptr<token::Token> readToken(const std::string &str);

    template <uint32_t code, typename T, typename ...Args>
    void pushError(Args &&...args);

    template <uint32_t code>
    void pushError();

    template <typename T, typename ...Args>
    void pushToken(Args &&...args);
};
