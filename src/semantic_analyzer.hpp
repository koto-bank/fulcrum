#pragma once

#include <memory>
#include <optional>

#include "parse_context.hpp"
#include "parser.hpp"

/*
   Level 2 transformer, which purpose is to convert our parsed syntax tree
   to LLVM-aware AST.
*/

struct SemanticAnalyzer {
    bool run(Parser &parser);

    std::unique_ptr<ModuleNode> releaseModule();

    bool parseModuleDefinition(const Parser::Expression &moduleForm);
    bool parseImport(const Parser::Expression &form);
    bool parseToplevelForm(const Parser::Expression &form);
    std::optional<std::unique_ptr<ASTType>> parseType(const Parser::Expression &form);
    bool parseFunctionDefinition(const Parser::Expression &form);
    bool parseStructureDefinition(const Parser::Expression &form);

    // Function related
    std::optional<ArgList> parseArgList(const Parser::Expression &form);
    std::optional<std::unique_ptr<ASTNode>> parseBodyForm(const Parser::Expression &form);

    std::optional<std::unique_ptr<VarDeclarationNode>> parseVariableDeclaraion(const Parser::Expression &form);
    std::optional<std::unique_ptr<FunctionCallNode>> parseFunctionCall(const Parser::Expression &form);
    std::optional<std::unique_ptr<ASTNode>> parseArgExpression(const Parser::Expression &form);

    struct Error {
        const std::string sourceLine;
        const std::string error;
        const uint32_t line;
        const uint32_t col;
    };

    void reportError(const std::unique_ptr<token::Token> &token, const std::string &errorMsg);
    std::vector<Error> getErrors() const;
    void dumpErrors() const;

    Parser *parser {};
    std::unique_ptr<ModuleNode> module;
    std::vector<Error> errors;
};
