#include <algorithm>

#include "assert.hpp"
#include "ast_name_path.hpp"

std::optional<NamePath> NamePath::create(const std::string &symbolName) {
    if (symbolName.empty()
        || symbolName.front() == '.'
        || symbolName.back() == '.')
    {
        return std::nullopt;
    }

    auto start = symbolName.begin();
    auto end = start;
    bool periodEncountered = false;
    NamePath result;
    while (end != symbolName.end()) {
        if (*end == '.') {
            if (periodEncountered) {
                // two periods in a row
                return std::nullopt;
            }
            periodEncountered = true;
            result.path.emplace_back(start, end);
        } else {
            if (periodEncountered) {
                start = end;
                periodEncountered = false;
            }
        }

        end++;
    }

    // last char can't be '.', so we always have this last portion
    fc_assert(periodEncountered == false);
    result.path.emplace_back(start, end);
    std::reverse(result.path.begin(), result.path.end());
    return result;
}

NamePath NamePath::create(const char *symbolName) {
    NamePath res;
    res.path.emplace_back(symbolName);
    return res;
}

void NamePath::add(const std::string &part) {
    path.push_back(part);
}

bool NamePath::operator==(const NamePath &other) const {
    return path == other.path;
}

std::string NamePath::join() const {
    if (path.size() == 0) {
        return "";
    }

    if (path.size() == 1) {
        return path.back();
    }

    std::string res = path.back();
    for (auto it = path.rbegin() + 1; it != path.rend(); it++) {
        res += "." + *it;
    }
    return res;
}

size_t NamePath::size() const {
    return path.size();
}

const std::vector<std::string> & NamePath::getPath() const {
    return path;
}
