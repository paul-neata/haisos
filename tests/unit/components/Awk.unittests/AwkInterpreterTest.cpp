#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinCommand.h"
#include "interfaces/IFileDescriptor.h"
#include "interfaces/IHaisosOS.h"
#include "interfaces/IProcess.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"

namespace Haisos {
namespace {

// The awk builtin run end to end: a program over the fixture's files and
// the standard input. Every expectation is gawk --posix 5.2.1's own output.
class AwkRunTest : public BuiltinCommandsTest {
protected:
    void SetUp() override {
        BuiltinCommandsTest::SetUp();
        WriteFile("/abc.txt", "a b c\nd e f\n");
        WriteFile("/data.csv", "x,1,2.5\ny,2,3.25\nz,3,4\n");
        WriteFile("/para.txt", "p1 l1\np1 l2\n\n\n\np2 l1\n\n");
    }

    // Runs /bin/awk with |stdIn| as its standard input and files as its
    // other two streams (RunCaptured's, without its input handling).
    std::shared_ptr<IProcess> StartAwk(const std::vector<std::string>& args,
                                       const std::shared_ptr<IFileDescriptor>& stdIn) {
        StartProcessOptions options;
        options.stdIn = stdIn;
        options.stdOut = streams->OpenFile("/out", kFileOpenWriteCreateTruncate, kFileCreateMode);
        options.stdErr = streams->OpenFile("/err", kFileOpenWriteCreateTruncate, kFileCreateMode);
        return os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/awk", args, "/", options);
    }
};

TEST_F(AwkRunTest, BeginOnlyAndEmptyProgram) {
    // A BEGIN-only program never touches its input.
    Captured captured = RunCaptured("awk", {"BEGIN { print \"b\" }"}, "x\n");
    EXPECT_EQ(captured.out, "b\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An empty program runs nothing at all.
    captured = RunCaptured("awk", {""}, "x\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, FieldsAndCounters) {
    Captured captured = RunCaptured("awk",
        {"{ print NR, FNR, FILENAME, NF, $2 }", "/abc.txt", "/abc.txt"});
    EXPECT_EQ(captured.out, "1 1 /abc.txt 3 b\n2 2 /abc.txt 3 e\n"
                            "3 1 /abc.txt 3 b\n4 2 /abc.txt 3 e\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The standard input is named "-", and FNR counts it alone.
    captured = RunCaptured("awk", {"{ print \"[\" FILENAME \"]\" }"}, "a\n");
    EXPECT_EQ(captured.out, "[-]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, SumsAColumn) {
    Captured captured = RunCaptured("awk",
        {"-F,", "NR > 1 { s += $3 } END { print s }", "/data.csv"});
    EXPECT_EQ(captured.out, "7.25\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, OperandAssignments) {
    Captured captured = RunCaptured("awk",
        {"-F,", "{ print $1, $2 * 2 }", "OFS=-", "/data.csv"});
    EXPECT_EQ(captured.out, "x-2\ny-4\nz-6\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An assignment operand applies when the input reaches it, so the file
    // before it saw the earlier value.
    captured = RunCaptured("awk",
        {"{ print x }", "x=1", "/abc.txt", "x=2", "/abc.txt"});
    EXPECT_EQ(captured.out, "1\n1\n2\n2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // BEGIN runs before any operand is considered.
    captured = RunCaptured("awk", {"BEGIN { print v }", "v=1"});
    EXPECT_EQ(captured.out, "\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The value's escapes are decoded, and it stays input text.
    captured = RunCaptured("awk", {"{ print v }", "v=a\\tb"}, "z\n");
    EXPECT_EQ(captured.out, "a\tb\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An empty operand is a skipped ARGV hole, not an empty file name.
    captured = RunCaptured("awk", {"{ print $1 }", ""}, "a b\n");
    EXPECT_EQ(captured.out, "a\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, DashV) {
    Captured captured = RunCaptured("awk",
        {"-v", "n=5", "-v", "s=a\\nb", "BEGIN { print n + 1; print s }"});
    EXPECT_EQ(captured.out, "6\na\nb\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"-v", "1x=3", "BEGIN { }"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: fatal: `1x' is not a legal variable name\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"-v", "s=\\q", "BEGIN { print s }"});
    EXPECT_EQ(captured.out, "q\n");
    EXPECT_EQ(captured.err, "awk: warning: escape sequence `\\q' treated as plain `q'\n");
    EXPECT_EQ(captured.status, 0);

    // The name is checked as it was written: its own escapes are not decoded.
    captured = RunCaptured("awk", {"-v", "a\\x41=1", "BEGIN { }"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: fatal: `a\\x41' is not a legal variable name\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(AwkRunTest, FieldAssignment) {
    Captured captured = RunCaptured("awk",
        {"{ $7 = \"z\"; print; print NF }", "OFS=:", "/abc.txt"});
    EXPECT_EQ(captured.out, "a:b:c::::z\n7\nd:e:f::::z\n7\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"{ $0 = \"q r\"; print $2, NF }", "/abc.txt"});
    EXPECT_EQ(captured.out, "r 2\nr 2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { $0 = \"a b c\"; NF = 2; print $0; $0 = \"  x  \"; "
         "print NF, \"[\" $1 \"]\" }"});
    EXPECT_EQ(captured.out, "a b\n1 [x]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"{ NF = 5; print; print NF; $2 = \"\"; print; print NF }"}, "a b c\n");
    EXPECT_EQ(captured.out, "a b c  \n5\na  c  \n5\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { NF = 2; print \"[\" $0 \"]\"; $2 = \"b\"; print \"[\" $0 \"]\" }"});
    EXPECT_EQ(captured.out, "[ ]\n[ b]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, RebuiltRecord) {
    // A rebuilt $0 is a string, not a strnum: "12" < 9 compares as text,
    // then $0 = 12 makes a record that is input again.
    Captured captured = RunCaptured("awk",
        {"{ $1 = 12; print ($0 < 9); $0 = 12; print ($0 < 9) }"}, "a\n");
    EXPECT_EQ(captured.out, "1\n0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The CONVFMT of the first read after the assignment rebuilds $0; a
    // later change finds it already built.
    captured = RunCaptured("awk",
        {"{ $2 = 3.5; CONVFMT = \"%.2f\"; print; CONVFMT = \"%.6g\"; print }"},
        "a b\n");
    EXPECT_EQ(captured.out, "a 3.50\na 3.50\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The OFS is the assignment's; the CONVFMT is the rebuild's.
    captured = RunCaptured("awk",
        {"{ CONVFMT = \"%.2f\"; $2 = 3.5; CONVFMT = \"%.6g\"; print; "
         "OFS = \":\"; $1 = \"c\"; OFS = \"-\"; print }"}, "a b\n");
    EXPECT_EQ(captured.out, "a 3.5\nc:3.5\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Reading far beyond NF is quiet (the limit is on assignment only).
    captured = RunCaptured("awk", {"BEGIN { print \"[\" $(1e12) \"]\" }"});
    EXPECT_EQ(captured.out, "[]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, FieldSeparators) {
    Captured captured = RunCaptured("awk", {"-F", "\t", "{ print $2 }"}, "a\tb  c\n");
    EXPECT_EQ(captured.out, "b  c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // -F t is a `t', not a tab (only the escapes are decoded).
    captured = RunCaptured("awk", {"-F", "t", "{ print $2 }"}, "atb\tc\n");
    EXPECT_EQ(captured.out, "b\tc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"-F", "|", "{ print $2 }"}, "a|b.c\n");
    EXPECT_EQ(captured.out, "b.c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"-F", ".", "{ print $2 }"}, "a|b.c\n");
    EXPECT_EQ(captured.out, "c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // FS set in BEGIN applies to every record; set in an action, from the
    // next record on -- the record in hand keeps the FS it was split with.
    captured = RunCaptured("awk", {"BEGIN { FS = \":\" } { print $2 }"}, "a:b:c\n");
    EXPECT_EQ(captured.out, "b\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"{ FS = \":\"; print $1 }"}, "a:b\nc:d\n");
    EXPECT_EQ(captured.out, "a:b\nc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An empty FS: no splitting at all.
    captured = RunCaptured("awk", {"BEGIN { FS = \"\" } { print NF, $1 }"}, "abc\n");
    EXPECT_EQ(captured.out, "1 abc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // RS: its first byte separates records (a last record needs no one).
    captured = RunCaptured("awk", {"BEGIN { RS = \"X\" } { print NR, $0 }"}, "aXbXc");
    EXPECT_EQ(captured.out, "1 a\n2 b\n3 c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"BEGIN { RS = \"ab\" } { print NR \": \" $0 }"},
                           "xaby\nzabw");
    EXPECT_EQ(captured.out, "1: x\n2: by\nz\n3: bw\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, Comparisons) {
    // A number and a strnum compare numerically; two strings as text; an
    // uninitialized value is both 0 and "".
    Captured captured = RunCaptured("awk",
        {"BEGIN { print 1 == 1.0, \"10\" < \"9\", 10 < 9, \"a\" < \"b\", "
         "x == 0, x == \"\" }"});
    EXPECT_EQ(captured.out, "1 1 0 1 1 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A strnum against a string is a string comparison; a number against
    // a strnum, numeric again.
    captured = RunCaptured("awk",
        {"{ print ($1 < $2), ($1 < \"9\"), ($1+0 < $2) }"}, "10 9\n");
    EXPECT_EQ(captured.out, "0 1 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"{ a[NR] = $1 } END { print (a[1] < a[2]) }"}, "10\n9\n");
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A for-in key is input text: a strnum, so it compares numerically.
    captured = RunCaptured("awk",
        {"BEGIN { a[9]; a[10]; for (k in a) if (k < 10) print \"lt\", k }"});
    EXPECT_EQ(captured.out, "lt 9\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A NaN is unordered: every comparison but != is false.
    captured = RunCaptured("awk",
        {"BEGIN { x = \"nan\" + 0; print (x == x), (x != x), (x < x), (x <= x), "
         "(x > x), (x >= x), (x < 1), (1 < x), (x != 1) }"});
    EXPECT_EQ(captured.out, "0 1 0 0 0 0 0 0 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, ArithmeticAndTruth) {
    Captured captured = RunCaptured("awk",
        {"BEGIN { print -3 % 2, 2 ^ 3 ^ 2, -2 ^ 2, 2 ^ -1, 7 / 2, "
         "1e6 * 1e6, 0.1 * 3, 100 / 3 }"});
    EXPECT_EQ(captured.out, "-1 512 -4 0.5 3.5 1000000000000 0.3 33.3333\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { x = \"3x\"; print x + 1, +\"\", -\"\", !\"\", !\"0\", !0, !\"a\" }"});
    EXPECT_EQ(captured.out, "4 0 0 1 0 1 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"{ print !$1, ($1 ? \"t\" : \"f\") }"}, "0\n");
    EXPECT_EQ(captured.out, "1 f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A post ++/-- gives the old number, not the old text.
    captured = RunCaptured("awk",
        {"BEGIN { x = \"abc\"; y = x++; print y; s = \"3x\"; t = s--; print t }"});
    EXPECT_EQ(captured.out, "0\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A compound assignment or ++ evaluates its target's subscript or field
    // index once.
    captured = RunCaptured("awk",
        {"BEGIN { i = 1; a[i++] += 5; print i; j = 1; b[j++]++; print j; "
         "k = 1; $0 = \"1 2 3\"; $(k++) += 10; print k, $0 }"});
    EXPECT_EQ(captured.out, "2\n2\n2 11 2 3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, NumberOutput) {
    // print's numbers go through OFMT, an integral value as its whole
    // decimal digits whatever the format; a conversion through CONVFMT.
    Captured captured = RunCaptured("awk",
        {"BEGIN { print 1e30, -1e30, 2^64, 0.1+0.2, 1e-5, 123456.7, 1234567.8; "
         "x = 1e30; y = x \"\"; print y; CONVFMT=\"%d\"; z = 2.5 \"\"; print z; "
         "OFMT=\"%x\"; print 255.5; z=-0; print z, -0.0; print 2^31, -2^63 }"});
    EXPECT_EQ(captured.out,
              "1000000000000000019884624838656 -1000000000000000019884624838656 "
              "18446744073709551616 0.3 1e-05 123457 1.23457e+06\n"
              "1000000000000000019884624838656\n"
              "2\n"
              "ff\n"
              "0 0\n"
              "2147483648 -9223372036854775808\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { CONVFMT = \"%.2f\"; a = 3.14159; b = a \"\"; print b; print a; "
         "OFMT = \"%.1f\"; print a, a \"\" }"});
    EXPECT_EQ(captured.out, "3.14\n3.14159\n3.1 3.14\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // What input counts as a strnum, and its number.
    captured = RunCaptured("awk",
        {"{ print $1+0, ($1 == $1+0) ? \"strnum\" : \"str\" }"},
        "0x11\n+inf\nnan\n -3.5e2x\n1e400\n.5.\n 12 \n1e\n");
    EXPECT_EQ(captured.out,
              "17 strnum\n+inf strnum\n+nan str\n-350 str\n+inf strnum\n"
              "0.5 str\n12 strnum\n1 str\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, Arrays) {
    Captured captured = RunCaptured("awk",
        {"BEGIN { x[\"a\"] = 1; x[\"b\"]; if (\"b\" in x) print \"in\"; "
         "delete x[\"a\"]; for (k in x) print k; delete x; "
         "for (k in x) print \"left\"; print \"ok\" }"});
    EXPECT_EQ(captured.out, "in\nb\nok\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Multi-dimensional subscripts are joined by SUBSEP.
    captured = RunCaptured("awk",
        {"BEGIN { a[1,2] = 3; for (k in a) if (k == 1 SUBSEP 2) print \"joined\"; "
         "if ((1,2) in a) print \"yes\" }"});
    EXPECT_EQ(captured.out, "joined\nyes\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { SUBSEP = \":\"; a[\"x\",\"y\"]; for (k in a) print k }"});
    EXPECT_EQ(captured.out, "x:y\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The subscript is the CONVFMT string of the number, so 0.1+0.2 and 0.3
    // are the same key.
    captured = RunCaptured("awk",
        {"BEGIN { x = 0.1 + 0.2; a[x]; for (k in a) print k; print (0.3 in a) }"});
    EXPECT_EQ(captured.out, "0.3\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Haisos's insertion order; gawk's is unspecified.
    captured = RunCaptured("awk",
        {"BEGIN { a[\"z\"]; a[\"a\"]; a[\"m\"]; for (k in a) print k }"});
    EXPECT_EQ(captured.out, "z\na\nm\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, ControlFlow) {
    Captured captured = RunCaptured("awk",
        {"BEGIN { while (i < 3) { i++; if (i == 2) continue; print i }; "
         "do { j++ } while (j < 5); print j; "
         "for (k = 0; k < 10; k++) if (k == 3) break; print k }"});
    EXPECT_EQ(captured.out, "1\n3\n5\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The separators the parser still accepts after an if's or do's body.
    captured = RunCaptured("awk",
        {"BEGIN { if (1) ; else print 2; if (1) print 1;\n"
         " else print 2; do x++; while (x < 3); print x }"});
    EXPECT_EQ(captured.out, "1\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, NextExitNextfile) {
    Captured captured = RunCaptured("awk",
        {"{ next; print \"no\" } END { print NR }", "/abc.txt"});
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"{ print; exit 5 } END { print \"end\", NR }", "/abc.txt"});
    EXPECT_EQ(captured.out, "a b c\nend 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 5);

    // exit without an expression keeps the code set before it.
    captured = RunCaptured("awk", {"BEGIN { exit } END { print \"end\" }"});
    EXPECT_EQ(captured.out, "end\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"BEGIN { exit 3 } END { print \"end\" }"});
    EXPECT_EQ(captured.out, "end\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 3);

    captured = RunCaptured("awk", {"{ exit 4 } END { print \"end\"; exit }"}, "q\n");
    EXPECT_EQ(captured.out, "end\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 4);

    // The code is a whole number truncated toward zero, then taken mod 256.
    captured = RunCaptured("awk", {"BEGIN { exit -1 }"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 255);

    captured = RunCaptured("awk", {"BEGIN { exit 257.9 }"});
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("awk", {"BEGIN { exit 1e30 }"});
    EXPECT_EQ(captured.status, 0);

    // nextfile ends the current input: the next record is the next operand's.
    captured = RunCaptured("awk",
        {"FNR == 1 { print FILENAME; nextfile } { print \"never\" }",
         "/abc.txt", "/data.csv"});
    EXPECT_EQ(captured.out, "/abc.txt\n/data.csv\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, Ranges) {
    Captured captured = RunCaptured("awk",
        {"NR==1, NR==1 { print \"one\", NR }", "/abc.txt"});
    EXPECT_EQ(captured.out, "one 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"$1 == \"a\", $1 == \"d\" { print \"r\", $1 }", "/abc.txt"});
    EXPECT_EQ(captured.out, "r a\nr d\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A range with no action prints the matching records; one whose end
    // never comes matches to the end of the input.
    captured = RunCaptured("awk", {"$1 == \"d\", 0", "/abc.txt"});
    EXPECT_EQ(captured.out, "d e f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, ArgvAndArgc) {
    Captured captured = RunCaptured("awk",
        {"BEGIN { print ARGC, ARGV[0], ARGV[1] }", "x"});
    EXPECT_EQ(captured.out, "2 awk x\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // What BEGIN writes into ARGV and ARGC is what gets read.
    captured = RunCaptured("awk",
        {"BEGIN { ARGV[1] = \"/abc.txt\"; ARGC = 2 } { print FILENAME, $0 }",
         "/nope"});
    EXPECT_EQ(captured.out, "/abc.txt a b c\n/abc.txt d e f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, RuntimeErrors) {
    // A fatal in BEGIN carries the program's place; one over a record names
    // the file and the record's number in it too.
    Captured captured = RunCaptured("awk", {"BEGIN { z = 0; x = 1 / z }"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: division by zero attempted\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"{ z = 0; x = 1 / z }"}, "a\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) fatal: division by zero attempted\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { z = 0; print 1 % z }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: division by zero attempted in `%'\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { z = 0; x /= z }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: division by zero attempted in `/='\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { z = 0; x %= z }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: division by zero attempted in `%='\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { x[1] = 1; x = 2 }"});
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: attempt to use array `x' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { x = 1; x[1] = 2 }"});
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: attempt to use scalar `x' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // Reading the variable typed it scalar, so the array use still fails.
    captured = RunCaptured("awk", {"BEGIN { if (x == 0) print \"z\"; x[1] = 1 }"});
    EXPECT_EQ(captured.out, "z\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: attempt to use scalar `x' as an array\n");
    EXPECT_EQ(captured.status, 2);

    // `k in x' types x an array (using it any other way types it scalar),
    // so the later assignment is the scalar-context error.
    captured = RunCaptured("awk", {"BEGIN { if (1 in x) ; x = 1 }"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: attempt to use array `x' in a scalar context\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { print $(-1) }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: attempt to access field -1\n");
    EXPECT_EQ(captured.status, 2);

    // A NaN, or a number out of intmax_t's range, is INTMAX_MIN as gawk
    // shows it.
    captured = RunCaptured("awk", {"BEGIN { print $(1e30) }"});
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: attempt to access field -9223372036854775808\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { NF = -1 }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: NF set to negative value\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { NF = 2^63 }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: NF set to negative value\n");
    EXPECT_EQ(captured.status, 2);

    // Haisos's own field limit (gawk has none; it fails on the allocation).
    captured = RunCaptured("awk", {"BEGIN { NF = 1e12 }"});
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: NF set to 1000000000000: more than 1000000 fields\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { $(1e12) = \"x\" }"});
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: fatal: attempt to assign field 1000000000000: "
              "more than 1000000 fields\n");
    EXPECT_EQ(captured.status, 2);

    // An input that cannot be opened is fatal, without a program place.
    captured = RunCaptured("awk", {"{ print }", "/nope"});
    EXPECT_EQ(captured.err,
              "awk: fatal: cannot open file `/nope' for reading: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"{ print }", "/docs"});
    EXPECT_EQ(captured.err, "awk: fatal: cannot open file `/docs' for reading: Is a directory\n");
    EXPECT_EQ(captured.status, 2);

    // A -f file's fatal errors carry that file's name and its own line.
    WriteFile("/dz.awk", "BEGIN {\n z=0\n x = 1/z }");
    captured = RunCaptured("awk", {"-f", "/dz.awk"});
    EXPECT_EQ(captured.err, "awk: /dz.awk:3: fatal: division by zero attempted\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(AwkRunTest, NotYetAvailable) {
    // Each unimplemented part fails with its own fatal error.
    Captured captured = RunCaptured("awk", {"BEGIN { print length(\"ab\") }"});

    captured = RunCaptured("awk", {"BEGIN { print length(\"ab\") }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: function `length' is not implemented yet\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { printf \"%d\", 1 }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: printf is not implemented yet\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { print \"a\" > \"/f\" }"});
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: output redirection is not implemented yet\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {"BEGIN { getline }"}, "a\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: getline is not implemented yet\n");
    EXPECT_EQ(captured.status, 2);
}

// --- stopping ---

// --- regexes ---

TEST_F(AwkRunTest, RegexPatterns) {
    // Literal patterns: matching, negated, and as an operand of ~; a regex
    // on its own is $0 matched against it.
    Captured captured = RunCaptured("awk",
        {"/b/ { print \"lit\" } !/z/ { print \"neg\" } $0 ~ /c$/ { print \"end\" } "
         "{ x = /a/; print x + 1 }"}, "abc\n");
    EXPECT_EQ(captured.out, "lit\nneg\nend\n2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A regex range and a range whose end never comes, in one run.
    captured = RunCaptured("awk",
        {"/b/,/e/ { print \"r\", $1 } $1 == \"d\", 0", "/abc.txt"});
    EXPECT_EQ(captured.out, "r a\nr d\nd e f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A slash in the pattern, written escaped and inside a bracket.
    captured = RunCaptured("awk", {"/\\// { print \"slash\" }"}, "a/b\n");
    EXPECT_EQ(captured.out, "slash\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"/a[/]b/"}, "a/b\n");
    EXPECT_EQ(captured.out, "a/b\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // In BEGIN $0 is empty, so a regex alone is 0 there.
    captured = RunCaptured("awk", {"BEGIN { print /a/ } { print /a/ }"}, "abc\n");
    EXPECT_EQ(captured.out, "0\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A parenthesized regex is the value of `($0 ~ /re/)': a number whose
    // text is then a dynamic regex (so "1" matches it, $0 does not).
    captured = RunCaptured("awk", {"{ print ($0 ~ (/a/)), (\"1\" ~ (/a/)) }"}, "a\n");
    EXPECT_EQ(captured.out, "0 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, RegexEscapes) {
    // An octal escape's byte is the metacharacter it spells.
    Captured captured = RunCaptured("awk", {"/a\\056b/ { print \"dot\" }"}, "axb\n");
    EXPECT_EQ(captured.out, "dot\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An unknown escape matches the plain byte, with gawk's warning.
    captured = RunCaptured("awk", {"/a\\wb/ { print \"w\" }"}, "awb\n");
    EXPECT_EQ(captured.out, "w\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: regexp escape sequence `\\w' is not a known regexp "
              "operator\n");
    EXPECT_EQ(captured.status, 0);

    // A ']' member of a bracket expression.
    captured = RunCaptured("awk", {"/a[\\]]b/ { print \"b\" }"}, "a]b\n");
    EXPECT_EQ(captured.out, "b\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Intervals are ERE, and match literally only when they match.
    captured = RunCaptured("awk", {"/a{2}b/ { print \"i\" }"}, "aab\n");
    EXPECT_EQ(captured.out, "i\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("awk", {"/a{2}b/ { print \"i\" }"}, "a{2}b\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A tab escape matches a real tab.
    captured = RunCaptured("awk", {"/a\\tb/ { print \"t\" }"}, "a\tb\n");
    EXPECT_EQ(captured.out, "t\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The same escape is warned about once per run, whatever meets it.
    captured = RunCaptured("awk", {"/b\\w/; /d\\w/"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: regexp escape sequence `\\w' is not a known regexp "
              "operator\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, DynamicRegexes) {
    // A string is a dynamic regex; its escapes are decoded by the string,
    // then translated for the engine, with the warning at the record.
    Captured captured = RunCaptured("awk", {"{ if ($0 ~ \"a\\\\wb\") print \"m\" }"}, "awb\n");
    EXPECT_EQ(captured.out, "m\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) warning: regexp escape sequence `\\w' is "
              "not a known regexp operator\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { r = \"^a\" } $0 ~ r { n++ } END { print n }"}, "ab\nab\n");
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The same dynamic regex warns once over two records.
    captured = RunCaptured("awk",
        {"{ if ($0 ~ \"a\\\\wb\") n++ } END { print n }"}, "awb\nawb\n");
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) warning: regexp escape sequence `\\w' is "
              "not a known regexp operator\n");
    EXPECT_EQ(captured.status, 0);

    // A literal's warning covers the same escape in a dynamic regex.
    captured = RunCaptured("awk",
        {"/b\\w/ { } { r = \"d\\\\w\" NR; if ($0 ~ r) n++ } END { print n + 0 }"},
        "x\ny\n");
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: warning: regexp escape sequence `\\w' is not a known regexp "
              "operator\n");
    EXPECT_EQ(captured.status, 0);

    // ~ is a search, not a match of the whole; a metacharacter in a dynamic
    // regex is the character it matches, not an anchor or a repeat.
    captured = RunCaptured("awk",
        {"{ print (\"a+\" ~ \"a+\"), (\"aa\" ~ \"a+\"), (\"b\" !~ \"a\"), "
         "(\"+\" ~ \"a+\") }"}, "x\n");
    EXPECT_EQ(captured.out, "1 1 1 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, RegexErrors) {
    // A dynamic regex that does not compile: a fatal at the record.
    Captured captured = RunCaptured("awk", {"$0 ~ \"a(\""}, "ab\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) fatal: invalid regexp: "
              "Unmatched ( or \\(: /a(/\n");
    EXPECT_EQ(captured.status, 2);

    // A literal one: reported before anything runs, status 1, only the
    // first such regex (its text as it was written).
    captured = RunCaptured("awk", {"/a(/"}, "ab\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: error: Unmatched ( or \\(: /a(/\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("awk", {"/a(/; /b(/"}, "ab\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: error: Unmatched ( or \\(: /a(/\n");
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("awk", {"/a\\/(/"}, "ab\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: error: Unmatched ( or \\(: /a\\/(/\n");
    EXPECT_EQ(captured.status, 1);

    // Nothing runs, BEGIN included.
    captured = RunCaptured("awk", {"BEGIN { print \"x\" } /a(/"}, "ab\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: error: Unmatched ( or \\(: /a(/\n");
    EXPECT_EQ(captured.status, 1);

    // A trailing backslash in a dynamic regex, reported the same way.
    captured = RunCaptured("awk", {"{ r = \"a\\\\\"; print ($0 ~ r) }"}, "a\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err,
              "awk: cmd. line:1: (FILENAME=- FNR=1) fatal: invalid regexp: "
              "Trailing backslash: /a\\/\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(AwkRunTest, RegexFieldSeparators) {
    // -F of two or more bytes is an ERE.
    Captured captured = RunCaptured("awk", {"-F", "[0-9]+", "{ print NF, $3 }"}, "a1b22c\n");
    EXPECT_EQ(captured.out, "3 c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"-F", "[ ]", "{ print NF }"}, "a  b\n");
    EXPECT_EQ(captured.out, "3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"BEGIN { FS = \", *\" } { print $2 \"|\" $3 }"}, "x, y,z\n");
    EXPECT_EQ(captured.out, "y|z\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"-F", ":+", "{ print NF }"}, ":a\n");
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"-F", " +", "{ print NF, \"[\" $1 \"]\" }"}, " a b \n");
    EXPECT_EQ(captured.out, "4 []\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An empty match never separates.
    captured = RunCaptured("awk", {"-F", "x*", "{ print NF }"}, "abc\n");
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkRunTest, ParagraphMode) {
    // RS = "": records are paragraphs, the newline a field separator for the
    // one-byte FS.
    Captured captured = RunCaptured("awk",
        {"BEGIN { RS = \"\" } { print NR \": \" $1 \"|\" $NF \"|\" NF }", "/para.txt"});
    EXPECT_EQ(captured.out, "1: p1|l2|4\n2: p2|l1|2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { RS = \"\"; FS = \"x\" } { print NF; print $2 }", "/para.txt"});
    EXPECT_EQ(captured.out, "2\np1 l2\n1\n\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {"BEGIN { RS = \"\"; FS = \"\" } { print NF \": \" $1 }", "/para.txt"});
    EXPECT_EQ(captured.out, "2: p1 l1\n1: p2 l1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A regex FS cuts the whole record: the newline separates only if the
    // regex matches it.
    captured = RunCaptured("awk",
        {"BEGIN { RS = \"\"; FS = \":+\" } { print NF \"|\" $2 }"}, "a:b\nc\n\nd\n");
    EXPECT_EQ(captured.out, "2|b\nc\n1|\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Leading blank lines skipped, a line of blanks kept in its record.
    captured = RunCaptured("awk", {"BEGIN { RS = \"\" } { print NR \": \" $0 }"},
                           "\n\na\nb\n\n\nc");
    EXPECT_EQ(captured.out, "1: a\nb\n2: c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {"BEGIN { RS = \"\" } { print NR \": \" $0 }"},
                           "a\n \nb\n\nc\n");
    EXPECT_EQ(captured.out, "1: a\n \nb\n2: c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

// --- stopping ---

TEST_F(AwkRunTest, StopsPromptly) {
    // Blocked on its input's next record. 3000 lines are fed first, so the
    // program's own output shows it runs (past its 4096-byte stdout buffer
    // and into /out); the stop then lands while it waits on the pipe.
    auto ends = os->GetPipeService()->CreatePipe();
    auto process = StartAwk({"{ print }"}, ends.readEnd);
    ASSERT_NE(process, nullptr);
    std::string fed;
    for (int i = 0; i < 3000; ++i) {
        fed += "y\n";
    }
    ASSERT_EQ(ends.writeEnd->Write(fed.data(), fed.size()),
              static_cast<ssize_t>(fed.size()));
    const auto waited = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(kWaitMs);
    std::string out;
    while (!ReadWholeFile(*streams, "/out", out) || out.empty()) {
        ASSERT_LT(std::chrono::steady_clock::now(), waited) << "awk never wrote anything";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(1000));
    ends.writeEnd.reset();
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);

    // An endless loop ends within the second as well: the same poll first,
    // so the stop is asked while the loop runs, not before it began.
    process = StartAwk({"BEGIN { for (i = 0; i < 3000; i++) print \"y\"; while (1) x++ }"},
                       nullptr);
    ASSERT_NE(process, nullptr);
    const auto looped = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(kWaitMs);
    while (!ReadWholeFile(*streams, "/out", out) || out.empty()) {
        ASSERT_LT(std::chrono::steady_clock::now(), looped) << "awk never wrote anything";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(1000));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}

TEST_F(AwkRunTest, BrokenPipeExits141) {
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();   // nobody can ever read this pipe
    StartProcessOptions options;
    options.stdOut = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/awk",
                                   {"BEGIN { while (1) print \"y\" }"}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), kExitCodeBrokenPipe);
}

} // namespace
} // namespace Haisos