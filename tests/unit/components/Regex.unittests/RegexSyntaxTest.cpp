#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Regex.h"
#include "RegexParser.h"

using Haisos::DumpRegexTree;
using Haisos::ParseRegex;
using Haisos::Regex;
using Haisos::RegexOptions;
using Haisos::RegexSyntax;
using Haisos::RegexTree;

namespace {

std::string Dump(std::string_view pattern, const RegexOptions& options) {
    RegexTree tree;
    std::string error;
    if (!ParseRegex(pattern, options, tree, error)) return "error: " + error;
    return DumpRegexTree(tree);
}

struct TreeCase {
    RegexSyntax syntax;
    const char* pattern;
    const char* expected;
};

void RunTreeCases(const TreeCase* cases, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        SCOPED_TRACE(cases[i].pattern);
        RegexOptions options;
        options.syntax = cases[i].syntax;
        EXPECT_EQ(Dump(cases[i].pattern, options), cases[i].expected);
    }
}

struct ErrorCase {
    RegexSyntax syntax;
    const char* pattern;
    const char* expected;
};

void RunErrorCases(const ErrorCase* cases, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        SCOPED_TRACE(cases[i].pattern);
        RegexOptions options;
        options.syntax = cases[i].syntax;
        std::string error;
        std::shared_ptr<const Regex> compiled = Regex::Compile(cases[i].pattern, options, error);
        EXPECT_EQ(compiled, nullptr);
        EXPECT_EQ(error, cases[i].expected);
    }
}

} // namespace

