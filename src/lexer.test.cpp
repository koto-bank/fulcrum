#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "lexer.hpp"

namespace {
auto processString(const std::string& s) {
    Lexer l;
    std::istringstream is(s);
    auto res = l.lex(&is);
    return res;
}

void checkError(const std::unique_ptr<token::Token>& token) {
    if (token->type == token::Type::Error) {
        std::cerr << "Error: " << static_cast<token::Error*>(token.get())->what << "\n";
    }
}
}

TEST(lexer_int, signed_) {
    const std::string i = "123";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<int32_t>(), 123);
    EXPECT_EQ(r->isSigned, true);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_int, signed_sign) {
    const std::string i = "-123";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);

    checkError(res[0]);
    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<int32_t>(), -123);
    EXPECT_EQ(r->isSigned, true);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_int, signed_suffix) {
    const std::string i = "123i";

    Lexer l;
    std::istringstream is(i);
    auto res = l.lex(&is);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<int32_t>(), 123);
    EXPECT_EQ(r->isSigned, true);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_int, signed_bits_normal) {
    const std::string i = "123i8";

    Lexer l;
    std::istringstream is(i);
    auto res = l.lex(&is);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<int8_t>(), 123);
    EXPECT_EQ(r->isSigned, true);
    EXPECT_EQ(r->bits, 8u);
}

TEST(lexer_int, signed_bits_exceeded) {
    const std::string i = "123i129";

    Lexer l;
    std::istringstream is(i);
    auto res = l.lex(&is);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::Error);
    auto r = res[0]->as<token::Error>();
    ASSERT_NE(r, nullptr);
    ASSERT_NE(r->originalToken, nullptr);

    ASSERT_EQ(r->originalToken->type, token::Type::IntegerLiteral);
    auto original = r->originalToken->as<token::IntegerLiteral>();
    EXPECT_EQ(original->get<int64_t>(), 123);
    EXPECT_EQ(original->isSigned, true);
    EXPECT_EQ(original->bits, 129u);
}

TEST(lexer_int, unsigned_suffix) {
    const std::string i = "123u";

    auto res = processString(i);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<uint32_t>(), 123u);
    EXPECT_EQ(r->isSigned, false);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_int, unsigned_bits_normal) {
    const std::string i = "123u8";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<uint8_t>(), 123u);
    EXPECT_EQ(r->isSigned, false);
    EXPECT_EQ(r->bits, 8u);
}

TEST(lexer_int, unsigned_bits_value_overflow) {
    const std::string i = "123u129";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::Error);
    auto r = res[0]->as<token::Error>();
    ASSERT_NE(r, nullptr);
    ASSERT_NE(r->originalToken, nullptr);

    ASSERT_EQ(r->originalToken->type, token::Type::IntegerLiteral);
    auto original = dynamic_cast<token::IntegerLiteral*>(r->originalToken.get());
    EXPECT_EQ(original->get<uint64_t>(), 123);
    EXPECT_EQ(original->isSigned, false);
    EXPECT_EQ(original->bits, 129u);
}

TEST(lexer_int, hex_signed) {
    const std::string i = "0x7Fi8";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);

    checkError(res[0]);
    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<int8_t>(), 127);
    EXPECT_EQ(r->isSigned, true);
    EXPECT_EQ(r->bits, 8u);
}

TEST(lexer_int, hex_signed_overflow_literal) {
    const std::string i = "0xFFi8";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<int8_t>(), -1);
    EXPECT_EQ(r->isSigned, true);
    EXPECT_EQ(r->bits, 8u);
}

TEST(lexer_int, hex_unsigned) {
    const std::string i = "0xFFu8";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<uint8_t>(), 255);
    EXPECT_EQ(r->isSigned, false);
    EXPECT_EQ(r->bits, 8u);
}

// Floats

