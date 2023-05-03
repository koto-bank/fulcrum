#pragma once

#include <memory>
#include <string>

#include "parse_context.hpp"
#include "lexer.hpp"

struct CodegenContext;

class Parser {
public:
    // parse stdin
    bool parse(CodegenContext *code);

    // parse file
    bool parse(CodegenContext *code, const std::filesystem::path &path);

    // get result
    std::unique_ptr<ModuleNode> getResult();

    struct ParsingError {
        std::string fileName;
        std::string line;
        std::string error;
        uint32_t lineNum;
        uint32_t colNum;
    };

    std::vector<ParsingError> getErrors();
    void dumpErrors();

private:
    bool parse();

    // Top level
    bool parseModule(CodegenContext *code);
    bool parseAlias(CodegenContext *code);
    bool parseStructrueDef(CodegenContext *code);
    bool parseFnDef(CodegenContext *code);

    // Types and literals
    bool parseType(CodegenContext *code);
    bool parseArray(CodegenContext *code);
    bool parseInteger(CodegenContext *code);
    bool parseFloat(CodegenContext *code);
    bool parseString(CodegenContext *code);
    bool parseBool(CodegenContext *code);

    std::unique_ptr<std::ifstream> file;
    CodegenContext *code = nullptr;
    std::istream *stream = nullptr;

    std::vector<ParsingError> parsingErrors;

    std::unique_ptr<ParseContext> ctx;
    std::unique_ptr<ModuleNode> parsedModule;

    void lex();

    Lexer lexer;
};
