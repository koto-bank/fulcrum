#include <gtest/gtest.h>

#include "parser.hpp"
#include "tree_matcher.hpp"

namespace {

#define L(...) tree_matcher::createList(__VA_ARGS__)

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

void checkParseResult(bool result,
                      const Parser &parser,
                      const std::string &program) {
    if (!result) {
        std::cout << "---+++++ Parsing failure! +++++---\n"
                  << "Program:" << program << "\n   Tree:\n";
        parser.getSyntaxTree()->dump();
        std::cout << " Errors:\n";
        parser.dumpErrors();
        std::cout << "---+++++ ---------------- +++++---\n";
    }
}
}

TEST(parser, sanity_check) {
    auto i1 = token::IntegerLiteral((int64_t)123, 32);
    auto i2 = token::IntegerLiteral((int64_t)123, 32);
    EXPECT_EQ(i1, i2);

    auto i3 = token::IntegerLiteral((int64_t)321, 32);
    auto i4 = token::IntegerLiteral((uint64_t)123, 32);
    auto i5 = token::IntegerLiteral((int64_t)123, 64);
    EXPECT_NE(i1, i3);
    EXPECT_NE(i1, i4);
    EXPECT_NE(i1, i5);

    i2.col = 1;
    EXPECT_NE(i1, i2);

    auto symbol1 = token::Symbol("foo");
    auto symbol2 = token::Symbol("foo");

    EXPECT_EQ(symbol1, symbol2);

    auto kw1 = token::Keyword(":foo");
    auto kw2 = token::Keyword(":foo");
}

TEST(parser, empty_list) {
    const std::string program = R""(
()
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.releaseSyntaxTree();
    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 1);
    EXPECT_EQ(tree->children[0].children.size(), 0);

    auto matcher = makeMatcher();
    res = matcher.match(tree, L());
    checkParseResult(res, parser, program);
    EXPECT_TRUE(res);
}

TEST(parser, nested_list) {
    const std::string program = R""(
(())
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.releaseSyntaxTree();

    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(L()));
    checkParseResult(res, parser, program);

    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 1);
    EXPECT_EQ(tree->children[0].children.size(), 1);
    EXPECT_EQ(tree->children[0].children[0].children.size(), 0);

    EXPECT_TRUE(res);
}

TEST(parser, nested_lists) {
    const std::string program = R""(
(()(()))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.releaseSyntaxTree();

    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(L(), L(L())));
    checkParseResult(res, parser, program);

    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 1);
    EXPECT_EQ(tree->children[0].children.size(), 2);
    EXPECT_EQ(tree->children[0].children[0].children.size(), 0);
    EXPECT_EQ(tree->children[0].children[1].children.size(), 1);
    EXPECT_EQ(tree->children[0].children[1].children[0].children.size(), 0);

    EXPECT_TRUE(res);
}

TEST(parser, empty_lists) {
    const std::string program = R""(
()()
()
(())
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.releaseSyntaxTree();
    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 4);
    EXPECT_EQ(tree->children[0].children.size(), 0);
    EXPECT_EQ(tree->children[1].children.size(), 0);
    EXPECT_EQ(tree->children[2].children.size(), 0);
    EXPECT_EQ(tree->children[3].children.size(), 1);
    EXPECT_EQ(tree->children[3].children[0].children.size(), 0);

    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(), L(), L(), L(L()));
    checkParseResult(res, parser, program);
    EXPECT_TRUE(res);
}

TEST(parser, simple_1) {
    const std::string program = R""(
(+ (* 1 2) (- 3 4))
(foo)
()
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.releaseSyntaxTree();
    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 3);
    EXPECT_EQ(tree->children[0].children.size(), 3);
    EXPECT_EQ(tree->children[1].children.size(), 1);
    EXPECT_EQ(tree->children[2].children.size(), 0);

    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Symbol>("+"),
                          L(N<token::Symbol>("*"),
                            N<token::IntegerLiteral>((int64_t)1, 32),
                            N<token::IntegerLiteral>((int64_t)2, 32)),
                          L(N<token::Symbol>("-"),
                            N<token::IntegerLiteral>((int64_t)3, 32),
                            N<token::IntegerLiteral>((int64_t)4, 32))),
                        L(N<token::Symbol>("foo")),
                        L());
    checkParseResult(res, parser, program);
    EXPECT_TRUE(res);
}

