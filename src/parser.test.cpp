#include <gtest/gtest.h>

#include "parser.hpp"
#include "tree_matcher.hpp"

namespace {
using L = tree_matcher::List;
template <typename T>
using N = tree_matcher::Node<T>;

auto makeMatcher() {
    return tree_matcher::create()
        .matchExprType([](auto parsed, auto expected) {
            EXPECT_EQ(parsed, expected);
        })
        .matchListSize([](size_t parsed, size_t expected) {
            EXPECT_EQ(parsed, expected);
        })
        .matchNodeType([](token::Type parsed, token::Type expected) {
            EXPECT_EQ(parsed, expected);
        })
        .matchNode([](auto parsed, auto expected) {
            EXPECT_EQ(parsed, expected);
        })
        .build();
}
}

TEST(parser, simple_1) {
    const std::string program = R""(
(fn test :attr (a b) (+ 123 a b))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.getSyntaxTree();
    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 1);
    EXPECT_EQ(tree->children[0].children.size(), 5);

    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Id>("fn"),
                          N<token::Id>("test"),
                          N<token::Keyword>(":attr"),
                          L(N<token::Id>("a"),
                            N<token::Id>("b")),
                          L(N<token::Id>("+"),
                            N<token::IntegerLiteral>((int64_t)123, 32),
                            N<token::Id>("a"),
                            N<token::Id>("b"))));
    EXPECT_TRUE(res);
}

TEST(parser, simple_error) {
    const std::string program = R""(
(module simple
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_FALSE(res); // unclosed paren
}

TEST(parser, simple_2) {
    const std::string program = R""(
(module simple)

(fn main i32 ()
  (return 0))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);
    auto tree = parser.getSyntaxTree();
    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Id>("module"),
                          N<token::Id>("simple")),
                        L(N<token::Id>("fn"),
                          N<token::Id>("main"),
                          N<token::Id>("i32"),
                          L(),
                          L(N<token::Id>("return"),
                            N<token::IntegerLiteral>((int64_t)0, 32))));
}

TEST(parser, simple_3) {
    const std::string program = R""(
(module simple)

(fn main i32 ((argc i32) (argv (array 100 (ptr u8))))
  (var (i i32 (+ 12 34 56))
       (j u32 (sizeof
               i32)))
  (return (+ i j)))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);
    auto tree = parser.getSyntaxTree();
    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Id>("module"),
                          N<token::Id>("simple")),
                        L(N<token::Id>("fn"),
                          N<token::Id>("main"),
                          N<token::Id>("i32"),
                          L(L(N<token::Id>("argc"),
                              N<token::Id>("i32")),
                            L(N<token::Id>("argv"),
                              L(N<token::Id>("array"),
                                N<token::IntegerLiteral>((int64_t)100, 32),
                                L(N<token::Id>("ptr"),
                                  N<token::Id>("u8"))))),
                          L(N<token::Id>("var"),
                            L(N<token::Id>("i"),
                              N<token::Id>("i32"),
                              L(N<token::Id>("+"),
                                N<token::IntegerLiteral>((int64_t)12, 32),
                                N<token::IntegerLiteral>((int64_t)34, 32),
                                N<token::IntegerLiteral>((int64_t)56, 32))),
                            L(N<token::Id>("j"),
                              N<token::Id>("u32"),
                              L(N<token::Id>("sizeof"),
                                N<token::Id>("i32")))),
                          L(N<token::Id>("return"),
                            L(N<token::Id>("+"),
                              N<token::Id>("i"),
                              N<token::Id>("j")))));
    EXPECT_TRUE(res);
}
