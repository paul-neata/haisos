// The find actions, each against GNU findutils 4.9.0's own output and
// messages in the C locale: -exec and its three relatives, -delete, -printf
// and its file-writing kin, -ls. The programs run through PATH, so every
// run takes an environment holding the builtins in /bin.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinDate.h"
#include "BuiltinCommandList.h"
#include "FindTestTree.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

// A PATH with the builtins in it: the environment every action test runs
// with, so -exec's echo and friends are found.
std::shared_ptr<IEnvironment> PathEnvironment(const std::shared_ptr<IFactory>& factory) {
    auto environment = factory->CreateEnvironment();
    environment->SetVariable("PATH", "/bin");
    return environment;
}

} // namespace

TEST_F(BuiltinCommandsTest, FindExecSemicolon) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    // Every {} in every argument -- the command slot included, wherever it
    // stands -- is the path.
    const auto found = RunCaptured("find", {"-name", "*.h", "-exec", "echo", "found", "{}", ";"},
                                   std::nullopt, "/proj", env);
    EXPECT_EQ(found.out, "found ./z.h\n");
    EXPECT_EQ(found.err, "");
    EXPECT_EQ(found.status, 0);
    const auto inside = RunCaptured("find", {"-exec", "echo", "x{}y", ";", "-quit"},
                                    std::nullopt, "/proj", env);
    EXPECT_EQ(inside.out, "x.y\n");
    EXPECT_EQ(inside.status, 0);
    // find's own buffered stdout reaches the file before each child writes
    // to the same one (RunProgramAndWait flushes), so -print's line is
    // whole in it first.
    const auto printed = RunCaptured("find", {"-name", "z.h", "-print", "-exec", "echo", "E{}", ";"},
                                     std::nullopt, "/proj", env);
    EXPECT_EQ(printed.out, "./z.h\nE./z.h\n");
    EXPECT_EQ(printed.status, 0);
    // False when the command fails: the -print after it never runs, and the
    // exit status stays as it was.
    const auto failed = RunCaptured("find", {"-name", "z.h", "-exec", "false", "{}", ";", "-print"},
                                    std::nullopt, "/proj", env);
    EXPECT_EQ(failed.out, "");
    EXPECT_EQ(failed.status, 0);
}

TEST_F(BuiltinCommandsTest, FindExecPlus) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    // One command with every path on it, as one line; the order is the
    // walk's, so the line is compared as a set.
    const auto all = RunCaptured("find", {"-type", "f", "-exec", "echo", "{}", "+"},
                                 std::nullopt, "/proj", env);
    std::string spaced = all.out;
    std::replace(spaced.begin(), spaced.end(), ' ', '\n');
    ExpectSameLines(spaced, "./a/b/empty\n./a/x.txt\n./a/y.md\n./z.h\n");
    EXPECT_EQ(SplitLines(all.out).size(), 1u);
    EXPECT_EQ(all.status, 0);
    // "+" binds {} to the command: more than one {} is refused, and one
    // that is not alone is refused, with GNU's two messages.
    const auto twice = RunCaptured("find", {"-exec", "echo", "{}", "{}", "+"},
                                   std::nullopt, "/proj", env);
    EXPECT_EQ(twice.err, "find: Only one instance of {} is supported with -exec ... +\n");
    EXPECT_EQ(twice.status, 1);
    const auto embedded = RunCaptured("find", {"-exec", "echo", "a{}", "+"},
                                      std::nullopt, "/proj", env);
    EXPECT_EQ(embedded.err,
        "find: In '-exec ... {} +' the '{}' must appear by itself, but you specified 'a{}'\n");
    EXPECT_EQ(embedded.status, 1);
    // A failing batch makes find exit 1; a failing ";"-run does not.
    const auto failing = RunCaptured("find", {"-type", "f", "-exec", "false", "{}", "+"},
                                     std::nullopt, "/proj", env);
    EXPECT_EQ(failing.out, "");
    EXPECT_EQ(failing.status, 1);
    // An empty command is GNU's "invalid argument"; no terminator at all is
    // its "missing argument".
    const auto empty = RunCaptured("find", {"-exec", ";"}, std::nullopt, "/proj", env);
    EXPECT_EQ(empty.err, "find: invalid argument `;' to `-exec'\n");
    EXPECT_EQ(empty.status, 1);
    const auto unterminated = RunCaptured("find", {"-exec", "echo"}, std::nullopt, "/proj", env);
    EXPECT_EQ(unterminated.err, "find: missing argument to `-exec'\n");
    EXPECT_EQ(unterminated.status, 1);
}