TEST(parser, simple_2) {
    const std::string program = R""(
(fn test :attr (a b) (+ 123 a b))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);

    const auto tree = parser.releaseSyntaxTree();
    EXPECT_FALSE(tree->children.empty());
    EXPECT_EQ(tree->children.size(), 1);
    EXPECT_EQ(tree->children[0].children.size(), 5);

    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Symbol>("fn"),
                          N<token::Symbol>("test"),
                          N<token::Keyword>(":attr"),
                          L(N<token::Symbol>("a"),
                            N<token::Symbol>("b")),
                          L(N<token::Symbol>("+"),
                            N<token::IntegerLiteral>((int64_t)123, 32),
                            N<token::Symbol>("a"),
                            N<token::Symbol>("b"))));
    checkParseResult(res, parser, program);

    EXPECT_TRUE(res);
}

TEST(parser, simple_error) {
    const std::string program = R""(
(module simple
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_FALSE(res); // unclosed paren

    const auto tree = parser.releaseSyntaxTree();
    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Symbol>("module"),
                          N<token::Symbol>("simple")));
    checkParseResult(res, parser, program);
    const auto &errors = parser.getErrors();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].error, "Unmatched opening parenthesis");
    EXPECT_TRUE(res);
}

TEST(parser, closing_paren_error) {
    const std::string program = R""(
)(module simple)
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_FALSE(res); // stray RParen

    const auto tree = parser.releaseSyntaxTree();
    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Symbol>("module"),
                          N<token::Symbol>("simple")));
    checkParseResult(res, parser, program);
    const auto &errors = parser.getErrors();
    EXPECT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].error, "Unmatched closing parenthesis");
    EXPECT_TRUE(res);
}

TEST(parser, simple_3) {
    const std::string program = R""(
(module simple)

(fn main i32 ()
  (return 0))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);
    auto tree = parser.releaseSyntaxTree();
    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Symbol>("module"),
                          N<token::Symbol>("simple")),
                        L(N<token::Symbol>("fn"),
                          N<token::Symbol>("main"),
                          N<token::Symbol>("i32"),
                          L(),
                          L(N<token::Symbol>("return"),
                            N<token::IntegerLiteral>((int64_t)0, 32))));
    checkParseResult(res, parser, program);

    EXPECT_TRUE(res);
}

TEST(parser, simple_4) {
    const std::string program = R""(
(module simple)

(fn main i32 ((argc i32) (argv (array 100 (ptr u8))))
  (var (i i32 (+ 12 34 56))
       (j u32 (size-of
               i32)))
  (return (+ i j)))
)"";

    Parser parser;
    auto res = parser.parse(program);
    EXPECT_TRUE(res);
    auto tree = parser.releaseSyntaxTree();
    auto matcher = makeMatcher();
    res = matcher.match(tree,
                        L(N<token::Symbol>("module"),
                          N<token::Symbol>("simple")),
                        L(N<token::Symbol>("fn"),
                          N<token::Symbol>("main"),
                          N<token::Symbol>("i32"),
                          L(L(N<token::Symbol>("argc"),
                              N<token::Symbol>("i32")),
                            L(N<token::Symbol>("argv"),
                              L(N<token::Symbol>("array"),
                                N<token::IntegerLiteral>((int64_t)100, 32),
                                L(N<token::Symbol>("ptr"),
                                  N<token::Symbol>("u8"))))),
                          L(N<token::Symbol>("var"),
                            L(N<token::Symbol>("i"),
                              N<token::Symbol>("i32"),
                              L(N<token::Symbol>("+"),
                                N<token::IntegerLiteral>((int64_t)12, 32),
                                N<token::IntegerLiteral>((int64_t)34, 32),
                                N<token::IntegerLiteral>((int64_t)56, 32))),
                            L(N<token::Symbol>("j"),
                              N<token::Symbol>("u32"),
                              L(N<token::Symbol>("size-of"),
                                N<token::Symbol>("i32")))),
                          L(N<token::Symbol>("return"),
                            L(N<token::Symbol>("+"),
                              N<token::Symbol>("i"),
                              N<token::Symbol>("j")))));
    checkParseResult(res, parser, program);

    EXPECT_TRUE(res);
}
