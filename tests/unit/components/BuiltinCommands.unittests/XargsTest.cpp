// xargs against GNU findutils 4.9.0's own output and messages in the C
// locale: the item grammar (quotes, backslashes, NULs, delimiters), the
// grouping modes (-n, -L, -I) and their mutual-exclusion warnings, the -s
// size rules with and without -x, and the exit codes GNU makes of the ways a
// child can end. Child programs run through PATH, so each run takes an
// environment holding the builtins in /bin.
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinCommandList.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

// A PATH with the builtins in it: the environment each run with child
// programs takes, so echo and friends are found.
std::shared_ptr<IEnvironment> PathEnvironment(const std::shared_ptr<IFactory>& factory) {
    auto environment = factory->CreateEnvironment();
    environment->SetVariable("PATH", "/bin");
    return environment;
}

} // namespace

TEST_F(BuiltinCommandsTest, XargsDefaultsToEcho) {
    const auto env = PathEnvironment(factory);
    // No command: GNU's own echo, and the items are its arguments.
    const auto plain = RunCaptured("xargs", {}, "a b\n", "/", env);
    EXPECT_EQ(plain.out, "a b\n");
    EXPECT_EQ(plain.err, "");
    EXPECT_EQ(plain.status, 0);
    // The grammar: quotes protect (no escapes inside), a backslash takes the
    // next byte literally, and blanks separate.
    const auto quoted = RunCaptured("xargs", {}, "a 'b c' d\n", "/", env);
    EXPECT_EQ(quoted.out, "a b c d\n");
    EXPECT_EQ(quoted.status, 0);
    const auto escaped = RunCaptured("xargs", {"-n", "1", "echo", "[]"},
                                     "a\\ b\na b\n", "/", env);
    EXPECT_EQ(escaped.out, "[] a b\n[] a\n[] b\n");
    EXPECT_EQ(escaped.status, 0);
    // Quotes in the middle of a token are removed; '' is an empty item.
    const auto glued = RunCaptured("xargs", {"-n", "1", "echo", "[]"},
                                   "a\"b\"c\n''\n", "/", env);
    EXPECT_EQ(glued.out, "[] abc\n[] \n");
    // Empty input runs the bare command once -- the default echo -- and -r
    // says not to.
    const auto bare = RunCaptured("xargs", {"echo", "X"}, "", "/", env);
    EXPECT_EQ(bare.out, "X\n");
    EXPECT_EQ(bare.status, 0);
    const auto none = RunCaptured("xargs", {"-r", "echo", "X"}, "", "/", env);
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.err, "");
    EXPECT_EQ(none.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsUnmatchedQuote) {
    const auto env = PathEnvironment(factory);
    // A quote left open at the line's end is GNU's error, after the items
    // the line had completed -- which still run.
    const auto single = RunCaptured("xargs", {"-n", "2", "echo", "X"},
                                   "a b 'c\n", "/", env);
    EXPECT_EQ(single.out, "X a b\n");
    EXPECT_EQ(single.err,
        "xargs: unmatched single quote; by default quotes are special to xargs "
        "unless you use the -0 option\n");
    EXPECT_EQ(single.status, 1);
    const auto dbl = RunCaptured("xargs", {"echo", "X"}, "a \"b\n", "/", env);
    EXPECT_EQ(dbl.out, "X a\n");
    EXPECT_EQ(dbl.err,
        "xargs: unmatched double quote; by default quotes are special to xargs "
        "unless you use the -0 option\n");
    EXPECT_EQ(dbl.status, 1);
}

