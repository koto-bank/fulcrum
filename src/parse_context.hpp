#pragma once

#include <string>
#include <vector>
#include <iostream>

#include <stdint.h>

#include "codegen_context.hpp"
#include "expressions.hpp"

union ParsedLine {
    long iNum;
    double fNum;
    char *str;
    bool boolConst;
};

struct ParseContext {
    struct FunctionDef {
        std::string name;
        std::vector<std::tuple<std::string, LanguageType *>> arguments;
        LanguageType *returnType = nullptr;
        std::vector<std::unique_ptr<Expression>> body;
        bool isPublic;
    };

    struct FunctionCall {
        std::string name;
        std::vector<std::unique_ptr<Expression>> args;
    };

    struct StructDef {
        std::string name;
        std::vector<std::tuple<std::string, LanguageType *>> fields;
        bool isPublic;
    };

    struct IntLiteral {
        union {
            uint64_t u;
            int64_t i;
        } num;
        std::string str;
        std::string bits;
        bool isSigned;
    };

    std::unique_ptr<FunctionDef> fnDef;
    std::unique_ptr<FunctionCall> fnCall;

    std::unique_ptr<LanguageType> type = nullptr;
    std::unique_ptr<StructDef> structDef;
    std::vector<std::unique_ptr<Expression>> exprStack;
    std::unique_ptr<Import> import;

    bool isSigned; // for integer types and literals

    IntLiteral intLiteral;

    size_t arraySize;
};

bool parse(CodegenContext *);