TEST(lexer_float, f64_default) {
    const std::string i = "123.34";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 123.34);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f64_suffix) {
    const std::string i = "123.34f64";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 123.34);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f64_suffix_no_period) {
    const std::string i = "123f64";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 123.0);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f64_hex) {
    const std::string i = "0xA1p0";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 161.0);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f64_scientific) {
    const std::string i = "123.34e-2";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 1.2334);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f64_scientific_suffix) {
    const std::string i = "123.34e-2f64";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 1.2334);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f64_hex_suffix) {
    const std::string i = "0xA1.B2p0f64";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<double>(), 0xA1.B2p0);
    EXPECT_EQ(r->bits, 64u);
}

TEST(lexer_float, f32_suffix) {
    const std::string i = "123.34f32";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<float>(), 123.34f);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_float, f32_suffix_no_period) {
    const std::string i = "123f32";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<float>(), 123.0f);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_float, f32_scientific) {
    const std::string i = "123.34e-2f32";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<float>(), 1.2334f);
    EXPECT_EQ(r->bits, 32u);
}

TEST(lexer_float, f32_hex) {
    const std::string i = "0xA1.B2p0f32";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::FloatLiteral);
    auto r = res[0]->as<token::FloatLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->get<float>(), 0xA1.B2p0f);
    EXPECT_EQ(r->bits, 32u);
}

// Ids and keywords
TEST(lexer_id, boolean_true) {
    const std::string i = "true";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::BooleanLiteral);
    auto r = res[0]->as<token::BooleanLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->value, true);
}

TEST(lexer_id, boolean_true_negative_case) {
    const std::string i = "True";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::Id);
    auto r = res[0]->as<token::Id>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->id, "True");
}

TEST(lexer_id, boolean_false) {
    const std::string i = "false";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::BooleanLiteral);
    auto r = res[0]->as<token::BooleanLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->value, false);
}

TEST(lexer_id, boolean_false_negative_case) {
    const std::string i = "False";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::Id);
    auto r = res[0]->as<token::Id>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->id, "False");
}

TEST(lexer_id, id_default) {
    const std::string i = "some-id";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::Id);
    auto r = res[0]->as<token::Id>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->id, "some-id");
}

TEST(lexer_id, id_non_ascii) {
    const std::string i = "識別名";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::Id);
    auto r = res[0]->as<token::Id>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->id, "識別名");
}

TEST(lexer_id, id_emoji) {
    const std::string i = "🤔";

    auto res = processString(i);
    ASSERT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::Id);
    auto r = res[0]->as<token::Id>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->id, "🤔");
}

// Strings
TEST(lexer_string, string_default) {
    const std::string i = "\"this is a string\"";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::StringLiteral);
    auto r = res[0]->as<token::StringLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->contents, "this is a string");
}

TEST(lexer_string, string_escape_newline) {
    const std::string i = "\"this is a line\\nbreak\"";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::StringLiteral);
    auto r = res[0]->as<token::StringLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->contents, "this is a line\nbreak");
}

TEST(lexer_string, string_escape_dquote) {
    const std::string i = "\"this is a \\\"quote\"";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::StringLiteral);
    auto r = res[0]->as<token::StringLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->contents, "this is a \"quote");
}

TEST(lexer_string, string_with_parens) {
    const std::string i = "\"(this) () is ((a ))) string))((with parens!\"";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::StringLiteral);
    auto r = res[0]->as<token::StringLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->contents, "(this) () is ((a ))) string))((with parens!");
}

TEST(lexer_string, string_with_parens_and_semicolons) {
    const std::string i = "\"(this; () is ;;;a  string))((;;with parens and semicolons!\"";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 1);
    checkError(res[0]);

    ASSERT_EQ(res[0]->type, token::Type::StringLiteral);
    auto r = res[0]->as<token::StringLiteral>();
    ASSERT_NE(r, nullptr);

    EXPECT_EQ(r->contents, "(this; () is ;;;a  string))((;;with parens and semicolons!");
}

// Test compounds

TEST(lexer_compounds, comment) {
    const std::string i = "; after comment all is ignored\"(this; () is ;;;a  string))((;;with parens and semicolons!\"";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 0);
}

