#include <filesystem>
#include <fstream>

#include "parser.hpp"
#include "codegen_context.hpp"
#include "lexer.hpp"
#include "parse_context.hpp"
#include "token.hpp"
#include "utils.hpp"

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

bool Parser::parse(CodegenContext *_code)
{
    code = _code;
    stream = &std::cin;
    return parse();
}

bool Parser::parse(CodegenContext *_code, const std::filesystem::path &path) {
    file = std::make_unique<std::ifstream>(path);
    if (!file->is_open()) {
        parsingErrors.push_back({ path, "", "Failed to open file", 0, 0 });
        return false;
    }
    code = _code;
    stream = file.get();
    return parse();
}

bool Parser::parse() {
    auto tokens = lexer.lex(stream);

    return true;
}

std::unique_ptr<ModuleNode> Parser::getResult() {
    return std::move(parsedModule);
}

std::vector<Parser::ParsingError> Parser::getErrors() {
    return parsingErrors;
}

void Parser::dumpErrors() {
    for (const auto &e : parsingErrors) {
        printParsingError(e);
    }
}
