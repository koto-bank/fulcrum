#pragma once

#include <filesystem>
#include <functional>
#include <istream>
#include <memory>
#include <string>

#include "lexer.hpp"

class Parser {
public:
    bool parse(); // parse stdin
    bool parse(const std::filesystem::path &path);
    bool parse(std::istream *stream);
    bool parse(const std::string &str);

    struct Error {
        const std::string sourceLine;
        const std::string error;
        const uint32_t line;
        const uint32_t col;
    };

    std::string currentFileName;

    std::vector<Error> getErrors() const;
    void dumpErrors() const;

    struct Expression {
        Expression() = default;
        Expression(Expression *parent);

        Expression *parent = nullptr;
        std::unique_ptr<token::Token> token; // lists have nullptr, terminals -- a corresponding token
        std::vector<Expression> children;

        void dump(uint32_t indent = 0u) const;
    };

    std::unique_ptr<Expression> releaseSyntaxTree();
    const Expression *getSyntaxTree() const;

private:
    bool process(std::istream *stream);
    bool parseProgram();
    bool parseExpression();

    bool parseList();
    bool parseTerminal();
    bool parseEndOfInput();

    bool matchNextToken(token::Type expectedType);
    bool matchNextTokenAndPushTerminal(token::Type expectedTerminalType);

    void pushExpression();
    void popExpression();

    std::unique_ptr<Expression> syntaxTree;
    Expression *currentExpression;

// Errors and recovery:
    struct LParen {
        uint32_t line;
        uint32_t col;
    };

    std::vector<LParen> openParens;

    bool parseStrayRParen();

    std::vector<Error> errors;
    Lexer::Tokens tokens {};
    Lexer::Tokens::iterator nextToken {};
};
