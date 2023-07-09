#include "tree_matcher.hpp"

namespace tree_matcher {
TreeMatcher &TreeMatcher::matchExprType(std::function<void(ExprType, ExprType)> matcher) {
    exprTypeMatcher = matcher;
    return *this;
}

TreeMatcher &TreeMatcher::matchListSize(std::function<void(size_t, size_t)> matcher) {
    listSizeMatcher = matcher;
    return *this;
}

TreeMatcher &TreeMatcher::matchNodeType(std::function<void(token::Type, token::Type)> matcher) {
    nodeTypeMatcher = matcher;
    return *this;
}

TreeMatcher &TreeMatcher::matchNode(std::function<void(const token::Token &, const token::Token &)> matcher) {
    nodeMatcher = matcher;
    return *this;
}

TreeMatcher &TreeMatcher::build() {
    return *this;
}

TreeMatcher create() {
    return TreeMatcher {};
}

List createList(List &&list) {
    List l;
    l.construct(std::move(list));
    return l;
}
}
