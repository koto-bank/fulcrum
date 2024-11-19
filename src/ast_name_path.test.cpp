#include <gtest/gtest.h>

#include "ast_name_path.hpp"

TEST(ast_name_path, error_periods) {
    std::string tok = "a.b..c.d";
    auto res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);

    tok = "a..b.c";
    res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);

    tok = "..a.b";
    res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);

    tok = "a..b";
    res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);

    tok = "a..";
    res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);

    tok = "..";
    res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);
}

TEST(ast_name_path, error_empty_string) {
    const std::string tok = "";
    auto res = NamePath::create(tok);
    EXPECT_EQ(res, std::nullopt);
}

TEST(ast_name_path, valid) {
    std::string tok = "a";
    auto res = NamePath::create(tok);
    EXPECT_NE(res, std::nullopt);
    auto &name = res.value();
    EXPECT_EQ(name.size(), 1);
    EXPECT_EQ(name.getPath()[0], "a");

    tok = "a.b";
    res = NamePath::create(tok);
    EXPECT_NE(res, std::nullopt);
    name = res.value();
    EXPECT_EQ(name.size(), 2);
    EXPECT_EQ(name.getPath()[0], "b");
    EXPECT_EQ(name.getPath()[1], "a");

    tok = "a.b.c";
    res = NamePath::create(tok);
    EXPECT_NE(res, std::nullopt);
    name = res.value();
    EXPECT_EQ(name.size(), 3);
    EXPECT_EQ(name.getPath()[0], "c");
    EXPECT_EQ(name.getPath()[1], "b");
    EXPECT_EQ(name.getPath()[2], "a");

    tok = "long-name";
    res = NamePath::create(tok);
    EXPECT_NE(res, std::nullopt);
    name = res.value();
    EXPECT_EQ(name.size(), 1);
    EXPECT_EQ(name.getPath()[0], "long-name");

    tok = "some-name.some-field.other-field";
    res = NamePath::create(tok);
    EXPECT_NE(res, std::nullopt);
    name = res.value();
    EXPECT_EQ(name.size(), 3);
    EXPECT_EQ(name.getPath()[0], "other-field");
    EXPECT_EQ(name.getPath()[1], "some-field");
    EXPECT_EQ(name.getPath()[2], "some-name");

    tok = "some.n.ame.so.m.e";
    res = NamePath::create(tok);
    EXPECT_NE(res, std::nullopt);
    name = res.value();
    EXPECT_EQ(name.size(), 6);
    EXPECT_EQ(name.getPath()[0], "e");
    EXPECT_EQ(name.getPath()[1], "m");
    EXPECT_EQ(name.getPath()[2], "so");
    EXPECT_EQ(name.getPath()[3], "ame");
    EXPECT_EQ(name.getPath()[4], "n");
    EXPECT_EQ(name.getPath()[5], "some");
}

TEST(ast_name_path, join) {
    NamePath p;
    p.add("fun");
    p.add("struct");
    p.add("module");

    auto res = p.join();
    EXPECT_EQ(res, "module.struct.fun");
}
