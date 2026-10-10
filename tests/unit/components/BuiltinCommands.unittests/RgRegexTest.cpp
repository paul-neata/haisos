#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "commands/rg/RgRegex.h"
#include "src/components/Regex/Regex.h"

using namespace Haisos;

namespace {

// The patterns translated, with the frame printed on a failure (and the
// test failed, so it shows in the log).
std::string Translated(const std::vector<std::string>& patterns, bool* hasUpper = nullptr) {
    std::string wrapped;
    RgRegexError error;
    bool hasUppercaseLiteral = false;
    const std::optional<std::string> result =
        TranslateRgPattern(patterns, wrapped, error, hasUppercaseLiteral);
    EXPECT_TRUE(result.has_value()) << FormatRgRegexError(wrapped, error);
    if (hasUpper != nullptr) *hasUpper = hasUppercaseLiteral;
    return result.value_or(std::string());
}

// The frame rg prints when |pattern| does not parse.
std::string Frame(const std::string& pattern) {
    std::string wrapped;
    RgRegexError error;
    bool hasUppercaseLiteral = false;
    const std::optional<std::string> result =
        TranslateRgPattern({pattern}, wrapped, error, hasUppercaseLiteral);
    if (result.has_value()) return "(parsed: " + *result + ")";
    return FormatRgRegexError(wrapped, error);
}

// The frame rg prints when several -e patterns do not parse.
std::string FrameOf(const std::vector<std::string>& patterns) {
    std::string wrapped;
    RgRegexError error;
    bool hasUppercaseLiteral = false;
    const std::optional<std::string> result =
        TranslateRgPattern(patterns, wrapped, error, hasUppercaseLiteral);
    if (result.has_value()) return "(parsed: " + *result + ")";
    return FormatRgRegexError(wrapped, error);
}

// The frame, from its pieces: the wrapped pattern, the caret line (its own
// four spaces of indent included) and the message.
std::string ExpectedFrame(const std::string& wrapped, const std::string& carets,
                         const std::string& message, bool hint = false) {
    std::string out = "rg: regex parse error:\n    " + wrapped + "\n    " + carets
        + "\nerror: " + message + "\n";
    if (hint) {
        out += "\nConsider enabling PCRE2 with the --pcre2 flag, which can handle "
               "backreferences\nand look-around.\n";
    }
    return out;
}

std::string Carets(size_t position, size_t count) {
    return std::string(position, ' ') + std::string(count, '^');
}

// A caret of its own at each of two positions, a space between.
std::string TwoCarets(size_t first, size_t second) {
    return std::string(first, ' ') + "^" + std::string(second - first - 1, ' ') + "^";
}

bool Matches(const std::string& perl, const std::string& text) {
    std::string error;
    const std::shared_ptr<const Regex> regex =
        Regex::Compile(perl, RegexOptions{RegexSyntax::Perl}, error);
    if (!regex) {
        ADD_FAILURE() << perl << ": " << error;
        return false;
    }
    RegexMatch match;
    return regex->Search(text, 0, match);
}

} // namespace

