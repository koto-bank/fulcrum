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

    struct ParsingError {
        std::string fileName;
        std::string line;
        std::string error;
        uint32_t lineNum;
        uint32_t colNum;
    };

    std::vector<ParsingError> getErrors();
    void dumpErrors();

    struct Expression {
        Expression() = default;
        Expression(Expression *parent);

        Expression *parent = nullptr;
        token::Token *token = nullptr; // lists have nullptr, terminals -- a corresponding token
        std::vector<Expression> children;

        void dump(uint32_t indent = 0u) const;
    };

    const Expression *getSyntaxTree() const;

private:
    bool process(std::istream *stream);
    bool parseProgram();
    bool parseExpression();

    bool parseList();
    bool parseTerminal();
    bool parseEndOfInput();

    bool matchNextToken(token::Type expectedType);
    bool matchAndPushNextToken(token::Type expectedType);

    void pushExpression();
    void popExpression();

    Expression syntaxTree;
    Expression *currentExpression;

    std::vector<ParsingError> parsingErrors;
    Lexer::Tokens tokens {};
    Lexer::Tokens::iterator nextToken {};
};
