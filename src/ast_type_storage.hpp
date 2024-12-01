#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "ast_nodes.hpp"

struct ASTTypeStorage {
    template <typename T, typename ...Args>
    ASTType *getType(Args  &&...args) {
        auto val = std::make_unique<T>(std::forward<Args>(args)...);
        return getOrEmplaceType(std::move(val));
    }

private:
    ASTType *getOrEmplaceType(std::unique_ptr<ASTType> &&target);

    std::unordered_map<std::string, std::unique_ptr<ASTType>> allTypes;
};
