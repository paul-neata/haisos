#include <gtest/gtest.h>

#include <string>

#include "HshShellFixture.h"

namespace Haisos {

// hsh's redirections and heredocs, byte for byte as dash prints them (the
// fixture's /notes.txt holds "one\ntwo\n\n\n\tthree\n").

TEST_F(HshShellTest, RedirectionsToAndFromFiles) {
    const ShellCase cases[] = {
        {"echo a > /o.txt; cat /o.txt", "a\n"},
        {"echo a > /o.txt; echo b >> /o.txt; cat /o.txt", "a\nb\n"},
        {"cat < /notes.txt", "one\ntwo\n\n\n\tthree\n"},
        {"cat 0</notes.txt", "one\ntwo\n\n\n\tthree\n"},
        {"echo z 1<>/new.txt; cat /new.txt", "z\n"},
        {"cat 0<>/notes.txt", "one\ntwo\n\n\n\tthree\n"},
        {"> /bare.txt; echo $?", "0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "RedirectionsToAndFromFiles");
    }
    ExpectSh({"echo x 1>/o2.txt"}, "RedirectionsToAndFromFiles");
    EXPECT_EQ(ReadRootFile("/o2.txt"), "x\n");
    FileStatus status;
    EXPECT_EQ(root->Stat("/new.txt", status), 0);
    EXPECT_EQ(status.type, DirectoryEntryType::File);
    EXPECT_EQ(root->Stat("/bare.txt", status), 0);
}

TEST_F(HshShellTest, RedirectionsAreUndone) {
    const ShellCase cases[] = {
        {"echo a > /u.txt; echo b; cat /u.txt", "b\na\n"},
        {": > /c.txt; echo after", "after\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "RedirectionsAreUndone");
    }
}

TEST_F(HshShellTest, StderrAndDuplicates) {
    const ShellCase cases[] = {
        {"ls /nope 2>/e.txt; cat /e.txt",
            "ls: cannot access '/nope': No such file or directory\n"},
        {"ls /nope >/e2.txt 2>&1; cat /e2.txt",
            "ls: cannot access '/nope': No such file or directory\n"},
        {"echo hi 1>&2", "", "hi\n"},
        {"echo x 3>&-; echo $?", "x\n0\n"},
        {"echo x >&5; echo $?", "2\n", "hsh: 1: 5: Bad file descriptor\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "StderrAndDuplicates");
    }
    // &> sends stdout and stderr to the one file (their order is not asserted).
    const Captured captured = Sh("ls /docs /nope &>/both.txt; cat /both.txt");
    EXPECT_NE(captured.out.find("ls: cannot access '/nope': No such file or directory"),
              std::string::npos);
    EXPECT_NE(captured.out.find("a.md"), std::string::npos);
    EXPECT_EQ(captured.err, "");
}

TEST_F(HshShellTest, BadFdNumberIsFatal) {
    // An expanded dup target that is no digit makes dash exit: "echo after"
    // never runs.
    ExpectSh({"x=a; echo hi >&$x; echo after", "", "hsh: 1: Syntax error: Bad fd number\n", 2},
        "BadFdNumberIsFatal");
}

TEST_F(HshShellTest, HereDocuments) {
    const ShellCase cases[] = {
        // An unquoted delimiter's body is expanded; a quoted one's is not.
        {"HOME=/h; cat <<E\nx $HOME\nE", "x /h\n"},
        {"cat <<'E'\n$x\nE", "$x\n"},
        // <<- strips the leading tabs of the body and the delimiter line.
        {"cat <<-E\n\ta\n\tE", "a\n"},
        {"cat <<E\nE", ""},
        {"cat 0<<E\ny\nE", "y\n"},
        {"wc -l <<E\nx\ny\nE", "2\n"},
        // Two heredocs on one line, their bodies in order.
        {"cat <<A; cat <<B\na\nA\nb\nB", "a\nb\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "HereDocuments");
    }
    // A heredoc far bigger than a default pipe: the pipe is sized to the text,
    // so writing it whole before the reader starts never blocks.
    ExpectSh({"wc -c <<E\n" + std::string(100000, 'x') + "\nE", "100001\n"}, "HereDocuments");
}

TEST_F(HshShellTest, HereStrings) {
    const ShellCase cases[] = {
        {"cat <<<\"one two\"", "one two\n"},
        {"x=v; cat <<<$x", "v\n"},
        {"wc -c <<<\"\"", "1\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "HereStrings");
    }
}

TEST_F(HshShellTest, Noclobber) {
    WriteFile("/n.txt", "old\n");
    {
        // -C: > onto an existing file is refused; >| overrides it.
        const Captured captured = RunCaptured("hsh",
            {"-C", "-c", "echo b > /n.txt; echo $?; echo c >| /n.txt; cat /n.txt"});
        EXPECT_EQ(captured.out, "2\nc\n");
        EXPECT_EQ(captured.err, "hsh: 1: cannot create /n.txt: File exists\n");
        EXPECT_EQ(captured.status, 0);
    }
    {
        // >> is not refused.
        const Captured captured = RunCaptured("hsh", {"-C", "-c", "echo d >> /n.txt; cat /n.txt"});
        EXPECT_EQ(captured.out, "c\nd\n");
        EXPECT_EQ(captured.err, "");
        EXPECT_EQ(captured.status, 0);
    }
    {
        // &> is refused the same way.
        const Captured captured = RunCaptured("hsh", {"-C", "-c", "echo e &> /n.txt; echo $?"});
        EXPECT_EQ(captured.out, "2\n");
        EXPECT_EQ(captured.err, "hsh: 1: cannot create /n.txt: File exists\n");
        EXPECT_EQ(captured.status, 0);
    }
}

TEST_F(HshShellTest, RedirectionErrors) {
    const ShellCase cases[] = {
        {"cat < /nonexist; echo $?", "2\n",
            "hsh: 1: cannot open /nonexist: No such file\n"},
        {"echo hi > /nonexist/x; echo $?", "2\n",
            "hsh: 1: cannot create /nonexist/x: Directory nonexistent\n"},
        {"echo hi > /docs; echo $?", "2\n",
            "hsh: 1: cannot create /docs: Is a directory\n"},
        {"echo hi > /bin/ls; echo $?", "2\n",
            "hsh: 1: cannot create /bin/ls: Permission denied\n"},
        // A special builtin's redirection error is fatal: the shell ends.
        {": < /nonexist; echo after", "",
            "hsh: 1: cannot open /nonexist: No such file\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "RedirectionErrors");
    }
}

TEST_F(HshShellTest, Exec) {
    WriteFile("/e.lua", "exit(3)");
    const ShellCase cases[] = {
        {"exec /bin/echo hi; echo no", "hi\n"},
        {"exec /e.lua; echo no", "", "", 3},
        {"exec nosuch; echo no", "", "hsh: 1: exec: nosuch: not found\n", 127},
        {"exec 3>/f.txt; echo z >&3; exec 3>&-; cat /f.txt", "z\n"},
        {"exec >/all.txt; echo a; echo b; cat /all.txt >&2", "", "a\nb\n"},
        {"exec 9>&-; echo $?", "0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Exec");
    }
}

TEST_F(HshShellTest, ClosedSlotsReachChildrenClosed) {
    // A closed slot reaches the child as a descriptor whose reads fail,
    // never as the console.
    ExpectSh({"cat 0<&-; echo $?", "1\n", "cat: -: Input/output error\n"},
        "ClosedSlotsReachChildrenClosed");
}

TEST_F(HshShellTest, AcceptanceScenarioTwo) {
    WriteFile("/abc.txt", "one\ntwo\nthree\n");
    ExpectSh({"ls /nope 2>/err.txt || echo \"failed: $?\"; cat /err.txt; cat < /abc.txt >> /out.txt 2>&1 && wc -c /out.txt",
        "failed: 2\nls: cannot access '/nope': No such file or directory\n14 /out.txt\n"},
        "AcceptanceScenarioTwo");
}

} // namespace Haisos