TEST(RegexSyntaxBasicTest, Trees) {
    const TreeCase cases[] = {
        {RegexSyntax::Basic, "abc", "(cat 'a' 'b' 'c')"},
        {RegexSyntax::Basic, "a*", "(rep 0 inf 'a')"},
        {RegexSyntax::Basic, "*a", "(cat '*' 'a')"},
        {RegexSyntax::Basic, "\\(*a\\)", "(group 1 (cat '*' 'a'))"},
        {RegexSyntax::Basic, "^*", "(cat (bol) '*')"},
        {RegexSyntax::Basic, "a^b$c", "(cat 'a' '^' 'b' '$' 'c')"},
        {RegexSyntax::Basic, "^a$", "(cat (bol) 'a' (eol))"},
        {RegexSyntax::Basic, "\\(^a$\\)", "(group 1 (cat (bol) 'a' (eol)))"},
        {RegexSyntax::Basic, "a\\|^b", "(alt 'a' (cat (bol) 'b'))"},
        {RegexSyntax::Basic, "a\\{2,3\\}", "(rep 2 3 'a')"},
        {RegexSyntax::Basic, "a\\{,2\\}", "(rep 0 2 'a')"},
        {RegexSyntax::Basic, "a\\{2,\\}", "(rep 2 inf 'a')"},
        {RegexSyntax::Basic, "a\\{2\\}", "(rep 2 2 'a')"},
        {RegexSyntax::Basic, "a\\+\\?", "(rep 0 1 (rep 1 inf 'a'))"},
        {RegexSyntax::Basic, "a+?(){}|", "(cat 'a' '+' '?' '(' ')' '{' '}' '|')"},
        {RegexSyntax::Basic, "\\(a\\)\\(b\\)\\2\\1", "(cat (group 1 'a') (group 2 'b') (backref 2) (backref 1))"},
        {RegexSyntax::Basic, "\\w\\W\\s\\S",
         "(cat [0-9A-Z_a-z] [\\x00-/:-@\\x5b-^`{-\\xff] [\\x09-\\x0d\\x20] "
         "[\\x00-\\x08\\x0e-\\x1f!-\\xff])"},
        {RegexSyntax::Basic, "\\b\\B\\<\\>\\`\\'",
         "(cat (wordb) (notwordb) (wordstart) (wordend) (begbuf) (endbuf))"},
        {RegexSyntax::Basic, ".", "[\\x00-\\xff]"},
        {RegexSyntax::Basic, "[]a]", "[\\x5da]"},
        {RegexSyntax::Basic, "[^]a]", "[\\x00-\\x5c^-`b-\\xff]"},
        {RegexSyntax::Basic, "[a-]", "[\\x2da]"},
        {RegexSyntax::Basic, "[\\n]", "[\\x5cn]"},
        {RegexSyntax::Basic, "[[:digit:]x]", "[0-9x]"},
        {RegexSyntax::Basic, "[[.-.]a]", "[\\x2da]"},
        {RegexSyntax::Basic, "[[=a=]]", "'a'"},
        {RegexSyntax::Basic, "\\n\\.", "(cat 'n' '.')"},
        {RegexSyntax::Basic, "\\0", "'0'"},
        {RegexSyntax::Basic, "\\(\\)", "(group 1 (empty))"},
    };
    RunTreeCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexSyntaxBasicTest, TreesIgnoreCase) {
    const TreeCase cases[] = {
        {RegexSyntax::Basic, "a", "[Aa]"},
        {RegexSyntax::Basic, "[a-c]", "[A-Ca-c]"},
        {RegexSyntax::Basic, "\\(a\\)\\1", "(cat (group 1 [Aa]) (backref-i 1))"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        SCOPED_TRACE(cases[i].pattern);
        RegexOptions options;
        options.syntax = cases[i].syntax;
        options.ignoreCase = true;
        EXPECT_EQ(Dump(cases[i].pattern, options), cases[i].expected);
    }
}

TEST(RegexSyntaxExtendedTest, Trees) {
    const TreeCase cases[] = {
        {RegexSyntax::Extended, "a|b|", "(alt 'a' 'b' (empty))"},
        {RegexSyntax::Extended, "(?:b)", "error: Invalid preceding regular expression"},
        {RegexSyntax::Extended, "a+?", "(rep 0 1 (rep 1 inf 'a'))"},
        {RegexSyntax::Extended, "a{2}b{1,}", "(cat (rep 2 2 'a') (rep 1 inf 'b'))"},
        {RegexSyntax::Extended, "a^b", "(cat 'a' (bol) 'b')"},
        {RegexSyntax::Extended, "\\(\\|\\{", "(cat '(' '|' '{')"},
        {RegexSyntax::Extended, "()", "(group 1 (empty))"},
        {RegexSyntax::Extended, "(a)\\1", "(cat (group 1 'a') (backref 1))"},
    };
    RunTreeCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexSyntaxPerlTest, Trees) {
    const TreeCase cases[] = {
        {RegexSyntax::Perl, "a+?b*?c??", "(cat (lazy 1 inf 'a') (lazy 0 inf 'b') (lazy 0 1 'c'))"},
        {RegexSyntax::Perl, "(?:ab)+", "(rep 1 inf (cat 'a' 'b'))"},
        {RegexSyntax::Perl, "(?<y>\\d{4})-(\\d\\d)",
         "(cat (group 1 (rep 4 4 [0-9])) '-' (group 2 (cat [0-9] [0-9])))"},
        {RegexSyntax::Perl, "a(?i)b", "(cat 'a' [Bb])"},
        {RegexSyntax::Perl, "(?i:a)b", "(cat [Aa] 'b')"},
        {RegexSyntax::Perl, "(?i)a(?-i)b", "(cat [Aa] 'b')"},
        {RegexSyntax::Perl, ".", "[\\x00-\\x09\\x0b-\\xff]"},
        {RegexSyntax::Perl, "(?s).", "[\\x00-\\xff]"},
        {RegexSyntax::Perl, "^a$", "(cat (bol) 'a' (eol-perl))"},
        {RegexSyntax::Perl, "(?m)^a$", "(cat (bol-m) 'a' (eol-m))"},
        {RegexSyntax::Perl, "\\Aa\\z\\Z", "(cat (begbuf) 'a' (endbuf) (endbuf-nl))"},
        {RegexSyntax::Perl, "a{", "(cat 'a' '{')"},
        {RegexSyntax::Perl, "a{x}", "(cat 'a' '{' 'x' '}')"},
        {RegexSyntax::Perl, "a{,3}", "(cat 'a' '{' ',' '3' '}')"},
        {RegexSyntax::Perl, "[\\]\\\\\\-a]", "[\\x2d\\x5c\\x5da]"},
        {RegexSyntax::Perl, "[[:^digit:]]", "[\\x00-/:-\\xff]"},
        {RegexSyntax::Perl, "\\x41\\x{42}\\cA\\t\\e", "(cat 'A' 'B' \\x01 \\x09 \\x1b)"},
        {RegexSyntax::Perl, "(a)\\1", "(cat (group 1 'a') (backref 1))"},
        {RegexSyntax::Perl, "\\.\\$", "(cat '.' '$')"},
    };
    RunTreeCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexSyntaxErrorTest, GnuMessages) {
    std::string deeplyNested;
    for (int i = 0; i < 1001; ++i) deeplyNested += "\\(";
    deeplyNested += 'a';
    for (int i = 0; i < 1001; ++i) deeplyNested += "\\)";

    const ErrorCase cases[] = {
        {RegexSyntax::Basic, "a[b", "Unmatched [, [^, [:, [., or [="},
        {RegexSyntax::Basic, "[[:foo:]]", "Invalid character class name"},
        {RegexSyntax::Basic, "[z-a]", "Invalid range end"},
        {RegexSyntax::Basic, "[[.ab.]]", "Invalid collation character"},
        {RegexSyntax::Basic, "\\(a", "Unmatched ( or \\("},
        {RegexSyntax::Basic, "a\\)", "Unmatched ) or \\)"},
        {RegexSyntax::Basic, "a\\{1", "Unmatched \\{"},
        {RegexSyntax::Basic, "a\\{x\\}", "Invalid content of \\{\\}"},
        {RegexSyntax::Basic, "a\\{2,1\\}", "Invalid content of \\{\\}"},
        {RegexSyntax::Basic, "a\\{32768\\}", "Regular expression too big"},
        {RegexSyntax::Basic, "\\{1\\}a", "Invalid preceding regular expression"},
        {RegexSyntax::Basic, "\\1", "Invalid back reference"},
        {RegexSyntax::Basic, "\\(a\\1\\)", "Invalid back reference"},
        {RegexSyntax::Basic, "a\\", "Trailing backslash"},
        {RegexSyntax::Extended, "*a", "Invalid preceding regular expression"},
        {RegexSyntax::Extended, "a|*b", "Invalid preceding regular expression"},
        {RegexSyntax::Extended, "(a", "Unmatched ( or \\("},
        {RegexSyntax::Extended, "a)", "Unmatched ) or \\)"},
        {RegexSyntax::Extended, "a{1", "Unmatched \\{"},
    };
    RunErrorCases(cases, sizeof(cases) / sizeof(cases[0]));
    // The deeply nested pattern: built, not a literal.
    {
        RegexOptions options;
        options.syntax = RegexSyntax::Basic;
        std::string error;
        EXPECT_EQ(Regex::Compile(deeplyNested, options, error), nullptr);
        EXPECT_EQ(error, "Regular expression too big");
    }
}

TEST(RegexSyntaxErrorTest, PerlMessages) {
    const ErrorCase cases[] = {
        {RegexSyntax::Perl, "(", "missing closing parenthesis"},
        {RegexSyntax::Perl, ")", "unmatched closing parenthesis"},
        {RegexSyntax::Perl, "[a", "missing terminating ] for character class"},
        {RegexSyntax::Perl, "*a", "quantifier does not follow a repeatable item"},
        {RegexSyntax::Perl, "[z-a]", "range out of order in character class"},
        {RegexSyntax::Perl, "a{3,2}", "numbers out of order in {} quantifier"},
        {RegexSyntax::Perl, "a{70000}", "number too big in {} quantifier"},
        {RegexSyntax::Perl, "\\", "\\ at end of pattern"},
        {RegexSyntax::Perl, "\\i", "unrecognized character follows \\"},
        {RegexSyntax::Perl, "(a)\\2", "reference to non-existent subpattern"},
        {RegexSyntax::Perl, "[[:foo:]]", "unknown POSIX class name"},
        {RegexSyntax::Perl, "(?=a)", "lookaround assertions are not supported"},
        {RegexSyntax::Perl, "a++", "possessive quantifiers are not supported"},
        {RegexSyntax::Perl, "\\p{L}", "\\p is not supported"},
        {RegexSyntax::Perl, "(?x)a", "unrecognized character after (? or (?-"},
    };
    RunErrorCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexSyntaxTest, GroupCountAndNames) {
    {
        RegexOptions options;
        options.syntax = RegexSyntax::Basic;
        std::string error;
        std::shared_ptr<const Regex> regex = Regex::Compile("\\(a\\)\\(b\\(c\\)\\)", options, error);
        ASSERT_NE(regex, nullptr);
        EXPECT_EQ(regex->GroupCount(), 3u);
        EXPECT_EQ(regex->GroupNames().size(), regex->GroupCount() + 1);
        for (const std::string& name : regex->GroupNames()) EXPECT_TRUE(name.empty());
        EXPECT_EQ(regex->Options().syntax, RegexSyntax::Basic);
        EXPECT_EQ(regex->Options().ignoreCase, false);
        EXPECT_EQ(regex->Options().multiline, false);
    }
    {
        RegexOptions options;
        options.syntax = RegexSyntax::Perl;
        std::string error;
        std::shared_ptr<const Regex> regex = Regex::Compile("(?:a)(b)", options, error);
        ASSERT_NE(regex, nullptr);
        EXPECT_EQ(regex->GroupCount(), 1u);
    }
    {
        RegexOptions options;
        options.syntax = RegexSyntax::Perl;
        options.ignoreCase = true;
        options.multiline = true;
        std::string error;
        std::shared_ptr<const Regex> regex = Regex::Compile("(?<y>\\d{4})-(\\d\\d)", options, error);
        ASSERT_NE(regex, nullptr);
        ASSERT_EQ(regex->GroupNames().size(), 3u);
        EXPECT_EQ(regex->GroupNames()[0], "");
        EXPECT_EQ(regex->GroupNames()[1], "y");
        EXPECT_EQ(regex->GroupNames()[2], "");
        EXPECT_EQ(regex->Options().ignoreCase, true);
        EXPECT_EQ(regex->Options().multiline, true);
    }
}

TEST(RegexSyntaxTest, LongPatternsDoNotRecurse) {
    RegexOptions options;
    options.syntax = RegexSyntax::Extended;

    // 200000 characters: one flat Concat, no chain 200000 deep.
    std::string characters(200000, 'a');
    std::string dumped = Dump(characters, options);
    EXPECT_EQ(dumped.substr(0, 10), "(cat 'a' '");
    EXPECT_EQ(dumped.substr(dumped.size() - 3), "a')");

    // 200000 alternatives: one flat Alternate.
    std::string alternatives;
    for (size_t i = 0; i < 200000; ++i) {
        if (i > 0) alternatives += '|';
        alternatives += 'a';
    }
    dumped = Dump(alternatives, options);
    EXPECT_EQ(dumped.substr(0, 8), "(alt 'a'");
    EXPECT_EQ(dumped.substr(dumped.size() - 4), "'a')");
}