#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "commands/jq/JqLexer.h"

namespace {

using Haisos::Jq::Lexer;
using Haisos::Jq::Token;
using Haisos::Jq::TokenType;

std::vector<Token> AllTokens(const std::string& text) {
    Lexer lexer(text);
    std::vector<Token> tokens;
    for (;;) {
        Token token = lexer.Next();
        tokens.push_back(token);
        if (token.type == TokenType::End)
            return tokens;
    }
}

// One expected token: its type, its text and its begin.
struct Expected {
    TokenType type;
    const char* text;
    size_t begin;
};

void CheckTokens(const std::string& text, const std::vector<Expected>& expected) {
    auto tokens = AllTokens(text);
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(tokens[i].type, expected[i].type) << "token " << i;
        EXPECT_EQ(tokens[i].text, expected[i].text) << "token " << i;
        EXPECT_EQ(tokens[i].begin, expected[i].begin) << "token " << i;
    }
}

} // namespace

TEST(JqLexerTest, TokensOfAProgram) {
    CheckTokens(".a | .[\"b\"][] as $x | @base64 \"v\\($x)\"",
                {
                    {TokenType::Field, "a", 0},
                    {TokenType::Char, "|", 3},
                    {TokenType::Char, ".", 5},
                    {TokenType::Char, "[", 6},
                    {TokenType::StringStart, "\"", 7},
                    {TokenType::StringText, "b", 8},
                    {TokenType::StringEnd, "\"", 9},
                    {TokenType::Char, "]", 10},
                    {TokenType::Char, "[", 11},
                    {TokenType::Char, "]", 12},
                    {TokenType::Keyword, "as", 14},
                    {TokenType::Variable, "x", 17},
                    {TokenType::Char, "|", 20},
                    {TokenType::Format, "base64", 22},
                    {TokenType::StringStart, "\"", 30},
                    {TokenType::StringText, "v", 31},
                    {TokenType::InterpolationStart, "\\(", 32},
                    {TokenType::Variable, "x", 34},
                    {TokenType::InterpolationEnd, ")", 36},
                    {TokenType::StringEnd, "\"", 37},
                    {TokenType::End, "", 38},
                });
}

TEST(JqLexerTest, NumbersKeepTheirText) {
    CheckTokens("1.50", {{TokenType::Number, "1.50", 0}, {TokenType::End, "", 4}});
    CheckTokens(".5", {{TokenType::Number, ".5", 0}, {TokenType::End, "", 2}});
    CheckTokens("1e2", {{TokenType::Number, "1e2", 0}, {TokenType::End, "", 3}});
    CheckTokens("1E-7",
                {{TokenType::Number, "1E-7", 0}, {TokenType::End, "", 4}});
    // A '.' before a name is a field, digits included in the name.
    CheckTokens(".a5", {{TokenType::Field, "a5", 0}, {TokenType::End, "", 3}});
    CheckTokens(".if", {{TokenType::Field, "if", 0}, {TokenType::End, "", 3}});
}

TEST(JqLexerTest, OperatorsLongestFirst) {
    CheckTokens("?// //= // |= != == <= >= ..",
                {
                    {TokenType::Operator, "?//", 0},
                    {TokenType::Operator, "//=", 4},
                    {TokenType::Operator, "//", 8},
                    {TokenType::Operator, "|=", 11},
                    {TokenType::Operator, "!=", 14},
                    {TokenType::Operator, "==", 17},
                    {TokenType::Operator, "<=", 20},
                    {TokenType::Operator, ">=", 23},
                    {TokenType::Operator, "..", 26},
                    {TokenType::End, "", 28},
                });
}

TEST(JqLexerTest, UnmatchedCloserIsInvalid) {
    auto tokens = AllTokens(")");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::Invalid);
    EXPECT_EQ(tokens[0].text, ")");
    // An unmatched closer carries no lexer error of its own.
    Lexer lexer(")");
    lexer.Next();
    EXPECT_TRUE(lexer.Error().empty());

    tokens = AllTokens("[}");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::Char);
    EXPECT_EQ(tokens[1].type, TokenType::Invalid);
    EXPECT_EQ(tokens[1].text, "}");
}

TEST(JqLexerTest, StringEscapes) {
    // tab, é, U+1F600 (four bytes), backslash, slash.
    auto tokens = AllTokens("\"\\t\\u00e9\\ud83d\\ude00\\\\\\/\"");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[1].type, TokenType::StringText);
    EXPECT_EQ(tokens[1].text,
              std::string("\t\xC3\xA9\xF0\x9F\x98\x80", 7) + "\\" + "/");

    // A low surrogate on its own becomes U+FFFD, not an error.
    tokens = AllTokens("\"\\ude00\"");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[1].type, TokenType::StringText);
    EXPECT_EQ(tokens[1].text, std::string("\xEF\xBF\xBD", 3));

    // Every bad-escape kind gives its message and an Invalid at the run's
    // first '\'.
    struct EscapeCase {
        const char* program;
        const char* message;
        size_t invalidBegin;
    };
    const EscapeCase cases[] = {
        {"\"\\q\"",
         "Invalid escape at line 1, column 4 (while parsing '\"\\q\"')", 1},
        {"\"\\ud83d\\ude00\\q\"",
         "Invalid escape at line 1, column 16 (while parsing "
         "'\"\\ud83d\\ude00\\q\"')",
         1},
        {"\"\\u12\"",
         "Invalid \\uXXXX escape at line 1, column 6 (while parsing "
         "'\"\\u12\"')",
         1},
        {"\"\\uzz\"",
         "Invalid \\uXXXX escape at line 1, column 6 (while parsing "
         "'\"\\uzz\"')",
         1},
        {"\"\\u12\\(1)\"",
         "Invalid \\uXXXX escape at line 1, column 6 (while parsing "
         "'\"\\u12\"')",
         1},
        {"\"\\u12zz\"",
         "Invalid characters in \\uXXXX escape at line 1, column 8 "
         "(while parsing '\"\\u12zz\"')",
         1},
        {"\"\\ud800x\"",
         "Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 8 "
         "(while parsing '\"\\ud800\"')",
         1},
        {"\"\\ud83d\\u0041\"",
         "Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 14 "
         "(while parsing '\"\\ud83d\\u0041\"')",
         1},
    };
    for (const auto& one : cases) {
        Lexer lexer(one.program);
        Token token = lexer.Next();  // the StringStart
        ASSERT_EQ(token.type, TokenType::StringStart) << one.program;
        token = lexer.Next();
        EXPECT_EQ(token.type, TokenType::Invalid) << one.program;
        EXPECT_EQ(token.begin, one.invalidBegin) << one.program;
        EXPECT_EQ(lexer.Error(), one.message) << one.program;
    }
}

TEST(JqLexerTest, Comments) {
    // A comment ends at its line's end; the program continues after it.
    CheckTokens("1 # x \n+ 1",
                {
                    {TokenType::Number, "1", 0},
                    {TokenType::Char, "+", 7},
                    {TokenType::Number, "1", 9},
                    {TokenType::End, "", 10},
                });
}