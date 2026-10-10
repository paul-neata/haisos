#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "AwkRunFixture.h"
#include "commands/awk/AwkError.h"
#include "commands/awk/AwkFormat.h"

namespace Haisos {
namespace {

using namespace Haisos::Awk;

// awk--printf-math: printf, sprintf and the math built-ins. Every
// expectation is gawk --posix 5.2.1's own output, rand()'s sequence excepted
// (Haisos's own generator, checked against the formula).

// The engine on its own: formats and arguments by hand, no program run.
TEST(AwkFormatTest, Engine) {
    // %c of an empty string is one NUL byte.
    EXPECT_EQ(FormatAwkPrintf("%d|%s|%c",
                              {Value::FromNumber(42.9), Value::FromString("s"),
                               Value::FromString("")},
                              "%.6g"),
              std::string("42|s|", 5) + '\0');
    // %5% is one %; %k an unknown conversion, copied as written.
    EXPECT_EQ(FormatAwkPrintf("%5%|%k", {}, "%.6g"), "%|%k");
    // A length modifier is refused.
    try {
        (void)FormatAwkPrintf("%ld", {Value::FromNumber(1)}, "%.6g");
        FAIL() << "the length modifier was accepted";
    } catch (const AwkFatal& error) {
        EXPECT_STREQ(error.what(), "`l' is not permitted in POSIX awk formats");
    }
    // The arguments running out: the format and the caret below it.
    try {
        (void)FormatAwkPrintf("%s %s", {Value::FromNumber(1)}, "%.6g");
        FAIL() << "the missing argument was accepted";
    } catch (const AwkFatal& error) {
        EXPECT_STREQ(error.what(),
                     "not enough arguments to satisfy format string\n\t`%s %s'\n\t"
                     "    ^ ran out for this one");
    }
}

class AwkPrintfTest : public AwkRunTest {};

TEST_F(AwkPrintfTest, Conversions) {
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { printf "%d|%i|%5.2f|%s|%c|%c|%x|%o|%e|%g|%%|%5s|%-5d|\n", )"
         R"(42.9, -3.7, 3.14159, "str", 65, "hello", 255, 8, 1234.5, 0.0001, "ab", 7 })"});
    EXPECT_EQ(captured.out, "42|-3| 3.14|str|A|h|ff|10|1.234500e+03|0.0001|%|"
                            "   ab|7    |\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { printf "%5.3d|%+d|% d|%#o|%#x|%E|%G|%.0e|%#.0f|%+s|%05s|%-05d\n", )"
         R"(7, 5, 5, 8, 255, 12345.678, 0.00001234, 12345, 3, "s", "ab", 3 })"});
    EXPECT_EQ(captured.out, "  007|+5| 5|010|0xff|1.234568E+04|1.234E-05|1e+04|3.|"
                            "s|   ab|3    \n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A '*' width or precision takes the next argument first: a negative
    // width the '-' flag, a negative precision none (none written here).
    captured = RunCaptured("awk",
        {R"(BEGIN { printf "%*d|%-*d|%.*f|%*s|%.*d|\n", )"
         R"(5, 42, 4, 7, 2, 3.14159, -6, "ab", 3, 7 })"});
    EXPECT_EQ(captured.out, "   42|7   |3.14|ab    |007|\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The parenthesized form; a string's number, and "" as 0.
    captured = RunCaptured("awk",
        {R"(BEGIN { printf("%s-%s\n", "a", "b"); printf "%d|%5.1f|%s|\n", "", "", "" })"});
    EXPECT_EQ(captured.out, "a-b\n0|  0.0||\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The format through CONVFMT when it is not a string.
    captured = RunCaptured("awk",
        {R"(BEGIN { CONVFMT = "%.2f"; printf 3.14159265; print "" })"});
    EXPECT_EQ(captured.out, "3.14\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, CharacterConversion) {
    // A field is a strnum: %c takes its number; a string its first byte.
    Captured captured = RunCaptured("awk",
        {R"({ printf "%c|%c|%c|%c|%c|%5c|%-3c|%.3c|\n", )"
         R"($1, $2, 321, -191, 65.7, "x", 66, "xyz"; )"
         R"(printf "[%s][%s][%d]\n", $3, $3 + 0, $3 })"},
        "65 hello 3.0\n");
    EXPECT_EQ(captured.out, "A|h|A|A|A|    x|B  |x|\n[3.0][3][3]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // 256, an empty string and an unset variable: one NUL byte each.
    captured = RunCaptured("awk", {R"(BEGIN { printf "%c|%c|%c|", 256, "", x })"});
    EXPECT_EQ(captured.out, std::string("\000|\000|\000|", 6));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The precision is ignored, 0 included; an infinity is byte 0.
    captured = RunCaptured("awk",
        {R"(BEGIN { printf "[%.0c][%*.*c][%c][%c]\n", "x", 3, 0, 65, -log(0), log(0) })"});
    EXPECT_EQ(captured.out, std::string("[x][  A][\000][\000]\n", 15));
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, NumbersAndStrings) {
    // %s converts a number through CONVFMT; an integral number is its digits
    // whatever the format says.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { CONVFMT = "%.2f"; OFMT = "%.3f"; x = 3.14159265; )"
         R"(printf "%s|%s|%d\n", x, x "", x; )"
         R"(printf "%s %s %s\n", 1e6, 1e16, 100000000000000000000 })"});
    EXPECT_EQ(captured.out, "3.14|3.14|3\n1000000 10000000000000000 "
                            "100000000000000000000\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // %d beyond 64 bits: every digit, as %.0f prints them.
    captured = RunCaptured("awk",
        {R"(BEGIN { printf "%d %d %d %d\n", 2^53, 2^63, 2^64, -2^63; )"
         R"(printf "%d %d %d|%.3d\n", 1e30, -1e30, "abc", -1e30; )"
         R"(printf "%i|%d|%d|%d\n", "0x1A", " 12abc", -0.5, 2^53 + 1 })"});
    EXPECT_EQ(captured.out,
              "9007199254740992 9223372036854775808 18446744073709551616 "
              "-9223372036854775808\n"
              "1000000000000000019884624838656 -1000000000000000019884624838656 "
              "0|-1000000000000000019884624838656\n"
              "26|12|0|9007199254740992\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // '#' adds no '.' to the digits beyond 64 bits.
    captured = RunCaptured("awk",
        {R"(BEGIN { printf "%#d|%#5d|%#.0d\n", 1e30, -1e30, 2^64 })"});
    EXPECT_EQ(captured.out, "1000000000000000019884624838656|-1000000000000000019884624838656|"
                            "18446744073709551616\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // %u %x %o: a negative value as its 64-bit two's complement, beyond that
    // %g with the same flags, width and precision.
    captured = RunCaptured("awk",
        {R"(BEGIN { printf "%u|%x|%X|%o|%u\n", -1, -1, 255, -8, 3.9; )"
         R"(printf "%20x|%.3x|%-12o|%u\n", 2^64, 2^64, -1e30, -2^63 - 5000 })"});
    EXPECT_EQ(captured.out,
              "18446744073709551615|ffffffffffffffff|FF|1777777777777777777770|3\n"
              "         1.84467e+19|1.84e+19|-1e+30      |-9.22337e+18\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, NonFinite) {
    // awk builds inf and nan itself: the sign by the sign bit or the flags,
    // uppercase only for E F G A, never padded with zeros.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { inf = -log(0); nan = log(-1); )"
         R"(printf "%5d|%-6d|%05d|%+d|% f|%05.1f|%E|%X|%d|%F|%e\n", )"
         R"(inf, inf, inf, inf, inf, -inf, inf, inf, nan, inf, nan })"});
    EXPECT_EQ(captured.out, "  inf|inf   |  inf|+inf| inf| -inf|INF|inf|-nan|INF|-nan\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: log: received negative argument -1\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, UnknownAndRefused) {
    // Unknown conversions are copied as written, whatever follows them; %5%
    // and %5.2% are one %.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { printf "a%kb|%5kc|%qd|%be|%I d|%'d|%5%|%5.2%|\n", 1234567; )"
         R"(printf "a%5"; printf "|a%"; printf "|a%-"; print "|" })"});
    EXPECT_EQ(captured.out, "a%kb|%5kc|%qd|%be|%I d|1234567|%|%|\na%5|a%|a%-|\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The length modifiers, each refused with its own letter.
    for (const char* spec : {"%ld", "%5hd", "%Lf", "%jd", "%zd"}) {
        const std::string modifier(1, spec[spec[1] == '5' ? 2 : 1]);
        captured = RunCaptured("awk",
            {"BEGIN { printf \"" + std::string(spec) + "\\n\", 1 }"});
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: `" + modifier +
                                "' is not permitted in POSIX awk formats\n");
        EXPECT_EQ(captured.status, 2);
    }
}

TEST_F(AwkPrintfTest, RunningOutOfArguments) {
    // The caret points at the place that ran out: the conversion character,
    // or the '*' of a width or precision.
    Captured captured = RunCaptured("awk", {R"(BEGIN { printf "ab %d %s|%d\n", 1 })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: not enough arguments to satisfy format string\n"
              "\t`ab %d %s|%d\n'\n"
              "\t       ^ ran out for this one\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { printf "%*d|\n" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: not enough arguments to satisfy format string\n"
              "\t`%*d|\n'\n"
              "\t ^ ran out for this one\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { printf "%*d|\n", 5 })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: not enough arguments to satisfy format string\n"
              "\t`%*d|\n'\n"
              "\t  ^ ran out for this one\n");
    EXPECT_EQ(captured.status, 2);

    // Past the first record the location names the file and the record.
    captured = RunCaptured("awk", {R"({ printf "%d %s\n", 1 })"}, "a\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) fatal: not enough arguments to "
              "satisfy format string\n"
              "\t`%d %s\n'\n"
              "\t    ^ ran out for this one\n");
    EXPECT_EQ(captured.status, 2);

    // sprintf's formats run out the same way, with no trailing newline in
    // the format.
    captured = RunCaptured("awk", {R"(BEGIN { x = sprintf("%s %s", "a") })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: not enough arguments to satisfy format string\n"
              "\t`%s %s'\n"
              "\t    ^ ran out for this one\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(AwkPrintfTest, SprintfAndBarePrintf) {
    // Extra arguments are ignored; a bare printf prints nothing.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { printf "x\n", 1, 2; printf; x = sprintf("%d%%", 50); )"
         R"(print x, length(x); print sprintf("abc"), sprintf(5) })"});
    EXPECT_EQ(captured.out, "x\n50% 3\nabc 5\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {R"({ printf })"}, "a b\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // sprintf with no argument fails only when the call runs.
    captured = RunCaptured("awk", {R"(BEGIN { print sprintf() })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: sprintf: no arguments\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { if (0) print sprintf(); print "ok" })"});
    EXPECT_EQ(captured.out, "ok\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, SumsWithPrintf) {
    Captured captured = RunCaptured("awk",
        {"-F,", "NR > 1 { s += $3 } END { printf \"%.2f\\n\", s }", "/data.csv"});
    EXPECT_EQ(captured.out, "7.25\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, SprintfReadsConvfmtAfterItsArguments) {
    // CONVFMT is read once every argument is evaluated: an argument that
    // assigns it counts, for the format's own conversion and for its %s
    // conversions alike (the printf statement already did).
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { x = 3.14159; s = sprintf("%s %s", x, CONVFMT = "%.2f"); print s })"});
    EXPECT_EQ(captured.out, "3.14 %.2f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { x = 3.14159; print sprintf(x, CONVFMT = "%.2f") })"});
    EXPECT_EQ(captured.out, "3.14\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { x = 3.14159; printf "%s %s\n", x, CONVFMT = "%.3f" })"});
    EXPECT_EQ(captured.out, "3.142 %.3f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, MathFunctions) {
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print sin(0), cos(0), atan2(0, -1), exp(1), log(10), sqrt(2), )"
         R"(int(3.9), int(-3.9), int("4.5abc"), int(""), int(1e30), int(-0.5), )"
         R"(log(0), exp("x") })"});
    EXPECT_EQ(captured.out,
              "0 1 3.14159 2.71828 2.30259 1.41421 3 -3 4 0 "
              "1000000000000000019884624838656 0 -inf 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { print atan2(1, 0), atan2(-1, -1), atan2(0, 0), cos("a"), log(1) })"});
    EXPECT_EQ(captured.out, "1.5708 -2.35619 0 1 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // exp out of range: infinite or below DBL_MIN (0 included).
    captured = RunCaptured("awk", {R"(BEGIN { print exp(1000); print exp(-1000); print exp(1e10) })"});
    EXPECT_EQ(captured.out, "+inf\n0\n+inf\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: exp: argument 1000 is out of range\n"
              "awk: cmd. line:1: warning: exp: argument -1000 is out of range\n"
              "awk: cmd. line:1: warning: exp: argument 1e+10 is out of range\n");
    EXPECT_EQ(captured.status, 0);

    // A subnormal result is not out of range.
    captured = RunCaptured("awk", {R"(BEGIN { print exp(-709), exp(-745) })"});
    EXPECT_EQ(captured.out, "1.21678e-308 4.94066e-324\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // log and sqrt of a negative argument: a warning, the result -nan.
    captured = RunCaptured("awk",
        {R"(BEGIN { print sqrt(-4); print log(-2); print log(-1.5) })"});
    EXPECT_EQ(captured.out, "-nan\n-nan\n-nan\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: sqrt: received negative argument -4\n"
              "awk: cmd. line:1: warning: log: received negative argument -2\n"
              "awk: cmd. line:1: warning: log: received negative argument -1.5\n");
    EXPECT_EQ(captured.status, 0);

    // Past the first record the warning carries the file and the record.
    captured = RunCaptured("awk", {R"({ print sqrt(-1) })"}, "q\n");
    EXPECT_EQ(captured.out, "-nan\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) warning: sqrt: received negative "
              "argument -1\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {R"(BEGIN { print int(-log(0)), int(log(-1)) })"});
    EXPECT_EQ(captured.out, "+inf -nan\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: log: received negative argument -1\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkPrintfTest, RandAndSrand) {
    // srand returns the previous seed, 1 before any srand.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print srand(7); print srand(3.7), srand(-2), srand("abc"), )"
         R"(srand(2^40), srand(5) })"});
    EXPECT_EQ(captured.out, "1\n7 3 -2 0 1099511627776\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The generator takes the seed given; srand() seeds with the time.
    captured = RunCaptured("awk",
        {R"(BEGIN { srand(10); a = rand(); b = rand(); srand(10); c = rand(); )"
         R"(print (a == c), (a != b), (a >= 0 && a < 1); srand(); x = srand(); )"
         R"(print (x > 1700000000) })"});
    EXPECT_EQ(captured.out, "1 1 1\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Before any srand the seed is 1.
    captured = RunCaptured("awk",
        {R"(BEGIN { a = rand(); srand(1); b = rand(); print (a == b) })"});
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Only the low 32 bits reach the generator.
    captured = RunCaptured("awk",
        {R"(BEGIN { srand(2^32 + 3); a = rand(); srand(3); b = rand(); print (a == b) })"});
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Seed 0 is not seed 1.
    captured = RunCaptured("awk",
        {R"(BEGIN { srand(0); a = rand(); srand(1); b = rand(); print (a == b) })"});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Haisos's own sequence: the 53-bit doubles of std::mt19937 seeded 1,
    // then seeded 10 (checked against the formula, not against gawk).
    captured = RunCaptured("awk",
        {R"(BEGIN { print rand(), rand(), rand(); srand(10); print rand() })"});
    EXPECT_EQ(captured.out, "0.417022 0.720324 0.000114375\n0.771321\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

} // namespace
} // namespace Haisos