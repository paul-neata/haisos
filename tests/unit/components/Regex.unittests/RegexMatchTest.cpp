#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "Regex.h"

using Haisos::kRegexNotBol;
using Haisos::kRegexNotEol;
using Haisos::Regex;
using Haisos::RegexMatch;
using Haisos::RegexOptions;
using Haisos::RegexSyntax;

namespace {

// Compiles |pattern| and searches |text| from |start|. Returns "error: ...",
// "nomatch", or all GroupCount() + 1 groups as "(a,b)(c,d)...".
std::string Find(RegexSyntax syntax, std::string_view pattern, std::string_view text,
                 size_t start = 0, int flags = 0, bool ignoreCase = false, bool multiline = false) {
    RegexOptions options;
    options.syntax = syntax;
    options.ignoreCase = ignoreCase;
    options.multiline = multiline;
    std::string error;
    std::shared_ptr<const Regex> compiled = Regex::Compile(pattern, options, error);
    if (!compiled) return "error: " + error;
    RegexMatch match;
    if (!compiled->Search(text, start, match, flags)) return "nomatch";
    std::string out;
    for (const auto& group : match.groups)
        out += "(" + std::to_string(group.first) + "," + std::to_string(group.second) + ")";
    return out;
}

struct MatchCase {
    RegexSyntax syntax;
    const char* pattern;
    std::string text;  // may hold real newlines; empty for the empty text
    size_t start = 0;
    int flags = 0;
    bool ignoreCase = false;
    bool multiline = false;
    const char* expected;  // "nomatch", "error: ..." or "(a,b)..."
};

std::string Text(std::string_view text) { return std::string(text); }

void RunMatchCases(const char* suite, const MatchCase* cases, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        SCOPED_TRACE(std::string(suite) + " case " + std::to_string(i) + ": pattern \"" +
                     cases[i].pattern + "\" text \"" + cases[i].text + "\"");
        EXPECT_EQ(Find(cases[i].syntax, cases[i].pattern, cases[i].text, cases[i].start,
                       cases[i].flags, cases[i].ignoreCase, cases[i].multiline),
                  cases[i].expected);
    }
}

} // namespace

