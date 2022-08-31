#pragma once

#include <string>
#include <vector>
#include <iostream>

#include "codegen_context.hpp"

enum class TypeKind {
    Integer,
    Float,
    String,
    Bool,
    Custom,
};

enum class ExprType {
    Expr,
    Id,
    Literal,
};

struct Expr;
struct ExprContents {
    std::string id;
    Expr *expr;
//    Literal *literal;
};

struct Expr {
    ExprType type;
    ExprContents expr;
};

struct LiteralContents {
    std::string str;
//    FloatValue f;
//    IntegerValue i;
};

struct Literal {
    TypeKind type;
    LiteralContents value;
};

struct IntegerValue {
    IntegerType type;
    long value;
};

struct FloatValue {
    FloatType type;
    double value;
};

struct Var {
    char *name;
    bool global;
    std::unique_ptr<Type> type;
    Expr initializer;
};

struct Alias {
    char *alias_name;
    char *target_type;
};

struct Fn {
    bool is_public;
    char *name;
//    Arg **args;
    Expr **body;
};

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
    std::vector<std::string> keywords;
};

struct Module {
    std::string name;
    std::vector<Import> imports;

    void print() {
        using namespace std;

        cout << "Module " << name << "\n";
        for (const auto &import : imports) {
            cout << "Imports " << import.filename;
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
    long i_num;
    double f_num;
    char *str;
};

std::unique_ptr<Program> parse(CodegenContext *codegenCont);
