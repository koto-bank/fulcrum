#pragma once

#include <string>

#include "ast_type_storage.hpp"
#include "c_module.hpp"

struct Compiler;

struct HeaderParser {
    HeaderParser(ASTTypeStorage &typeStorage, const Compiler &c);

    bool parseHeader(const std::string &path, const Compiler &c, const std::string& moduleName);
    CModule takeModule();

private:
    const Compiler &compiler;
    CModule cModule;
};
