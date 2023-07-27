#pragma once

#include <string>
#include <map>
#include <memory>

#include <clang-c/Index.h>

struct CodegenContext;
struct CModule;
struct ModuleNodeBase;
struct VarDeclarationNode;
struct ASTType;

namespace clang {
struct Interpreter;
}

struct HeaderParser {
    HeaderParser(CodegenContext &parentContext);
    ~HeaderParser();

    bool parseHeader(const std::string &path);
    std::unique_ptr<CModule> releaseModule();

private:
    CodegenContext &codegenContext;
    std::unique_ptr<clang::Interpreter> interp;
    std::map<std::string, int> anonymousNumbers;
    std::unique_ptr<CModule> module;

    std::string getAnonName(const CXCursor cur);
    std::unique_ptr<ASTType> clangToASTType(CXType clangTp);
    std::unique_ptr<clang::Interpreter> createClangInterpreter() const;
    std::unique_ptr<VarDeclarationNode> evalMacro(const std::string &name);
    void includeHeader(const std::string &name);
};
