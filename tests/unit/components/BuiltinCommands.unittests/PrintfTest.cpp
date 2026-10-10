#include "BuiltinCommandsFixture.h"
#include "BuiltinPrintf.h"
#include <string>
#include <vector>

using namespace Haisos;

// --- printf ---

TEST_F(BuiltinCommandsTest, PrintfReusesTheFormatUntilArgumentsRunOut) {
    Captured captured = RunCaptured("printf", {"%d %d\\n", "1", "2", "3"});
    EXPECT_EQ(captured.out, "1 2\n3 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"%s %s\\n", "a", "b", "c"});
    EXPECT_EQ(captured.out, "a b\nc \n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A format that consumes nothing runs exactly once.
    captured = RunCaptured("printf", {"%s\\n"});
    EXPECT_EQ(captured.out, "\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfConversions) {
    Captured captured = RunCaptured("printf",
        {"%5.2f|%-5s|%05d|%x|%o|%e|%g\\n", "3.14159", "ab", "42", "255", "8", "1234.5", "0.0001"});
    EXPECT_EQ(captured.out, " 3.14|ab   |00042|ff|10|1.234500e+03|0.0001\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"%#x %#o %+d % d\\n", "255", "8", "5", "5"});
    EXPECT_EQ(captured.out, "0xff 010 +5  5\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"%.3d|%*d|%-*d|\\n", "5", "6", "7", "8", "9"});
    EXPECT_EQ(captured.out, "005|     7|9       |\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"%10.4s|\\n", "abcdef"});
    EXPECT_EQ(captured.out, "      abcd|\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"%G %E %F\\n", "1e-10", "1e10", "1.5"});
    EXPECT_EQ(captured.out, "1E-10 1.000000E+10 1.500000\n");
    EXPECT_EQ(captured.status, 0);

    // Length modifiers are accepted and skipped: the type comes from the
    // conversion.
    captured = RunCaptured("printf",
        {"%ld %hd %lld %Lf %jd %zd\\n", "1", "2", "3", "4.5", "5", "6"});
    EXPECT_EQ(captured.out, "1 2 3 4.500000 5 6\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfNumericArguments) {
    Captured captured = RunCaptured("printf", {"%i\\n", "0x1F", "010", "-5", "+3", " 7"});
    EXPECT_EQ(captured.out, "31\n8\n-5\n3\n7\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Character constants: the byte after the quote.
    captured = RunCaptured("printf", {"%d\\n", "'A", "\"B"});
    EXPECT_EQ(captured.out, "65\n66\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // %u of -1: strtoumax wraps, as GNU's does.
    captured = RunCaptured("printf", {"%u\\n", "-1"});
    EXPECT_EQ(captured.out, "18446744073709551615\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfNumericDiagnostics) {
    Captured captured = RunCaptured("printf", {"%d\\n", "abc"});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "printf: 'abc': expected a numeric value\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("printf", {"%d\\n", "12abc"});
    EXPECT_EQ(captured.out, "12\n");
    EXPECT_EQ(captured.err, "printf: '12abc': value not completely converted\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("printf", {"%d\\n", "99999999999999999999"});
    EXPECT_EQ(captured.out, "9223372036854775807\n");
    EXPECT_EQ(captured.err, "printf: '99999999999999999999': Numerical result out of range\n");
    EXPECT_EQ(captured.status, 1);

    // An empty argument is 0, with no message.
    captured = RunCaptured("printf", {"%d\\n", ""});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfEscapes) {
    // \c stops the output at once, with status 0, rest of the format ignored.
    Captured captured = RunCaptured("printf", {"a\\x41\\101\\c xyz"});
    EXPECT_EQ(captured.out, "aAA");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"x\\0101y\\n"});
    EXPECT_EQ(captured.out, "x\b1y\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A character with no escape: the backslash and it, as written.
    captured = RunCaptured("printf", {"\\q\\n"});
    EXPECT_EQ(captured.out, "\\q\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"\\u0041\\u00e9\\n"});
    EXPECT_EQ(captured.out, "A\xc3\xa9\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"\\u12"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "printf: missing hexadecimal number in escape\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, PrintfPercentB) {
    Captured captured = RunCaptured("printf", {"%b|\\n", "a\\tb", "x\\0101y"});
    EXPECT_EQ(captured.out, "a\tb|\nxAy|\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // \c in a %b argument stops everything, status 0.
    captured = RunCaptured("printf", {"%b", "a\\cb", "c"});
    EXPECT_EQ(captured.out, "a");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfPercentQ) {
    Captured captured = RunCaptured("printf",
        {"%q\\n", "a b", "it's", "x\ny", "", "abc"});
    EXPECT_EQ(captured.out,
        "'a b'\n"
        "\"it's\"\n"
        "'x'$'\\n''y'\n"
        "''\n"
        "abc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfInvalidSpecifications) {
    // z is a length modifier, so %zcd is %c: the first byte of the argument.
    Captured captured = RunCaptured("printf", {"ab%zcd", "1"});
    EXPECT_EQ(captured.out, "ab1d");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"%z\\n"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "printf: %z\\: invalid conversion specification\n");
    EXPECT_EQ(captured.status, 1);

    // Only exactly %b and %q are special: a width or flag before them makes
    // them ordinary -- invalid -- conversions.
    captured = RunCaptured("printf", {"%5q|"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "printf: %5q: invalid conversion specification\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("printf", {"%-10b|"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "printf: %-10b: invalid conversion specification\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("printf", {"%"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "printf: %: invalid conversion specification\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, PrintfExcessArgumentsWarn) {
    Captured captured = RunCaptured("printf", {"lit", "extra"});
    EXPECT_EQ(captured.out, "lit");
    EXPECT_EQ(captured.err, "printf: warning: ignoring excess arguments, starting with 'extra'\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfMissingOperandAndOptions) {
    Captured captured = RunCaptured("printf", {});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "printf: missing operand\nTry 'printf --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);

    // No options but --help and --version, and those only as the sole
    // argument: everything else is FORMAT.
    captured = RunCaptured("printf", {"-x"});
    EXPECT_EQ(captured.out, "-x");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"--", "-x"});
    EXPECT_EQ(captured.out, "-x");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("printf", {"--help", "x"});
    EXPECT_EQ(captured.out, "--help");
    EXPECT_EQ(captured.err, "printf: warning: ignoring excess arguments, starting with 'x'\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, PrintfCharacterConstantWarning) {
    Captured captured = RunCaptured("printf", {"%d\\n", "'ab"});
    EXPECT_EQ(captured.out, "97\n");
    EXPECT_EQ(captured.err,
        "printf: warning: b: character(s) following character constant have been ignored\n");
    EXPECT_EQ(captured.status, 0);
}

// --- the BuiltinPrintf engine itself ---

TEST(BuiltinPrintfTest, FormatsLikeGlibc) {
    PrintfSpec spec;
    size_t pos = 0;
    ASSERT_TRUE(ParsePrintfSpec("%-08.3Lf", pos, spec));
    EXPECT_EQ(spec.flags, "-0");
    ASSERT_TRUE(spec.width.has_value());
    EXPECT_EQ(*spec.width, 8);
    ASSERT_TRUE(spec.precision.has_value());
    EXPECT_EQ(*spec.precision, 3);
    EXPECT_EQ(spec.conversion, 'f');
    EXPECT_EQ(pos, 8u);
    EXPECT_FALSE(spec.widthFromArgument);
    EXPECT_FALSE(spec.precisionFromArgument);

    pos = 0;
    ASSERT_TRUE(ParsePrintfSpec("%*.*d", pos, spec));
    EXPECT_TRUE(spec.widthFromArgument);
    EXPECT_TRUE(spec.precisionFromArgument);
    EXPECT_EQ(spec.conversion, 'd');

    pos = 0;
    EXPECT_FALSE(ParsePrintfSpec("%5", pos, spec));

    spec = PrintfSpec{};
    spec.flags = "+";
    spec.width = 5;
    spec.conversion = 'd';
    EXPECT_EQ(FormatPrintfSigned(spec, 42), "  +42");
    // A negative width (from '*') is '-' with the absolute value -- the '+'
    // sign stays, as glibc's "%+-5d" prints it.
    spec.width = -5;
    EXPECT_EQ(FormatPrintfSigned(spec, 42), "+42  ");
    spec.flags.clear();
    spec.width = -5;
    EXPECT_EQ(FormatPrintfSigned(spec, 42), "42   ");

    spec = PrintfSpec{};
    spec.precision = 2;
    spec.conversion = 'e';
    EXPECT_EQ(FormatPrintfFloat(spec, 12345.678L), "1.23e+04");

    spec = PrintfSpec{};
    spec.flags = "-";
    spec.width = 4;
    spec.conversion = 'c';
    EXPECT_EQ(FormatPrintfString(spec, "x"), "x   ");
    spec.flags.clear();
    spec.width.reset();
    spec.precision = 2;
    spec.conversion = 's';
    EXPECT_EQ(FormatPrintfString(spec, "abcdef"), "ab");
    // Embedded NUL bytes go through: %c of an empty argument is one NUL.
    spec.precision.reset();
    spec.width = 3;
    EXPECT_EQ(FormatPrintfString(spec, std::string_view("\0", 1)), std::string("  \0", 3));
}