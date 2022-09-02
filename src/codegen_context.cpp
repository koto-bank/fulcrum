#include <fmt/format.h>

#include "codegen_context.hpp"
#include "expressions.hpp"

std::string Function::dump() {
        std::vector<std::string> argumentDumps, expressionDumps;
        for (auto i = 0; i < argumentNames.size(); i++)
            argumentDumps.push_back(
                fmt::format("({} {})", argumentNames[i], type->arguments[i]->signature())
            );
        for (auto &expr : body) {
            // Indent by 4
            expressionDumps.push_back(expr->dump(4));
        }

        return fmt::format(
            "({} {} {} ({})\n{})",
            isPublic ? "fn" : "fn-",
            name,
            type->returnType->signature(),
            fmt::join(argumentDumps, " "),
            fmt::join(expressionDumps, "\n")
        );
}