TEST_F(BuiltinCommandsTest, XargsNullAndDelimiter) {
    const auto env = PathEnvironment(factory);
    // -0: an item is everything up to a NUL, blanks inside included; the
    // undelimited tail is an item too when it is not empty.
    const auto nul = RunCaptured("xargs", {"-0", "echo", "X"}, std::string("a b\0c\0", 6),
                                 "/", env);
    EXPECT_EQ(nul.out, "X a b c\n");
    EXPECT_EQ(nul.status, 0);
    // Every delimited item is kept, even an empty one.
    const auto empties = RunCaptured("xargs", {"-0", "-n", "1", "echo", "X"},
                                     std::string("a\0\0b\0", 5), "/", env);
    EXPECT_EQ(empties.out, "X a\nX \nX b\n");
    // -d takes one character or one escape; -L counts each item as a line.
    const auto comma = RunCaptured("xargs", {"-d", ",", "echo", "X"}, "a,b,c", "/", env);
    EXPECT_EQ(comma.out, "X a b c\n");
    const auto escape = RunCaptured("xargs", {"-d", "\\n", "echo", "X"}, "a\nb", "/", env);
    EXPECT_EQ(escape.out, "X a b\n");
    const auto lines = RunCaptured("xargs", {"-d", ",", "-L", "2", "echo", "X"},
                                   "a,b,c,d,", "/", env);
    EXPECT_EQ(lines.out, "X a b\nX c d\n");
    // A NUL in the quote modes is the warning, the pending word becoming an
    // item (an empty one when none was started), and the end of the input --
    // where the once-at-least bare run happens even under -r.
    const auto warned = RunCaptured("xargs", {"echo", "X"}, std::string("a \0", 3), "/", env);
    EXPECT_EQ(warned.out, "X a \n");
    EXPECT_EQ(warned.err,
        "xargs: WARNING: a NUL character occurred in the input.  It cannot be "
        "passed through in the argument list.  Did you mean to use the "
        "--null option?\n");
    EXPECT_EQ(warned.status, 0);
    // The delimiter specifications GNU refuses.
    const auto twoChars = RunCaptured("xargs", {"-d", "ab", "echo"}, "", "/", env);
    EXPECT_EQ(twoChars.err,
        "xargs: Invalid input delimiter specification ab: the delimiter must be "
        "either a single character or an escape sequence starting with \\.\n");
    EXPECT_EQ(twoChars.status, 1);
    const auto badEscape = RunCaptured("xargs", {"-d", "\\z", "echo"}, "", "/", env);
    EXPECT_EQ(badEscape.err,
        "xargs: Invalid escape sequence \\z in input delimiter specification.\n");
    EXPECT_EQ(badEscape.status, 1);
    // -E says nothing in these modes, and is told so.
    const auto eof = RunCaptured("xargs", {"-0", "-E", "_", "echo", "X"},
                                 std::string("a\0", 2), "/", env);
    EXPECT_EQ(eof.err,
        "xargs: warning: the -E option has no effect if -0 or -d is used.\n");
    EXPECT_EQ(eof.out, "X a\n");
    EXPECT_EQ(eof.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsLinesAndReplace) {
    const auto env = PathEnvironment(factory);
    // -L continues a line that ends in a blank; empty lines separate and are
    // not counted.
    const auto continued = RunCaptured("xargs", {"-L", "1", "echo", "X"},
                                       "a \nb\nc\n", "/", env);
    EXPECT_EQ(continued.out, "X a b\nX c\n");
    EXPECT_EQ(continued.err, "");
    EXPECT_EQ(continued.status, 0);
    const auto blanks = RunCaptured("xargs", {"-L", "1", "echo", "X"},
                                   "a\n\n\nb\n", "/", env);
    EXPECT_EQ(blanks.out, "X a\nX b\n");
    // -l defaults to one line; its count, when given, is attached.
    const auto shortL = RunCaptured("xargs", {"-l", "echo", "X"}, "a b\nc\n", "/", env);
    EXPECT_EQ(shortL.out, "X a b\nX c\n");
    // The mutual exclusions, in the order the options came.
    const auto argsOverLines = RunCaptured("xargs", {"-L", "2", "-n", "1", "echo", "X"},
                                           "a\n", "/", env);
    EXPECT_EQ(argsOverLines.err,
        "xargs: warning: options --max-lines and --max-args/-n are mutually "
        "exclusive, ignoring previous --max-lines value\n");
    EXPECT_EQ(argsOverLines.out, "X a\n");
    EXPECT_EQ(argsOverLines.status, 0);
    const auto linesOverReplace = RunCaptured("xargs", {"-I{}", "-L", "2", "echo", "X"},
                                              "a\nb\n", "/", env);
    EXPECT_EQ(linesOverReplace.err,
        "xargs: warning: options --replace and -L are mutually exclusive, "
        "ignoring previous --replace value\n");
    EXPECT_EQ(linesOverReplace.out, "X a b\n");
    // -I: one item per line, leading blanks trimmed and the rest kept; R is
    // replaced everywhere in the initial arguments and nowhere else; -i
    // defaults R to {}.
    const auto replaced = RunCaptured("xargs", {"-I{}", "echo", "pre-{}-post"},
                                      "x\n  a  b \n\n c\n", "/", env);
    EXPECT_EQ(replaced.out, "pre-x-post\npre-a  b -post\npre-c-post\n");
    EXPECT_EQ(replaced.status, 0);
    const auto defaultR = RunCaptured("xargs", {"-i", "echo", "X{}Y"}, "a\n", "/", env);
    EXPECT_EQ(defaultR.out, "XaY\n");
    // An item with no R anywhere to go is dropped, but the command still
    // runs.
    const auto lost = RunCaptured("xargs", {"-I{}", "echo", "X"}, "a\nb\n", "/", env);
    EXPECT_EQ(lost.out, "X\nX\n");
    EXPECT_EQ(lost.status, 0);
    // Quotes and backslashes are processed in -I mode too, but blanks inside
    // the item are kept.
    const auto quoted = RunCaptured("xargs", {"-I{}", "echo", "[{}]"},
                                    "'a b'\na\\ b\n", "/", env);
    EXPECT_EQ(quoted.out, "[a b]\n[a b]\n");
    // -n 1 while -I is active says nothing new: GNU drops it without the
    // warning, and -I stays.
    const auto quiet = RunCaptured("xargs", {"-I{}", "-n", "1", "echo", "X"},
                                   "a\nb\n", "/", env);
    EXPECT_EQ(quiet.err, "");
    EXPECT_EQ(quiet.out, "X\nX\n");
    EXPECT_EQ(quiet.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsEofString) {
    const auto env = PathEnvironment(factory);
    // The EOF string ends the input where it appears as an item, quoted or
    // not; what came before still runs, and nothing else does.
    const auto ended = RunCaptured("xargs", {"-E", "_", "echo", "X"}, "a\n_\nb\n", "/", env);
    EXPECT_EQ(ended.out, "X a\n");
    EXPECT_EQ(ended.status, 0);
    const auto quoted = RunCaptured("xargs", {"-E", "_", "echo", "X"}, "'_'\n", "/", env);
    EXPECT_EQ(quoted.out, "X\n");
    EXPECT_EQ(quoted.status, 0);
    // -e alone clears the string; -E after it sets one again.
    const auto cleared = RunCaptured("xargs", {"-e", "echo", "X"}, "a\n_\nb\n", "/", env);
    EXPECT_EQ(cleared.out, "X a _ b\n");
    const auto reset = RunCaptured("xargs", {"-e", "-E", "_", "echo", "X"},
                                   "a\n_\nb\n", "/", env);
    EXPECT_EQ(reset.out, "X a\n");
    // -L mode honors it too.
    const auto lines = RunCaptured("xargs", {"-L", "5", "-E", "_", "echo", "X"},
                                   "a\n_\nb\n", "/", env);
    EXPECT_EQ(lines.out, "X a\n");
}

TEST_F(BuiltinCommandsTest, XargsSizes) {
    const auto env = PathEnvironment(factory);
    // echo X is 7 bytes with its nulls; -s counts every argument and the
    // command, one byte each for the null.
    // A line that cannot hold one more item is split; one that cannot hold
    // the item at all is the error, and what is already on it still runs.
    const auto split = RunCaptured("xargs", {"-s", "9", "echo", "X"}, "a\nb\nc\n", "/", env);
    EXPECT_EQ(split.out, "X a\nX b\nX c\n");
    EXPECT_EQ(split.status, 0);
    const auto together = RunCaptured("xargs", {"-s", "11", "echo", "X"}, "a\nb\n", "/", env);
    EXPECT_EQ(together.out, "X a b\n");
    const auto tooLong = RunCaptured("xargs", {"-s", "8", "echo", "X"}, "aaaaa\n", "/", env);
    EXPECT_EQ(tooLong.out, "");
    EXPECT_EQ(tooLong.err, "xargs: argument line too long\n");
    EXPECT_EQ(tooLong.status, 1);
    const auto pending = RunCaptured("xargs", {"-s", "9", "echo", "X"},
                                     "a\nb\ncccc\n", "/", env);
    EXPECT_EQ(pending.out, "X a\nX b\n");
    EXPECT_EQ(pending.err, "xargs: argument line too long\n");
    EXPECT_EQ(pending.status, 1);
    // -x forbids splitting an -n batch; a -L line can never be split. Either
    // way nothing runs.
    const auto exitFull = RunCaptured("xargs", {"-x", "-n", "3", "-s", "10", "echo", "X"},
                                      "a\nb\nc\nd\n", "/", env);
    EXPECT_EQ(exitFull.out, "");
    EXPECT_EQ(exitFull.err, "xargs: argument list too long\n");
    EXPECT_EQ(exitFull.status, 1);
    const auto splitBatch = RunCaptured("xargs", {"-n", "3", "-s", "10", "echo", "X"},
                                        "a\nb\nc\nd\n", "/", env);
    EXPECT_EQ(splitBatch.out, "X a\nX b\nX c\nX d\n");
    EXPECT_EQ(splitBatch.status, 0);
    const auto unbreakable = RunCaptured("xargs", {"-L", "2", "-s", "10", "echo", "X"},
                                         "a\nb\nc\nd\n", "/", env);
    EXPECT_EQ(unbreakable.out, "");
    EXPECT_EQ(unbreakable.err, "xargs: argument list too long\n");
    EXPECT_EQ(unbreakable.status, 1);
    // The command with its initial arguments must fit before anything is
    // read -- under -r too, but never in -I mode, whose own ladder says it.
    const auto base = RunCaptured("xargs", {"-s", "6", "echo", "X"}, "", "/", env);
    EXPECT_EQ(base.out, "");
    EXPECT_EQ(base.err, "xargs: cannot fit single argument within argument list size limit\n");
    EXPECT_EQ(base.status, 1);
    const auto baseWithR = RunCaptured("xargs", {"-r", "-s", "6", "echo", "X"}, "", "/", env);
    EXPECT_EQ(baseWithR.err, "xargs: cannot fit single argument within argument list size limit\n");
    EXPECT_EQ(baseWithR.status, 1);
    // -I's own ladder, in order: the item, the command, the longest replaced
    // argument, then the line as it will run.
    const auto itemTooLong = RunCaptured("xargs", {"-I{}", "-s", "10", "echo", "X"},
                                         "xxxxxxxxxx\n", "/", env);
    EXPECT_EQ(itemTooLong.err, "xargs: argument line too long\n");
    EXPECT_EQ(itemTooLong.status, 1);
    const auto cmdTooLong = RunCaptured("xargs", {"-I{}", "-s", "4", "echo", "XX"},
                                        "a\n", "/", env);
    EXPECT_EQ(cmdTooLong.err,
        "xargs: cannot fit single argument within argument list size limit\n");
    EXPECT_EQ(cmdTooLong.status, 1);
    const auto argTooLong = RunCaptured("xargs", {"-I{}", "-s", "5", "echo", "XXXX"},
                                        "a\n", "/", env);
    EXPECT_EQ(argTooLong.err, "xargs: command too long\n");
    EXPECT_EQ(argTooLong.status, 1);
    const auto listTooLong = RunCaptured("xargs", {"-I{}", "-s", "9", "echo", "X", "{}", "Y"},
                                         "aa\n", "/", env);
    EXPECT_EQ(listTooLong.err, "xargs: argument list too long\n");
    EXPECT_EQ(listTooLong.status, 1);
    // A -s below 1 is warned of, taken as 1, and carried on from; one above
    // the upper limit is warned of with its own text and clamped.
    const auto zero = RunCaptured("xargs", {"-s", "0", "echo", "X"}, "", "/", env);
    EXPECT_EQ(zero.err,
        "xargs: value 0 for -s option should be >= 1\n"
        "xargs: cannot fit single argument within argument list size limit\n");
    EXPECT_EQ(zero.status, 1);
    const auto huge = RunCaptured("xargs", {"-s", "99999999", "echo", "X"}, "a\n",
                                  "/", factory->CreateEnvironment());
    EXPECT_EQ(huge.err, "xargs: value 99999999 for -s option should be <= 2095104\n");
    EXPECT_EQ(huge.out, "X a\n");
    EXPECT_EQ(huge.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsExitCodes) {
    MakeFiveNames();
    const auto env = PathEnvironment(factory);
    // Any child failing with 1 to 125 makes the run 123, whatever else ran.
    const auto failed = RunCaptured("xargs", {"-n", "1", "false"}, "a\nb\n", "/", env);
    EXPECT_EQ(failed.out, "");
    EXPECT_EQ(failed.status, 123);
    const auto thenSucceeded = RunCaptured("xargs", {"-n", "1", "hsh", "-c",
        "test \"$1\" = a && exit 3; exit 0", "_"}, "a\nb\n", "/", env);
    EXPECT_EQ(thenSucceeded.status, 123);
    // 255 aborts; a code 129-254 is taken as a signal death, as GNU takes
    // WIFSIGNALED -- either way the message is printed once and no further
    // command runs.
    const auto aborted = RunCaptured("xargs", {"-n", "1", "hsh", "-c", "exit 255"},
                                     "a\nb\n", "/", env);
    EXPECT_EQ(aborted.err, "xargs: hsh: exited with status 255; aborting\n");
    EXPECT_EQ(aborted.status, 124);
    const auto signal = RunCaptured("xargs", {"-n", "1", "hsh", "-c", "exit 143"},
                                    "a\nb\n", "/", env);
    EXPECT_EQ(signal.err, "xargs: hsh: terminated by signal 15\n");
    EXPECT_EQ(signal.status, 125);
    // A program that cannot be started is the shell's 126 and 127, at once.
    const auto denied = RunCaptured("xargs", {"/five/cat"}, "a\n", "/", env);
    EXPECT_EQ(denied.err, "xargs: /five/cat: Permission denied\n");
    EXPECT_EQ(denied.status, 126);
    const auto missing = RunCaptured("xargs", {"nosuchcmd"}, "a\n", "/", env);
    EXPECT_EQ(missing.err, "xargs: nosuchcmd: No such file or directory\n");
    EXPECT_EQ(missing.status, 127);
}

TEST_F(BuiltinCommandsTest, XargsVerboseAndEmpty) {
    const auto env = PathEnvironment(factory);
    // -t prints the line as a shell would take it, to stderr, before running.
    const auto traced = RunCaptured("xargs", {"-t", "echo", "X"}, "a b\n", "/", env);
    EXPECT_EQ(traced.err, "echo X a b\n");
    EXPECT_EQ(traced.out, "X a b\n");
    EXPECT_EQ(traced.status, 0);
    // Words needing quotes are quoted; control bytes become $'...' escapes.
    const auto quoted = RunCaptured("xargs", {"-t", "-n", "1", "echo", "[]"},
                                    "'a b'\n", "/", env);
    EXPECT_EQ(quoted.err, "echo '[]' 'a b'\n");
    EXPECT_EQ(quoted.out, "[] a b\n");
    const auto escapes = RunCaptured("xargs", {"-t", "-n", "1", "echo", "[]"},
                                     "x\001y\n", "/", env);
    EXPECT_EQ(escapes.err, "echo '[]' 'x'$'\\001''y'\n");
    EXPECT_EQ(escapes.out, "[] x\001y\n");
    // The once-at-least bare run is traced too; -r keeps even that from
    // running.
    const auto bare = RunCaptured("xargs", {"-t", "echo", "X"}, "", "/", env);
    EXPECT_EQ(bare.err, "echo X\n");
    EXPECT_EQ(bare.out, "X\n");
    const auto none = RunCaptured("xargs", {"-r", "-t", "echo", "X"}, "", "/", env);
    EXPECT_EQ(none.out, "");
    EXPECT_EQ(none.err, "");
    EXPECT_EQ(none.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsPromptWithoutTerminal) {
    const auto env = PathEnvironment(factory);
    // -p prints the line and asks on /dev/tty; Haisos has none, so the ask
    // always fails as GNU's does without one, and nothing runs.
    const auto prompted = RunCaptured("xargs", {"-p", "echo", "X"}, "a\n", "/", env);
    EXPECT_EQ(prompted.out, "");
    EXPECT_EQ(prompted.err,
        "echo X axargs: failed to open /dev/tty for reading: No such device or address\n");
    EXPECT_EQ(prompted.status, 1);
    // -o opens /dev/tty for each command it runs; with none, the first
    // command fails -- and with -r and nothing to run, none is tried.
    const auto openTty = RunCaptured("xargs", {"-o", "echo", "X"}, "a\n", "/", env);
    EXPECT_EQ(openTty.out, "");
    EXPECT_EQ(openTty.err, "xargs: '/dev/tty': No such device or address\n");
    EXPECT_EQ(openTty.status, 1);
    const auto quiet = RunCaptured("xargs", {"-o", "-r", "echo", "X"}, "", "/", env);
    EXPECT_EQ(quiet.out, "");
    EXPECT_EQ(quiet.err, "");
    EXPECT_EQ(quiet.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsArgFileAndSlotVar) {
    WriteFile("/items.txt", "a\nb\n");
    const auto env = PathEnvironment(factory);
    // -a reads its items from the file instead of standard input.
    const auto fromFile = RunCaptured("xargs", {"-a", "/items.txt", "echo", "X"},
                                      std::nullopt, "/", env);
    EXPECT_EQ(fromFile.out, "X a b\n");
    EXPECT_EQ(fromFile.status, 0);
    // A file that is not there is the error; a directory opens and reads
    // nothing, as GNU's own open of one does, so the bare command runs.
    const auto missing = RunCaptured("xargs", {"-a", "/nothere", "echo", "X"},
                                     std::nullopt, "/", env);
    EXPECT_EQ(missing.err,
        "xargs: Cannot open input file '/nothere': No such file or directory\n");
    EXPECT_EQ(missing.status, 1);
    const auto directory = RunCaptured("xargs", {"-a", "/docs", "echo", "X"},
                                       std::nullopt, "/", env);
    EXPECT_EQ(directory.out, "X\n");
    EXPECT_EQ(directory.err, "");
    EXPECT_EQ(directory.status, 0);
    // --process-slot-var numbers the commands 0, 1, ... in the child's
    // environment.
    const auto slots = RunCaptured("xargs", {"--process-slot-var=SLOT", "-n", "1",
                                   "hsh", "-c", "echo $SLOT"}, "a\nb\n", "/", env);
    EXPECT_EQ(slots.out, "0\n1\n");
    EXPECT_EQ(slots.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsChildGetsEmptyInput) {
    const auto env = PathEnvironment(factory);
    // Each child reads an input at its end at once -- never xargs's own --
    // so cat finds nothing and the script's own output is all that shows.
    const auto captured = RunCaptured("xargs", {"hsh", "-c", "cat; echo done"},
                                      "a\n", "/", env);
    EXPECT_EQ(captured.out, "done\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, XargsShowLimitsAndValidation) {
    const auto env = PathEnvironment(factory);
    // The limits out of a fixed ARG_MAX less 2048 and the environment's own
    // bytes -- here none, as the environment is empty; printed, then xargs
    // carries on.
    const auto limits = RunCaptured("xargs", {"--show-limits", "echo"}, "",
                                    "/", factory->CreateEnvironment());
    EXPECT_EQ(limits.err,
        "Your environment variables take up 0 bytes\n"
        "POSIX upper limit on argument length (this system): 2095104\n"
        "POSIX smallest allowable upper limit on argument length (all systems): 4096\n"
        "Maximum length of command we could actually use: 2095104\n"
        "Size of command buffer we are actually using: 131072\n"
        "Maximum parallelism (--max-procs must be no greater): 2147483647\n");
    EXPECT_EQ(limits.out, "\n");
    EXPECT_EQ(limits.status, 0);
    // The counting options, each under its short name however spelled.
    const auto zeroArgs = RunCaptured("xargs", {"-n", "0", "echo"}, "", "/", env);
    EXPECT_EQ(zeroArgs.err,
        "xargs: value 0 for -n option should be >= 1\n"
        "Try 'xargs --help' for more information.\n");
    EXPECT_EQ(zeroArgs.status, 1);
    const auto longZeroArgs = RunCaptured("xargs", {"--max-args", "0", "echo"}, "", "/", env);
    EXPECT_EQ(longZeroArgs.err,
        "xargs: value 0 for -n option should be >= 1\n"
        "Try 'xargs --help' for more information.\n");
    const auto longZeroLines = RunCaptured("xargs", {"--max-lines=0", "echo"}, "", "/", env);
    EXPECT_EQ(longZeroLines.err,
        "xargs: value 0 for -l option should be >= 1\n"
        "Try 'xargs --help' for more information.\n");
    EXPECT_EQ(longZeroLines.status, 1);
    const auto junk = RunCaptured("xargs", {"-P", "x", "echo"}, "", "/", env);
    EXPECT_EQ(junk.err,
        "xargs: invalid number \"x\" for -P option\n"
        "Try 'xargs --help' for more information.\n");
    EXPECT_EQ(junk.status, 1);
    // -P is checked and then not acted on (one command at a time).
    const auto procs = RunCaptured("xargs", {"-P", "0", "echo", "X"}, "a\n", "/", env);
    EXPECT_EQ(procs.out, "X a\n");
    EXPECT_EQ(procs.err, "");
    EXPECT_EQ(procs.status, 0);
    // The options stop at the first operand: what follows the command is
    // never an option.
    const auto operands = RunCaptured("xargs", {"echo", "-s", "5"}, "a\n", "/", env);
    EXPECT_EQ(operands.out, "-s 5 a\n");
    EXPECT_EQ(operands.status, 0);
    const auto unknown = RunCaptured("xargs", {"-z"}, "", "/", env);
    EXPECT_NE(unknown.err.find("invalid option -- 'z'"), std::string::npos);
    EXPECT_EQ(unknown.status, 1);
}

TEST_F(BuiltinCommandsTest, XargsIsStoppedPromptly) {
    const auto env = PathEnvironment(factory);
    // xargs sleep 30, stopped while it waits on the child: the stop reaches
    // the child, xargs lets go at once, and the whole process reports 143.
    auto process = os->StartProcess(env, "/bin/xargs", {"sleep", "30"}, "/",
                                    StartProcessOptions{});
    ASSERT_NE(process, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 143);
}

} // namespace Haisos