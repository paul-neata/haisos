#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "HshShellFixture.h"
#include "tests/mocks/MockFileDescriptor.h"

namespace Haisos {

TEST_F(HshShellTest, Cd) {
    const ShellCase cases[] = {
        {"cd /docs; pwd", "/docs\n"},
        // cd - prints the new directory; OLDPWD unset is "." -- the same
        // directory -- and it is printed all the same, as dash.
        {"cd /docs; cd -; pwd", "/\n/\n"},
        {"cd -", "/\n"},
        {"cd /docs extra ignored; pwd", "/docs\n"},
        {"cd \"\"; pwd", "/\n"},
        {"unset HOME; cd; pwd", "/\n"},
        // CDPATH: the entry that finds it is printed, unless it is the
        // working-directory entry ("" -- the leading ':' one here too).
        {"CDPATH=/docs; cd sub; pwd", "/docs/sub\n/docs/sub\n"},
        {"CDPATH=/x:/docs; cd sub; pwd", "/docs/sub\n/docs/sub\n"},
        {"CDPATH=:/docs; cd docs; pwd", "/docs\n"},
        // Failed cds are reported, not fatal (cd is a regular builtin).
        {"cd /nonexist", "", "hsh: 1: cd: can't cd to /nonexist\n", 2},
        {"cd /notes.txt", "", "hsh: 1: cd: can't cd to /notes.txt\n", 2},
        {"cd /nonexist; echo after", "after\n", "hsh: 1: cd: can't cd to /nonexist\n", 0},
        {"cd -x", "", "hsh: 1: cd: Illegal option -x\n", 2},
        {"cd -- /docs; pwd", "/docs\n"},
        {"cd /docs; echo $PWD $OLDPWD", "/docs /\n"},
        {"cd /docs; cd sub; pwd", "/docs/sub\n"},
        {"cd /docs; cd ..; pwd", "/\n"},
        {"HOME=/docs; cd; pwd", "/docs\n"},
        // Children started afterwards start in the new directory.
        {"cd /docs; hsh -c pwd", "/docs\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Cd");
    }
    // cd sets PWD and OLDPWD, both exported, as dash's setpwd.
    ExpectSh({"cd /docs; export -p | cat",
        "export OLDPWD='/'\nexport PWD='/docs'\n"}, "Cd");
    // A read-only PWD: hsh's AssignVariable makes cd's set of PWD a fatal
    // error, where dash prints "cd: PWD: is read only" and goes on. A
    // documented deviation (the plan's).
    ExpectSh({"readonly PWD; cd /docs; echo after", "", "hsh: 1: PWD: is read only\n", 2}, "Cd");
}

TEST_F(HshShellTest, ExportAndReadonly) {
    const ShellCase cases[] = {
        // dash's showvars: sorted by name, set ones single-quoted.
        {"export A=1 B; export -p", "export A='1'\nexport B\nexport PWD='/'\n"},
        {"export A=1 B; export", "export A='1'\nexport B\nexport PWD='/'\n"},
        {"readonly R=1 Q; readonly -p", "readonly Q\nreadonly R='1'\n"},
        {"readonly -p", ""},
        {"export Q=\"it's\"; export -p", "export PWD='/'\nexport Q='it'\"'\"'s'\n"},
        // -p lists, its operands notwithstanding.
        {"export -p A=3", "export PWD='/'\n"},
        // A bare name only flags it -- a read-only one's value stays, an
        // unknown one lists from now on. No "is read only" for the bare form.
        {"readonly R=1; export R; export -p", "export PWD='/'\nexport R='1'\n"},
        {"export W; export X=2; readonly -p; export -p",
            "export PWD='/'\nexport W\nexport X='2'\n"},
        // Bad names and read-only assignments are fatal (special builtins).
        {"export 1x=3; echo after", "", "hsh: 1: export: 1x: bad variable name\n", 2},
        {"export a-b; echo after", "", "hsh: 1: export: a-b: bad variable name\n", 2},
        {"readonly 3; echo after", "", "hsh: 1: readonly: 3: bad variable name\n", 2},
        {"readonly A; export A=2; echo after", "", "hsh: 1: export: A: is read only\n", 2},
        {"readonly x=1; readonly x=2; echo after", "", "hsh: 1: readonly: x: is read only\n", 2},
        // Options: -p clusters, "--" ends them; other letters are illegal.
        {"export -- X=1; export -p", "export PWD='/'\nexport X='1'\n"},
        {"export -z; echo after", "", "hsh: 1: export: Illegal option -z\n", 2},
        // Exported variables reach children; unset ones no longer do.
        {"export y=5; hsh -c 'echo $y'", "5\n"},
        {"export E=1; unset E; hsh -c 'echo \"[$E]\"'", "[]\n"},
        {"readonly x=1; x=2; echo no", "", "hsh: 1: x: is read only\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "ExportAndReadonly");
    }
}

TEST_F(HshShellTest, Unset) {
    const ShellCase cases[] = {
        {"x=1; unset x; echo \"[$x]\"", "[]\n"},
        {"unset nosuchvar"},
        {"unset"},
        {"x=1; unset -v x; echo \"[$x]\"", "[]\n"},
        {"x=1 y=2; unset -- x; echo \"[$x][$y]\"", "[][2]\n"},
        // No functions yet: -f always succeeds; the last option wins.
        {"unset -f q; echo ok", "ok\n"},
        {"x=1; unset -fv x; echo \"[$x]\"", "[]\n"},
        // Errors are fatal: unset is a special builtin.
        {"readonly z=1; unset z; echo after", "", "hsh: 1: unset: z: is read only\n", 2},
        {"unset 1x; echo after", "", "hsh: 1: unset: 1x: bad variable name\n", 2},
        {"unset -z q", "", "hsh: 1: unset: Illegal option -z\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Unset");
    }
}

TEST_F(HshShellTest, SetListsAndPositionals) {
    const ShellCase cases[] = {
        {"set -- a b; echo \"$#:$1:$2\"", "2:a:b\n"},
        {"set q; echo $#:$1", "1:q\n"},
        {"set 1 2; echo $# $1; set --; echo $#", "2 1\n0\n"},
        // A lone "-" turns -x and -v off and keeps the parameters (as dash;
        // -v is accepted, not acted on).
        {"set 1 2; set -; echo $# $1", "2 1\n"},
        // "+" alone is an empty cluster: the option scan goes on.
        {"set + -e; echo $-", "e\n"},
        // `set` alone: every set variable, sorted, single-quoted -- with
        // literal tab and newline bytes in IFS's quotes, as dash.
        {"unset OPTIND PPID PATH PS1 PS2 PS4 PWD; empty=; set", "IFS=' \t\n'\nempty=''\n"},
        {"unset OPTIND PPID PATH PS1 PS2 PS4 PWD IFS; q=\"''\"; v=\"it's\"; set",
            "q=''\"''\"\nv='it'\"'\"'s'\n"},
        // Flagged-but-unset variables do not show in the plain listing.
        {"unset OPTIND PPID PATH PS1 PS2 PS4 PWD IFS; export W; set", ""},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "SetListsAndPositionals");
    }
}

TEST_F(HshShellTest, SetOptions) {
    const ShellCase cases[] = {
        {"set -e -u; echo $-", "ue\n"},
        {"set +eu; echo \"[$-]\"", "[]\n"},
        {"set -o nounset; echo $-", "u\n"},
        {"set -o errexit +o nounset; echo $-", "e\n"},
        // An -o not last in a cluster leaves the cluster's remaining letters,
        // as dash (the invocation does the same).
        {"set -oe nounset; echo $-", "ue\n"},
        // Unknown options are fatal (set is special); the -o message always
        // says "-o", "+o" included.
        {"set -c", "", "hsh: 1: set: Illegal option -c\n", 2},
        {"set -l", "", "hsh: 1: set: Illegal option -l\n", 2},
        {"set -o nosuch", "", "hsh: 1: set: Illegal option -o nosuch\n", 2},
        {"set +o nosuch", "", "hsh: 1: set: Illegal option -o nosuch\n", 2},
        {"set --frob", "", "hsh: 1: set: Illegal option --\n", 2},
        {"set -- -x; echo $1", "-x\n"},
        {"set - a; echo $1 \"[$-]\"", "a []\n"},
        {"set -C; echo a > /noclobber.txt; echo b > /noclobber.txt", "",
            "hsh: 1: cannot create /noclobber.txt: File exists\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "SetOptions");
    }
    // Options hsh does not act on are reported as not treated -- through the
    // shell's stderr (BuiltinContext's NotTreated would dedup and miss
    // redirections) -- and the option just isn't set.
    ExpectSh({"set -m +V -o monitor +o debug; echo ok; echo $?", "ok\n0\n",
        NotTreatedLine("-m") + NotTreatedLine("+V") + NotTreatedLine("-o monitor") + NotTreatedLine("+o debug"),
        0}, "SetOptions");
    // `set -o` lists every option of dash's table, "on"/"off", padded to 16.
    ExpectSh({"set -eu; set -o",
        "Current option settings\n"
        "errexit         on\n"
        "noglob          off\n"
        "ignoreeof       off\n"
        "interactive     off\n"
        "monitor         off\n"
        "noexec          off\n"
        "stdin           off\n"
        "xtrace          off\n"
        "verbose         off\n"
        "vi              off\n"
        "emacs           off\n"
        "noclobber       off\n"
        "allexport       off\n"
        "notify          off\n"
        "nounset         on\n"
        "privileged      off\n"
        "nolog           off\n"
        "debug           off\n"}, "SetOptions");
    // `set +o`'s listing recreates the settings.
    ExpectSh({"set -eu; set +o",
        "set -o errexit\n"
        "set +o noglob\n"
        "set +o ignoreeof\n"
        "set +o interactive\n"
        "set +o monitor\n"
        "set +o noexec\n"
        "set +o stdin\n"
        "set +o xtrace\n"
        "set +o verbose\n"
        "set +o vi\n"
        "set +o emacs\n"
        "set +o noclobber\n"
        "set +o allexport\n"
        "set +o notify\n"
        "set -o nounset\n"
        "set +o privileged\n"
        "set +o nolog\n"
        "set +o debug\n"}, "SetOptions");
}

TEST_F(HshShellTest, Shift) {
    ExpectSh({"shift 2; echo $# $1", "1 c\n", "", 0, {"nm", "a", "b", "c"}}, "Shift");
    ExpectSh({"shift; echo $#", "0\n", "", 0, {"nm", "x"}}, "Shift");
    ExpectSh({"shift 0 9 8; echo $#", "1\n", "", 0, {"nm", "a"}}, "Shift");
    const ShellCase cases[] = {
        {"shift 4", "", "nm: 1: shift: can't shift that many\n", 2, {"nm", "a", "b", "c"}},
        {"shift", "", "hsh: 1: shift: can't shift that many\n", 2},
        {"shift x; echo after", "", "hsh: 1: shift: Illegal number: x\n", 2},
        {"shift -1", "", "hsh: 1: shift: Illegal number: -1\n", 2},
        {"shift 99999999999999999999", "", "hsh: 1: shift: Illegal number: 99999999999999999999\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Shift");
    }
}

TEST_F(HshShellTest, TestAndBracket) {
    WriteFile("/empty", "");
    const ShellCase cases[] = {
        {"test", "", "", 1}, {"[", "", "hsh: 1: [: missing ]\n", 2},
        {"test x"}, {"test ''", "", "", 1}, {"[ x ]"},
        {"[ ]", "", "", 1},
        // dash's POSIX two-, three- and four-operand reductions, and the
        // descent, byte for byte.
        {"test !"}, {"[ ! ]"}, {"test ! !", "", "", 1},
        {"test a -a", "", "", 1}, {"test a -o"},
        {"test \\( \\)", "", "", 1}, {"test \\( -d /docs \\)"},
        {"test = = ="}, {"test ! a = b"},
        {"test -e -e", "", "", 1}, {"test -n = x", "", "", 1}, {"test -n"},
        {"test a 2", "", "hsh: 1: test: a: unexpected operator\n", 2},
        {"test 1 2 3", "", "hsh: 1: test: 1: unexpected operator\n", 2},
        {"test = =", "", "hsh: 1: test: =: argument expected\n", 2},
        {"test = a", "", "hsh: 1: test: =: unexpected operator\n", 2},
        {"test = -n", "", "hsh: 1: test: =: unexpected operator\n", 2},
        {"test -a a", "", "hsh: 1: test: -a: unexpected operator\n", 2},
        {"test \\( x", "", "hsh: 1: test: closing paren expected\n", 2},
        {"test \\( \\) x", "", "hsh: 1: test: ): unexpected operator\n", 2},
        // [: the last argument only needs to begin with ']'.
        {"[ -n x ]foo"}, {"[ -n x", "", "hsh: 1: [: missing ]\n", 2},
        // Strings and numbers.
        {"[ a = a ]"}, {"[ a != a ]", "", "", 1}, {"[ a \\< b ]"}, {"[ b \\< a ]", "", "", 1},
        {"[ 12 -eq 12 ]"}, {"[ 12 -lt 5 ]", "", "", 1}, {"[ \" 12 \" -eq 12 ]"},
        {"[ -5 -lt 5 ]"}, {"[ 5 -ge 3 ]"}, {"[ 5 -gt 5 ]", "", "", 1},
        {"[ a -eq 1 ]; echo after", "after\n", "hsh: 1: [: Illegal number: a\n", 0},
        {"test 99999999999999999999 -eq 1", "", "hsh: 1: test: Illegal number: 99999999999999999999\n", 2},
        // Files. -r, -w, -x, -O and -G are "it is there" (no permissions or
        // users yet); -h, -L, -b, -p, -S, -u, -g, -k never match.
        {"[ -d /docs ]"}, {"[ -f /notes.txt ]"}, {"[ -d /notes.txt ]", "", "", 1},
        {"[ -e /none ]", "", "", 1}, {"[ -r /notes.txt ]"}, {"[ -w /docs ]"},
        {"[ -x /bin/ls ]"}, {"[ -O /notes.txt ]"}, {"[ -G / ]"},
        {"[ -h /notes.txt ]", "", "", 1}, {"[ -L /notes.txt ]", "", "", 1},
        {"[ -b /notes.txt ]", "", "", 1}, {"[ -p /z ]", "", "", 1}, {"[ -S /z ]", "", "", 1},
        {"[ -s /notes.txt ]"}, {"[ -s /empty ]", "", "", 1},
        // The fixture's descriptors are in-memory files: -t is false.
        {"test -t 1", "", "", 1}, {"test -t x", "", "hsh: 1: test: Illegal number: x\n", 2},
        {"test -t 5", "", "", 1},
        // -nt/-ot/-ef.
        {"test /notes.txt -nt /notes.txt", "", "", 1}, {"test /notes.txt -ot /notes.txt", "", "", 1},
        {"test none -nt /notes.txt", "", "", 1},
        {"test /notes.txt -ef /notes.txt"}, {"test /notes.txt -ef ./notes.txt"},
        {"test /notes.txt -ef /docs/a.md", "", "", 1}, {"test none -ef none", "", "", 1},
        // ! and -a/-o inside.
        {"[ ! -d /docs ]", "", "", 1},
        {"[ -d /docs -a -f /notes.txt ]"}, {"[ -d /docs -a -f /none ]", "", "", 1},
        {"[ -d /none -o -f /notes.txt ]"},
        {"[ \\( 1 = 1 \\) -o 1 = 2 ]"}, {"test -n a -a -z \"\""},
        {"test x = x -a ! y = z"}, {"test ! ! a"}, {"test \"\" -a a", "", "", 1},
        {"[ 1 -eq ]", "", "hsh: 1: [: -eq: argument expected\n", 2},
        {"[ x -foo y ]", "", "hsh: 1: [: x: unexpected operator\n", 2},
        {"test a b", "", "hsh: 1: test: a: unexpected operator\n", 2},
        {"test 1 -eq 1x", "", "hsh: 1: test: Illegal number: 1x\n", 2},
        // test errors are reported, status 2, and the shell goes on (test is
        // a regular builtin).
        {"[ a -eq 1 ]; echo after", "after\n", "hsh: 1: [: Illegal number: a\n", 0},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "TestAndBracket");
    }
}

TEST_F(HshShellTest, Xtrace) {
    // PS4 raw, then the assignments as name=value, then the fields, one space
    // each; the trace comes before the redirections.
    ExpectSh({"set -x\necho \"a b\" c\nx=1 y=\"2 3\"\nx=1 echo hi >/o.txt\necho \"[$x]\"",
        "a b c\n[1]\n",
        "+ echo a b c\n+ x=1 y=2 3\n+ x=1 echo hi\n+ echo [1]\n"}, "Xtrace");
    EXPECT_EQ(ReadRootFile("/o.txt"), "hi\n");
    // PS4 as it is, glued right onto the line (only the seed "+ " carries a
    // space) -- and dash makes the assignments before tracing, so a PS4
    // assignment restyles its own trace line.
    ExpectSh({"set -x; PS4='>'; echo a", "a\n", ">PS4=>\n>echo a\n"}, "Xtrace");
    // The trace goes to the stderr from before the command's redirections,
    // as dash: `2>f` does not catch it.
    ExpectSh({"set -x; echo a 2>/t.txt; : 2>/t.txt", "a\n", "+ echo a\n+ :\n"}, "Xtrace");
    EXPECT_EQ(ReadRootFile("/t.txt"), "");
    // +x turns it back off; set +x itself is still traced.
    ExpectSh({"set -x; echo a; set +x; echo b", "a\nb\n", "+ echo a\n+ set +x\n"}, "Xtrace");
}

TEST_F(HshShellTest, AllexportAndNoexec) {
    const ShellCase cases[] = {
        // -a exports assignments the shell makes: bare ones, and the prefix
        // of a shell builtin -- but not a child's prefix assignments.
        {"set -a; v=abc; hsh -c 'echo \"[$v]\"'", "[abc]\n"},
        {"set -a; x=1 echo hi; export -p", "hi\nexport PWD='/'\n"},
        {"set -a; y=2 :; export -p", "export PWD='/'\nexport y='2'\n"},
        {"set -a; v=1; export -p", "export PWD='/'\nexport v='1'\n"},
        // dash's readonly/export under -a export as well.
        {"set -a; readonly r=5; export -p", "export PWD='/'\nexport r='5'\n"},
        // -n: parse, don't run; parse errors still report. Once set there is
        // no way back within the run: `set +n` itself is skipped, as dash.
        {"set -n; echo no"},
        {"echo a; set -n; echo b", "a\n"},
        {"set -n; echo no; set +n; echo yes"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "AllexportAndNoexec");
    }
    Captured captured = RunCaptured("hsh", {"-n", "-c", "echo no"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("hsh", {"-n", "-c", "echo no; fi"});
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "hsh: 1: Syntax error: \"fi\" unexpected\n");
    EXPECT_EQ(captured.status, 2);
}

TEST_F(HshShellTest, PrefixAssignmentOfReadonlyExpandsFirst) {
    // dash expands a child command's prefix assignments before complaining
    // about a read-only variable: the substitution has run.
    const Captured captured = Sh("readonly x=1; x=$(pwd >/seen.txt) /bin/true");
    EXPECT_EQ(captured.out, "");
    EXPECT_EQ(captured.err, "hsh: 1: x: is read only\n");
    EXPECT_EQ(captured.status, 2);
    EXPECT_EQ(ReadRootFile("/seen.txt"), "/\n");
}

TEST_F(HshShellTest, ABrokenPipeInAnInShellStageEndsOnlyThatStage) {
    WriteFile("/big.txt", std::string(200000, 'q'));
    // `set` past a 200 KB variable far outgrows the 64 KiB pipe; /bin/echo
    // reads nothing. The in-shell stage dies of the broken pipe alone -- as a
    // forked dash subshell would -- and the shell goes on.
    ExpectSh({"x=$(cat /big.txt); set | /bin/echo x; echo $?", "x\n0\n"},
        "ABrokenPipeInAnInShellStageEndsOnlyThatStage");
}

TEST_F(HshShellTest, ABrokenPipeAtTheTopLevelEndsTheShell) {
    // The shell's stdout is a pipe nobody reads: the first byte a shell
    // builtin writes ends the whole shell, quietly, exit code 141.
    auto ends = os->GetPipeService()->CreatePipe();
    ends.readEnd.reset();
    auto stdErr = std::make_shared<Mocks::MockFileDescriptor>();
    StartProcessOptions options;
    options.stdOut = ends.writeEnd;
    options.stdErr = stdErr;
    auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/hsh",
        {"-c", "set; echo after"}, "/", options);
    ASSERT_NE(process, nullptr);
    ends.writeEnd.reset();  // the test's copy: the process holds the only one
    EXPECT_TRUE(process->WaitToFinish(kWaitMs));
    EXPECT_EQ(process->ExitCode().value_or(-1), 141);
    EXPECT_EQ(stdErr->Written(), "");
}

} // namespace Haisos
