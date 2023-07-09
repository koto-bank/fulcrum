#pragma once

#include "assert.hpp"
#include "token.hpp"
#include "parser.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace tree_matcher {
enum class ExprType {
    Node,
    List,
};

using ExprTypeMatcherFn = std::function<void(ExprType, ExprType)>;
using ListSizeMatcherFn = std::function<void(size_t, size_t)>;
using TokenTypeMatcherFn = std::function<void(token::Type, token::Type)>;
using NodeMatcherFn = std::function<void(const token::Token &, const token::Token &)>;

struct TreeMatcher {
    template <typename ...Args>
    bool match(const Parser::Expression *tree, Args &&...args);

    TreeMatcher &matchExprType(ExprTypeMatcherFn matcher);
    TreeMatcher &matchListSize(ListSizeMatcherFn matcher);
    TreeMatcher &matchNodeType(TokenTypeMatcherFn matcher);
    TreeMatcher &matchNode(NodeMatcherFn matcher);
    TreeMatcher &build();

    std::function<void(ExprType, ExprType)>  exprTypeMatcher;
    std::function<void(size_t, size_t)> listSizeMatcher;
    std::function<void(token::Type, token::Type)> nodeTypeMatcher;
    std::function<void(const token::Token &, const token::Token &)> nodeMatcher;
};

TreeMatcher create();

struct Expr {
    virtual ~Expr() = default;
    virtual bool match(const Parser::Expression *, const TreeMatcher &) const = 0;
};

struct NodeBase : Expr {
    NodeBase(token::Type type) : type(type) {}
    const token::Type type = token::Type::Error;
};

template <typename T>
struct Node : NodeBase {
    template <typename U>
    Node(Node<U> &other)
        : NodeBase(token::classToType<U>())
        , token(other.token) {}

    template <typename ...Args>
    Node(Args &&...args)
        : NodeBase(token::classToType<T>())
        , token(std::forward<Args>(args)...) {}

    bool match(const Parser::Expression *node, const TreeMatcher &matcher) const override {
        auto isNode = node->token != nullptr;
        if (matcher.exprTypeMatcher != nullptr) {
            matcher.exprTypeMatcher(isNode ? ExprType::Node : ExprType::List, ExprType::Node);
        }
        if (!isNode) {
            return false;
        }
        if (matcher.nodeTypeMatcher != nullptr) {
            matcher.nodeTypeMatcher(node->token->type, this->type);
        }
        if (node->token->type != this->type) {
            return false;
        }
        // need to remove line/col info for checking
        auto t = *node->token->as<T>();
        t.line = 0u;
        t.col = 0u;
        if (matcher.nodeMatcher != nullptr) {
            matcher.nodeMatcher(t, token);
        }
        return t == token;
    }

    T token;
};

struct List : Expr {
    template <typename ...Args>
    List(Args &&...args) {
        construct(std::forward<Args>(args)...);
    }

    void construct() {}

    template <typename ...Args>
    void construct(List &&l, Args &&...args) {
        auto list = std::make_unique<List>();
        list->exprs = std::move(l.exprs);
        exprs.push_back(std::move(list));
        construct(std::forward<Args>(args)...);
    }

    template <typename T, typename ...Args>
    void construct(T &&arg, Args &&...args) {
        exprs.push_back(std::make_unique<T>(arg));
        construct(std::forward<Args>(args)...);
    }

    bool match(const Parser::Expression *list, const TreeMatcher &matcher) const override {
        auto isList = list->token == nullptr;
        if (matcher.exprTypeMatcher != nullptr) {
            matcher.exprTypeMatcher(isList ? ExprType::List : ExprType::Node, ExprType::List);
        }
        if (!isList) {
            return false;
        }
        auto parsedSize = list->children.size();
        auto expectedSize = size();
        if (matcher.listSizeMatcher != nullptr) {
            matcher.listSizeMatcher(parsedSize, expectedSize);
        }
        if (parsedSize != expectedSize) {
            return false;
        }
        bool res = true;
        for (auto i = 0u; res && i < size(); i++) {
            res = res && exprs[i]->match(&list->children[i], matcher);
        }
        return res;
    }

    template <typename T>
    T *get(size_t i) {
        return dynamic_cast<T *>(exprs[i].get());
    }

    size_t size() const {
        return exprs.size();
    }

    std::vector<std::unique_ptr<Expr>> exprs;
};

// helper
List createList(List &&list);

template <typename ...Args>
auto createList(Args &&...args) {
    return List(std::forward<Args>(args)...);
}

template <typename ...Args>
bool TreeMatcher::match(const Parser::Expression *tree, Args &&...args) {
    List l;
    l.construct(std::forward<Args>(args)...);
    return l.match(tree, *this);
}
}
