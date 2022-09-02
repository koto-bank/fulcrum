#pragma once

#include <string>
#include <vector>
#include <iostream>

#include "codegen_context.hpp"
#include "expressions.hpp"

struct Statement {
    virtual ~Statement() = default;

    template <typename T>
    T* as() {
        T* ret = dynamic_cast<T*>(this);
        assert(ret != nullptr);
        return ret;
    }

    virtual void print() = 0;
};

struct Import {
    std::string filename;
    std::string module;
    std::vector<std::string> keywords;
};

struct Module {
    std::string name;
    std::vector<Import> imports;

    void print() {
        using namespace std;

        cout << "Module " << name << "\n";
        for (const auto &import : imports) {
            if (!import.filename.empty()) {
                cout << "Imports C header " << import.filename;
            }

            if (!import.module.empty()) {
                cout << "Imports module " << import.module;
            }

            if (import.keywords.empty() == false) {
                cout << " with options: ";
                for (const auto &kw : import.keywords) {
                    cout << "'" << kw << "'" << " ";
                }
            }
            cout << "\n";
        }
    }
};

struct Program {
    Module module;
    std::vector<std::unique_ptr<Statement>> statements;

    void print() {
        module.print();
        std::cout << "---------------------\n";
        for (const auto &s : statements) {
            if (s != nullptr) {
                s->print();
            }
        }
    }
};

union ParsedLine {
    long iNum;
    double fNum;
    char *str;
    bool boolConst;
};

struct FunctionParseContext {
    std::string name;
    std::vector<std::tuple<std::string, LanguageType *>> arguments;
    LanguageType *returnType = nullptr;
    std::vector<std::unique_ptr<Expression>> body;
};

struct FunctionCallParseContext {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
};

std::unique_ptr<Program> parse(CodegenContext *codegenCont);