// ripgrep 14's frames, byte for byte: the wrapped pattern, the carets and
// the message (each verified against the reference).
TEST(RgRegexTest, Frames) {
    EXPECT_EQ(Frame("("), ExpectedFrame("(?:()", Carets(0, 1), "unclosed group"));
    EXPECT_EQ(Frame("a(b(c"), ExpectedFrame("(?:a(b(c)", Carets(4, 1), "unclosed group"));
    EXPECT_EQ(Frame("ab)"), ExpectedFrame("(?:ab))", Carets(6, 1), "unopened group"));
    EXPECT_EQ(Frame("x[ab"),
        ExpectedFrame("(?:x[ab)", Carets(4, 1), "unclosed character class"));
    EXPECT_EQ(Frame("xy{2"),
        ExpectedFrame("(?:xy{2)", Carets(5, 2), "unclosed counted repetition"));
    EXPECT_EQ(Frame("a{,3}"),
        ExpectedFrame("(?:a{,3})", Carets(5, 1),
                      "repetition quantifier expects a valid decimal"));
    EXPECT_EQ(Frame("a{"),
        ExpectedFrame("(?:a{)", Carets(5, 1),
                      "repetition quantifier expects a valid decimal"));
    EXPECT_EQ(Frame("a{2,1}"),
        ExpectedFrame("(?:a{2,1})", Carets(4, 5),
                      "invalid repetition count range, the start must be <= the end"));
    EXPECT_EQ(Frame("*a"),
        ExpectedFrame("(?:*a)", Carets(3, 1), "repetition operator missing expression"));
    EXPECT_EQ(Frame("ab\\Qx"),
        ExpectedFrame("(?:ab\\Qx)", Carets(5, 2), "unrecognized escape sequence"));
    EXPECT_EQ(Frame("(?z)a"), ExpectedFrame("(?:(?z)a)", Carets(5, 1), "unrecognized flag"));
    EXPECT_EQ(Frame("[\\b]"),
        ExpectedFrame("(?:[\\b])", Carets(4, 2),
                      "invalid escape sequence found in character class"));
    EXPECT_EQ(Frame("a\\"), ExpectedFrame("(?:a\\)", Carets(0, 1), "unclosed group"));
    EXPECT_EQ(Frame("(?"),
        ExpectedFrame("(?:(?)", Carets(4, 1), "repetition operator missing expression"));
    // Two carets, one at each offending byte.
    EXPECT_EQ(Frame("(?i-i)"),
        ExpectedFrame("(?:(?i-i))", TwoCarets(5, 7), "duplicate flag"));
    EXPECT_EQ(Frame("(?-i-s)a"),
        ExpectedFrame("(?:(?-i-s)a)", TwoCarets(5, 7), "flag negation operator repeated"));
}

// The frames a range prints: the carets run from the range's start item
// through its end item, both whole (each verified against the reference).
TEST(RgRegexTest, RangeFrames) {
    const char* const range =
        "invalid character class range, the start must be <= the end";
    EXPECT_EQ(Frame("[z-a]"), ExpectedFrame("(?:[z-a])", Carets(4, 3), range));
    EXPECT_EQ(Frame("[z-\\x61]"), ExpectedFrame("(?:[z-\\x61])", Carets(4, 6), range));
    // a raw '[' at a range's end is a literal byte, so 'a-[' is the span
    EXPECT_EQ(Frame("[a-[:alpha:]]"),
        ExpectedFrame("(?:[a-[:alpha:]])", Carets(4, 3), range));
    // the end never arrives: through the pattern's end, the wrapper's ')'
    EXPECT_EQ(Frame("[z-"), ExpectedFrame("(?:[z-)", Carets(4, 3), range));
    // an escape cut short by the end is the same error, a byte more under it
    EXPECT_EQ(Frame("[z-\\\\"), ExpectedFrame("(?:[z-\\\\)", Carets(4, 4), range));
    // a unicode class can bound no range: the whole escape under the carets
    EXPECT_EQ(Frame("[a-\\p{L}]"),
        ExpectedFrame("(?:[a-\\p{L}])", Carets(6, 5),
                      "invalid range boundary, must be a literal"));
    EXPECT_EQ(Frame("[a-\\pL]"),
        ExpectedFrame("(?:[a-\\pL])", Carets(6, 3),
                      "invalid range boundary, must be a literal"));
}

TEST(RgRegexTest, Pcre2Hint) {
    const std::string hint =
        "\nConsider enabling PCRE2 with the --pcre2 flag, which can handle "
        "backreferences\nand look-around.\n";
    EXPECT_EQ(Frame("(a)b\\1"),
        ExpectedFrame("(?:(a)b\\1)", Carets(7, 2), "backreferences are not supported") + hint);
    EXPECT_EQ(Frame("ab(?<=x)"),
        ExpectedFrame("(?:ab(?<=x))", Carets(5, 4),
                      "look-around, including look-ahead and look-behind, is not supported")
            + hint);
    EXPECT_EQ(Frame("\\0"),
        ExpectedFrame("(?:\\0)", Carets(3, 2), "backreferences are not supported") + hint);
}

TEST(RgRegexTest, SeveralPatterns) {
    // -e b -e 'a(' : the second pattern's group is what never closes.
    EXPECT_EQ(FrameOf({"b", "a("}),
        ExpectedFrame("(?:b)|(?:a()", Carets(6, 1), "unclosed group"));
}