TEST(RegexMatchTest, GnuLeftmostLongest) {
    const MatchCase cases[] = {
        {RegexSyntax::Basic, "abc", Text("xabcx"), 0, 0, false, false, "(1,4)"},
        {RegexSyntax::Basic, "a*", Text("baaa"), 0, 0, false, false, "(0,0)"},
        {RegexSyntax::Basic, "a\\{2,3\\}", Text("aaaa"), 0, 0, false, false, "(0,3)"},
        {RegexSyntax::Basic, "\\(a*\\)\\(b*\\)", Text("aabbb"), 0, 0, false, false, "(0,5)(0,2)(2,5)"},
        {RegexSyntax::Basic, "\\(a\\)*", Text("aaa"), 0, 0, false, false, "(0,3)(2,3)"},
        {RegexSyntax::Basic, "x*", Text("aaxx"), 2, 0, false, false, "(2,4)"},
        {RegexSyntax::Basic, "a\\|b", Text("xb"), 0, 0, false, false, "(1,2)"},
        {RegexSyntax::Extended, "a|ab", Text("ab"), 0, 0, false, false, "(0,2)"},
        {RegexSyntax::Extended, "(wee|week)(knights|night)", Text("weeknights"), 0, 0, false, false,
         "(0,10)(0,3)(3,10)"},
        {RegexSyntax::Extended, "(a)|b", Text("b"), 0, 0, false, false, "(0,1)(-1,-1)"},
        {RegexSyntax::Extended, "(a|b)*", Text("abab"), 0, 0, false, false, "(0,4)(3,4)"},
        // glibc's tie rule, not strict POSIX: verified against GNU sed -E.
        {RegexSyntax::Extended, "(a|ab)(c|bcd)(d*)", Text("abcd"), 0, 0, false, false,
         "(0,4)(0,1)(1,4)(4,4)"},
        {RegexSyntax::Extended, "(a|ab)(bc|c)", Text("abc"), 0, 0, false, false, "(0,3)(0,1)(1,3)"},
        {RegexSyntax::Extended, "[a-c]*", Text(""), 0, 0, false, false, "(0,0)"},
        {RegexSyntax::Basic, "ABC", Text("xabc"), 0, 0, true, false, "(1,4)"},
        {RegexSyntax::Basic, "[a-c]\\+", Text("ABCD"), 0, 0, true, false, "(0,3)"},
        // A repetition whose body matches empty completes that one iteration
        // with its groups recorded (verified against GNU sed, via a back
        // reference that can only match if the group is set).
        {RegexSyntax::Basic, "\\(a\\?\\)*", Text("b"), 0, 0, false, false, "(0,0)(0,0)"},
    };
    RunMatchCases("GnuLeftmostLongest", cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexMatchTest, AnchorsAndFlags) {
    const MatchCase cases[] = {
        {RegexSyntax::Basic, "^a", Text("ba"), 0, 0, false, false, "nomatch"},
        {RegexSyntax::Basic, "^b", Text("a\nb"), 0, 0, false, false, "nomatch"},
        {RegexSyntax::Basic, "^b", Text("a\nb"), 0, 0, false, true, "(2,3)"},
        {RegexSyntax::Extended, "a$", Text("a\nb"), 0, 0, false, false, "nomatch"},
        {RegexSyntax::Extended, "a$", Text("a\nb"), 0, 0, false, true, "(0,1)"},
        {RegexSyntax::Basic, "$", Text("ab"), 0, 0, false, false, "(2,2)"},
        {RegexSyntax::Basic, "$", Text("ab"), 0, kRegexNotEol, false, false, "nomatch"},
        {RegexSyntax::Basic, "^", Text("ab"), 0, kRegexNotBol, false, false, "nomatch"},
        {RegexSyntax::Basic, "^", Text("a\nb"), 0, kRegexNotBol, false, true, "(2,2)"},
        {RegexSyntax::Basic, "\\`a", Text("ab"), 0, kRegexNotBol, false, false, "(0,1)"},
        {RegexSyntax::Extended, "\\`b", Text("a\nb"), 0, 0, false, true, "nomatch"},
        {RegexSyntax::Basic, "^", Text("ab"), 1, 0, false, false, "nomatch"},
        // The text before start is visible to assertions, as re_search sees it.
        {RegexSyntax::Basic, "\\bb", Text("ab b"), 1, 0, false, false, "(3,4)"},
        {RegexSyntax::Basic, "\\<the\\>", Text("other the"), 0, 0, false, false, "(6,9)"},
        {RegexSyntax::Basic, "\\bx", Text("ax x"), 0, 0, false, false, "(3,4)"},
        {RegexSyntax::Extended, "\\Bb", Text("abc"), 0, 0, false, false, "(1,2)"},
        {RegexSyntax::Basic, "\\w\\+", Text("  foo_1 bar"), 0, 0, false, false, "(2,7)"},
        {RegexSyntax::Basic, "[[:digit:]]\\+", Text("ab123c"), 0, 0, false, false, "(2,5)"},
        {RegexSyntax::Basic, "a.c", Text("a\nc"), 0, 0, false, false, "(0,3)"},
        {RegexSyntax::Basic, "[^a]", Text("\n"), 0, 0, false, false, "(0,1)"},
        {RegexSyntax::Basic, "a", Text("abc"), 4, 0, false, false, "nomatch"},
    };
    RunMatchCases("AnchorsAndFlags", cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexMatchTest, BackReferences) {
    const MatchCase cases[] = {
        {RegexSyntax::Basic, "\\(.\\)\\1", Text("abccd"), 0, 0, false, false, "(2,4)(2,3)"},
        {RegexSyntax::Basic, "\\(a*\\)\\1", Text("aaaa"), 0, 0, false, false, "(0,4)(0,2)"},
        {RegexSyntax::Extended, "(a)|b\\1", Text("b"), 0, 0, false, false, "nomatch"},
        {RegexSyntax::Basic, "\\(a\\)\\1", Text("aA"), 0, 0, true, false, "(0,2)(0,1)"},
        {RegexSyntax::Perl, "(\\w)\\1", Text("hello"), 0, 0, false, false, "(2,4)(2,3)"},
        // The empty last iteration of the outer loop sets group 1 to "", which
        // \\1 then matches (verified against GNU sed on the whole pattern).
        {RegexSyntax::Basic, "\\(\\(a\\?\\)\\+\\)*b\\1x", Text("abx"), 0, 0, false, false,
         "(0,3)(1,1)(1,1)"},
    };
    RunMatchCases("BackReferences", cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexMatchTest, PerlLeftmostFirst) {
    const MatchCase cases[] = {
        {RegexSyntax::Perl, "a|ab", Text("ab"), 0, 0, false, false, "(0,1)"},
        {RegexSyntax::Perl, "a+?", Text("aaa"), 0, 0, false, false, "(0,1)"},
        {RegexSyntax::Perl, "a*?b", Text("aab"), 0, 0, false, false, "(0,3)"},
        {RegexSyntax::Perl, "(a|ab)c", Text("abc"), 0, 0, false, false, "(0,3)(0,2)"},
        {RegexSyntax::Perl, "(a|ab)(c|bcd)(d*)", Text("abcd"), 0, 0, false, false,
         "(0,4)(0,1)(1,4)(4,4)"},
        {RegexSyntax::Perl, "\\d+", Text("ab12"), 0, 0, false, false, "(2,4)"},
        {RegexSyntax::Perl, "(?i)abc", Text("ABC"), 0, 0, false, false, "(0,3)"},
        {RegexSyntax::Perl, "a(?i)b", Text("aB"), 0, 0, false, false, "(0,2)"},
        {RegexSyntax::Perl, "a(?i)b", Text("AB"), 0, 0, false, false, "nomatch"},
        {RegexSyntax::Perl, "a.b", Text("a\nb"), 0, 0, false, false, "nomatch"},
        {RegexSyntax::Perl, "(?s)a.b", Text("a\nb"), 0, 0, false, false, "(0,3)"},
        {RegexSyntax::Perl, "a$", Text("a\n"), 0, 0, false, false, "(0,1)"},
        {RegexSyntax::Perl, "(?<y>\\d{4})-(\\d\\d)", Text("on 2024-01"), 0, 0, false, false,
         "(3,10)(3,7)(8,10)"},
        {RegexSyntax::Perl, "\\bfoo\\b", Text("a foo."), 0, 0, false, false, "(2,5)"},
        {RegexSyntax::Perl, "(?:ab)+", Text("ababx"), 0, 0, false, false, "(0,4)"},
        // One empty iteration of a nullable body, its groups recorded, then
        // the loop leaves (PCRE2's answers; a lazy loop exits directly, so
        // its group stays unset).
        {RegexSyntax::Perl, "(a*)*", Text("b"), 0, 0, false, false, "(0,0)(0,0)"},
        {RegexSyntax::Perl, "(a|)*", Text("b"), 0, 0, false, false, "(0,0)(0,0)"},
        {RegexSyntax::Perl, "(a*)*?", Text("b"), 0, 0, false, false, "(0,0)(-1,-1)"},
        {RegexSyntax::Perl, "((a?)+)*", Text("b"), 0, 0, false, false, "(0,0)(0,0)(0,0)"},
    };
    RunMatchCases("PerlLeftmostFirst", cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(RegexMatchTest, GroupsArePresentForEveryGroup) {
    // Patterns with 0, 1 and 10 capture groups.
    const char* patterns[] = {"abc", "(a)", "((((((((((a))))))))))"};
    for (const char* pattern : patterns) {
        SCOPED_TRACE(pattern);
        RegexOptions options;
        options.syntax = RegexSyntax::Extended;
        std::string error;
        std::shared_ptr<const Regex> compiled = Regex::Compile(pattern, options, error);
        ASSERT_NE(compiled, nullptr);
        RegexMatch match;
        ASSERT_TRUE(compiled->Search("xabc", 0, match));
        EXPECT_EQ(match.groups.size(), compiled->GroupCount() + 1);
    }
}

TEST(RegexMatchTest, TooBigPatternsFail) {
    for (auto syntax : {RegexSyntax::Extended, RegexSyntax::Perl}) {
        SCOPED_TRACE(syntax == RegexSyntax::Perl ? "Perl" : "Extended");
        RegexOptions options;
        options.syntax = syntax;
        std::string error;
        std::shared_ptr<const Regex> compiled = Regex::Compile("(a{1000}){1000}", options, error);
        EXPECT_EQ(compiled, nullptr);
        EXPECT_EQ(error, syntax == RegexSyntax::Perl ? "regular expression is too large"
                                                     : "Regular expression too big");
    }
}

namespace {

double SecondsSince(std::chrono::steady_clock::time_point began) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
}

std::shared_ptr<const Regex> CompileOrDie(std::string_view pattern, RegexSyntax syntax) {
    RegexOptions options;
    options.syntax = syntax;
    std::string error;
    std::shared_ptr<const Regex> compiled = Regex::Compile(pattern, options, error);
    if (!compiled) ADD_FAILURE() << "compile failed: " << error;
    return compiled;
}

void ExpectFound(const std::shared_ptr<const Regex>& compiled, std::string_view text,
                 const char* expected) {
    RegexMatch match;
    std::string out;
    if (!compiled->Search(text, 0, match)) {
        out = "nomatch";
    } else {
        for (const auto& group : match.groups)
            out += "(" + std::to_string(group.first) + "," + std::to_string(group.second) + ")";
    }
    EXPECT_EQ(out, expected);
}

} // namespace

TEST(RegexMatchPerformanceTest, LongLinesDoNotOverflowTheStack) {
    const std::string text(1000000, 'a');
    const std::string textEnd = text + "b";

    auto began = std::chrono::steady_clock::now();
    ExpectFound(CompileOrDie("(a|b)*c", RegexSyntax::Extended), textEnd, "nomatch");
    ExpectFound(CompileOrDie("\\(.*\\)b", RegexSyntax::Basic), textEnd,
                "(0,1000001)(0,1000000)");
    ExpectFound(CompileOrDie(".*?b", RegexSyntax::Perl), textEnd, "(0,1000001)");
    ExpectFound(CompileOrDie("\\(a\\)\\1", RegexSyntax::Basic), textEnd, "(0,2)(0,1)");
    {
        // A 30000-copy pattern: the closure must stay iterative. It matches the
        // first 30000 a's, group 1 reporting its last iteration.
        std::shared_ptr<const Regex> compiled = CompileOrDie("(a?){30000}", RegexSyntax::Extended);
        if (compiled) {
            RegexMatch match;
            ASSERT_TRUE(compiled->Search(textEnd, 0, match));
            EXPECT_EQ(match.groups.size(), 2u);
            EXPECT_EQ(match.groups[0].first, 0);
            EXPECT_EQ(match.groups[0].second, 30000);
            EXPECT_EQ(match.groups[1].first, 29999);
            EXPECT_EQ(match.groups[1].second, 30000);
        }
    }
    EXPECT_LT(SecondsSince(began), 10.0);
}

TEST(RegexMatchPerformanceTest, NoCatastrophicBacktrackingWithoutBackReferences) {
    const std::string text = std::string(5000, 'a') + "!";
    auto began = std::chrono::steady_clock::now();
    ExpectFound(CompileOrDie("(a*)*b", RegexSyntax::Extended), text, "nomatch");
    ExpectFound(CompileOrDie("(a|aa)*c", RegexSyntax::Extended), text, "nomatch");
    ExpectFound(CompileOrDie("(a+)+$", RegexSyntax::Perl), text, "nomatch");
    ExpectFound(CompileOrDie("(a|a)*b", RegexSyntax::Perl), text, "nomatch");
    EXPECT_LT(SecondsSince(began), 10.0);
}