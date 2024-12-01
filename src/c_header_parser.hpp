#pragma once

#include <filesystem>
#include <string>

#include "ast_type_storage.hpp"
#include "c_module.hpp"
#include "fulcrum_module.hpp"

struct Compiler;

struct HeaderParser {
    HeaderParser(ASTTypeStorage &typeStorage, const Compiler &c);

    bool parseHeader(const std::filesystem::path &path);

    CModule takeModule();

private:
    bool moduleTaken = false;
    const Compiler &compiler;
    CModule cModule;
};
