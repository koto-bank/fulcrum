#include <filesystem>
#include <fstream>
#include <sstream>

#include <fmt/format.h>

#include <spdlog/spdlog.h>

#include "assert.hpp"
#include "parser.hpp"
#include "utils.hpp"

// The grammar (for reference):
// program -> expr * end
// expr -> atom
//      |  list
// atom -> literal
//      |  symbol
//      |  kw
// list -> '(' expr * ')'

namespace {
void printError(const std::string &fileName, const Parser::Error &e) {
    auto msg = fmt::format("{}:{}:{}: error: {}", fileName, e.pos.line, e.pos.col, e.error);
    spdlog::error(msg);
}
}

Parser::Expression::Expression(Pos begin)
    : extent{ .begin = begin } {}

Parser::Expression::Expression(Parser::Expression *parent, Pos begin)
    : parent(parent)
    , extent{ .begin = begin } {}

std::unique_ptr<Parser::Expression> Parser::releaseSyntaxTree() {
    return std::move(syntaxTree);
}

const Parser::Expression *Parser::getSyntaxTree() const {
    return syntaxTree.get();
}

void Parser::Expression::dump(uint32_t indent) const {
    if (token != nullptr) {
        for (auto i = indent * 4; i > 0; i--) {
            std::cout << " ";
        }
        std::cout << token->type;
        if (token->type == token::Type::Symbol) {
            std::cout << " " << token->as<token::Symbol>()->symbol;
        } else if (token->type == token::Type::Error) {
            std::cout << " " << token->as<token::Error>()->what;
        }
        std::cout << '\n';
    } else {
        for (auto i = indent * 4; i > 0; i--) {
            std::cout << " ";
        }
        if (children.empty()) {
            std::cout << "()\n";
            return;
        } else {
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
}

bool Parser::parse(std::istream *stream) {
    currentFileName = "*stream*";
    return process(stream);
}

bool Parser::parse(const std::string &str) {
    currentFileName = "*string*";
    std::istringstream stringStream(str);
    return process(&stringStream);
}

bool Parser::parse() {
    currentFileName = "*stdin*";
    return process(&std::cin);
}

bool Parser::parse(const std::filesystem::path &path) {
    currentFileName = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        errors.push_back({ "Failed to open file", { 0u, 0u } });
        return false;
    }
    return process(&file);
}

bool Parser::process(std::istream *stream) {
    Lexer lexer;
    tokens = lexer.lex(stream);
    syntaxTree = std::make_unique<Expression>(Pos { 0u, 0u });
    syntaxTree->children.clear();
    currentExpression = syntaxTree.get();
    nextToken = tokens.begin();
    return parseProgram();
}

bool Parser::parseProgram() {
    auto res = true;
    while (res && nextToken != tokens.end()) {
        auto save = nextToken;
        res = res && (parseExpression()
                      || (nextToken = save, parseStrayRParen())
                      || (nextToken = save, matchNextToken(token::Type::EndOfFile)));
    }
    return res && errors.empty();
}

bool Parser::parseExpression() {
    auto save = nextToken;
    return parseAtom()
        || (nextToken = save, parseList());
}

bool Parser::parseStrayRParen() {
    auto save = nextToken;
    if (matchNextToken(token::Type::RParen)) {
        errors.push_back({ "Unmatched closing parenthesis", { (*save)->line, (*save)->col } });
        return true;
    } else {
        return false;
    }
}

bool Parser::parseList() {
    auto save = nextToken;
    if (!matchNextToken(token::Type::LParen)) {
        return false;
    }
    Pos start { (*save)->line, (*save)->col };
    openParens.push_back({ start });
    pushExpression(start);

    while (save = nextToken, parseExpression());

    if (nextToken = save, !matchNextToken(token::Type::RParen)) {
        fc_assert(!openParens.empty());
        const auto& paren = openParens.back();
        errors.push_back({ "Unmatched opening parenthesis", { paren.pos.line, paren.pos.col } });
    }
    openParens.pop_back();
    popExpression({ (*save)->line, (*save)->col }); // maybe error here
    return true;
}

bool Parser::parseAtom() {
    auto save = nextToken;
    if (matchNextTokenAndPushAtom(token::Type::Symbol)
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::Keyword))
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::BooleanLiteral))
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::CharLiteral))
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::StringLiteral))
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::IntegerLiteral))
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::FloatLiteral))
        || (nextToken = save, matchNextTokenAndPushAtom(token::Type::Error))) {
        return true;
    } else {
        return false;
    }
}

bool Parser::matchNextToken(token::Type expectedType) {
    return (nextToken != tokens.end()
            && (*nextToken++)->type == expectedType);
}

bool Parser::matchNextTokenAndPushAtom(token::Type expectedAtomType) {
    if (nextToken != tokens.end()
        && (*nextToken)->type == expectedAtomType) {
        pushExpression({ (*nextToken)->line, (*nextToken)->col });
        currentExpression->token = std::move(*nextToken++);
        popExpression({ (*nextToken)->line, (*nextToken)->col });
        return true;
    } else {
        return false;
    };
}

void Parser::pushExpression(Pos start) {
    fc_assert(currentExpression != nullptr);
    currentExpression->children.emplace_back(currentExpression, start); // expression's parent
    currentExpression = &currentExpression->children.back();
}

void Parser::popExpression(Pos end) {
    fc_assert(currentExpression->parent != nullptr);
    currentExpression->extent.end = end;
    currentExpression = currentExpression->parent;
}

std::vector<Parser::Error> Parser::getErrors() const {
    return errors;
}

void Parser::dumpErrors() const {
    for (const auto &e : errors) {
        printError(currentFileName, e);
    }
}
