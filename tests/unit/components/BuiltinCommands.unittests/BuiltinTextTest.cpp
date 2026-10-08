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