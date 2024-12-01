#pragma once

#include <vector>
#include <string>

#include "ast_nodes.hpp"
#include "ast_type_storage.hpp"

struct FulcrumModule {
    FulcrumModule(ASTTypeStorage &typeStorage);
    FulcrumModule(FulcrumModule &&other) = default;

    std::string name;

    struct Import {
        std::string target;
        std::string nickname;
        bool isCImport;
    };

    std::vector<Import> imports;

    std::vector<FunctionNode> functions;
    std::vector<StructNode> structs;
    std::vector<TypeAliasNode> typeAliases;
    std::vector<VariableDeclarationNode> globalVariables;

    ASTTypeStorage &types;
};
