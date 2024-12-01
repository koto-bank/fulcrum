#include <spdlog/spdlog.h>

#include "compiler.hpp"

int main(int argc, char *argv[]) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$] %v");

    Compiler c;
    return c.run(argc, argv);
}
