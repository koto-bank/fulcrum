#include <memory>

#include <gtest/gtest.h>

#include "ast_type_storage.hpp"
#include "parser.hpp"
#include "semantic_analyzer.hpp"

struct SemaTest : public testing::Test {
    SemaTest()
        : sema(types) {}

    Parser::Expression parseForm(const std::string &input) {
        auto res = parser.parse(input);
        EXPECT_TRUE(res);
        return std::move(parser.releaseSyntaxTree()->children[0]);
    }

    ASTTypeStorage types;
    SemanticAnalyzer sema;
    Parser parser;
};

TEST_F(SemaTest, structure_definition_ok) {
    const auto ok1 = "(struct s)";

    auto expr = parseForm(ok1);
    auto res = sema.parseStructureDefinition(expr);
    EXPECT_TRUE(res);

    EXPECT_EQ(sema.module.structs.size(), 1);
    {
        auto &s = sema.module.structs[0];

        EXPECT_EQ(s.name, "s");
        EXPECT_TRUE(s.isPublic);
        EXPECT_EQ(s.fields.size(), 0);
    }

    const auto ok2 = "(struct s2 (x i32))";
    expr = parseForm(ok2);
    res = sema.parseStructureDefinition(expr);
    EXPECT_TRUE(res);

    EXPECT_EQ(sema.module.structs.size(), 2);
    {
        auto &s = sema.module.structs[1];
        EXPECT_EQ(s.name, "s2");
        EXPECT_TRUE(s.isPublic);
        EXPECT_EQ(s.fields.size(), 1);

        EXPECT_EQ(s.fields[0].name, "x");
        auto fieldType = dynamic_cast<const ASTIntegerType *>(s.fields[0].type);
        EXPECT_NE(fieldType, nullptr);
        EXPECT_EQ(fieldType->bits, 32);
        EXPECT_TRUE(fieldType->isSigned);
    }

    const auto ok3 = "(struct s3 (x i32) (y f64) (z str))";
    expr = parseForm(ok3);
    res = sema.parseStructureDefinition(expr);
    EXPECT_TRUE(res);

    EXPECT_EQ(sema.module.structs.size(), 3);
    {
        auto &s = sema.module.structs[2];
        EXPECT_EQ(s.name, "s3");
        EXPECT_TRUE(s.isPublic);
        EXPECT_EQ(s.fields.size(), 3);

        {
            EXPECT_EQ(s.fields[0].name, "x");
            auto fieldType = dynamic_cast<const ASTIntegerType *>(s.fields[0].type);
            EXPECT_NE(fieldType, nullptr);
            EXPECT_EQ(fieldType->bits, 32);
            EXPECT_TRUE(fieldType->isSigned);
        }

        {
            EXPECT_EQ(s.fields[1].name, "y");
            auto fieldType = dynamic_cast<const ASTFloatType *>(s.fields[1].type);
            EXPECT_NE(fieldType, nullptr);
            EXPECT_EQ(fieldType->bits, 64);
        }

        {
            EXPECT_EQ(s.fields[2].name, "z");
            auto fieldType = dynamic_cast<const ASTNamedType *>(s.fields[2].type);
            EXPECT_NE(fieldType, nullptr);
            EXPECT_EQ(fieldType->name, "str");
        }
    }

}