TEST(RgRegexTest, Translations) {
    // \v is Rust's vertical tab (Perl's \v is a class).
    EXPECT_TRUE(Matches(Translated({"\\v"}), std::string(1, '\v')));
    EXPECT_FALSE(Matches(Translated({"\\v"}), " "));
    // One-sided word boundaries are \b here.
    EXPECT_TRUE(Matches(Translated({"\\<word"}), "<word> x"));
    EXPECT_TRUE(Matches(Translated({"word\\>"}), "<word> x"));
    EXPECT_TRUE(Matches(Translated({"\\b{start}word"}), "word"));
    EXPECT_TRUE(Matches(Translated({"[[:word:]]+"}), "x_y"));
    // Stacked repetitions wrap; spaces inside { } are dropped.
    EXPECT_TRUE(Matches(Translated({"a**"}), "aaa"));
    EXPECT_TRUE(Matches(Translated({"x{2}{3}"}), "xxxxxx"));
    EXPECT_FALSE(Matches(Translated({"x{2}{3}"}), "xxx"));
    EXPECT_TRUE(Matches(Translated({"a{ 2 }"}), "aa"));
    // x mode skips the unescaped whitespace, U swaps greed.
    EXPECT_TRUE(Matches(Translated({"(?x) a b"}), "ab"));
    EXPECT_FALSE(Matches(Translated({"(?x) a b"}), "a b"));
    {
        std::string error;
        const std::shared_ptr<const Regex> lazy =
            Regex::Compile(Translated({"(?U)a+"}), RegexOptions{RegexSyntax::Perl}, error);
        ASSERT_TRUE(lazy);
        RegexMatch match;
        ASSERT_TRUE(lazy->Search("aaa", 0, match));
        EXPECT_EQ(match.groups[0].first, 0);
        EXPECT_EQ(match.groups[0].second, 1);
    }
    {
        std::string error;
        EXPECT_TRUE(Regex::Compile(Translated({"(?P<n>a)"}), RegexOptions{RegexSyntax::Perl},
                                  error));
        EXPECT_TRUE(Regex::Compile(Translated({"(?<n>a)"}), RegexOptions{RegexSyntax::Perl},
                                  error));
    }
    // \x{...}, \u{...}: the code point's bytes.
    EXPECT_TRUE(Matches(Translated({"\\x{41}"}), "A"));
    EXPECT_TRUE(Matches(Translated({"\\u{e9}"}), "\xc3\xa9"));
    // A quantifier with a 0 minimum on a zero-width assertion makes it
    // optional, as Rust's regex does (^*abc matches xabc); a non-zero
    // minimum leaves the assertion itself.
    EXPECT_TRUE(Matches(Translated({"\\b{2}word"}), "word"));
    EXPECT_FALSE(Matches(Translated({"\\b{2}word"}), "sword"));
    EXPECT_TRUE(Matches(Translated({"\\b{0}"}), ""));
    EXPECT_TRUE(Matches(Translated({"^*"}), ""));
    EXPECT_TRUE(Matches(Translated({"$*"}), ""));
    EXPECT_TRUE(Matches(Translated({"\\A*x"}), "x"));
    // ... and the optional assertion may be skipped anywhere
    EXPECT_TRUE(Matches(Translated({"^*abc"}), "xabc"));
    EXPECT_TRUE(Matches(Translated({"\\b?x"}), "ax"));
    EXPECT_TRUE(Matches(Translated({"^{0,2}abc"}), "xabc"));
    EXPECT_FALSE(Matches(Translated({"^+abc"}), "xabc"));
    EXPECT_FALSE(Matches(Translated({"\\b{1,2}x"}), "ax"));
    // a code point outside a class is one atom: a quantifier repeats its
    // whole UTF-8 sequence, not its last byte (a literal in the pattern
    // or a \u{...}/\x{...} above 0x7f)
    {
        std::string error;
        const std::shared_ptr<const Regex> repeated = Regex::Compile(
            Translated({"\xc3\xa9+"}), RegexOptions{RegexSyntax::Perl}, error);
        ASSERT_NE(repeated, nullptr);
        RegexMatch match;
        ASSERT_TRUE(repeated->Search("\xc3\xa9\xc3\xa9", 0, match));
        EXPECT_EQ(match.groups[0].second, 4);  // both bytes of both code points
    }
    EXPECT_TRUE(Matches(Translated({"\\u{e9}{2}"}), "\xc3\xa9\xc3\xa9"));
    EXPECT_FALSE(Matches(Translated({"\\u{e9}{2}"}), "\xc3\xa9\xa9"));
    EXPECT_TRUE(Matches(Translated({"\\u{e9}?x"}), "x"));
    // Outside a class, [:name:] is a class of its literal bytes, as Rust
    // reads it: ':', 'a', 'l', 'p', 'h'
    EXPECT_TRUE(Matches(Translated({"[:alpha:]"}), ":"));
    EXPECT_TRUE(Matches(Translated({"[:alpha:]"}), "h"));
    EXPECT_FALSE(Matches(Translated({"[:alpha:]"}), "z"));
    // rg starts no range from a leading ']': three literal members
    EXPECT_FALSE(Matches(Translated({"[]-a]"}), "_"));
    EXPECT_TRUE(Matches(Translated({"[]-a]"}), "a"));
    EXPECT_TRUE(Matches(Translated({"[]-a]"}), "]"));
    // A 100000-byte pattern, in one pass with no recursion per byte.
    EXPECT_EQ(Translated({std::string(100000, 'a')}).size(), 100004u);
}

