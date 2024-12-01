#include <spdlog/spdlog.h>

#include "ast_type_storage.hpp"

ASTType *ASTTypeStorage::getOrEmplaceType(std::unique_ptr<ASTType> &&target) {
    auto sig = target->signature();
    if (allTypes.contains(sig)) {
        return allTypes.at(sig).get();
    }
    return allTypes.emplace(sig, std::move(target))
        .first->second.get();
}
