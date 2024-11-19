#pragma once

#include <filesystem>
#include <vector>

struct Compiler {
    int run(int argc, char *argv[]);

    std::vector<std::filesystem::path> includeDirectories;
};