TEST_F(BuiltinCommandsTest, FindExecMissingCommand) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    // Reported for the file and the primary false; the exit status stays.
    const auto captured = RunCaptured("find", {"-maxdepth", "0", "-exec", "nocmd", "{}", ";"},
                                      std::nullopt, "/proj", env);
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "find: 'nocmd': No such file or directory\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, FindExecdir) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    // The program runs in the file's directory, {} is its "./" name.
    const auto names = RunCaptured("find", {"-execdir", "echo", "{}", ";"},
                                   std::nullopt, "/proj", env);
    ExpectSameLines(names.out, "./.\n./a\n./b\n./empty\n./x.txt\n./y.md\n./z.h\n");
    EXPECT_EQ(names.status, 0);
    // pwd shows the directory each child ran in: the starting point's own
    // (the working directory) and each subdirectory of it.
    const auto dirs = RunCaptured("find", {"a", "-execdir", "pwd", ";"}, std::nullopt, "/proj", env);
    ExpectSameLines(dirs.out, "/proj\n/proj/a\n/proj/a\n/proj/a\n/proj/a/b\n");
    EXPECT_EQ(dirs.status, 0);
    // An empty or relative PATH entry is refused, at parse time, with the
    // message of whichever came first.
    auto insecure = factory->CreateEnvironment();
    insecure->SetVariable("PATH", ":/bin");
    const auto refused = RunCaptured("find", {"-execdir", "echo", "{}", ";"},
                                     std::nullopt, "/proj", insecure);
    EXPECT_EQ(refused.out, "");
    EXPECT_EQ(refused.err,
        "find: The current directory is included in the PATH environment variable, which is insecure in combination with the -execdir action of find.  Please remove the current directory from your $PATH (that is, remove \".\", doubled colons, or leading or trailing colons)\n");
    EXPECT_EQ(refused.status, 1);
    auto relative = factory->CreateEnvironment();
    relative->SetVariable("PATH", "x:/bin");
    const auto rel = RunCaptured("find", {"-okdir", "echo", "{}", ";"},
                                  std::nullopt, "/proj", relative);
    EXPECT_EQ(rel.err,
        "find: The relative path 'x' is included in the PATH environment variable, which is insecure in combination with the -okdir action of find.  Please remove that entry from $PATH\n");
    EXPECT_EQ(rel.status, 1);
}

TEST_F(BuiltinCommandsTest, FindOk) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    // One prompt per file on stderr, "< CMD ... PATH > ? " (the command by
    // its name, the path as given); the answers are read in order from
    // stdin, and only the answered files run the command.
    const auto captured = RunCaptured("find", {"a/x.txt", "z.h", "-ok", "echo", "{}", ";"},
                                      "n\ny\n", "/proj", env);
    EXPECT_EQ(captured.out, "z.h\n");
    EXPECT_EQ(captured.err, "< echo ... a/x.txt > ? < echo ... z.h > ? ");
    EXPECT_EQ(captured.status, 0);
    // -ok never takes "+" as its terminator.
    const auto plus = RunCaptured("find", {"-maxdepth", "0", "-ok", "echo", "{}", "+"},
                                  std::nullopt, "/proj", env);
    EXPECT_EQ(plus.err, "find: missing argument to `-ok'\n");
    EXPECT_EQ(plus.status, 1);
}

