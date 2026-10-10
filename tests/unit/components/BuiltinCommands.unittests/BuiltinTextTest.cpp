#include <gtest/gtest.h>
#include <string>
#include "BuiltinCompare.h"
#include "BuiltinText.h"

using namespace Haisos;

// --- GnuQuote ---

TEST(GnuQuoteTest, GnuQuoteEscapes) {
    EXPECT_EQ(GnuQuote("a'b"), "'a\\'b'");
    EXPECT_EQ(GnuQuote("ab"), "'ab'");
    EXPECT_EQ(GnuQuote(std::string("a\nb\001") + "\xc3\xa9"), "'a\\nb\\001\\303\\251'");
    EXPECT_EQ(GnuQuote("\\"), "'\\\\'");
    EXPECT_EQ(GnuQuote("a\tb"), "'a\\tb'");
    EXPECT_EQ(GnuQuote("a\vb\fc"), "'a\\vb\\fc'");
    EXPECT_EQ(GnuQuote(std::string("a\177b")), "'a\\177b'");
    EXPECT_EQ(GnuQuote(""), "''");
}

// --- CompareNumeric ---

TEST(CompareNumericTest, CompareNumericCases) {
    // Signs and zeros: -0, 0, -, -. and the empty text are all zero.
    EXPECT_EQ(CompareNumeric("-0", "0"), 0);
    EXPECT_EQ(CompareNumeric("-", "0"), 0);
    EXPECT_EQ(CompareNumeric("-.", "0.000"), 0);
    EXPECT_EQ(CompareNumeric("", "abc"), 0);
    // A '+' does not fit the number grammar: '+1' contributes nothing.
    EXPECT_EQ(CompareNumeric("+1", "0"), 0);
    EXPECT_EQ(CompareNumeric("+1", "1"), -1);
    // Fractions, trailing zeros ignored.
    EXPECT_EQ(CompareNumeric("0.10", "0.1"), 0);
    EXPECT_EQ(CompareNumeric(".5", "0.5"), 0);
    EXPECT_EQ(CompareNumeric(".5", "-.5"), 1);
    EXPECT_EQ(CompareNumeric("-.5", ".5"), -1);
    // Parsing stops at the first byte that does not fit.
    EXPECT_EQ(CompareNumeric("1,000", "2"), -1);
    EXPECT_EQ(CompareNumeric("1,000", "1"), 0);
    // Integers: more significant digits are larger, then digit by digit.
    EXPECT_EQ(CompareNumeric("9", "10"), -1);
    EXPECT_EQ(CompareNumeric("09", "10"), -1);
    EXPECT_EQ(CompareNumeric("2", "10"), -1);
    EXPECT_EQ(CompareNumeric("10", "9"), 1);
    EXPECT_EQ(CompareNumeric("007", "7"), 0);
    // Negatives compare as the reversed magnitudes.
    EXPECT_EQ(CompareNumeric("-1", "-.5"), -1);
    EXPECT_EQ(CompareNumeric("-.5", "-1"), 1);
    EXPECT_EQ(CompareNumeric("-10", "-9"), -1);
    // Numbers of any length compare exactly, never through a float.
    const std::string nines(40, '9');
    const std::string tenPow40 = "1" + std::string(40, '0');
    EXPECT_EQ(CompareNumeric(nines, tenPow40), -1);
    EXPECT_EQ(CompareNumeric(tenPow40, nines), 1);
    EXPECT_EQ(CompareNumeric(nines, nines), 0);
    // Fractions too: the difference sits in a digit no double could tell.
    EXPECT_EQ(CompareNumeric("0.1" + std::string(40, '0') + "1", "0.1"), 1);
    EXPECT_EQ(CompareNumeric("0.1" + std::string(40, '0'), "0.1"), 0);
    EXPECT_EQ(CompareNumeric("0." + std::string(40, '0') + "1", "0.1"), -1);
}

// --- CompareGeneralNumeric (sort -g) ---

TEST(CompareGeneralTest, CompareGeneralCases) {
    // not-a-number < NaN < numbers, two of a kind equal.
    EXPECT_EQ(CompareGeneralNumeric("", "abc"), 0);
    EXPECT_EQ(CompareGeneralNumeric("abc", "nan"), -1);
    EXPECT_EQ(CompareGeneralNumeric("nan", "NaN"), 0);
    EXPECT_EQ(CompareGeneralNumeric("nan", "-inf"), -1);
    EXPECT_EQ(CompareGeneralNumeric("-inf", "9"), -1);
    EXPECT_EQ(CompareGeneralNumeric("abc", "9"), -1);
    EXPECT_EQ(CompareGeneralNumeric("inf", "9"), 1);
    EXPECT_EQ(CompareGeneralNumeric("-inf", "inf"), -1);
    // Numbers by value; -0 == +0; hex and exponents as strtold reads them.
    EXPECT_EQ(CompareGeneralNumeric("-0", "0"), 0);
    EXPECT_EQ(CompareGeneralNumeric("9", "10"), -1);
    EXPECT_EQ(CompareGeneralNumeric("1e1", "0x10"), -1);  // 10 < 16
    EXPECT_EQ(CompareGeneralNumeric("0x10", "1e1"), 1);
    // A partial conversion is a number up to where it stopped.
    EXPECT_EQ(CompareGeneralNumeric("10abc", "9"), 1);
    // nan with a tail parses as NaN, not as not-a-number.
    EXPECT_EQ(CompareGeneralNumeric("nanabc", "1"), -1);
}

