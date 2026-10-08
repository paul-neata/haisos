#include <gtest/gtest.h>
#include <string>
#include "BuiltinCommandsFixture.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"

using namespace Haisos;

// --- tr ---

TEST_F(BuiltinCommandsTest, TrTranslates) {
    {
    const Captured captured = RunCaptured("tr", {"el", "ip"}, "hello\n");
    EXPECT_EQ(captured.out, "hippo\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"abc", "x"}, "abcd\n");
    EXPECT_EQ(captured.out, "xxxd\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-t", "abcd", "x"}, "abcd\n");
    EXPECT_EQ(captured.out, "xbcd\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"\\101", "B"}, "AB\n");
    EXPECT_EQ(captured.out, "BB\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"[:lower:]", "[:upper:]"}, "Hello, World!\n");
    EXPECT_EQ(captured.out, "HELLO, WORLD!\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-c", "a", "x"}, "abc\n");
    EXPECT_EQ(captured.out, "axxx");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-c", "b", "xy"}, "abc\n");
    EXPECT_EQ(captured.out, "ybyy");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-c", "[:upper:]", "x"}, "aAbB\n");
    EXPECT_EQ(captured.out, "xAxBx");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-c", "a", "[x*]y"}, "\005\377z\n");
    EXPECT_EQ(captured.out, "xyxx");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-t", "abc", ""}, "abc\n");
    EXPECT_EQ(captured.out, "abc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"a-c", "x-z"}, "abcdef\n");
    EXPECT_EQ(captured.out, "xyzdef\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, TrDeletesAndSqueezes) {
    {
    const Captured captured = RunCaptured("tr", {"-d", "a"}, "banana\n");
    EXPECT_EQ(captured.out, "bnn\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-c", "-d", "b"}, "aabbcc\n");
    EXPECT_EQ(captured.out, "bb");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-d", ""}, "abc\n");
    EXPECT_EQ(captured.out, "abc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-s", "a"}, "aaabbb\n");
    EXPECT_EQ(captured.out, "abbb\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-s", "ab"}, "aabbbaab\n");
    EXPECT_EQ(captured.out, "abab\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-ds", "a", "b"}, "aabbcc\n");
    EXPECT_EQ(captured.out, "bcc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-cs", "a"}, "xxyy\n");
    EXPECT_EQ(captured.out, "xy\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-s", ""}, "abc\n");
    EXPECT_EQ(captured.out, "abc\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"-s", "a", "x"}, "aaa\n");
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, TrSets) {
    {
    const Captured captured = RunCaptured("tr", {"ab", "[x*]c"}, "abcd\n");
    EXPECT_EQ(captured.out, "xccd\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"abcde", "[x*]y"}, "abcde\n");
    EXPECT_EQ(captured.out, "xxxxy\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"[a*3]b", "x"}, "aab\n");
    EXPECT_EQ(captured.out, "xxx\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"[[:upper:]]", "y"}, "A[\n");
    EXPECT_EQ(captured.out, "yy\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"[=a=]", "x"}, "a\n");
    EXPECT_EQ(captured.out, "x\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"a\\", "x"}, "ab\n");
    EXPECT_EQ(captured.out, "xb\n");
    EXPECT_EQ(captured.err, "tr: warning: an unescaped backslash at end of string is not portable\n");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"a", "\\400"}, "ab\n");
    EXPECT_EQ(captured.out, " b\n");
    EXPECT_EQ(captured.err, "tr: warning: the ambiguous octal escape \\400 is being\n\tinterpreted as the 2-byte sequence \\040, 0\n");
    EXPECT_EQ(captured.status, 0);
    }

    {
    const Captured captured = RunCaptured("tr", {"a", "[b*010]c"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(BuiltinCommandsTest, TrErrors) {

    {
    const Captured captured = RunCaptured("tr", {}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: missing operand\nTry 'tr --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"a"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: missing operand after 'a'\nTwo strings must be given when translating.\nTry 'tr --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"a", "b", "c"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: extra operand 'c'\nTry 'tr --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"-d", "a", "b"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: extra operand 'b'\nOnly one string may be given when deleting without squeezing repeats.\nTry 'tr --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"-ds", "a"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: missing operand after 'a'\nTwo strings must be given when both deleting and squeezing repeats.\nTry 'tr --help' for more information.\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"[:upper:]", "x[:lower:]"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: misaligned [:upper:] and/or [:lower:] construct\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"a", "[:digit:]"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: when translating, the only character classes that may appear in\nstring2 are 'upper' and 'lower'\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"a", "b[=x=]"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: [=c=] expressions may not appear in string2 when translating\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"[:upper:]a", "[:lower:]"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: when translating with string1 longer than string2,\nthe latter string must not end with a character class\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"-c", "[:upper:]", "xy"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: when translating with complemented character classes,\nstring2 must map all characters in the domain to one\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"abc", ""}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: when not truncating set1, string2 must be non-empty\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"[:foo:]", "x"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: invalid character class 'foo'\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"z-a", "x"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: range-endpoints of 'z-a' are in reverse collating sequence order\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"a[x*]", "b"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: the [c*] repeat construct may not appear in string1\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"ab", "[x*][y*]"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: only one [c*] repeat construct may appear in string2\n");
    EXPECT_EQ(captured.status, 1);
    }

    {
    const Captured captured = RunCaptured("tr", {"a", "[b*x]c"}, "");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "tr: invalid repeat count 'x' in [c*n] construct\n");
    EXPECT_EQ(captured.status, 1);
    }
}


// The runs a 64 KiB read cannot hold: the squeeze state, the delete and the
// complement all carry across chunk boundaries.
TEST_F(BuiltinCommandsTest, TrCarriesStateAcrossChunks) {
    const std::string big(200000, 'a');
    {
        const Captured captured = RunCaptured("tr", {"-s", "a"}, big + "\n");
        EXPECT_EQ(captured.out, "a\n");
        EXPECT_EQ(captured.status, 0);
    }
    {
        const Captured captured = RunCaptured("tr", {"-d", "a"}, big + "\n");
        EXPECT_EQ(captured.out, "\n");
        EXPECT_EQ(captured.status, 0);
    }
    {
        const Captured captured = RunCaptured("tr", {"-c", "-d", "a"}, big + "\n");
        EXPECT_EQ(captured.out, big);
        EXPECT_EQ(captured.status, 0);
    }
}

// Writing into a pipe nobody reads ends tr quietly, code 141, as SIGPIPE
// ends the real one.
TEST_F(BuiltinCommandsTest, TrIntoAPipeWithNoReaderExits141Quietly) {
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    // Standard input holding something to translate: without it tr would
    // read an input that ends at once and never write, so no pipe would
    // ever break.
    auto inFile = streams->OpenFile("/trin", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(inFile, nullptr);
    inFile->Write("aaa\n", 4);
    inFile.reset();
    StartProcessOptions options;
    options.stdIn = streams->OpenFile("/trin", kFileOpenReadOnly);
    options.stdOut = ends.writeEnd;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/tr", {"a", "b"}, "/", options);
    ends.writeEnd.reset();
    options.stdOut.reset();
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), kExitCodeBrokenPipe);
    EXPECT_EQ(console->TakeOut(), "");
    EXPECT_EQ(console->TakeErr(), "");
}

