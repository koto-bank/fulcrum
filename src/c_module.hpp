#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast_nodes.hpp"

struct CModule {
    std::vector<FunctionNode> functions;
    std::vector<StructNode> structs;
    std::vector<TypeAliasNode> typeAliases;
    std::vector<VariableDeclarationNode> globalVariables;

    ASTTypeStorage types;
};
