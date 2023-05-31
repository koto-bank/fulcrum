#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <fmt/format.h>

#include "assert.hpp"
#include "parser.hpp"
#include "utils.hpp"

// The grammar:
// program -> expr * end
// expr -> atom
//      |  list
// atom -> literal
//      |  id
//      |  kw
// list -> '(' expr * ')'

namespace {
void printParsingError(const Parser::ParsingError &e) {
    std::cerr << fmt::format("Parsing error on {}:{}\n", e.fileName, e.lineNum)
              << e.line << '\n';
    for (auto i = 0u; i < e.colNum; i++) {
        std::cerr << fmt::format("{:<{}}\n", "", e.colNum);
    }

    std::cerr << e.error << '\n';
}
}

Parser::Expression::Expression(Parser::Expression *_parent)
    : parent(_parent) {}

const Parser::Expression *Parser::getSyntaxTree() const {
    return &syntaxTree;
}

void Parser::Expression::dump(uint32_t indent) const {
    if (children.empty()) {
        for (auto i = indent * 4; i > 0; i--) {
            std::cout << " ";
        }
        fc_assert(token != nullptr);
        std::cout << token->type << '\n';
    } else {
        for (auto i = indent * 4; i > 0; i--) {
            std::cout << " ";
        }
        std::cout << "(\n";
        for (const auto &c : children) {
            c.dump(indent + 1);
        }
        for (auto i = indent * 4; i > 0; i--) {
            std::cout << " ";
        }
        std::cout << ")\n";
    }
}

bool Parser::parse(std::istream *stream) {
    return process(stream);
}

bool Parser::parse(const std::string &str) {
    std::istringstream stringStream(str);
    return process(&stringStream);
}

bool Parser::parse() {
    return process(&std::cin);
}

bool Parser::parse(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        parsingErrors.push_back({ path, "", "Failed to open file", 0, 0 });
        return false;
    }
    return process(&file);
}

bool Parser::process(std::istream *stream) {
    Lexer lexer;
    tokens = lexer.lex(stream);

    syntaxTree.children.clear();
    currentExpression = &syntaxTree;
    nextToken = tokens.begin();
    bool parsed = parseProgram();

    // TODO: check parsed tree
    return parsed;
}

bool Parser::parseProgram() {
    auto res = true;
    while (res && nextToken != tokens.end()) {
        auto save = nextToken;
        res = res && (parseExpression()
                      || (nextToken = save, parseEndOfInput()));
    }
    return res;
}

bool Parser::parseExpression() {
    auto save = nextToken;
    return parseEndOfInput()
        || (nextToken = save, parseTerminal())
        || (nextToken = save, parseList());
}

bool Parser::parseList() {
    auto lParen = matchNextToken(token::Type::LParen);
    if (!lParen) {
        return false;
    }

    pushExpression();

    auto save = nextToken;
    while (parseExpression()) {
        save = nextToken;
    };
    nextToken = save;
    popExpression();
    return matchNextToken(token::Type::RParen);
}

bool Parser::parseEndOfInput() {
    return matchNextToken(token::Type::EndOfFile);
}

bool Parser::parseTerminal() {
    auto save = nextToken;
    if (matchAndPushNextToken(token::Type::Id)
        || (nextToken = save, matchAndPushNextToken(token::Type::Keyword))
        || (nextToken = save, matchAndPushNextToken(token::Type::BooleanLiteral))
        || (nextToken = save, matchAndPushNextToken(token::Type::CharLiteral))
        || (nextToken = save, matchAndPushNextToken(token::Type::StringLiteral))
        || (nextToken = save, matchAndPushNextToken(token::Type::IntegerLiteral))
        || (nextToken = save, matchAndPushNextToken(token::Type::FloatLiteral))) {
        return true;
    } else {
        return false;
    }
}

bool Parser::matchNextToken(token::Type expectedType) {
    return nextToken != tokens.end()
        && (*nextToken++)->type == expectedType;
}

bool Parser::matchAndPushNextToken(token::Type expectedType) {
    if (nextToken != tokens.end()
        && (*nextToken)->type == expectedType) {
        pushExpression();
        currentExpression->token = nextToken++->get();
        currentExpression = currentExpression->parent;
        return true;
    } else {
        return false;
    };
}

void Parser::pushExpression() {
    fc_assert(currentExpression != nullptr);
    currentExpression->children.emplace_back(currentExpression);
    currentExpression = &currentExpression->children.back();
}

void Parser::popExpression() {
    fc_assert(currentExpression->parent != nullptr);
    currentExpression = currentExpression->parent;
}

std::vector<Parser::ParsingError> Parser::getErrors() {
    return parsingErrors;
}

void Parser::dumpErrors() {
    for (const auto &e : parsingErrors) {
        printParsingError(e);
    }
}
