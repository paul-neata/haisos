#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinText.h"

using namespace Haisos;

namespace {

const std::string kNone;

} // namespace

// --- chmod ---

TEST_F(BuiltinCommandsTest, ChmodValidatesAndChangesNothing) {
    const auto run = RunCaptured("chmod", {"755", "/notes.txt"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
    std::string content;
    ASSERT_TRUE(ReadWholeFile(*root, "/notes.txt", content));
    EXPECT_EQ(content, "one\ntwo\n\n\n\tthree\n");
}

TEST_F(BuiltinCommandsTest, ChmodVerboseAndChanges) {
    const auto changed = RunCaptured("chmod", {"-v", "755", "/notes.txt"});
    EXPECT_EQ(changed.out, "mode of '/notes.txt' changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)\n");
    EXPECT_EQ(changed.err, kNone);
    EXPECT_EQ(changed.status, 0);

    const auto retained = RunCaptured("chmod", {"-v", "777", "/notes.txt"});
    EXPECT_EQ(retained.out, "mode of '/notes.txt' retained as 0777 (rwxrwxrwx)\n");
    EXPECT_EQ(retained.err, kNone);
    EXPECT_EQ(retained.status, 0);

    const auto onlyChanges = RunCaptured("chmod", {"-c", "777", "/notes.txt"});
    EXPECT_EQ(onlyChanges.out, kNone);
    EXPECT_EQ(onlyChanges.err, kNone);
    EXPECT_EQ(onlyChanges.status, 0);

    struct Case {
        const char* mode;
        const char* to;
    };
    for (const auto& c : std::vector<Case>{
        {"a=rwx,g-w,o=", "0750 (rwxr-x---)"},
        {"ug+s,+t", "7777 (rwsrwsrwt)"},
        {"=644", "0644 (rw-r--r--)"},
        {"0", "0000 (---------)"},
        {"4755", "4755 (rwsr-xr-x)"},
        {"u=g,go-w", "0755 (rwxr-xr-x)"},
        {"o-x,+t", "1776 (rwxrwxrwT)"},
        {"ug+s", "6777 (rwsrwsrwx)"},
    }) {
        const auto run = RunCaptured("chmod", {"-v", c.mode, "/notes.txt"});
        EXPECT_EQ(run.out, std::string("mode of '/notes.txt' changed from 0777 (rwxrwxrwx) to ") + c.to + "\n") << c.mode;
        EXPECT_EQ(run.err, kNone) << c.mode;
        EXPECT_EQ(run.status, 0) << c.mode;
    }
}

TEST_F(BuiltinCommandsTest, ChmodOptionLikeMode) {
    const auto removeWrite = RunCaptured("chmod", {"-v", "-w", "/notes.txt"});
    EXPECT_EQ(removeWrite.out, "mode of '/notes.txt' changed from 0777 (rwxrwxrwx) to 0555 (r-xr-xr-x)\n");
    EXPECT_EQ(removeWrite.err, kNone);
    EXPECT_EQ(removeWrite.status, 0);

    const auto twoClauses = RunCaptured("chmod", {"-v", "-x,+r", "/notes.txt"});
    EXPECT_EQ(twoClauses.out, "mode of '/notes.txt' changed from 0777 (rwxrwxrwx) to 0666 (rw-rw-rw-)\n");
    EXPECT_EQ(twoClauses.err, kNone);
    EXPECT_EQ(twoClauses.status, 0);
}

TEST_F(BuiltinCommandsTest, ChmodInvalidModes) {
    for (const std::string mode : {"9z", "8", "u+q", "77777", "", ",u+x"}) {
        const auto run = RunCaptured("chmod", {mode, "/notes.txt"});
        EXPECT_EQ(run.out, kNone) << mode;
        EXPECT_EQ(run.err, "chmod: invalid mode: " + GnuQuote(mode) +
            "\nTry 'chmod --help' for more information.\n") << mode;
        EXPECT_EQ(run.status, 1) << mode;
    }
}

TEST_F(BuiltinCommandsTest, ChmodOperandErrors) {
    const auto none = RunCaptured("chmod", {});
    EXPECT_EQ(none.out, kNone);
    EXPECT_EQ(none.err, "chmod: missing operand\nTry 'chmod --help' for more information.\n");
    EXPECT_EQ(none.status, 1);

    const auto modeOnly = RunCaptured("chmod", {"755"});
    EXPECT_EQ(modeOnly.out, kNone);
    EXPECT_EQ(modeOnly.err, "chmod: missing operand after '755'\nTry 'chmod --help' for more information.\n");
    EXPECT_EQ(modeOnly.status, 1);

    const auto missing = RunCaptured("chmod", {"755", "/nothing"});
    EXPECT_EQ(missing.out, kNone);
    EXPECT_EQ(missing.err, "chmod: cannot access '/nothing': No such file or directory\n");
    EXPECT_EQ(missing.status, 1);

    const auto forced = RunCaptured("chmod", {"-f", "755", "/nothing"});
    EXPECT_EQ(forced.out, kNone);
    EXPECT_EQ(forced.err, kNone);
    EXPECT_EQ(forced.status, 1);

    const auto badReference = RunCaptured("chmod", {"--reference=/nothing", "/notes.txt"});
    EXPECT_EQ(badReference.out, kNone);
    EXPECT_EQ(badReference.err, "chmod: failed to get attributes of '/nothing': No such file or directory\n");
    EXPECT_EQ(badReference.status, 1);

    const auto reference = RunCaptured("chmod", {"--reference=/docs", "-v", "/notes.txt"});
    EXPECT_EQ(reference.out, "mode of '/notes.txt' retained as 0777 (rwxrwxrwx)\n");
    EXPECT_EQ(reference.err, kNone);
    EXPECT_EQ(reference.status, 0);
}

TEST_F(BuiltinCommandsTest, ChmodRecursive) {
    const auto run = RunCaptured("chmod", {"-Rv", "755", "/docs"});
    EXPECT_EQ(run.out,
        "mode of '/docs' changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)\n"
        "mode of '/docs/a.md' changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)\n"
        "mode of '/docs/sub' changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)\n"
        "mode of '/docs/sub/b.md' changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    const auto root = RunCaptured("chmod", {"-R", "--preserve-root", "755", "/"});
    EXPECT_EQ(root.out, kNone);
    EXPECT_EQ(root.err,
        "chmod: it is dangerous to operate recursively on '/'\n"
        "chmod: use --no-preserve-root to override this failsafe\n");
    EXPECT_EQ(root.status, 1);
}

// --- basename ---

TEST_F(BuiltinCommandsTest, BasenameCases) {
    const auto withSuffix = RunCaptured("basename", {"/a/b/c.txt", ".txt"});
    EXPECT_EQ(withSuffix.out, "c\n");
    EXPECT_EQ(withSuffix.err, kNone);
    EXPECT_EQ(withSuffix.status, 0);

    const auto trailingSlash = RunCaptured("basename", {"a/b/"});
    EXPECT_EQ(trailingSlash.out, "b\n");
    EXPECT_EQ(trailingSlash.status, 0);

    EXPECT_EQ(RunCaptured("basename", {"/"}).out, "/\n");
    EXPECT_EQ(RunCaptured("basename", {"//"}).out, "/\n");
    EXPECT_EQ(RunCaptured("basename", {""}).out, "\n");

    const auto multiple = RunCaptured("basename", {"-a", "x/y", "z/w"});
    EXPECT_EQ(multiple.out, "y\nw\n");
    EXPECT_EQ(multiple.status, 0);

    const auto suffixOption = RunCaptured("basename", {"-s", ".c", "a.c", "b/b.c"});
    EXPECT_EQ(suffixOption.out, "a\nb\n");
    EXPECT_EQ(suffixOption.status, 0);

    const auto extra = RunCaptured("basename", {"a", "b", "c"});
    EXPECT_EQ(extra.out, kNone);
    EXPECT_EQ(extra.err, "basename: extra operand 'c'\nTry 'basename --help' for more information.\n");
    EXPECT_EQ(extra.status, 1);

    const auto zero = RunCaptured("basename", {"-z", "x/y"});
    EXPECT_EQ(zero.out, std::string("y\0", 2));
    EXPECT_EQ(zero.status, 0);

    const auto suffixEqualsBase = RunCaptured("basename", {".txt", ".txt"});
    EXPECT_EQ(suffixEqualsBase.out, ".txt\n");
    EXPECT_EQ(suffixEqualsBase.status, 0);

    const auto none = RunCaptured("basename", {});
    EXPECT_EQ(none.out, kNone);
    EXPECT_EQ(none.err, "basename: missing operand\nTry 'basename --help' for more information.\n");
    EXPECT_EQ(none.status, 1);
}

// --- dirname ---

TEST_F(BuiltinCommandsTest, DirnameCases) {
    const auto run = RunCaptured("dirname",
        {"/a/b/c", "a", "/", "//", "a/b/", "a//b//", "", "///a//b///"});
    EXPECT_EQ(run.out, "/a/b\n.\n/\n/\na\na\n.\n///a\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    const auto zero = RunCaptured("dirname", {"-z", "a/b"});
    EXPECT_EQ(zero.out, std::string("a\0", 2));
    EXPECT_EQ(zero.status, 0);

    const auto none = RunCaptured("dirname", {});
    EXPECT_EQ(none.out, kNone);
    EXPECT_EQ(none.err, "dirname: missing operand\nTry 'dirname --help' for more information.\n");
    EXPECT_EQ(none.status, 1);
}

// --- realpath ---

TEST_F(BuiltinCommandsTest, RealpathCases) {
    ASSERT_EQ(root->CreateDirectory("/w", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/w/d", kDirMode), 0);
    ASSERT_EQ(root->CreateDirectory("/w/d/e", kDirMode), 0);
    WriteFile("/w/f", "file");
    WriteFile("/w/d/e/g", "g");
    const std::string cwd = "/w";

    const auto basic = RunCaptured("realpath", {"f", "d/e/../e", "nothing"}, std::nullopt, cwd);
    EXPECT_EQ(basic.out, "/w/f\n/w/d/e\n/w/nothing\n");
    EXPECT_EQ(basic.err, kNone);
    EXPECT_EQ(basic.status, 0);

    const auto existing = RunCaptured("realpath", {"-e", "nothing"}, std::nullopt, cwd);
    EXPECT_EQ(existing.out, kNone);
    EXPECT_EQ(existing.err, "realpath: nothing: No such file or directory\n");
    EXPECT_EQ(existing.status, 1);

    const auto missing = RunCaptured("realpath", {"-m", "nothing/x/../y"}, std::nullopt, cwd);
    EXPECT_EQ(missing.out, "/w/nothing/y\n");
    EXPECT_EQ(missing.err, kNone);
    EXPECT_EQ(missing.status, 0);

    const auto throughFile = RunCaptured("realpath", {"f/x"}, std::nullopt, cwd);
    EXPECT_EQ(throughFile.out, kNone);
    EXPECT_EQ(throughFile.err, "realpath: f/x: Not a directory\n");
    EXPECT_EQ(throughFile.status, 1);

    const auto missingIntermediate = RunCaptured("realpath", {"nothing/x"}, std::nullopt, cwd);
    EXPECT_EQ(missingIntermediate.out, kNone);
    EXPECT_EQ(missingIntermediate.err, "realpath: nothing/x: No such file or directory\n");
    EXPECT_EQ(missingIntermediate.status, 1);

    const auto trailingSlash = RunCaptured("realpath", {"f/"}, std::nullopt, cwd);
    EXPECT_EQ(trailingSlash.out, kNone);
    EXPECT_EQ(trailingSlash.err, "realpath: f/: Not a directory\n");
    EXPECT_EQ(trailingSlash.status, 1);

    const auto relativeTo = RunCaptured("realpath", {"--relative-to=d", "f", "d/e/g"}, std::nullopt, cwd);
    EXPECT_EQ(relativeTo.out, "../f\ne/g\n");
    EXPECT_EQ(relativeTo.err, kNone);
    EXPECT_EQ(relativeTo.status, 0);

    const auto relativeToParent = RunCaptured("realpath", {"--relative-to=d/e", "d"}, std::nullopt, cwd);
    EXPECT_EQ(relativeToParent.out, "..\n");
    EXPECT_EQ(relativeToParent.status, 0);

    const auto relativeBase = RunCaptured("realpath", {"--relative-base=d", "f", "d/e/g"}, std::nullopt, cwd);
    EXPECT_EQ(relativeBase.out, "/w/f\ne/g\n");
    EXPECT_EQ(relativeBase.status, 0);

    const auto both = RunCaptured("realpath",
        {"--relative-to=/w", "--relative-base=d", "f", "d/e"}, std::nullopt, cwd);
    EXPECT_EQ(both.out, "/w/f\n/w/d/e\n");
    EXPECT_EQ(both.status, 0);

    const auto relativeToMissing = RunCaptured("realpath", {"--relative-to=nothing", "f"}, std::nullopt, cwd);
    EXPECT_EQ(relativeToMissing.out, "../f\n");
    EXPECT_EQ(relativeToMissing.err, kNone);
    EXPECT_EQ(relativeToMissing.status, 0);

    const auto quiet = RunCaptured("realpath", {"-q", "/nothing/x", "/w/f"}, std::nullopt, cwd);
    EXPECT_EQ(quiet.out, "/w/f\n");
    EXPECT_EQ(quiet.err, kNone);
    EXPECT_EQ(quiet.status, 1);

    const auto zero = RunCaptured("realpath", {"-z", "/w/f"}, std::nullopt, cwd);
    EXPECT_EQ(zero.out, std::string("/w/f\0", 5));
    EXPECT_EQ(zero.status, 0);

    const auto empty = RunCaptured("realpath", {""}, std::nullopt, cwd);
    EXPECT_EQ(empty.out, kNone);
    EXPECT_EQ(empty.err, "realpath: '': No such file or directory\n");
    EXPECT_EQ(empty.status, 1);

    const auto none = RunCaptured("realpath", {});
    EXPECT_EQ(none.out, kNone);
    EXPECT_EQ(none.err, "realpath: missing operand\nTry 'realpath --help' for more information.\n");
    EXPECT_EQ(none.status, 1);
}