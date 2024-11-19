#pragma once

#include <optional>
#include <string>
#include <vector>

// TODO: move this to lexer level, when reader macros are implemented? (will they ever be?)
// to more efficiently catch errors like two periods in a row
struct NamePath {
    // path to name access can be single name,
    // prefixed with module or some structure field of unknown depth
    // E.g.:
    // (stdio.printf "%d" errno.ERRNO) ; paths are ("printf" "stdio") and ("ERRNO" "stdlib")
    // (+ user.stats.counter 1) ; path is ("counter" "stats" "user")
    // (let ((w (new SDL.window)) ...) ; path is ("window" "SDL")

    static std::optional<NamePath> create(const std::string &symbolName);
    static NamePath create(const char *symbolName); // for built-in literal types

    std::string join() const;

    void add(const std::string &part);

    bool operator==(const NamePath &other) const;

    size_t size() const;

    // for tests
    const std::vector<std::string> & getPath() const;

private:
    std::vector<std::string> path;
};
