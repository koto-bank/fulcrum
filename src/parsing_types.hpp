#pragma once

#include <string>
#include <vector>
#include <iostream>

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

struct Type {
    virtual ~Type() = default;

    virtual std::string to_str() const = 0;

    template <typename T>
    T* as() {
        T* ret = dynamic_cast<T*>(this);
        assert(ret != nullptr);
        return ret;
    }
};

struct PtrType : Type {
    std::string to_str() const override {
        return "ptr to " + target->to_str();
    }

    std::unique_ptr<Type> target;
};

struct IntegerType : Type {
    std::string to_str() const override {
        return (is_signed ? "i" : "u") + std::to_string(bits);
    }

    bool is_signed;
    int bits;
};

struct FloatType : Type {
    std::string to_str() const override {
        return (is_double ? "f64" : "f32");
    }

    bool is_double;
};

struct BoolType : Type {
    std::string to_str() const override {
        return "bool";
    }
};

struct VoidType : Type {
    std::string to_str() const override {
        return "void";
    }
};

struct CustomType : Type {
    std::string to_str() const override {
        return "Custom type (" + name + ")";
    }

    std::string name;
};

struct StringType : Type {
    std::string to_str() const override {
        return "str";
    }
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

struct StructField {
    std::string name;
    std::unique_ptr<Type> type;
};

struct Struct : Statement {
    Struct(bool is_public_)
        : is_public(is_public_) {}

    void print() override {
        std::cout << "Hello from " << (is_public ? "public" : "private") << " struct of type " << type.name << '\n';
        if (!fields.empty()) {
            std::cout << "Fields:\n";
            for (const auto& f : fields) {
                std::cout << f.name << " of type " << f.type->to_str() << '\n';
            }
        }
        std::cout << '\n';
    }

    CustomType type;
    bool is_public = true;
    std::vector<StructField> fields;
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

struct TypeStorage {
    void addType(std::unique_ptr<Type>&& new_type) {
        if (type == nullptr) {
            type = std::move(new_type);
        } else {
            auto t = type->as<PtrType>();
            assert(t != nullptr);
            while (t->target != nullptr) {
                t = t->target->as<PtrType>();
                assert(t != nullptr);
            }
            t->target = std::move(new_type);
        }
    }

    template <typename T>
    T* as() {
        assert(type != nullptr);
        T* ret = dynamic_cast<T*>(type.get());
        assert(ret != nullptr);
        return ret;
    }

    std::unique_ptr<Type> consume() {
        return std::move(type);
    }

    std::unique_ptr<Type> type;
};

union ParsedLine {
    long i_num;
    double f_num;
    char *str;
};