TEST(lexer_compounds, comment_and_string) {
    const std::string i = "123; comment ends when line ends 123\n\"String on new line\"";
    auto res = processString(i);
    ASSERT_EQ(res.size(), 2);

    ASSERT_EQ(res[0]->type, token::Type::IntegerLiteral);
    ASSERT_EQ(res[1]->type, token::Type::StringLiteral);

    auto r = res[0]->as<token::IntegerLiteral>();
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->get<int32_t>(), 123);

    auto s = res[1]->as<token::StringLiteral>();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->contents, "String on new line");
}

TEST(lexer_compounds, simple_1) {
    const std::string i = "()";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 2);

    EXPECT_EQ(res[0]->type, token::Type::LParen);
    EXPECT_EQ(res[1]->type, token::Type::RParen);
 }

TEST(lexer_compounds, simple_2) {
    const std::string i = "(+ 123u8 345u16)";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 5);

    EXPECT_EQ(res[0]->type, token::Type::LParen);

    EXPECT_EQ(res[1]->type, token::Type::Id);
    EXPECT_EQ(res[1]->as<token::Id>()->id, "+");

    EXPECT_EQ(res[2]->type, token::Type::IntegerLiteral);
    EXPECT_EQ(res[2]->as<token::IntegerLiteral>()->get<uint8_t>(), 123u);
    EXPECT_EQ(res[2]->as<token::IntegerLiteral>()->bits, 8u);

    EXPECT_EQ(res[3]->type, token::Type::IntegerLiteral);
    EXPECT_EQ(res[3]->as<token::IntegerLiteral>()->get<uint16_t>(), 345u);
    EXPECT_EQ(res[3]->as<token::IntegerLiteral>()->bits, 16u);

    EXPECT_EQ(res[4]->type, token::Type::RParen);
}

TEST(lexer_compounds, compound_1) {
    const std::string i = "(format true \"{} {}\\n\" \"The result:\" (* a b c 0x12u8)) ; printing";

    auto res = processString(i);
    EXPECT_EQ(res.size(), 13);

    EXPECT_EQ(res[0]->type, token::Type::LParen);

    EXPECT_EQ(res[1]->type, token::Type::Id);
    EXPECT_EQ(res[1]->as<token::Id>()->id, "format");

    EXPECT_EQ(res[2]->type, token::Type::BooleanLiteral);
    EXPECT_EQ(res[2]->as<token::BooleanLiteral>()->value, true);

    EXPECT_EQ(res[3]->type, token::Type::StringLiteral);
    EXPECT_EQ(res[3]->as<token::StringLiteral>()->contents, "{} {}\n");

    EXPECT_EQ(res[4]->type, token::Type::StringLiteral);
    EXPECT_EQ(res[4]->as<token::StringLiteral>()->contents, "The result:");

    EXPECT_EQ(res[5]->type, token::Type::LParen);

    EXPECT_EQ(res[6]->type, token::Type::Id);
    EXPECT_EQ(res[6]->as<token::Id>()->id, "*");

    EXPECT_EQ(res[7]->type, token::Type::Id);
    EXPECT_EQ(res[7]->as<token::Id>()->id, "a");

    EXPECT_EQ(res[8]->type, token::Type::Id);
    EXPECT_EQ(res[8]->as<token::Id>()->id, "b");

    EXPECT_EQ(res[9]->type, token::Type::Id);
    EXPECT_EQ(res[9]->as<token::Id>()->id, "c");

    EXPECT_EQ(res[10]->type, token::Type::IntegerLiteral);
    EXPECT_EQ(res[10]->as<token::IntegerLiteral>()->get<uint8_t>(), 0x12u);
    EXPECT_EQ(res[10]->as<token::IntegerLiteral>()->bits, 8u);

    EXPECT_EQ(res[11]->type, token::Type::RParen);
    EXPECT_EQ(res[12]->type, token::Type::RParen);
}
