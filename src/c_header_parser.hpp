#pragma once

#include <string>

#include "c_module.hpp"

struct Compiler;

struct HeaderParser {
    ~HeaderParser();

    bool parseHeader(const std::string &path, const Compiler &c, const std::string& moduleName);
    CModule takeModule();

private:
    CModule cModule;
};
