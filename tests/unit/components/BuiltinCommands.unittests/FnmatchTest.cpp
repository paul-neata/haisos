#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "src/components/BuiltinCommands/BuiltinFnmatch.h"

using namespace Haisos;

namespace {

struct FnmatchRow {
    std::string pattern;
    std::string text;
    int flags;
    bool expected;
};

// Every row verified against glibc's fnmatch() in the C locale (through
// ctypes), as the plan requires.
void CheckRows(const std::vector<FnmatchRow>& rows) {
    for (const auto& row : rows) {
        EXPECT_EQ(FnMatch(row.pattern, row.text, row.flags), row.expected)
            << "pattern=" << row.pattern << " text=" << row.text << " flags=" << row.flags;
    }
}

} // namespace

TEST(FnmatchTest, MatchesGlibcTable) {
    CheckRows({
        // The star, with and without Pathname.
        {"*.c", "a.c", 0, true},
        {"*.c", "dir/a.c", 0, true},
        {"*.c", "dir/a.c", kFnmPathname, false},
        // Period: a leading '.' only matches a literal '.'.
        {"*", ".hidden", kFnmPeriod, false},
        {"*", ".hidden", 0, true},
        {"?a", ".a", kFnmPeriod, false},
        {"[.]a", ".a", kFnmPeriod, false},
        {"a/*", "a/.b", kFnmPathname | kFnmPeriod, false},
        {"a/*", "a/.b", kFnmPathname, true},
        // Brackets: negation, a ']' first, ranges, a reversed range.
        {"[!a]b", "cb", 0, true},
        {"[^a]b", "ab", 0, false},
        {"[]a]", "a", 0, true},
        {"[]a]", "]", 0, true},
        {"[a-c]", "b", 0, true},
        {"[z-a]", "b", 0, false},
        // An unterminated '[' is a literal '['.
        {"[", "[", 0, true},
        {"a[", "a[", 0, true},
        // Escapes: '\*' a star, a trailing lone '\' never matches.
        {"\\*", "*", 0, true},
        {"\\*", "a", 0, false},
        {"a\\", "a\\", 0, false},
        {"a\\", "a\\", kFnmNoEscape, true},
        // Case folding, ASCII.
        {"*.C", "a.c", kFnmCaseFold, true},
        // LeadingDir: a match of a '/'-terminated prefix counts.
        {"src", "src/a/b", kFnmLeadingDir, true},
        {"s*", "src/a", kFnmLeadingDir | kFnmPathname, true},
        // '/' is only matched by a '/', whatever the wildcard.
        {"a?c", "a/c", kFnmPathname, false},
        {"a[/]c", "a/c", kFnmPathname, false},
        {"a[/]c", "a/c", 0, true},
        // Character classes, and an unknown one matching nothing.
        {"[[:digit:]]x", "1x", 0, true},
        {"[[:foo:]]", "f", 0, false},
        // The empty text, and the empty pattern.
        {"*", "", 0, true},
        {"", "", 0, true},
        // Several stars in one segment.
        {"a*b*c", "axxbyyc", 0, true},
        // '-' first or last in a bracket is a member.
        {"[a-]", "-", 0, true},
        // A negated bracket whose ']' is its first member.
        {"[!]]", "a", 0, true},
    });
}

TEST(FnmatchTest, LongTextDoesNotBlowUp) {
    // Many stars in one pattern on a long text: iterative matching, no
    // recursion per byte, so this stays linear-ish.
    const std::string pattern = std::string(30, '*') + "b";
    const std::string text(100000, 'a');
    ASSERT_FALSE(FnMatch(pattern, text, 0));
}