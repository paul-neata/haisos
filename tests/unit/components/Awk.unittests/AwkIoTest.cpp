#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "AwkRunFixture.h"
#include "interfaces/IProcess.h"

namespace Haisos {
namespace {

// awk--io: getline in every form, the output redirections, close, fflush,
// system and ENVIRON. Every expectation is gawk 5.2.1 --posix's own output --
// except system()'s and close()-of-a-pipe's values, which are plain gawk
// 5.2.1's (the user's choice), and the documented exceptions (the special
// names are awk's own streams; a command stopped or broken-piped inside the
// shell maps to 271/269). Commands run in hsh, found in PATH.
class AwkIoTest : public AwkRunTest {};

TEST_F(AwkIoTest, GetlineMainInput) {
    // Simple getline: $0 and NF without a target, NR and FNR +1; the main
    // loop goes on from the next record.
    Captured captured = RunCaptured("awk",
        {R"({ print "rec", NR, FNR, NF, $0; r = getline; print "got", r, NR, FNR, NF, $0 })"},
        "l1\nl2 x\nl3\nl4\n");
    EXPECT_EQ(captured.out, "rec 1 1 1 l1\ngot 1 2 2 2 l2 x\nrec 3 3 1 l3\ngot 1 4 4 1 l4\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // With a target: the variable gets the record, $0 untouched.
    captured = RunCaptured("awk", {R"({ r = getline v; print r, NR, FNR, NF, $0, "v=" v })"},
                           "l1\nl2 x\nl3\n");
    EXPECT_EQ(captured.out, "1 2 2 1 l1 v=l2 x\n0 3 3 1 l3 v=l2 x\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // In BEGIN it opens the first operand; the main loop continues after it.
    captured = RunCaptured("awk",
        {R"(BEGIN { getline; print "B", NR, $0 } { print "M", NR, $0 })", "/abc.txt"});
    EXPECT_EQ(captured.out, "B 1 a b c\nM 2 d e f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Over every operand; FILENAME of the last one.
    captured = RunCaptured("awk",
        {R"(BEGIN { while ((getline line) > 0) n++; print n, NR, FNR, FILENAME })",
         "/abc.txt", "/data.csv"});
    EXPECT_EQ(captured.out, "5 5 3 /data.csv\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // In END the main input is used up.
    captured = RunCaptured("awk", {R"(END { r = getline; print r, $0 })", "/abc.txt"});
    EXPECT_EQ(captured.out, "0 d e f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The end leaves the target alone.
    captured = RunCaptured("awk",
        {R"(BEGIN { getline; getline x; print NR, $0, "[" x "]" })"}, "a\n");
    EXPECT_EQ(captured.out, "1 a []\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, GetlineFromFiles) {
    // An open file is read on from where it stopped; 0 again at its end.
    Captured captured = RunCaptured("awk",
        {R"(NR == 1 { while ((r = (getline line < "/abc.txt")) > 0) )"
         R"(print r, NR, FNR, NF, line; print "end", r; )"
         R"(r = getline line < "/abc.txt"; print "again", r })"},
        "l1\n");
    EXPECT_EQ(captured.out, "1 1 1 1 a b c\n1 1 1 1 d e f\nend 0\nagain 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Without a target: $0 and NF; NR and FNR are the main input's alone.
    captured = RunCaptured("awk",
        {R"({ while ((getline < "/abc.txt") > 0) print NR, FNR, NF, $2 })"}, "l1\n");
    EXPECT_EQ(captured.out, "1 1 3 b\n1 1 3 e\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // -1 for a file that cannot be read, quietly.
    captured = RunCaptured("awk",
        {R"(BEGIN { r = getline line < "/nope.txt"; print r; r = getline < "/docs"; print r })"});
    EXPECT_EQ(captured.out, "-1\n-1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // FILENAME names the file being worked on.
    captured = RunCaptured("awk", {R"({ getline x < FILENAME; print $0 "|" x })", "/abc.txt"});
    EXPECT_EQ(captured.out, "a b c|a b c\nd e f|d e f\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // close() and a fresh open: the file is read from its start again.
    captured = RunCaptured("awk",
        {R"(BEGIN { getline a < "/abc.txt"; close("/abc.txt"); )"
         R"(getline b < "/abc.txt"; print a "|" b })"});
    EXPECT_EQ(captured.out, "a b c|a b c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // RS as it is at the call.
    captured = RunCaptured("awk",
        {R"(BEGIN { RS = "e"; while ((getline x < "/abc.txt") > 0) print "[" x "]" })"});
    EXPECT_EQ(captured.out, "[a b c\nd ]\n[ f\n]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, GetlineFromCommands) {
    // A command's records into $0; NR and FNR untouched.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { while (("echo a b; echo c" | getline) > 0) print NR, NF, $0; )"
         R"(print close("echo a b; echo c") })"});
    EXPECT_EQ(captured.out, "0 2 a b\n0 1 c\n0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Into a variable.
    captured = RunCaptured("awk",
        {R"(BEGIN { while (("echo a b; echo c" | getline v) > 0) print NR, NF, v })"});
    EXPECT_EQ(captured.out, "0 0 a b\n0 0 c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The end stays the end until close(); a reopen reads afresh.
    captured = RunCaptured("awk",
        {R"(BEGIN { while (("echo 1; echo 2" | getline) > 0) n++; print n, NR; )"
         R"(print ("echo 1; echo 2" | getline), close("echo 1; echo 2"), )"
         R"(("echo 1; echo 2" | getline), $0 })"});
    EXPECT_EQ(captured.out, "2 0\n0 0 1 1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Each command name is a stream of its own; close finds them by name.
    captured = RunCaptured("awk",
        {R"(BEGIN { "echo a" | getline; "echo b" | getline; print $0; )"
         R"(print close("echo a"), close("echo b"), close("echo a") })"});
    EXPECT_EQ(captured.out, "b\n0 0 -1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A command reads awk's own standard input.
    captured = RunCaptured("awk", {R"(BEGIN { "cat" | getline x; print "[" x "]" })"}, "in1\n");
    EXPECT_EQ(captured.out, "[in1]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A command that cannot be found reads nothing: 0, hsh's message on
    // stderr.
    captured = RunCaptured("awk", {R"(BEGIN { r = ("nocmdx" | getline); print r })"});
    EXPECT_EQ(captured.out, "0\n");
    EXPECT_NE(captured.err.find("nocmdx: not found"), std::string::npos);
    EXPECT_EQ(captured.status, 0);

    // close() of a pipe: the command's exit status, as plain gawk returns it
    // (--posix gives 0).
    captured = RunCaptured("awk",
        {R"(BEGIN { "echo q; exit 4" | getline; print close("echo q; exit 4") })"});
    EXPECT_EQ(captured.out, "4\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A command lost to a broken pipe: 256 + 13 (the documented exception;
    // gawk with dash prints 141).
    captured = RunCaptured("awk",
        {R"(BEGIN { "seq 100000" | getline; print $0, close("seq 100000") })"});
    EXPECT_EQ(captured.out, "1 269\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, OutputRedirections) {
    // One stream per name: `>' twice appends; `>>' after close appends too;
    // close of a closed name is -1.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print "a" > "/out1.txt"; print "b" > "/out1.txt"; )"
         R"(close("/out1.txt"); print "c" >> "/out1.txt"; )"
         R"(print close("/out1.txt"), close("/out1.txt"), close("never"); )"
         R"(while ((getline l < "/out1.txt") > 0) print "read", l })"});
    EXPECT_EQ(captured.out, "0 -1 -1\nread a\nread b\nread c\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    std::string text;
    ReadWholeFile(*root, "/out1.txt", text);
    EXPECT_EQ(text, "a\nb\nc\n");

    // A stream opened by `>' is not reopened by `>>'.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" > "/out2.txt"; print "y" >> "/out2.txt"; )"
         R"(close("/out2.txt"); while ((getline l < "/out2.txt") > 0) print "read", l })"});
    EXPECT_EQ(captured.out, "read x\nread y\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The target is an expression, evaluated like any other.
    captured = RunCaptured("awk",
        {R"(BEGIN { printf "%s-%d\n", "n", 1 > "/out" "9.txt"; close("/out9.txt"); )"
         R"(getline l < "/out9.txt"; print l })"});
    EXPECT_EQ(captured.out, "n-1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // An output file is written out at its buffer's end, not before.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" > "/out4.txt"; getline l < "/out4.txt"; print "[" l "]" })"});
    EXPECT_EQ(captured.out, "[]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // close() writes it out.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" > "/out10.txt"; print "y" > "/out10.txt"; )"
         R"(print close("/out10.txt"); getline l < "/out10.txt"; print l })"});
    EXPECT_EQ(captured.out, "0\nx\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A pipe and a file of the same name: two streams; close takes the file
    // (the most recently opened), the pipe's `x' reaches awk's stdout at the
    // end.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" | "cat"; print "y" > "cat"; close("cat") })"});
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    ReadWholeFile(*root, "/cat", text);
    EXPECT_EQ(text, "y\n");
}

TEST_F(AwkIoTest, CloseOrder) {
    // One stream per close, the most recently opened of the name; a pipe's
    // status as plain gawk returns it.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print "x" | "cat; exit 3"; print "y" > "cat; exit 3"; )"
         R"(print "c1", close("cat; exit 3"); print "c2", close("cat; exit 3"); )"
         R"(print "c3", close("cat; exit 3") })"});
    EXPECT_EQ(captured.out, "c1 0\nx\nc2 3\nc3 -1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    std::string text;
    ReadWholeFile(*root, "/cat; exit 3", text);
    EXPECT_EQ(text, "y\n");

    // The other way round: the pipe is the most recent.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "y" > "cat; exit 3"; print "x" | "cat; exit 3"; )"
         R"(print "c1", close("cat; exit 3"); print "c2", close("cat; exit 3"); )"
         R"(print "c3", close("cat; exit 3") })"});
    EXPECT_EQ(captured.out, "x\nc1 3\nc2 0\nc3 -1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Closing a pipe writes awk's own stdout out first.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" | "cat"; print "y"; close("cat"); print "z" })"});
    EXPECT_EQ(captured.out, "y\nx\nz\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // At the end the pipes finish before awk's buffered stdout.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" | "cat"; print "y"; print "z" })"});
    EXPECT_EQ(captured.out, "x\ny\nz\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // The end closes the most recently opened first.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "1" | "sort -r"; print "2" | "sort"; print "3" | "cat" })"});
    EXPECT_EQ(captured.out, "3\n2\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A command that cannot be found: hsh's 127.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" | "nocmdx"; print close("nocmdx") })"});
    EXPECT_EQ(captured.out, "127\n");
    EXPECT_NE(captured.err.find("nocmdx: not found"), std::string::npos);
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, RedirectionErrors) {
    // The reason found through Stat: the parent, a directory, ...
    Captured captured = RunCaptured("awk", {R"(BEGIN { print "x" > "/nonexistent/dir/f"; print "after" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: cannot redirect to `/nonexistent/dir/f': "
                            "No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { print "x" > "/docs" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: cannot redirect to `/docs': Is a directory\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { printf "x" >> "/nonexistent/f" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: cannot redirect to `/nonexistent/f': "
                            "No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    // An empty target, each operator named in its own message.
    captured = RunCaptured("awk", {R"(BEGIN { print "x" > "" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: expression for `>' redirection has null "
                            "string value\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { print "x" | "" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: expression for `|' redirection has null "
                            "string value\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { "" | getline })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: expression for `|' redirection has null "
                            "string value\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk", {R"(BEGIN { getline x < "" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: expression for `<' redirection has null "
                            "string value\n");
    EXPECT_EQ(captured.status, 2);

    // A broken redirected pipe is gawk's fatal, not a quiet 141.
    captured = RunCaptured("awk",
        {R"(BEGIN { for (i = 0; i < 100000; i++) print i | "true"; print "end" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: print to \"true\" failed: Broken pipe\n");
    EXPECT_EQ(captured.status, 2);

    captured = RunCaptured("awk",
        {R"(BEGIN { for (i = 0; i < 100000; i++) printf "%d\n", i | "true"; print "end" })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: printf to \"true\" failed: Broken pipe\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(AwkIoTest, PipesAndOrdering) {
    // Two pipes of one command name: the first is reused.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print "z" | "cat"; print "w" | "cat"; r = close("cat"); print "closed", r })"});
    EXPECT_EQ(captured.out, "z\nw\nclosed 0\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { print "z" | "cat; exit 3"; print "closed", close("cat; exit 3") })"});
    EXPECT_EQ(captured.out, "z\nclosed 3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // awk's output is flushed before any command starts.
    captured = RunCaptured("awk", {R"(BEGIN { printf "1"; system("echo 2"); print "3" })"});
    EXPECT_EQ(captured.out, "12\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {R"(BEGIN { printf "1"; print "2" | "cat"; print "3" })"});
    EXPECT_EQ(captured.out, "12\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk",
        {R"(BEGIN { printf "1"; print "2" | "cat"; close("cat"); print "3" })"});
    EXPECT_EQ(captured.out, "12\n3\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {R"(BEGIN { print "to-cat" | "cat"; print "direct" })"});
    EXPECT_EQ(captured.out, "to-cat\ndirect\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A file's own buffer is flushed before the command reads it.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "a" > "/o6.txt"; system("cat /o6.txt"); print "b" })"});
    EXPECT_EQ(captured.out, "a\nb\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A record's fields into a sorting pipe, closed in END.
    captured = RunCaptured("awk",
        {R"({ print $3 | "sort -r" } END { close("sort -r"); print "done" })", "/abc.txt"});
    EXPECT_EQ(captured.out, "f\nc\ndone\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, SystemStatus) {
    // The command's exit status as plain gawk returns it (not --posix's).
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { r = system("exit 3"); print r; print system("true"); )"
         R"(print system(""); print system("false") })"});
    EXPECT_EQ(captured.out, "3\n0\n0\n1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // A command that cannot be found: hsh's 127.
    captured = RunCaptured("awk", {R"(BEGIN { print system("nocmdx") })"});
    EXPECT_EQ(captured.out, "127\n");
    EXPECT_NE(captured.err.find("nocmdx: not found"), std::string::npos);
    EXPECT_EQ(captured.status, 0);

    // The command reads awk's own standard input.
    captured = RunCaptured("awk", {R"(BEGIN { system("cat") })"}, "in1\nin2\n");
    EXPECT_EQ(captured.out, "in1\nin2\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, Fflush) {
    // fflush() and fflush("") flush everything; the standard output names
    // work without being opened; an unknown name warns.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print fflush(), fflush(""), fflush("nope"), fflush("/dev/stdout") })"});
    EXPECT_EQ(captured.out, "0 0 -1 0\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: warning: fflush: `nope' is not an open file, "
                            "pipe or co-process\n");
    EXPECT_EQ(captured.status, 0);

    // An open output file written out.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "a" > "/out3.txt"; print fflush("/out3.txt"); )"
         R"(while ((getline l < "/out3.txt") > 0) print "read", l })"});
    EXPECT_EQ(captured.out, "0\nread a\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // `-' is not special to fflush.
    captured = RunCaptured("awk", {R"(BEGIN { print fflush("-"), fflush("/dev/stderr") })"});
    EXPECT_EQ(captured.out, "-1 0\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: warning: fflush: `-' is not an open file, "
                            "pipe or co-process\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, SpecialFiles) {
    // Haisos's own behaviour: the special names are awk's own streams (gawk
    // --posix opens the devices and loses lines: the documented exception).
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print "a" > "/dev/stderr"; print "b" > "/dev/stdout"; )"
         R"(print "c" > "-"; print "d" | "cat 1>&2"; print "e" })"});
    EXPECT_EQ(captured.out, "b\nc\ne\n");
    EXPECT_EQ(captured.err, "a\nd\n");
    EXPECT_EQ(captured.status, 0);

    // close() of a standard output name flushes awk's stdout.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" > "/dev/stdout"; print close("/dev/stdout"); print "y" })"});
    EXPECT_EQ(captured.out, "x\n0\ny\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Never opened: -1, quietly.
    captured = RunCaptured("awk", {R"(BEGIN { print close("/dev/stderr") })"});
    EXPECT_EQ(captured.out, "-1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // Standard input: each name a stream of its own, its own reader.
    captured = RunCaptured("awk",
        {R"(BEGIN { getline v < "-"; print v; getline w < "/dev/stdin"; print "[" w "]" })"},
        "z\n");
    EXPECT_EQ(captured.out, "z\n[]\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    // As an operand, /dev/stdin is the standard input; FILENAME as written.
    captured = RunCaptured("awk", {R"({ print FILENAME, $0 })", "/dev/stdin"}, "q\n");
    EXPECT_EQ(captured.out, "/dev/stdin q\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, Environ) {
    auto environment = factory->CreateEnvironment();
    environment->SetVariable("HOME_X", "hx");

    // A copy of the process's variables; changing it changes nothing for the
    // commands, which get awk's own environment.
    Captured captured = RunCaptured("awk",
        {R"(BEGIN { print ENVIRON["HOME_X"], length(ENVIRON["NOPE"]); )"
         R"(ENVIRON["HOME_X"] = "changed"; system("echo $HOME_X") })"},
        std::nullopt, "/", environment);
    EXPECT_EQ(captured.out, "hx 0\nhx\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {R"(BEGIN { for (k in ENVIRON) n++; print (n > 0) })"},
                           std::nullopt, "/", environment);
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(AwkIoTest, ExitCodes) {
    // The exit code modulo 256; the streams still closed on every way out.
    Captured captured = RunCaptured("awk", {R"(BEGIN { exit -1 })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 255);

    captured = RunCaptured("awk", {R"(BEGIN { exit 256 })"});
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("awk", {R"(BEGIN { exit "3x" })"});
    EXPECT_EQ(captured.status, 3);

    captured = RunCaptured("awk", {R"(BEGIN { exit 2.9 })"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 2);

    // exit with an open pipe: the pipe finishes, then awk ends.
    captured = RunCaptured("awk", {R"(BEGIN { print "x" | "cat"; exit 3 })"});
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 3);

    // A fatal error: the message first, then the pipes and files finished.
    captured = RunCaptured("awk",
        {R"(BEGIN { print "x" | "cat"; print "f" > "/o7.txt"; z = 0; y = 1 / z })"});
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "awk: cmd. line:1: fatal: division by zero attempted\n");
    EXPECT_EQ(captured.status, 2);
    std::string text;
    ReadWholeFile(*root, "/o7.txt", text);
    EXPECT_EQ(text, "f\n");
}

TEST_F(AwkIoTest, StopsPromptly) {
    // Blocked on a command, on a pipe's read, and on waiting for a pipe's
    // command at the end: each stops within the grace, its children stopped
    // too, and nothing of the commands outlives awk.
    const char* programs[] = {
        R"(BEGIN { system("sleep 100") })",
        R"(BEGIN { while (("sleep 100" | getline) > 0) ; })",
        R"(BEGIN { print "x" | "sleep 100" })",
    };
    for (const char* program : programs) {
        auto process = StartAwk({program}, nullptr);
        ASSERT_NE(process, nullptr) << program;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        process->TriggerStop();
        EXPECT_TRUE(process->WaitToFinish(3000)) << program;
        ASSERT_TRUE(process->ExitCode().has_value()) << program;
        EXPECT_EQ(process->ExitCode().value(), 143) << program;
        // The children went with it. The OS prunes finished processes from
        // its list when a next process starts, so poll with a trivial start
        // until no sleep is listed (up to 5 s: a child awk left running
        // would stay listed).
        bool sleepGone = false;
        for (int attempts = 0; attempts < 50 && !sleepGone; ++attempts) {
            auto probe = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/pwd",
                                          {}, "/", StartProcessOptions{});
            ASSERT_NE(probe, nullptr) << program;
            EXPECT_TRUE(probe->WaitToFinish(kWaitMs)) << program;
            sleepGone = true;
            for (const std::shared_ptr<IProcess>& running : os->GetRunningProcesses()) {
                if (running->Path().find("sleep") != std::string::npos) {
                    sleepGone = false;
                }
            }
            if (!sleepGone) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        EXPECT_TRUE(sleepGone) << program;
    }
}

} // namespace
} // namespace Haisos