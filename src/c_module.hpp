#pragma once

#include <map>
#include <string>
#include <vector>

#include "ast_nodes.hpp"
#include "ast_type_storage.hpp"

struct CModule {
    CModule(ASTTypeStorage &typeStorage);

    std::vector<FunctionNode> functions;
    std::vector<StructNode> structs;
    std::vector<TypeAliasNode> typeAliases;
    std::vector<VariableDeclarationNode> globalVariables;

    ASTTypeStorage &types;
};