// rg strips its line terminator out of every class; one left holding
// nothing but '\n' matches nothing, and takes the multiline message, not
// a parse frame (a class with other members, or negated, still parses).
TEST(RgRegexTest, NewlineOnlyClass) {
    std::string wrapped;
    RgRegexError error;
    bool hasUppercaseLiteral = false;
    EXPECT_FALSE(TranslateRgPattern({"[\\n]"}, wrapped, error, hasUppercaseLiteral));
    EXPECT_TRUE(error.multiline);
    EXPECT_FALSE(TranslateRgPattern({"[\\x0a]"}, wrapped, error, hasUppercaseLiteral));
    EXPECT_TRUE(error.multiline);
    EXPECT_FALSE(TranslateRgPattern({"[\\n-\\x0a]"}, wrapped, error, hasUppercaseLiteral));
    EXPECT_TRUE(error.multiline);
    EXPECT_TRUE(TranslateRgPattern({"[\\nab]"}, wrapped, error, hasUppercaseLiteral));
    EXPECT_FALSE(error.multiline);
    EXPECT_TRUE(TranslateRgPattern({"[^\\n]"}, wrapped, error, hasUppercaseLiteral));
    EXPECT_FALSE(error.multiline);
}

TEST(RgRegexTest, HaisosRefusals) {
    EXPECT_EQ(Frame("\\pL"),
        ExpectedFrame("(?:\\pL)", Carets(3, 3),
                      "Unicode classes are not supported by HaisosOS rg"));
    EXPECT_EQ(Frame("[a-z&&[^aeiou]]"),
        ExpectedFrame("(?:[a-z&&[^aeiou]])", Carets(7, 2),
                      "class set operations and nested classes are not supported by "
                      "HaisosOS rg"));
    EXPECT_EQ(Frame("[\xc3\xa9]"),
        ExpectedFrame("(?:[\xc3\xa9])", Carets(4, 1),
                      "non-ASCII characters in a class are not supported by HaisosOS rg"));
}

TEST(RgRegexTest, SmartCase) {
    bool hasUpper = false;
    Translated({"todo"}, &hasUpper);
    EXPECT_FALSE(hasUpper);
    Translated({"Todo"}, &hasUpper);
    EXPECT_TRUE(hasUpper);
    Translated({"\\S\\W"}, &hasUpper);
    EXPECT_FALSE(hasUpper);
    Translated({"[A-Z]"}, &hasUpper);
    EXPECT_TRUE(hasUpper);
    // An uppercase letter an escape spells does not count.
    Translated({"\\x54odo"}, &hasUpper);
    EXPECT_FALSE(hasUpper);
}