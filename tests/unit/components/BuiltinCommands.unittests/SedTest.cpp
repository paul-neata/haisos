#include "BuiltinCommandsFixture.h"
#include <string>
#include <vector>

using namespace Haisos;

// --- sed ---
// Every expectation below is GNU sed 4.9's output with LC_ALL=C (the
// builtins' locale), byte for byte.

namespace {

const std::string kFour = "one\ntwo\nthree\nfour\n";
const std::string kTwo = "one\ntwo\n";

} // namespace

TEST_F(BuiltinCommandsTest, SedPrintsAndDeletes) {
    // With no script GNU prints its usage and exits 1; ours prints its help.
    Captured captured = RunCaptured("sed", {}, kFour);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, BuiltinHelpText(*CreateSedCommand()));
    EXPECT_EQ(captured.status, 1);

    captured = RunCaptured("sed", {"s/o/0/"}, kFour);
    EXPECT_EQ(captured.out, "0ne\ntw0\nthree\nf0ur\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sed", {"2d"}, kFour);
    EXPECT_EQ(captured.out, "one\nthree\nfour\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sed", {"$d"}, kFour);
    EXPECT_EQ(captured.out, "one\ntwo\nthree\n");
    EXPECT_EQ(captured.status, 0);

    // The address alone doubles the line; -n leaves p alone.
    captured = RunCaptured("sed", {"2p"}, kFour);
    EXPECT_EQ(captured.out, "one\ntwo\ntwo\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "p"}, kFour);
    EXPECT_EQ(captured.out, kFour);
    captured = RunCaptured("sed", {"-n", "--quiet", "p"}, kFour);
    EXPECT_EQ(captured.out, kFour);
    captured = RunCaptured("sed", {"-n", "--silent", "p"}, kFour);
    EXPECT_EQ(captured.out, kFour);
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedSubstitute) {
    Captured captured = RunCaptured("sed", {"s/o/0/"}, "foooo\n");
    EXPECT_EQ(captured.out, "f0ooo\n");
    captured = RunCaptured("sed", {"s/o/0/g"}, "foooo\n");
    EXPECT_EQ(captured.out, "f0000\n");
    captured = RunCaptured("sed", {"s/o/0/2"}, "foooo\n");
    EXPECT_EQ(captured.out, "fo0oo\n");
    // The number counts matches from the first on: /2g replaces 2nd and up.
    captured = RunCaptured("sed", {"s/o/0/2g"}, "foooo\n");
    EXPECT_EQ(captured.out, "fo000\n");

    // Empty matches, GNU's do_sub: between every pair and at both ends.
    captured = RunCaptured("sed", {"s/x*/-/g"}, "one\n");
    EXPECT_EQ(captured.out, "-o-n-e-\n");
    captured = RunCaptured("sed", {"s/b*/X/g"}, "abc\n");
    EXPECT_EQ(captured.out, "XaXcX\n");

    // Groups in -E, & and the occurrence flag together.
    captured = RunCaptured("sed", {"-E", "s/(o)(n)/\\2\\1/g"}, "one\n");
    EXPECT_EQ(captured.out, "noe\n");
    captured = RunCaptured("sed", {"s/./&+&/"}, "a\n");
    EXPECT_EQ(captured.out, "a+a\n");
    captured = RunCaptured("sed", {"s/a/*/2"}, "banana\n");
    EXPECT_EQ(captured.out, "ban*na\n");

    // p prints only when a substitution happened, and the line again.
    captured = RunCaptured("sed", {"2,4s/o/0/gp"}, kFour);
    EXPECT_EQ(captured.out, "one\ntw0\ntw0\nthree\nf0ur\nf0ur\n");
    captured = RunCaptured("sed", {"-n", "s/o/0/p"}, "one\ntwo\n");
    EXPECT_EQ(captured.out, "0ne\ntw0\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedReplacementEscapes) {
    Captured captured = RunCaptured("sed", {"s/one/a\\nb/"}, "one\n");
    EXPECT_EQ(captured.out, "a\nb\n");
    captured = RunCaptured("sed", {"s/o/\\t/"}, "one\n");
    EXPECT_EQ(captured.out, "\tne\n");
    captured = RunCaptured("sed", {"s/o/\\x41/"}, "one\n");
    EXPECT_EQ(captured.out, "Ane\n");
    // \d is decimal, \o octal, \c a control character.
    captured = RunCaptured("sed", {"s/o/\\d123/"}, "one\n");
    EXPECT_EQ(captured.out, "{ne\n");
    captured = RunCaptured("sed", {"s/o/\\o123/"}, "one\n");
    EXPECT_EQ(captured.out, "Sne\n");
    captured = RunCaptured("sed", {"s/o/\\cA/"}, "one\n");
    EXPECT_EQ(captured.out, "\x01ne\n");
    captured = RunCaptured("sed", {"s/a/\\o101\\cJ/"}, "a\n");
    EXPECT_EQ(captured.out, "A\n\n");

    // The case escapes, and \E ending them.
    captured = RunCaptured("sed", {"s/one/\\U&/"}, "one\n");
    EXPECT_EQ(captured.out, "ONE\n");
    captured = RunCaptured("sed", {"s/ONE/\\L&/"}, "ONE\n");
    EXPECT_EQ(captured.out, "one\n");
    captured = RunCaptured("sed", {"s/one/\\u&/"}, "one\n");
    EXPECT_EQ(captured.out, "One\n");
    captured = RunCaptured("sed", {"s/ONE/\\l&/"}, "ONE\n");
    EXPECT_EQ(captured.out, "oNE\n");
    captured = RunCaptured("sed", {"s/one/\\Uone\\Etwo/"}, "one\n");
    EXPECT_EQ(captured.out, "ONEtwo\n");

    // A literal backslash and ampersand.
    captured = RunCaptured("sed", {"s/a/\\\\/"}, "a\n");
    EXPECT_EQ(captured.out, "\\\n");
    captured = RunCaptured("sed", {"s/a/\\&/"}, "a\n");
    EXPECT_EQ(captured.out, "&\n");
    captured = RunCaptured("sed", {"s/one/&/"}, "one\n");
    EXPECT_EQ(captured.out, "one\n");

    // The case escapes together with groups and &: \U\2\E\1x\u& on "one".
    captured = RunCaptured("sed", {"s/\\(o\\)\\(n\\)/\\U\\2\\E\\1x\\u&/"}, "one\n");
    EXPECT_EQ(captured.out, "NoxOne\n");
    // A custom delimiter, '|' escaped in the replacement as the delimiter.
    captured = RunCaptured("sed", {"s|/|\\||"}, "a/b\n");
    EXPECT_EQ(captured.out, "a|b\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedExtendedRegex) {
    Captured captured = RunCaptured("sed", {"-E", "s/o|n/!/gp"}, "one\n");
    // Both substitutions are printed: the p flag and the automatic printing.
    EXPECT_EQ(captured.out, "!!e\n!!e\n");
    captured = RunCaptured("sed", {"-r", "s/o+/@/"}, "one\n");
    EXPECT_EQ(captured.out, "@ne\n");
    captured = RunCaptured("sed", {"-E", "s/[[:upper:]]/=/I"}, "one\n");
    EXPECT_EQ(captured.out, "=ne\n");
    captured = RunCaptured("sed", {"-E", "s/(a)(b)/\\2\\1/"}, "ab\n");
    EXPECT_EQ(captured.out, "ba\n");
    // The task's acceptance scenario: a group kept and a () pair dropped.
    captured = RunCaptured("sed", {"-E", "s/(Create)\\(\\)/\\1Instance()/g"},
                           "auto a = Create();\n");
    EXPECT_EQ(captured.out, "auto a = CreateInstance();\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedRegexFlagsIAndM) {
    // I ignores case in the regex.
    Captured captured = RunCaptured("sed", {"s/one/x/I"}, "ONE\n");
    EXPECT_EQ(captured.out, "x\n");
    captured = RunCaptured("sed", {"s/X/Y/Ig"}, "xox\n");
    EXPECT_EQ(captured.out, "YoY\n");

    // M is glibc's REG_NEWLINE, on a pattern space with an embedded newline
    // (the replacement of the first command puts one there): without it
    // ^ and $ match only the ends of the whole space, . and a nonmatching
    // list also match the '\n'; with it, per line, and they do not.
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/$/Y/"}, "one\n");
    EXPECT_EQ(captured.out, "a\nbY\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/$/Y/M"}, "one\n");
    EXPECT_EQ(captured.out, "aY\nb\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/^/X/g"}, "one\n");
    EXPECT_EQ(captured.out, "Xa\nb\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/^/X/Mg"}, "one\n");
    EXPECT_EQ(captured.out, "Xa\nXb\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/./Z/g"}, "one\n");
    EXPECT_EQ(captured.out, "ZZZ\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/./Z/Mg"}, "one\n");
    EXPECT_EQ(captured.out, "Z\nZ\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/[^x]/Q/g"}, "one\n");
    EXPECT_EQ(captured.out, "QQQ\n");
    captured = RunCaptured("sed", {"s/one/a\\nb/;s/[^x]/Q/Mg"}, "one\n");
    EXPECT_EQ(captured.out, "Q\nQ\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedEscapesInRegex) {
    Captured captured = RunCaptured("sed", {"s/[aeiou]/./g"}, kTwo);
    EXPECT_EQ(captured.out, ".n.\ntw.\n");
    captured = RunCaptured("sed", {"s/\\tone/\\tX/"}, "\tone\n");
    EXPECT_EQ(captured.out, "\tX\n");
    captured = RunCaptured("sed", {"s/\\(o\\)\\1/!/"}, "oo\n");
    EXPECT_EQ(captured.out, "!\n");
    captured = RunCaptured("sed", {"s/\\x6f/0/"}, "one\n");
    EXPECT_EQ(captured.out, "0ne\n");
    captured = RunCaptured("sed", {"s/\\d111/0/"}, "one\n");
    EXPECT_EQ(captured.out, "0ne\n");
    captured = RunCaptured("sed", {"s/^/X/"}, kTwo);
    EXPECT_EQ(captured.out, "Xone\nXtwo\n");
    captured = RunCaptured("sed", {"s/a\\-b/OK/"}, "a-b\n");
    EXPECT_EQ(captured.out, "OK\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedAddresses) {
    Captured captured = RunCaptured("sed", {"-n", "2,$p"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "1~2p"}, kFour);
    EXPECT_EQ(captured.out, "one\nthree\n");
    captured = RunCaptured("sed", {"-n", "2~3p"}, kFour);
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"-n", "2,+2p"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "1~2!p"}, kFour);
    EXPECT_EQ(captured.out, "two\nfour\n");
    captured = RunCaptured("sed", {"-n", "/tw/p"}, kFour);
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"-n", "/one/,/three/p"}, kFour);
    EXPECT_EQ(captured.out, "one\ntwo\nthree\n");
    captured = RunCaptured("sed", {"-n", "1!p"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "$p"}, kFour);
    EXPECT_EQ(captured.out, "four\n");
    captured = RunCaptured("sed", {"-n", "2!p"}, kFour);
    EXPECT_EQ(captured.out, "one\nthree\nfour\n");
    // A second address below the first matches the first line alone.
    captured = RunCaptured("sed", {"-n", "3,2p"}, kFour);
    EXPECT_EQ(captured.out, "three\n");
    captured = RunCaptured("sed", {"-n", "/nope/p"}, kFour);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-n", "/four/q"}, kFour);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
    // An open range ends at the end of input.
    captured = RunCaptured("sed", {"-n", "/tw/,/nope/p"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");

    // A regex address with I, a custom delimiter, and a negated match.
    captured = RunCaptured("sed", {"-n", "/x/I p"}, "aXb\nc\n");
    EXPECT_EQ(captured.out, "aXb\n");
    captured = RunCaptured("sed", {"-n", "\\%t%p"}, "two\nfoo\n");
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"-n", "/x/ !p"}, "x\ny\n");
    EXPECT_EQ(captured.out, "y\n");
    EXPECT_EQ(captured.status, 0);

    // 1,/re/ starts at the first match, so it may run past it; 0,/re/ is
    // open from the first line, so the regex ends it there.
    captured = RunCaptured("sed", {"-n", "1,/one/p"}, "one\ntwo\nthree\n");
    EXPECT_EQ(captured.out, "one\ntwo\nthree\n");
    captured = RunCaptured("sed", {"-n", "0,/one/p"}, "one\ntwo\nthree\n");
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedZeroAndStepAddresses) {
    Captured captured = RunCaptured("sed", {"-n", "0,/tw/p"}, "one\ntwo\nthree\n");
    EXPECT_EQ(captured.out, "one\ntwo\n");
    // A step of 0 matches the first line only, as GNU's does.
    captured = RunCaptured("sed", {"1~0p"}, "one\n");
    EXPECT_EQ(captured.out, "one\none\n");
    captured = RunCaptured("sed", {"-n", "1~3p"}, kFour);
    EXPECT_EQ(captured.out, "one\nfour\n");

    // 0 is legal only as the first half of 0,/re/; GNU reports it at the '!'
    // when there is one, else at the command, else right after the address.
    captured = RunCaptured("sed", {"0p"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 2: invalid usage of line address 0\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("sed", {"0!p"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 2: invalid usage of line address 0\n");
    captured = RunCaptured("sed", {"0,1p"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 4: invalid usage of line address 0\n");
    captured = RunCaptured("sed", {"0,1!p"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 4: invalid usage of line address 0\n");
    captured = RunCaptured("sed", {"0"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 1: invalid usage of line address 0\n");
    // +N and ~N are second addresses only.
    captured = RunCaptured("sed", {"+2p"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 2: invalid usage of +N or ~N as first address\n");

    // ~0 ends the range on the line that started it, as GNU's, and never
    // divides by it; +0 the same. A ~N with N > 0 ends at the next multiple
    // strictly after the starting line, even when that line is one.
    captured = RunCaptured("sed", {"-n", "2,~0p"}, kFour);
    EXPECT_EQ(captured.out, "two\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-n", "/tw/,+0p"}, kFour);
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"-n", "2,~2p"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "2,~4p"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "/on/,~0p;/th/,+0p"}, kFour);
    EXPECT_EQ(captured.out, "one\nthree\n");

    // Options may come after the script operand, as GNU's.
    captured = RunCaptured("sed", {"1~2p", "-n"}, "1\n2\n3\n");
    EXPECT_EQ(captured.out, "1\n3\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedRangeRestarts) {
    // A range that ended is opened again by its first address: a1 is
    // matched again on every line after a range closed, so a later match
    // starts a range of its own. Only 0,/re/ never restarts.
    Captured captured = RunCaptured("sed", {"-n", "/a/,/b/p"}, "a\nb\na\nb\n");
    EXPECT_EQ(captured.out, "a\nb\na\nb\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"/a/,/b/d"}, "a\nb\nc\na\nb\n");
    EXPECT_EQ(captured.out, "c\n");
    captured = RunCaptured("sed", {"-n", "1~3,+1p"}, "1\n2\n3\n4\n5\n6\n");
    EXPECT_EQ(captured.out, "1\n2\n4\n5\n");
    // A one-line range (its second address reached at once) opens again too.
    captured = RunCaptured("sed", {"-n", "/a/,+0p"}, "a\nb\na\nb\n");
    EXPECT_EQ(captured.out, "a\na\n");
    captured = RunCaptured("sed", {"-n", "/a/,~0p"}, "a\nb\na\nb\n");
    EXPECT_EQ(captured.out, "a\na\n");
    captured = RunCaptured("sed", {"-n", "/a/,2p"}, "a\n1\na\n2\na\n3\n");
    EXPECT_EQ(captured.out, "a\n1\na\na\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedContinuedLines) {
    // A backslash before a newline keeps a newline in the text, as GNU's,
    // in the replacement as in the regex.
    Captured captured = RunCaptured("sed", {"s/o/x\\\ny/"}, "one\n");
    EXPECT_EQ(captured.out, "x\nyne\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"s/a/x\\\ny/;s/x\\\ny/Z/"}, "a\nb\n");
    EXPECT_EQ(captured.out, "Z\nb\n");
    EXPECT_EQ(captured.status, 0);
    // In an address regex too: the pattern just holds a newline.
    captured = RunCaptured("sed", {"-n", "/a\\\nb/p"}, "x\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedRegexScanning) {
    // An address regex scans as s's halves do: an escape is one unit and
    // '[' brackets hold their delimiter.
    Captured captured = RunCaptured("sed", {"-n", "/a\\\\/p"}, "a\\\nb\n");
    EXPECT_EQ(captured.out, "a\\\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-n", "/[/]/p"}, "a/b\nc\n");
    EXPECT_EQ(captured.out, "a/b\n");

    // Inside a bracket a [:class:], [.x.] or [=x=] is one unit (its ']'
    // does not close the bracket), and a ']' right after '[' or '[^' is
    // a literal member.
    captured = RunCaptured("sed", {"s/[[:alpha:]/]/x/g"}, "a/\n");
    EXPECT_EQ(captured.out, "xx\n");
    captured = RunCaptured("sed", {"s/[]/]/x/g"}, "a]/\n");
    EXPECT_EQ(captured.out, "axx\n");
    captured = RunCaptured("sed", {"s/[^]]x/y/"}, "ax\n]x\n");
    EXPECT_EQ(captured.out, "y\n]x\n");

    // \d takes one to three digits, \o and \x as GNU's.
    captured = RunCaptured("sed", {"s/a/\\d65/"}, "a\n");
    EXPECT_EQ(captured.out, "A\n");
    captured = RunCaptured("sed", {"s/\\d97/b/"}, "a\n");
    EXPECT_EQ(captured.out, "b\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedBlocksAndSeparators) {
    Captured captured = RunCaptured("sed", {"-n", "2,3{p}"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\n");
    captured = RunCaptured("sed", {"-n", "1,4{2,3p}"}, kFour);
    EXPECT_EQ(captured.out, "two\nthree\n");
    captured = RunCaptured("sed", {"2,3{s/o/0/;d}"}, kFour);
    EXPECT_EQ(captured.out, "one\nfour\n");
    captured = RunCaptured("sed", {"-n", "1{1{1{p}}}"}, kFour);
    EXPECT_EQ(captured.out, "one\n");
    captured = RunCaptured("sed", {"s/o/0/;s/e/2/"}, kTwo);
    EXPECT_EQ(captured.out, "0n2\ntw0\n");

    // A real newline separates commands; ';' after an address is not a command.
    captured = RunCaptured("sed", {"-n", "2p\n# trailing"}, kTwo);
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"-n", "2p\n{d}"}, kTwo);
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"-n", "2;p"}, kTwo);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 2: unknown command: `;'\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("sed", {"-n", "2 ; p"}, kTwo);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 3: unknown command: `;'\n");
    captured = RunCaptured("sed", {"p{"}, kTwo);
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 2: extra characters after command\n");
    EXPECT_EQ(captured.status, 1);

    // A block's commands may carry their own addresses: the q inside stops
    // the run after the second line was printed.
    captured = RunCaptured("sed", {"-n", "2,4{p;2q}"}, "1\n2\n3\n4\n5\n");
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-n", "$!{n;p}"}, "1\n2\n3\n");
    EXPECT_EQ(captured.out, "2\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedLabelsAndComments) {
    // #n as the first two characters of the script makes it quiet.
    Captured captured = RunCaptured("sed", {"#n"}, kTwo);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-e", "#n", "-e", "2p"}, kTwo);
    EXPECT_EQ(captured.out, "two\n");
    captured = RunCaptured("sed", {"#n\n2p"}, kTwo);
    EXPECT_EQ(captured.out, "two\n");

    // A comment to its end of line; a label ends at the next blank too.
    captured = RunCaptured("sed", {"# comment"}, kTwo);
    EXPECT_EQ(captured.out, kTwo);
    captured = RunCaptured("sed", {"-n", "# comment"}, kTwo);
    EXPECT_EQ(captured.out, "");
    captured = RunCaptured("sed", {"-e", "# comment", "-e", "2p"}, kTwo);
    EXPECT_EQ(captured.out, "one\ntwo\ntwo\n");
    captured = RunCaptured("sed", {"-n", "-e", ":top", "-e", "p"}, kTwo);
    EXPECT_EQ(captured.out, kTwo);
    captured = RunCaptured("sed", {"-n", ":a p"}, "one\n");
    EXPECT_EQ(captured.out, "one\n");
    captured = RunCaptured("sed", {"-n", "-e", ":a", "-e", "p;:b"}, "one\n");
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedNext) {
    Captured captured = RunCaptured("sed", {"n"}, kFour);
    EXPECT_EQ(captured.out, kFour);
    captured = RunCaptured("sed", {"-n", "n;p"}, kFour);
    EXPECT_EQ(captured.out, "two\nfour\n");
    captured = RunCaptured("sed", {"2{n;d}"}, kFour);
    EXPECT_EQ(captured.out, "one\ntwo\nfour\n");
    // n at the last line ends the cycle: the last line is printed once.
    captured = RunCaptured("sed", {"n"}, "a\nb");
    EXPECT_EQ(captured.out, "a\nb");
    EXPECT_EQ(captured.status, 0);
    // Nothing after n runs when it read the end: the substitution is skipped.
    captured = RunCaptured("sed", {"n;s/1/X/"}, "1\n");
    EXPECT_EQ(captured.out, "1\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedQuit) {
    Captured captured = RunCaptured("sed", {"q"}, kFour);
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"2q"}, kFour);
    EXPECT_EQ(captured.out, "one\ntwo\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"q7"}, "one\n");
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.status, 7);
    // Q quits without printing the pattern space -- line 2 included.
    captured = RunCaptured("sed", {"2Q"}, kFour);
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"Q3"}, "one\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.status, 3);
    captured = RunCaptured("sed", {"-n", "p;2q"}, kFour);
    EXPECT_EQ(captured.out, "one\ntwo\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-e", "p", "-e", "q"}, "one\n");
    EXPECT_EQ(captured.out, "one\none\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedMissingNewline) {
    // A last line without its newline: written without one, and the newline
    // owed is paid by the next write -- or dropped at the end of the run.
    Captured captured = RunCaptured("sed", {"p"}, "x");
    EXPECT_EQ(captured.out, "x\nx");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-n", "p"}, "x");
    EXPECT_EQ(captured.out, "x");
    captured = RunCaptured("sed", {"p;d"}, "x");
    EXPECT_EQ(captured.out, "x");
    captured = RunCaptured("sed", {"-n", "p;n"}, "x");
    EXPECT_EQ(captured.out, "x");
    // q writes the pattern space terminated by a newline, and flushes the
    // pending one even under -n; Q does neither.
    captured = RunCaptured("sed", {"q"}, "x");
    EXPECT_EQ(captured.out, "x\n");
    captured = RunCaptured("sed", {"-n", "p;q"}, "x");
    EXPECT_EQ(captured.out, "x\n");
    captured = RunCaptured("sed", {"-n", "p;Q"}, "x");
    EXPECT_EQ(captured.out, "x");
    captured = RunCaptured("sed", {"-n", "q"}, "x");
    EXPECT_EQ(captured.out, "");
    captured = RunCaptured("sed", {"$q"}, "a\nb");
    EXPECT_EQ(captured.out, "a\nb\n");
    captured = RunCaptured("sed", {"-n", "p;$q"}, "a\nb");
    EXPECT_EQ(captured.out, "a\nb\n");
    // '=' is always complete with its newline.
    captured = RunCaptured("sed", {"-n", "="}, "x");
    EXPECT_EQ(captured.out, "1\n");
    captured = RunCaptured("sed", {"$="}, "a\nb");
    EXPECT_EQ(captured.out, "a\n2\nb");
    captured = RunCaptured("sed", {"-n", "$="}, "a\nb");
    EXPECT_EQ(captured.out, "2\n");
    captured = RunCaptured("sed", {"$p"}, "a\nb");
    EXPECT_EQ(captured.out, "a\nb\nb");
    captured = RunCaptured("sed", {"$p"}, "x");
    EXPECT_EQ(captured.out, "x\nx");
}

TEST_F(BuiltinCommandsTest, SedSeveralFiles) {
    WriteFile("/f1", kFour);
    WriteFile("/f2", "three\nfour\n");

    // Without -s one continuous stream, with it per file.
    Captured captured = RunCaptured("sed", {"-n", "3p", "/f1", "/f2"});
    EXPECT_EQ(captured.out, "three\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-n", "-s", "2,3p", "/f1", "/f2"});
    EXPECT_EQ(captured.out, "two\nthree\nfour\n");
    captured = RunCaptured("sed", {"-n", "-s", "$=", "/f1", "/f2"});
    EXPECT_EQ(captured.out, "4\n2\n");
    captured = RunCaptured("sed", {"-n", "-s", "1=", "/f1", "/f2"});
    EXPECT_EQ(captured.out, "1\n1\n");

    // The '$' lookahead crosses into the next file, quietly going past a
    // directory; a missing one is reported and costs status 2.
    captured = RunCaptured("sed", {"-n", "$p", "/f1", "/docs", "/f2"});
    EXPECT_EQ(captured.out, "four\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"$p", "/f1", "/docs", "/f2"});
    EXPECT_EQ(captured.out, "one\ntwo\nthree\nfour\nthree\nfour\nfour\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"$=", "/f1", "/docs", "/f2"});
    EXPECT_EQ(captured.out, "one\ntwo\nthree\nfour\nthree\n6\nfour\n");
    captured = RunCaptured("sed", {"-n", "$p", "/missing", "/f1"});
    EXPECT_EQ(captured.out, "four\n");
    EXPECT_EQ(captured.err, "sed: can't read /missing: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    // A missing file reported, gone past, status 2; a read one is fatal: 4.
    captured = RunCaptured("sed", {"-n", "p", "/missing", "/f1"});
    EXPECT_EQ(captured.out, kFour);
    EXPECT_EQ(captured.err, "sed: can't read /missing: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("sed", {"p", "/missing", "/f1", "/f2"});
    EXPECT_EQ(captured.out, "one\none\ntwo\ntwo\nthree\nthree\nfour\nfour\n"
                            "three\nthree\nfour\nfour\n");
    EXPECT_EQ(captured.status, 2);
    captured = RunCaptured("sed", {"p", "/docs"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "sed: read error on /docs: Is a directory\n");
    EXPECT_EQ(captured.status, 4);
    captured = RunCaptured("sed", {"-n", "2p", "/f1", "/docs", "/f2"});
    EXPECT_EQ(captured.out, "two\n");
    EXPECT_EQ(captured.err, "sed: read error on /docs: Is a directory\n");
    EXPECT_EQ(captured.status, 4);

    // A read error's status 2 wins over a later q's code; q's over 0.
    captured = RunCaptured("sed", {"q0", "/missing", "/f1"});
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.err, "sed: can't read /missing: No such file or directory\n");
    EXPECT_EQ(captured.status, 2);

    // '-' is the standard input.
    captured = RunCaptured("sed", {"p", "-"}, "one\n");
    EXPECT_EQ(captured.out, "one\none\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedScriptFile) {
    WriteFile("/quiet", "#n\np\n");
    WriteFile("/brace", "2p\n{\n");
    WriteFile("/noregex", "2p\n//p\n");
    WriteFile("/mixed", "s/o/0/\n/tw/d\n");
    WriteFile("/badcmd", "p\nk\n");

    // #n at the start of the first script file makes it quiet too.
    Captured captured = RunCaptured("sed", {"-f", "/quiet"}, kTwo);
    EXPECT_EQ(captured.out, kTwo);
    EXPECT_EQ(captured.status, 0);
    // A directory reads as an empty script.
    captured = RunCaptured("sed", {"-f", "/docs"}, "one\n");
    EXPECT_EQ(captured.out, "one\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-f", "/mixed"}, kTwo);
    EXPECT_EQ(captured.out, "0ne\n");
    EXPECT_EQ(captured.status, 0);

    // Pieces of a -f file report by line number.
    captured = RunCaptured("sed", {"-f", "/brace"}, kTwo);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "sed: file /brace line 2: unmatched `{'\n");
    EXPECT_EQ(captured.status, 1);
    // A bad command on the file's second line, reported there.
    captured = RunCaptured("sed", {"-f", "/badcmd"}, kTwo);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "sed: file /badcmd line 2: unknown command: `k'\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("sed", {"-f", "/nosuch"});
    EXPECT_EQ(captured.err, "sed: couldn't open file /nosuch: No such file or directory\n");
    EXPECT_EQ(captured.status, 4);
    // The error fires on the first cycle, before anything is printed, and is
    // reported at the script's end (GNU's position for a missing regex).
    captured = RunCaptured("sed", {"-f", "/noregex"}, "one\ntwo\nthree\n");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "sed: file /noregex line 3: no previous regular expression\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, SedLastRegex) {
    // An empty regex reuses the last one, in the address as in s.
    Captured captured = RunCaptured("sed", {"s/a/A/;//p"}, "aa\nab\n");
    EXPECT_EQ(captured.out, "Aa\nAa\nAb\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-e", "s/o/0/", "-e", "//p"}, kFour);
    EXPECT_EQ(captured.out, "0ne\ntw0\nthree\nf0ur\n");
    EXPECT_EQ(captured.status, 0);

    // Without one before it, the run reports it at the script's end.
    captured = RunCaptured("sed", {"-n", "//p"}, "x");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 0: no previous regular expression\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("sed", {"-n", "s//x/"}, "x");
    EXPECT_EQ(captured.err, "sed: -e expression #1, char 0: no previous regular expression\n");
    EXPECT_EQ(captured.status, 1);
    captured = RunCaptured("sed", {"-n", "-e", "2p", "-e", "//p"}, "x");
    EXPECT_EQ(captured.err, "sed: -e expression #2, char 0: no previous regular expression\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, SedEmptyScript) {
    // An explicit empty script is valid: no commands, auto-printing alone.
    Captured captured = RunCaptured("sed", {""}, kFour);
    EXPECT_EQ(captured.out, kFour);
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedScriptErrors) {
    // A piece's own errors name its number and the revealing character's
    // position (1 past it, clamped to the piece's length).
    const std::vector<std::vector<std::string>> cases = {
        {"unknown"},
        {"s/o/0"},
        {"s"},
        {"s/a/b/gg"},
        {"s/a/b/0"},
        {"s/a/b/zz"},
        {"s/a/b/\\"},
        {"1,2,3p"},
        {"q1q"},
        {"}"},
        {"{"},
        {"{2p"},
        {"s/x/\\1/"},
        {"s/o/0/w"},
        {"p,"},
        {"1,2q"},
        {":"},
        {"p!"},
        {"1!"},
        {"2,3"},
        {"s,/x,"},
        {"\\$p"},
        {"s/a"},
        {"s/[/x/"},
        {"s/[[:alpha:]/x/"},
        {"p;k"},
        {"{p"},
        {"p}"},
        {"s/a/b/q"},
        {"s/a/b/pp"},
        {"s/\\(a/b/"},
        {"0,5p"},
        {"1!!p"},
        {"p x"},
        {"/x"},
        {"\\%x"},
        {"{p;1}"},
        {"1}"},
        {"1,2}"},
    };
    const std::vector<std::string> errors = {
        "sed: -e expression #1, char 1: unknown command: `u'\n",
        "sed: -e expression #1, char 5: unterminated `s' command\n",
        "sed: -e expression #1, char 1: unterminated `s' command\n",
        "sed: -e expression #1, char 8: multiple `g' options to `s' command\n",
        "sed: -e expression #1, char 7: number option to `s' command may not be zero\n",
        "sed: -e expression #1, char 7: unknown option to `s'\n",
        "sed: -e expression #1, char 7: unknown option to `s'\n",
        "sed: -e expression #1, char 4: unknown command: `,'\n",
        "sed: -e expression #1, char 3: extra characters after command\n",
        "sed: -e expression #1, char 1: unexpected `}'\n",
        "sed: -e expression #1, char 0: unmatched `{'\n",
        "sed: -e expression #1, char 0: unmatched `{'\n",
        "sed: -e expression #1, char 7: invalid reference \\1 on `s' command's RHS\n",
        "sed: -e expression #1, char 7: missing filename in r/R/w/W commands\n",
        "sed: -e expression #1, char 2: extra characters after command\n",
        "sed: -e expression #1, char 4: command only uses one address\n",
        "sed: -e expression #1, char 1: \":\" lacks a label\n",
        "sed: -e expression #1, char 2: extra characters after command\n",
        "sed: -e expression #1, char 2: missing command\n",
        "sed: -e expression #1, char 3: missing command\n",
        "sed: -e expression #1, char 5: unterminated `s' command\n",
        "sed: -e expression #1, char 3: unterminated address regex\n",
        "sed: -e expression #1, char 3: unterminated `s' command\n",
        "sed: -e expression #1, char 6: unterminated `s' command\n",
        "sed: -e expression #1, char 15: unterminated `s' command\n",
        "sed: -e expression #1, char 3: unknown command: `k'\n",
        "sed: -e expression #1, char 0: unmatched `{'\n",
        "sed: -e expression #1, char 2: unexpected `}'\n",
        "sed: -e expression #1, char 7: unknown option to `s'\n",
        "sed: -e expression #1, char 8: multiple `p' options to `s' command\n",
        "sed: -e expression #1, char 8: Unmatched ( or \\(\n",
        "sed: -e expression #1, char 4: invalid usage of line address 0\n",
        "sed: -e expression #1, char 3: multiple `!'s\n",
        "sed: -e expression #1, char 3: extra characters after command\n",
        "sed: -e expression #1, char 2: unterminated address regex\n",
        "sed: -e expression #1, char 3: unterminated address regex\n",
        "sed: -e expression #1, char 5: `}' doesn't want any addresses\n",
        "sed: -e expression #1, char 2: unexpected `}'\n",
        "sed: -e expression #1, char 4: unexpected `}'\n",
    };
    for (size_t i = 0; i < cases.size(); ++i) {
        Captured captured = RunCaptured("sed", cases[i], kFour);
        EXPECT_EQ(captured.err, errors[i]) << "case " << i;
        EXPECT_EQ(captured.status, 1) << "case " << i;
    }

    // No script and no input file: the help text on stderr, status 1.
    Captured captured = RunCaptured("sed", {});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, BuiltinHelpText(*CreateSedCommand()));
    EXPECT_EQ(captured.status, 1);

    // A second -e piece numbers its own errors.
    captured = RunCaptured("sed", {"-e", "p", "-e", "unknown"}, kFour);
    EXPECT_EQ(captured.err, "sed: -e expression #2, char 1: unknown command: `u'\n");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(BuiltinCommandsTest, SedNotTreatedOptions) {
    // The options of the advanced tasks are accepted and reported.
    Captured captured = RunCaptured("sed", {"--posix", "p"}, "one\n");
    EXPECT_EQ(captured.out, "one\none\n");
    EXPECT_EQ(captured.err, "Parameter --posix is not treated by HaisosOS sed v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sed", {"-i", "s/o/0/"}, "one\n");
    EXPECT_EQ(captured.out, "0ne\n");
    EXPECT_EQ(captured.err, "Parameter -i is not treated by HaisosOS sed v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"-i.sfx", "s/o/0/"}, "one\n");
    EXPECT_EQ(captured.out, "0ne\n");
    EXPECT_EQ(captured.err, "Parameter -i is not treated by HaisosOS sed v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sed", {"s/o/0/e"}, "one\n");
    EXPECT_EQ(captured.out, "0ne\n");
    EXPECT_EQ(captured.err, "Parameter s///e is not treated by HaisosOS sed v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"s/o/0/w /out"}, "one\n");
    EXPECT_EQ(captured.out, "0ne\n");
    EXPECT_EQ(captured.err, "Parameter s///w is not treated by HaisosOS sed v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);

    captured = RunCaptured("sed", {"-z", "-u", "p"}, "one\n");
    EXPECT_EQ(captured.out, "one\none\n");
    EXPECT_EQ(captured.err,
        "Parameter -z is not treated by HaisosOS sed v. 1.0.0\n"
        "Parameter -u is not treated by HaisosOS sed v. 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedHelpAndVersion) {
    Captured captured = RunCaptured("sed", {"--version"});
    EXPECT_EQ(captured.out, "sed (HaisosOS builtin) 1.0.0\n");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("sed", {"--help"});
    EXPECT_EQ(captured.out, BuiltinHelpText(*CreateSedCommand()));
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, SedIsStoppedPromptly) {
    // Input large enough that the run cannot finish before the stop lands.
    std::string big;
    for (int i = 0; i < 20000; ++i) {
        big += "line\n";
    }
    WriteFile("/big", big);
    StartProcessOptions options;
    options.stdOut = streams->OpenFile("/out", kFileOpenWriteCreateTruncate, kFileCreateMode);
    options.stdErr = streams->OpenFile("/err", kFileOpenWriteCreateTruncate, kFileCreateMode);
    ASSERT_NE(options.stdOut, nullptr);
    ASSERT_NE(options.stdErr, nullptr);
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/sed",
        {"p", "/big"}, "/", options);
    ASSERT_NE(process, nullptr);
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(process->ExitCode().value(), 143);
}