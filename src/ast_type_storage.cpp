#include <spdlog/spdlog.h>

#include "ast_type_storage.hpp"

void ASTTypeStorage::merge(ASTTypeStorage &&other) {
    allTypes.merge(std::move(other.allTypes));
}

void ASTTypeStorage::addNick(const std::string &nick) {
    for (auto &[sig, t] : allTypes) {
        if (auto nt = dynamic_cast<ASTNamedType *>(t.get()); nt != nullptr) {
            auto newName = NamePath(nt->name);
            newName.add(nick);
            spdlog::info("Module import with nick: renaming type {} to {}", nt->name.join(), newName.join());
            nt->name.add(nick);
        }
    }
}
