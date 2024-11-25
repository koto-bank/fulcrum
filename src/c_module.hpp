#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast_nodes.hpp"
#include "ast_type_storage.hpp"

struct CModule {
    std::string name; // file name without extension, e.g. 'stdio.h' -> 'stdio'
                      // name is used when no nickname is specified

    std::vector<FunctionNode> functions;
    std::vector<StructNode> structs;
    std::vector<TypeAliasNode> typeAliases;
    std::vector<VariableDeclarationNode> globalVariables;

    ASTTypeStorage types;
};
