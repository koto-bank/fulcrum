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
    bool readingToken = false;
    bool stringEscape = false;

    std::string currentTokenStr;

    size_t line = 1;
    size_t col = 0;

    void pushToken();

    Tokens tokens;

    std::unique_ptr<token::Token> readNumber();
    std::unique_ptr<token::Token> readToken(const std::string &str);
};
