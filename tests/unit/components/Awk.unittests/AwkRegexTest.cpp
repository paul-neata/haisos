#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "commands/awk/AwkRegex.h"

using namespace Haisos;
using namespace Haisos::Awk;

namespace {

// One regex text translated, with the warnings it produced.
std::string Translated(const std::string& awkRegex, std::vector<std::string>& warnings) {
    return TranslateAwkRegex(awkRegex, warnings);
}

std::string Translated(const std::string& awkRegex) {
    std::vector<std::string> warnings;
    return TranslateAwkRegex(awkRegex, warnings);
}

std::vector<std::string> WarningsOf(const std::string& awkRegex) {
    std::vector<std::string> warnings;
    TranslateAwkRegex(awkRegex, warnings);
    return warnings;
}

// One regex compiled through the translation, as the interpreter compiles it.
std::shared_ptr<const Regex> Compiled(const std::string& awkRegex, std::string& error) {
    std::vector<std::string> warnings;
    return Regex::Compile(Translated(awkRegex, warnings), RegexOptions{RegexSyntax::Extended},
                          error);
}

std::vector<std::string> Split(const std::string& awkRegex, const std::string& text) {
    std::string error;
    std::shared_ptr<const Regex> regex = Compiled(awkRegex, error);
    std::vector<std::string> fields;
    if (regex) {
        SplitByRegex(text, *regex, fields);
    }
    return fields;
}

}  // namespace

TEST(AwkRegexTest, OutsideEscapes) {
    // \/ is a slash, and never warns.
    EXPECT_EQ(Translated("a\\/b"), "a/b");
    EXPECT_TRUE(WarningsOf("a\\/b").empty());

    // A metacharacter keeps its backslash: the engine matches the character.
    EXPECT_EQ(Translated("a\\.b"), "a\\.b");
    EXPECT_EQ(Translated("a\\[b"), "a\\[b");
    EXPECT_TRUE(WarningsOf("a\\.b").empty());

    // The control escapes mean one byte.
    EXPECT_EQ(Translated("a\\tb"), std::string("a\tb"));
    EXPECT_EQ(Translated("a\\nb"), std::string("a\nb"));

    // An octal escape's byte is written raw: the metacharacter it spells
    // (here 056, '.') stays one.
    EXPECT_EQ(Translated("\\056"), ".");
    EXPECT_TRUE(WarningsOf("\\056").empty());

    // An unknown escape is the plain character, with gawk's warning.
    EXPECT_EQ(Translated("a\\wb"), "awb");
    EXPECT_EQ(WarningsOf("a\\wb"),
              std::vector<std::string>{
                  "regexp escape sequence `\\w' is not a known regexp operator"});
    EXPECT_EQ(Translated("a\\\"b"), "a\"b");
    EXPECT_EQ(WarningsOf("a\\\"b"),
              std::vector<std::string>{
                  "regexp escape sequence `\\\"' is not a known regexp operator"});

    // A final lone backslash is kept: the engine reports it.
    EXPECT_EQ(Translated("a\\"), "a\\");

    // Intervals need no escape and stay whole.
    EXPECT_EQ(Translated("a{2}b"), "a{2}b");
    EXPECT_TRUE(WarningsOf("a{2}b").empty());
}

TEST(AwkRegexTest, BracketEscapes) {
    // A ']' member is re-emitted first, where the engine reads it as one.
    EXPECT_EQ(Translated("[a\\]b]"), "[]ab]");
    EXPECT_EQ(Translated("[]\\]]"), "[]]");

    // A '\' member is the byte itself.
    EXPECT_EQ(Translated("[\\\\]"), "[\\]");

    // A '-' member is re-emitted last, where the engine reads it as one.
    EXPECT_EQ(Translated("[a\\-z]"), "[az-]");

    // A '/' member needs no escape inside a bracket.
    EXPECT_EQ(Translated("[[:alpha:]\\/]"), "[[:alpha:]/]");

    // A '^' member is kept away from the first place, where the engine would
    // read it as the negation.
    EXPECT_EQ(Translated("[\\^a]"), "[a^]");
    EXPECT_EQ(Translated("[\\^]"), "[[.^.]]");

    // A range is kept a range, its endpoints collated when their bytes would
    // be bracket syntax; a reversed one is kept where it is, so the engine
    // refuses it as gawk does.
    EXPECT_EQ(Translated("[a-z]"), "[a-z]");
    EXPECT_EQ(Translated("[\\]-^]"), "[[.].]-[.^.]]");
    EXPECT_EQ(Translated("[z-a]"), "[z-a]");
    std::string error;
    EXPECT_EQ(Compiled("[z-a]", error), nullptr);
    EXPECT_NE(error.find("Invalid range end"), std::string::npos) << error;

    // A bracket that does not close is passed through for the engine's error.
    EXPECT_EQ(Translated("[ab"), "[ab");
    EXPECT_EQ(Compiled("[ab", error), nullptr);
    EXPECT_NE(error.find("Unmatched ["), std::string::npos) << error;
}

TEST(AwkRegexTest, AsWritten) {
    // Every '/' outside a bracket is shown escaped; one inside is bare.
    EXPECT_EQ(AwkRegexAsWritten("a/b"), "a\\/b");
    EXPECT_EQ(AwkRegexAsWritten("[a/b]c"), "[a/b]c");
    // A backslash pair is copied whole.
    EXPECT_EQ(AwkRegexAsWritten("a\\.b"), "a\\.b");
}

TEST(AwkRegexTest, SplitByRegexCutsBetweenMatches) {
    EXPECT_EQ(Split("[0-9]+", "a1b22c"), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(Split("[ ]", "a  b"), (std::vector<std::string>{"a", "", "b"}));

    // A match at the very start and at the very end give empty fields.
    EXPECT_EQ(Split(" +", " a b "), (std::vector<std::string>{"", "a", "b", ""}));

    // An empty match never separates.
    EXPECT_EQ(Split("x*", "abc"), (std::vector<std::string>{"abc"}));
    EXPECT_EQ(Split("x*", "axxbc"), (std::vector<std::string>{"a", "bc"}));

    // An empty text has no fields.
    EXPECT_EQ(Split("[0-9]+", ""), (std::vector<std::string>{}));
}

TEST(AwkRegexTest, CacheCompilesOnceAndKeeps) {
    AwkRegexCache cache;
    std::vector<std::string> warnings;
    std::string error;
    const std::shared_ptr<const Regex> first = cache.Get("a\\wb", warnings, error);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(warnings,
              std::vector<std::string>{
                  "regexp escape sequence `\\w' is not a known regexp operator"})
        << "the warnings come with the compile";

    warnings.clear();
    error.clear();
    const std::shared_ptr<const Regex> second = cache.Get("a\\wb", warnings, error);
    EXPECT_EQ(second, first);
    EXPECT_TRUE(warnings.empty()) << "a hit compiles nothing, so warns of nothing";

    // A text that does not compile is reported and not cached.
    warnings.clear();
    error.clear();
    EXPECT_EQ(cache.Get("[z-a]", warnings, error), nullptr);
    EXPECT_NE(error.find("Invalid range end"), std::string::npos) << error;
}