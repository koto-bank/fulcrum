#pragma once

#include <string>

#include "c_module.hpp"

struct Compiler;

struct HeaderParser {
    ~HeaderParser();

    bool parseHeader(const std::string &path, const Compiler &c);
    CModule takeModule();

private:
    CModule cModule;
};
