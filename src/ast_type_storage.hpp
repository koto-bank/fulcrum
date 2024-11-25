#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "ast_nodes.hpp"

struct ASTTypeStorage {
    template <typename T, typename ...Args>
    ASTType *getType(Args  &&...args) {
        auto val = std::make_unique<T>(std::forward<Args>(args)...);
        auto sig = val->signature();
        if (allTypes.contains(sig)) {
            return allTypes.at(sig).get();
        }
        return allTypes.emplace(sig, std::move(val))
            .first->second.get();
    }

    void merge(ASTTypeStorage &&other);
    void addNick(const std::string &nick);

private:
    std::unordered_map<std::string, std::unique_ptr<ASTType>> allTypes;
};