// --- CompareHumanNumeric (sort -h) ---

TEST(CompareHumanTest, CompareHumanCases) {
    // Unit first: none < K < M < ... < Q, negated by a '-'.
    EXPECT_EQ(CompareHumanNumeric("1", "1K"), -1);
    EXPECT_EQ(CompareHumanNumeric("1K", "2M"), -1);
    EXPECT_EQ(CompareHumanNumeric("2M", "1K"), 1);
    EXPECT_EQ(CompareHumanNumeric("-1G", "0"), -1);
    EXPECT_EQ(CompareHumanNumeric("1M", "-1G"), 1);
    // A number with no non-zero digit has no unit, however it ends.
    EXPECT_EQ(CompareHumanNumeric("0K", "1"), -1);
    EXPECT_EQ(CompareHumanNumeric("-0K", "0"), 0);
    EXPECT_EQ(CompareHumanNumeric("3.", "3"), 0);
    // k is K; the unit decides, the rest compares numerically.
    EXPECT_EQ(CompareHumanNumeric("1k", "1K"), 0);
    EXPECT_EQ(CompareHumanNumeric("2K", "2K1"), 0);
    EXPECT_EQ(CompareHumanNumeric("1.5K", "2K"), -1);
    // A unit byte no unit is: 'e' of "1e3" is not a unit.
    EXPECT_EQ(CompareHumanNumeric("1e3", "2"), -1);
    EXPECT_EQ(CompareHumanNumeric("3.5", "3.4K"), -1);
    // No number at all: not a number, 0 against everything.
    EXPECT_EQ(CompareHumanNumeric("K", "1"), -1);
}

// --- CompareMonth (sort -M) ---

TEST(CompareMonthTest, CompareMonthCases) {
    // The first three bytes, case-insensitively, blanks skipped; anything
    // else 0, and two unknowns equal.
    EXPECT_EQ(CompareMonth("JANUARY", "FEBRUARY"), -1);
    EXPECT_EQ(CompareMonth("JANx", "FEB"), -1);
    EXPECT_EQ(CompareMonth("JAN", "jan"), 0);
    EXPECT_EQ(CompareMonth("jan", "FEB"), -1);
    EXPECT_EQ(CompareMonth("  dec", "feb"), 1);
    EXPECT_EQ(CompareMonth("foo", "Jan"), -1);
    EXPECT_EQ(CompareMonth("foo", "bar"), 0);
    // Fewer than three bytes name no month.
    EXPECT_EQ(CompareMonth("J", "JAN"), -1);
    EXPECT_EQ(CompareMonth("ja", "JAN"), -1);
    EXPECT_EQ(CompareMonth("se", "sep"), -1);
    EXPECT_EQ(CompareMonth("DEC", "nov"), 1);
}

// --- CompareVersion (sort -V, gnulib filevercmp) ---

TEST(CompareVersionTest, CompareVersionCases) {
    // GNU's order, verified against sort -V: '.', '..', '.a', 'foo~',
    // 'foo', 'foo-1.9', 'foo-1.9.tar.gz', 'foo-1.10'.
    const std::string ordered[] = {
        ".", "..", ".a", "foo~", "foo", "foo-1.9", "foo-1.9.tar.gz", "foo-1.10",
    };
    for (size_t i = 0; i < 8; ++i) {
        for (size_t j = 0; j < 8; ++j) {
            const int expected = i < j ? -1 : (i > j ? 1 : 0);
            EXPECT_EQ(CompareVersion(ordered[i], ordered[j]), expected)
                << ordered[i] << " vs " << ordered[j];
        }
    }
    // An empty text sorts before any other.
    EXPECT_EQ(CompareVersion("", "a"), -1);
    EXPECT_EQ(CompareVersion("a", ""), 1);
    EXPECT_EQ(CompareVersion("", ""), 0);
    // Leading zeros of a digit run are equal: 1.02 == 1.2.
    EXPECT_EQ(CompareVersion("1.02", "1.2"), 0);
    // '~' sorts before the end of a text.
    EXPECT_EQ(CompareVersion("a~", "a"), -1);
    EXPECT_EQ(CompareVersion("1~", "1"), -1);
    // The end of a text against a digit run: "a" == "a0", shorter first
    // against a non-digit ("a" < "ab").
    EXPECT_EQ(CompareVersion("a", "a0"), 0);
    EXPECT_EQ(CompareVersion("a", "ab"), -1);
    // Version-like suffixes cut before the numbers compare; equal prefixes
    // with suffixes compare on the whole texts.
    EXPECT_EQ(CompareVersion("foo-1.9", "foo-1.9.tar.gz"), -1);
    EXPECT_EQ(CompareVersion("a.gz", "a.tar"), -1);
    EXPECT_EQ(CompareVersion("a", "a.b"), -1);
    // A suffix chain that does not reach the end is no suffix.
    EXPECT_EQ(CompareVersion("a.tar.gz-b", "a.tar.gz-c"), -1);
}