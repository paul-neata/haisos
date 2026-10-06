#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "commands/hsh/HshPattern.h"

using namespace Haisos::Hsh;

namespace {

TEST(HshPatternTest, Matches) {
    struct Row { std::string pattern; std::string text; bool expected; };
    const std::vector<Row> rows = {
        {"*", "", true}, {"*", "abc", true},
        {"a*", "a", true}, {"a*", "abc", true}, {"a*", "bac", false},
        {"*b", "ab", true}, {"*b", "a", false},
        {"a*b*c", "aXbYc", true}, {"a*b*c", "abc", true},
        {"a*b*c", "aXbc", true}, {"a*b*c", "aXbY", false},
        {"?", "x", true}, {"?", "", false}, {"?", "xy", false},
        {"a?c", "abc", true}, {"a?c", "ac", false},
        {"[abc]", "b", true}, {"[abc]", "d", false},
        {"[!a]", "b", true}, {"[!a]", "a", false},
        {"[^a]", "^", true}, {"[^a]", "a", true}, {"[^a]", "b", false},
        {"[a-c]", "b", true}, {"[a-c]", "a", true}, {"[a-c]", "d", false},
        {"[]a]", "]", true}, {"[]a]", "a", true}, {"[]a]", "b", false},
        {"[!]a]", "b", true}, {"[!]a]", "]", false}, {"[!]a]", "a", false},
        {"[a-]", "-", true}, {"[a-]", "a", true}, {"[a-]", "b", false},
        {"[-a]", "-", true}, {"[-a]", "a", true}, {"[-a]", "b", false},
        {"[[:upper:]]", "A", true}, {"[[:upper:]]", "a", false},
        {"[[:space:]]", " ", true}, {"[[:space:]]", "\t", true}, {"[[:space:]]", "x", false},
        {"[[:digit:][:alpha:]]", "5", true}, {"[[:digit:][:alpha:]]", "z", true},
        {"[[:digit:][:alpha:]]", "!", false},
        {"[[:nope:]]", "a", false}, {"[[:nope:]]", "n", false},
        {"\\*", "*", true}, {"\\*", "x", false}, {"\\*", "", false},
        {"a\\*b", "a*b", true}, {"a\\*b", "axb", false},
        {"[a", "[a", true}, {"[a", "a", false}, {"[", "[", true}, {"[", "a", false},
        {"[!]", "[!]", true}, {"[!]", "!", false}, {"[!]", "[a]", false},
        {"", "", true}, {"", "a", false}, {"a", "", false},
        {"??", "\xc3\xa9", true}, {"?", "\xc3\xa9", false},
    };
    for (const Row& row : rows)
        EXPECT_EQ(MatchPattern(row.pattern, row.text), row.expected)
            << "pattern: " << row.pattern << " text: " << row.text;
}

TEST(HshPatternTest, HasPatternCharacters) {
    struct Row { std::string pattern; bool expected; };
    const std::vector<Row> rows = {
        {"a", false}, {"a*", true}, {"a\\*", false}, {"[a", false},
        {"[a]", true}, {"?", true}, {"a?", true}, {"[a]*", true}, {"[!]x", false},
    };
    for (const Row& row : rows)
        EXPECT_EQ(HasPatternCharacters(row.pattern), row.expected) << "pattern: " << row.pattern;
}

TEST(HshPatternTest, UnescapeAndEscape) {
    EXPECT_EQ(UnescapePattern("a\\*"), "a*");
    EXPECT_EQ(UnescapePattern("\\\\"), "\\");
    EXPECT_EQ(UnescapePattern("a"), "a");
    EXPECT_EQ(UnescapePattern("a\\"), "a\\"); // a trailing lone backslash keeps itself
    EXPECT_EQ(EscapeForPattern("abc"), "abc");
    EXPECT_EQ(EscapeForPattern("a*?[\\x"), "a\\*\\?\\[\\\\x");
    for (const std::string& text : {"plain", "a*b?c[d]", "\\", "*", "with space"}) {
        EXPECT_EQ(UnescapePattern(EscapeForPattern(text)), text) << "text: " << text;
        EXPECT_FALSE(HasPatternCharacters(EscapeForPattern(text))) << "text: " << text;
        EXPECT_TRUE(MatchPattern(EscapeForPattern(text), text)) << "text: " << text;
    }
}

TEST(HshPatternTest, RemovePattern) {
    struct Row {
        std::string value; std::string pattern; PatternRemoval which; std::string expected;
    };
    const std::vector<Row> rows = {
        {"hello", "l*", PatternRemoval::SmallestSuffix, "hel"},
        {"hello", "l*", PatternRemoval::LargestSuffix, "he"},
        {"hello", "*l", PatternRemoval::SmallestPrefix, "lo"},
        {"hello", "*l", PatternRemoval::LargestPrefix, "o"},
        {"a.b.c", ".*", PatternRemoval::SmallestSuffix, "a.b"},
        {"a.b.c", ".*", PatternRemoval::LargestSuffix, "a"},
        {"a.b.c", "*.", PatternRemoval::SmallestPrefix, "b.c"},
        {"a.b.c", "*.", PatternRemoval::LargestPrefix, "c"},
        {"aaa", "a*", PatternRemoval::SmallestPrefix, "aa"},
        {"aaa", "a*", PatternRemoval::LargestPrefix, ""},
        {"aaa", "a*", PatternRemoval::SmallestSuffix, "aa"},
        {"aaa", "a*", PatternRemoval::LargestSuffix, ""},
        {"/a/b", "*/", PatternRemoval::LargestPrefix, "b"},
        {"/a/b", "*/", PatternRemoval::SmallestPrefix, "a/b"},
        {"hello", "x*", PatternRemoval::SmallestSuffix, "hello"},
        {"hello", "x*", PatternRemoval::LargestPrefix, "hello"},
        {"hello", "", PatternRemoval::SmallestPrefix, "hello"},
        {"hello", "", PatternRemoval::LargestSuffix, "hello"},
    };
    for (const Row& row : rows)
        EXPECT_EQ(RemovePattern(row.value, row.pattern, row.which), row.expected)
            << "value: " << row.value << " pattern: " << row.pattern;
}

} // namespace