TEST_F(BuiltinCommandsTest, OpenEmptyInputReadsNothing) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    // The -ok action's child reads an input that is at its end at once (not
    // find's own standard input), so cat finds nothing and the script's own
    // output is all that shows.
    const auto captured = RunCaptured("find",
        {"a/x.txt", "-ok", "hsh", "-c", "cat; echo done", ";"}, "y\n", "/proj", env);
    EXPECT_EQ(captured.out, "done\n");
    EXPECT_EQ(captured.status, 0);
}

TEST_F(BuiltinCommandsTest, FindDelete) {
    MakeProj(root);
    const auto env = PathEnvironment(factory);
    ASSERT_EQ(root->CreateDirectory("/proj/d", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/proj/d/e", kDirMode), 0);
    WriteFile("/proj/d/e/f", "f");
    WriteFile("/proj/d/g", "g");
    // A directory that still holds entries cannot be deleted; the message
    // and the exit status are GNU's, and the tree is left as it was.
    const auto notEmpty = RunCaptured("find", {"d", "-name", "e", "-delete"},
                                      std::nullopt, "/proj", env);
    EXPECT_EQ(notEmpty.err, "find: cannot delete 'd/e': Directory not empty\n");
    EXPECT_EQ(notEmpty.status, 1);
    FileStatus status;
    EXPECT_EQ(root->Stat("/proj/d/e", status), 0);
    // -delete turns -depth on: children first, printed as they go away, the
    // starting point itself last.
    const auto deleted = RunCaptured("find", {"d", "-delete", "-print"}, std::nullopt, "/proj", env);
    const Lines lines = SplitLines(deleted.out);
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines.back(), "d");
    EXPECT_EQ(deleted.status, 0);
    EXPECT_NE(root->Stat("/proj/d", status), 0);
    // -prune does nothing once -delete has turned -depth on: refused before
    // the walk, unless -depth was written outright.
    ASSERT_EQ(root->CreateDirectory("/proj/d", kDirMode), 0);
    const auto pruned = RunCaptured("find", {"d", "-delete", "-prune"}, std::nullopt, "/proj", env);
    EXPECT_EQ(pruned.out, "");
    EXPECT_EQ(pruned.err,
        "find: The -delete action automatically turns on -depth, but -prune does nothing when -depth is in effect.  If you want to carry on anyway, just explicitly use the -depth option.\n");
    EXPECT_EQ(pruned.status, 1);
    EXPECT_EQ(root->Stat("/proj/d", status), 0);
    const auto depth = RunCaptured("find", {"d", "-depth", "-delete", "-prune"},
                                   std::nullopt, "/proj", env);
    EXPECT_EQ(depth.err, "");
    EXPECT_EQ(depth.status, 0);
    EXPECT_NE(root->Stat("/proj/d", status), 0);
}

TEST_F(BuiltinCommandsTest, FindPrintf) {
    MakeProj(root);
    // One directive of each kind, with the values HaisosOS reports: mode
    // 0777, owner and group haisos (uid and gid 0), inode 0, no links, no
    // birth times, no SELinux, filesystem type unknown.
    const auto captured = RunCaptured("find", {"a/x.txt", "-printf",
        "%p|%f|%h|%P|%H|%d|%s|%k|%b|%n|%m|%M|%u|%g|%U|%G|%y|%Y|%l|%i|%D|%F|%Z|%%\\n"},
        std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(captured.out,
        "a/x.txt|x.txt|a||a/x.txt|0|3|1|1|1|777|-rwxrwxrwx|haisos|haisos|0|0|f|f||0|0|unknown||%\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    // %S: the blocks-to-size ratio, and 1 for an empty file.
    const auto spacing = RunCaptured("find", {"a/x.txt", "-printf", "%S\\n"},
                                     std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(spacing.out, "170.667\n");
    const auto empty = RunCaptured("find", {"a/b/empty", "-printf", "%S\\n"},
                                   std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(empty.out, "1\n");
    // Widths and precisions: padding for %p and %s, %05d zero-padded, %05s
    // space-padded (strings pad with spaces even under the 0 flag), %#m.
    const auto widths = RunCaptured("find", {"a/x.txt", "-printf",
        "[%10p][%-10f][%5s][%-5d][%05s][%05d][%#m][%.3p]\\n"},
        std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(widths.out,
        "[   a/x.txt][x.txt     ][    3][0    ][    3][00000][0777][a/x]\n");
    // The time directives, with a time the test sets: %T@ is the seconds
    // and the nanoseconds (nine digits, with GNU's own tenth digit 0), %t
    // is ctime's shape, %TY is the strftime kind.
    const FileDateTime when{1704157445, 0};
    ASSERT_EQ(root->SetTimes("/proj/a/x.txt", when, when), 0);
    const auto times = RunCaptured("find", {"a/x.txt", "-printf", "%T@|%t|%TY\\n"},
                                   std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(times.out,
        "1704157445.0000000000|"
        + FormatDateTime("%a %b %e %H:%M:%S", when, false) + ".0000000000 "
        + FormatDateTime("%Y", when, false) + "|" + FormatDateTime("%Y", when, false) + "\n");
    // \\c stops the output for the file; what follows is not even scanned,
    // so no warning about it either.
    const auto stop = RunCaptured("find", {"a/x.txt", "-printf", "AB\\cCD"},
                                  std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(stop.out, "AB");
    EXPECT_EQ(stop.err, "");
    EXPECT_EQ(stop.status, 0);
    // The escapes: the known ones, an octal one, and the unknown one warned
    // about (always, not only under -warn -- stdin here is no terminal, so
    // find's own warnings are off) and printed as it stands.
    const auto escapes = RunCaptured("find", {"a/x.txt", "-printf",
        "\\a\\b\\f\\n\\r\\t\\v\\\\\\012\\q"}, std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(escapes.out, "\a\b\f\n\r\t\v\\\n\\q");
    EXPECT_EQ(escapes.err, "find: warning: unrecognized escape `\\q'\n");
    // The directives find does not know, likewise; %h and %l it does know,
    // so only their values print.
    const auto directives = RunCaptured("find", {"-maxdepth", "0", "-printf",
        "[%d][%'d][%Id][%ld][%hd][%qd]"}, std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(directives.out, "[0][%'d][%Id][d][.d][%qd]");
    EXPECT_EQ(directives.err,
        "find: warning: unrecognized format directive `%''\n"
        "find: warning: unrecognized format directive `%I'\n"
        "find: warning: unrecognized format directive `%q'\n");
    // A % that ends the format is GNU's error.
    const auto trailing = RunCaptured("find", {"a/x.txt", "-printf", "a%"},
                                      std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(trailing.out, "");
    EXPECT_EQ(trailing.err, "find: error: % at end of format string\n");
    EXPECT_EQ(trailing.status, 1);
    // A time directive the format ends before is warned about and printed
    // as it stands.
    const auto bareTime = RunCaptured("find", {"a/x.txt", "-printf", "x%A"},
                                      std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(bareTime.out, "x%A");
    EXPECT_EQ(bareTime.err,
        "find: warning: format directive `%A' should be followed by another character\n");
    EXPECT_EQ(bareTime.status, 0);
}

TEST_F(BuiltinCommandsTest, FindFprintAndLs) {
    MakeProj(root);
    // One fixed time for everything, so the -ls lines can be written out in
    // full: six months past is the year column.
    const FileDateTime when{1704157445, 0};
    for (const std::string& path : {"/proj", "/proj/a", "/proj/a/b", "/proj/a/b/empty",
                                    "/proj/a/x.txt", "/proj/a/y.md", "/proj/z.h"}) {
        ASSERT_EQ(root->SetTimes(path, when, when), 0) << path;
    }
    // The file-writing actions share one record per distinct name (/out1 is
    // opened once, and both -fprints write to the same buffer), flushed
    // whole when the walk ends. The files live outside /proj: they are made
    // at parse time, and /proj's own times are part of what -fls prints.
    const auto captured = RunCaptured("find", {"-fprint", "/out1", "-fprint", "/out1",
        "-fprint0", "/out2", "-fprintf", "/out3", "%f\\n", "-fls", "/out4"},
        std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(captured.status, 0);
    std::string text;
    EXPECT_TRUE(ReadWholeFile(*root, "/out1", text));
    ExpectSameLines(text, std::string(kProjTree) + kProjTree);
    EXPECT_TRUE(ReadWholeFile(*root, "/out2", text));
    std::replace(text.begin(), text.end(), '\0', '\n');
    ExpectSameLines(text, kProjTree);
    EXPECT_TRUE(ReadWholeFile(*root, "/out3", text));
    ExpectSameLines(text, ".\na\nb\nempty\nx.txt\ny.md\nz.h\n");
    // -fls: GNU -ls's own columns, with HaisosOS's own values (inode 0,
    // owner haisos, mode 0777, the in-memory filesystem's blocks).
    const std::string year = FormatDateTime("%b %e  %Y", when, false);
    EXPECT_TRUE(ReadWholeFile(*root, "/out4", text));
    ExpectSameLines(text,
        "        0      0 drwxrwxrwx   3 haisos   haisos          0 " + year + " .\n"
        "        0      0 drwxrwxrwx   3 haisos   haisos          0 " + year + " ./a\n"
        "        0      0 drwxrwxrwx   2 haisos   haisos          0 " + year + " ./a/b\n"
        "        0      0 -rwxrwxrwx   1 haisos   haisos          0 " + year + " ./a/b/empty\n"
        "        0      1 -rwxrwxrwx   1 haisos   haisos          3 " + year + " ./a/x.txt\n"
        "        0      2 -rwxrwxrwx   1 haisos   haisos       1100 " + year + " ./a/y.md\n"
        "        0      1 -rwxrwxrwx   1 haisos   haisos          2 " + year + " ./z.h\n");
    // -ls on find's own stdout, one line in full.
    const auto lsOut = RunCaptured("find", {"a/x.txt", "-ls"}, std::nullopt, "/proj",
                                   PathEnvironment(factory));
    EXPECT_EQ(lsOut.out,
        "        0      1 -rwxrwxrwx   1 haisos   haisos          3 " + year + " a/x.txt\n");
    EXPECT_EQ(lsOut.status, 0);
    // /dev/stdout is find's own stdout, in -print's own order; /dev/stderr
    // is its own standard error.
    const auto interleave = RunCaptured("find", {"a", "-fprint", "/dev/stdout", "-print"},
                                        std::nullopt, "/proj", PathEnvironment(factory));
    ExpectSameLines(interleave.out,
        "a\na\na/b\na/b\na/b/empty\na/b/empty\na/x.txt\na/x.txt\na/y.md\na/y.md\n");
    const auto toStderr = RunCaptured("find", {"-maxdepth", "0", "-fprint", "/dev/stderr"},
                                      std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(toStderr.err, ".\n");
    EXPECT_EQ(toStderr.status, 0);
    // A file that cannot be made, with GNU's wording for the three reasons.
    const auto missing = RunCaptured("find", {"-fprint", "/nonexist/x"},
                                     std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(missing.err, "find: '/nonexist/x': No such file or directory\n");
    EXPECT_EQ(missing.status, 1);
    const auto directory = RunCaptured("find", {"-maxdepth", "0", "-fprint", "a"},
                                       std::nullopt, "/proj", PathEnvironment(factory));
    EXPECT_EQ(directory.err, "find: 'a': Is a directory\n");
    EXPECT_EQ(directory.status, 1);
}

} // namespace Haisos