#include <gtest/gtest.h>
#include <string>
#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"

using namespace Haisos;

// --- nl ---

TEST_F(BuiltinCommandsTest, NlNumbers) {
    {
        const Captured captured = RunCaptured("nl", {}, "a\n\nb\n\\:\\:\\:\nh1\n\n\\:\\:\nb1\n\n\\:\nf1\n\n\\:\\:\nb2\n");
        EXPECT_EQ(captured.out, "     1\ta\n       \n     2\tb\n\n       h1\n       \n\n     1\tb1\n       \n\n       f1\n       \n\n     1\tb2\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-ba", "-ht", "-ft"}, "a\n\nb\n\\:\\:\\:\nh1\n\n\\:\\:\nb1\n\n\\:\nf1\n\n\\:\\:\nb2\n");
        EXPECT_EQ(captured.out, "     1\ta\n     2\t\n     3\tb\n\n     1\th1\n       \n\n     1\tb1\n     2\t\n\n     1\tf1\n       \n\n     1\tb2\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-p", "-ba"}, "a\n\nb\n\\:\\:\\:\nh1\n\n\\:\\:\nb1\n\n\\:\nf1\n\n\\:\\:\nb2\n");
        EXPECT_EQ(captured.out, "     1\ta\n     2\t\n     3\tb\n\n       h1\n       \n\n     4\tb1\n     5\t\n\n       f1\n       \n\n     6\tb2\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-n", "rz", "-v", "5", "-i", "2"}, "a\n\nb\n\\:\\:\\:\nh1\n\n\\:\\:\nb1\n\n\\:\nf1\n\n\\:\\:\nb2\n");
        EXPECT_EQ(captured.out, "000005\ta\n       \n000007\tb\n\n       h1\n       \n\n000005\tb1\n       \n\n       f1\n       \n\n000005\tb2\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-n", "ln", "-w", "3", "-s", "|"}, "a\n\nb\n");
        EXPECT_EQ(captured.out, "1  |a\n    \n2  |b\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-s", "=>", "-w", "4"}, "a\n\nb\n");
        EXPECT_EQ(captured.out, "   1=>a\n      \n   2=>b\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-s", ""}, "a\n\nb\n");
        EXPECT_EQ(captured.out, "     1a\n      \n     2b\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "-5", "-i", "2"}, "a\nb\nc\n");
        EXPECT_EQ(captured.out, "    -5\ta\n    -3\tb\n    -1\tc\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    // A negative increment counts down, and overflows past INTMAX_MIN.
    {
        const Captured captured = RunCaptured("nl", {"-i", "-1"}, "a\nb\nc\n");
        EXPECT_EQ(captured.out, "     1\ta\n     0\tb\n    -1\tc\n");
        EXPECT_EQ(captured.status, 0);
    }
    {
        const Captured captured =
            RunCaptured("nl", {"-v", "-9223372036854775807", "-i", "-1"}, "a\nb\nc\n");
        EXPECT_EQ(captured.out, "-9223372036854775807\ta\n-9223372036854775808\tb\n");
        EXPECT_EQ(captured.err, "nl: line number overflow\n");
        EXPECT_EQ(captured.status, 1);
    }
    // A width past what a small buffer holds is kept whole.
    {
        const Captured captured = RunCaptured("nl", {"-w", "33", "-n", "ln", "-s", "|"}, "a\n");
        EXPECT_EQ(captured.out, "1" + std::string(32, ' ') + "|a\n");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "-3", "-n", "rz"}, "a\nb\n");
        EXPECT_EQ(captured.out, "-00003\ta\n-00002\tb\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "-3", "-n", "ln", "-w", "6"}, "a\n");
        EXPECT_EQ(captured.out, "-3    \ta\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "-9223372036854775808"}, "a\n");
        EXPECT_EQ(captured.out, "-9223372036854775808\ta\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "123456789012"}, "x\n");
        EXPECT_EQ(captured.out, "123456789012\tx\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {}, "a");
        EXPECT_EQ(captured.out, "     1\ta\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {}, "a\nb\n");
        EXPECT_EQ(captured.out, "     1\ta\n     2\tb\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, NlSections) {
    {
        const Captured captured = RunCaptured("nl", {"-ba", "-d", "X"}, "a\nX:X:X:\nh\nX:X:\nb\nX:\nf\nz\n");
        EXPECT_EQ(captured.out, "     1\ta\n\n       h\n\n     1\tb\n\n       f\n       z\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-ba", "-d", "XY"}, "a\nXY\nz\nXYXY\nq\nXYXYXY\nH\n");
        EXPECT_EQ(captured.out, "     1\ta\n\n       z\n\n     1\tq\n\n       H\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-ba", "-d", ""}, "\\:\\:\\:\nh\n");
        EXPECT_EQ(captured.out, "     1\t\\:\\:\\:\n     2\th\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-ba"}, "a\n\\:\\:\\:x\nb\n");
        EXPECT_EQ(captured.out, "     1\ta\n     2\t\\:\\:\\:x\n     3\tb\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, NlJoinBlanks) {
    {
        const Captured captured = RunCaptured("nl", {"-ba", "-l", "2"}, "a\n\n\nb\n\n\n\n\nc\n");
        EXPECT_EQ(captured.out, "     1\ta\n       \n     2\t\n     3\tb\n       \n     4\t\n       \n     5\t\n     6\tc\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-ba", "-l", "3"}, "a\n\n\n\nb\n");
        EXPECT_EQ(captured.out, "     1\ta\n       \n       \n     2\t\n     3\tb\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-ba"}, "a\n\nb\n");
        EXPECT_EQ(captured.out, "     1\ta\n     2\t\n     3\tb\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, NlStylePatterns) {
    {
        const Captured captured = RunCaptured("nl", {"-b", "pfoo"}, "foo\nbarfoo\nbaz\n\n");
        EXPECT_EQ(captured.out, "     1\tfoo\n     2\tbarfoo\n       baz\n       \n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-b", "p"}, "a\n\n");
        EXPECT_EQ(captured.out, "     1\ta\n     2\t\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, NlOverflow) {
    {
        const Captured captured = RunCaptured("nl", {"-v", "9223372036854775807", "-i", "1"}, "a\nb\nc\n");
        EXPECT_EQ(captured.out, "9223372036854775807\ta\n");
        EXPECT_EQ(captured.err, "nl: line number overflow\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "9223372036854775807", "-i", "1"}, "a\n");
        EXPECT_EQ(captured.out, "9223372036854775807\ta\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "9223372036854775807", "-i", "1"}, "x\n\\:\\:\ny\n");
        EXPECT_EQ(captured.out, "9223372036854775807\tx\n\n9223372036854775807\ty\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
}


// The numbering carries across FILE operands, and "-" is the standard input
// wherever it stands.
TEST_F(BuiltinCommandsTest, NlCarriesAcrossFiles) {
    WriteFile("/n1", "x\n");
    WriteFile("/n2", "y\n");
    {
        const Captured captured = RunCaptured("nl", {"n1", "n2"}, std::nullopt);
        EXPECT_EQ(captured.out, "     1\tx\n     2\ty\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
    {
        const Captured captured = RunCaptured("nl", {"n1", "-", "n2"}, "mid\n");
        EXPECT_EQ(captured.out, "     1\tx\n     2\tmid\n     3\ty\n");
        EXPECT_EQ(captured.status, 0);
    }
    // An unreadable operand is reported and gone past; the rest still number.
    {
        const Captured captured = RunCaptured("nl", {"/docs", "n1"}, std::nullopt);
        EXPECT_EQ(captured.out, "     1\tx\n");
        EXPECT_EQ(captured.err, "nl: /docs: Is a directory\n");
        EXPECT_EQ(captured.status, 1);
    }
}

TEST_F(BuiltinCommandsTest, NlErrors) {
    {
        const Captured captured = RunCaptured("nl", {"-b", "x"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid body numbering style: 'x'\nTry 'nl --help' for more information.\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-f", "y"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid footer numbering style: 'y'\nTry 'nl --help' for more information.\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-h", "z"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid header numbering style: 'z'\nTry 'nl --help' for more information.\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-n", "xx"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line numbering format: 'xx'\nTry 'nl --help' for more information.\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-w", "0"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number field width: '0': Numerical result out of range\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-w", "x"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number field width: 'x'\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-w", "2147483648"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number field width: '2147483648': Value too large for defined data type\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-w", "-3"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number field width: '-3': Numerical result out of range\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-w", ""}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number field width: ''\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "x"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid starting line number: 'x'\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-v", "99999999999999999999"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid starting line number: '99999999999999999999': Value too large for defined data type\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-i", "x"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number increment: 'x'\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-l", "0"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number of blank lines: '0': Numerical result out of range\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-l", "x"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: invalid line number of blank lines: 'x'\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-b", "p\\("}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: Unmatched ( or \\(\n");
        EXPECT_EQ(captured.status, 1);
    }

    {
        const Captured captured = RunCaptured("nl", {"-h", "p("}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }

    {
        const Captured captured = RunCaptured("nl", {"nofile"}, "");
        EXPECT_EQ(captured.out, "");
        EXPECT_EQ(captured.err, "nl: nofile: No such file or directory\n");
        EXPECT_EQ(captured.status, 1);
    }
}


// Writing into a pipe nobody reads ends nl quietly, code 141, as SIGPIPE
// ends the real one.
TEST_F(BuiltinCommandsTest, NlIntoAPipeWithNoReaderExits141Quietly) {
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    // Standard input holding lines to number: without it nl would read an
    // input that ends at once and never write, so no pipe would ever break.
    auto inFile = streams->OpenFile("/nlin", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(inFile, nullptr);
    inFile->Write("a\nb\n", 4);
    inFile.reset();
    StartProcessOptions options;
    options.stdIn = streams->OpenFile("/nlin", kFileOpenReadOnly);
    options.stdOut = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/nl", {}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), kExitCodeBrokenPipe);
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "");
}
