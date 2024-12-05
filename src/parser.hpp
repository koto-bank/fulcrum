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

    struct Pos {
        uint32_t line = 0u;
        uint32_t col = 0u;
    };

    std::string currentFileName;

    struct Expression {
        struct Extent {
            Pos begin {};
            Pos end {};
        };

        Expression(Pos begin);
        Expression(Expression *parent, Pos begin);

        Expression *parent = nullptr;
        std::unique_ptr<token::Token> token; // lists have nullptr, atoms -- a corresponding token
        std::vector<Expression> children;
        Extent extent;

        void dump(uint32_t indent = 0u) const;
    };

    struct Error {
        const std::string error;
        Pos pos;
    };

    std::vector<Error> getErrors() const;
    void dumpErrors() const;

    std::unique_ptr<Expression> releaseSyntaxTree();
    const Expression *getSyntaxTree() const;

private:
    bool process(std::istream *stream);
    bool parseProgram();
    bool parseExpression();

    bool parseList();
    bool parseAtom();
    bool parseEndOfInput();

    bool matchNextToken(token::Type expectedType);
    bool matchNextTokenAndPushAtom(token::Type expectedAtomType);

    void pushExpression(Pos start);
    void popExpression(Pos end);

    std::unique_ptr<Expression> syntaxTree;
    Expression *currentExpression;

// Errors and recovery:
    struct LParen {
        Pos pos;
    };

    std::vector<LParen> openParens;

    bool parseStrayRParen();

    std::vector<Error> errors;
    Lexer::Tokens tokens {};
    Lexer::Tokens::iterator nextToken {};
};
