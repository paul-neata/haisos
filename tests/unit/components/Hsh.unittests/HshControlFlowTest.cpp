#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "HshShellFixture.h"

namespace Haisos {

TEST_F(HshShellTest, GroupsAndSubshells) {
    const ShellCase cases[] = {
        {"{ echo a; echo b; } > /g.txt; cat /g.txt", "a\nb\n"},
        // A subshell leaks nothing: not its variables, not its directory.
        {"x=1; (x=2; cd /docs; echo $x); echo $x; pwd", "2\n1\n/\n"},
        {"(exit 3); echo $?", "3\n"},
        {"{ echo in; } | wc -l", "1\n"},
        {"( echo e >&2 ) 2>/e.txt; cat /e.txt", "e\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "GroupsAndSubshells");
    }
}

TEST_F(HshShellTest, IfWhileUntil) {
    const ShellCase cases[] = {
        {"if true; then echo y; else echo n; fi", "y\n"},
        {"if false; then echo n; else echo y; fi", "y\n"},
        {"if false; then :; elif true; then echo elif; fi", "elif\n"},
        {"if false; then :; fi; echo $?", "0\n"},
        {"i=0; while [ $i -lt 3 ]; do i=$((i+1)); echo $i; done", "1\n2\n3\n"},
        {"i=0; until [ $i -ge 2 ]; do i=$((i+1)); echo $i; done", "1\n2\n"},
        {"until true; do :; done; echo $?", "0\n"},
        {"while false; do :; done; echo $?", "0\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "IfWhileUntil");
    }
}

TEST_F(HshShellTest, ForAndCase) {
    const ShellCase cases[] = {
        {"for f in a b c; do echo $f; done", "a\nb\nc\n"},
        {"for x in; do echo no; done; echo $?", "0\n"},
        {"set -- p q; for x; do echo $x; done", "p\nq\n"},
        {"for f in /docs/*; do echo $f; done", "/docs/a.md\n/docs/sub\n"},
        {"case abc in a*) echo m;; esac", "m\n"},
        {"case x in y) ;; esac; echo $?", "0\n"},
        {"case \"a b\" in \"a b\") echo q;; esac", "q\n"},
        // Quoted matches literally; unquoted $x with x='*' is a glob pattern.
        {"x='*'; case a in \"$x\") echo lit;; $x) echo glob;; esac", "glob\n"},
        {"case b in a|b) echo or;; esac", "or\n"},
        {"case z in (z) echo paren;; esac", "paren\n"},
        // A read-only loop variable is fatal.
        {"readonly r=1; for r in a; do :; done; echo no", "", "hsh: 1: r: is read only\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "ForAndCase");
    }
}

TEST_F(HshShellTest, BreakContinueReturn) {
    const ShellCase cases[] = {
        {"for i in 1 2 3; do [ $i = 2 ] && break; echo $i; done", "1\n"},
        // break 5 leaves both loops.
        {"for i in 1 2; do for j in a b; do break 5; done; echo $i; done; echo end", "end\n"},
        // continue 2 with only one loop running continues it.
        {"for i in a b; do continue 2; echo no; done; echo $i", "b\n"},
        // A break inside a subshell ends the subshell, not the outer loop.
        {"for i in 1 2; do (break); echo $i; done", "1\n2\n"},
        // Outside a loop break is a quiet 0.
        {"break; echo $?", "0\n"},
        {"for i in 1; do break 0; done", "", "hsh: 1: break: Illegal number: 0\n", 2},
        {"for i in 1; do continue x; done", "", "hsh: 1: continue: Illegal number: x\n", 2},
        // A break in a function does not reach the caller's loop.
        {"f(){ break; }; for i in 1 2; do f; echo $i; done", "1\n2\n"},
        {"f(){ return 7; echo no; }; f; echo $?", "7\n"},
        // A top-level return ends the -c string.
        {"return 3; echo no", "", "", 3},
        {"return x", "", "hsh: 1: return: Illegal number: x\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "BreakContinueReturn");
    }
}

TEST_F(HshShellTest, Functions) {
    const ShellCase cases[] = {
        // A call has its own positional parameters; $0 does not change.
        {"f(){ echo \"$# $1\"; set -- z; }; set -- a b; f x; echo \"$1\"", "1 x\na\n"},
        {"f() { echo $0 $1; }; f a", "hsh a\n"},
        {"f(){ x=in; }; x=out; f; echo $x", "in\n"},
        // Prefix assignments are the call's only.
        {"f(){ echo \"x=$x\"; }; x=a f; echo \"after=[$x]\"", "x=a\nafter=[]\n"},
        // A function may override a regular builtin, not a special one.
        {"cd() { echo mine; }; cd /", "mine\n"},
        {"exit() { echo mine; }; exit", "", "", 0},
        // The definition's redirections apply on every call.
        {"f() { echo out; } > /f.txt; f; cat /f.txt", "out\n"},
        // A function as a pipeline stage runs in a subshell.
        {"f() { echo piped; }; f | wc -l", "1\n"},
        {"f(){ :; }; unset -f f; f", "", "hsh: 1: f: not found\n", 127},
        // A definition inside a substitution does not leak.
        {"x=$(f(){ echo sub; }; f); echo $x; f", "sub\n", "hsh: 1: f: not found\n", 127},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Functions");
    }
}

TEST_F(HshShellTest, BackgroundCompoundCommandsAndFunctions) {
    const ShellCase cases[] = {
        // The background child hsh gets the functions from the prelude.
        {"f() { echo in-bg; }; f & wait", "in-bg\n"},
        {"{ echo a; echo b; } > /bg.txt & wait $!; cat /bg.txt", "a\nb\n"},
        {"(exit 4) & wait $!; echo $?", "4\n"},
        {"for i in 1 2; do echo $i; done | wc -l & wait", "2\n"},
        // The child sees only exported variables: a documented exception.
        {"x=1; { echo \"[$x]\"; } & wait", "[]\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "BackgroundCompoundCommandsAndFunctions");
    }
}

TEST_F(HshShellTest, DotAndEval) {
    WriteFile("/src.sh", "echo sourced $1\nreturn 4\necho no\n");
    WriteFile("/bad.sh", "echo in\nfi\n");
    const ShellCase cases[] = {
        // dash does not set $1 from the extra operands; return ends the script.
        {". /src.sh x; echo $?", "sourced\n4\n"},
        {".; echo $?", "0\n"},
        {". nosuchfile; echo no", "", "hsh: 1: .: nosuchfile: not found\n", 2},
        // A syntax error in the file is named after it, at its line.
        {". /bad.sh; echo after", "in\n", "hsh: 2: /bad.sh: Syntax error: \"fi\" unexpected\n", 2},
        // A name without a slash is searched in PATH.
        {"PATH=/docs; . a.md", "", "hsh: 1: alpha: not found\n", 127},
        {"eval \"echo a; echo b\"; eval; echo $?", "a\nb\n0\n"},
        {"x='echo $y'; y=v; eval $x", "v\n"},
        {"eval \"fi\"; echo after", "", "hsh: 1: eval: Syntax error: \"fi\" unexpected\n", 2},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "DotAndEval");
    }
}

TEST_F(HshShellTest, Read) {
    const ShellCase cases[] = {
        // The last name takes the rest of the line.
        {"read x y; echo \"[$x][$y]\"", "[a][b c]\n", "", 0, {}, "a b c\n"},
        // A backslash protects the next byte from splitting; newline joins.
        {"read x y; echo \"[$x][$y]\"", "[a b][c]\n", "", 0, {}, "  a\\ b  c  \n"},
        {"read -r x y; echo \"[$x][$y]\"", "[a\\][b  c]\n", "", 0, {}, "  a\\ b  c  \n"},
        {"read x; echo \"[$x]\"", "[ab]\n", "", 0, {}, "a\\\nb\n"},
        // End of the input: status 1, the variables still set.
        {"read x; echo \"st=$? [$x]\"", "st=1 [noeol]\n", "", 0, {}, "noeol"},
        {"read x; echo \"st=$? [$x]\"", "st=1 []\n", "", 0, {}, ""},
        // A non-white IFS character delimits once.
        {"IFS=:; read x y; echo \"[$x][$y]\"", "[a][b:c]\n", "", 0, {}, "a:b:c\n"},
        // One name: inner delimiters kept, the edges' IFS white space off.
        {"read x; echo \"[$x]\"", "[a b]\n", "", 0, {}, "a b\n"},
        {"read", "", "hsh: 1: read: arg count\n", 2},
        {"read 1x", "", "hsh: 1: read: 1x: bad variable name\n", 2, {}, "q\n"},
        {"read -z x", "", "hsh: 1: read: Illegal option -z\n", 2, {}, "q\n"},
        // The prompt goes to stderr only when stdin is a terminal: empty here.
        {"read -p \"P> \" x; echo $x", "q\n", "", 0, {}, "q\n"},
        // Byte-wise input: cat gets the rest of the same stdin.
        {"read a; cat", "l2\n", "", 0, {}, "l1\nl2\n"},
        // Left-over names are set empty.
        {"read x y; echo \"[$x][$y]\"", "[a][]\n", "", 0, {}, "a\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Read");
    }
    // Reading a file one line at a time; the last line's leading tab goes.
    ExpectSh({"while read l; do echo \"<$l>\"; done < /notes.txt",
        "<one>\n<two>\n<>\n<>\n<three>\n"}, "Read");
    // An in-shell stage between two child stages, reading a pipe byte-wise.
    ExpectSh({"cat /notes.txt | while read l; do echo $l; done | wc -l", "5\n"}, "Read");
}

TEST_F(HshShellTest, Errexit) {
    const ShellCase cases[] = {
        // Tested commands never trip it: ||/&& operands, conditions, a
        // negated pipeline, a short-circuited &&.
        {"set -e; false || echo or; if false; then :; fi; ! true; false && x; echo still",
            "or\nstill\n"},
        // A function called in a tested context inherits it ...
        {"set -e; f(){ false; echo inf; }; f && :; echo after; false; echo no",
            "inf\nafter\n", "", 1},
        // ... while untested, the last command's failure ends it.
        {"set -e; f(){ false; echo inf; }; f; echo no", "", "", 1},
        // A subshell that exits through errexit gives its status to the
        // subshell command, which trips -e outside.
        {"set -e; (false); echo no", "", "", 1},
        {"set +e; false; echo on; set -e; false; echo no", "on\n", "", 1},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "Errexit");
    }
    // -e as an invocation option, on a script file.
    WriteFile("/e.sh", "echo a\nfalse\necho b\n");
    const Captured captured = RunCaptured("hsh", {"-e", "/e.sh"});
    EXPECT_EQ(captured.out, "a\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 1);
}

TEST_F(HshShellTest, NestedEverywhere) {
    const ShellCase cases[] = {
        {"x=$(for i in 1 2; do echo $i; done); echo $x", "1 2\n"},
        {"{ while read l; do echo $l; done; } <<E\nh1\nE\n", "h1\n"},
        // In-shell stages on both ends of a child: the pipe rule.
        {"for i in a b; do echo $i; done | cat | while read j; do echo \"<$j>\"; done",
            "<a>\n<b>\n"},
        {"case $(echo x$y) in x) echo hit;; esac", "hit\n"},
    };
    for (const auto& c : cases) {
        ExpectSh(c, "NestedEverywhere");
    }
}

TEST_F(HshShellTest, AcceptanceScenarioThree) {
    // The exact text of goal.md's scenario 3: a for loop, $((...)), a heredoc
    // inside $(...), [, &&, if and case.
    WriteFile("/count.sh",
        "n=0\n"
        "for f in a b c; do n=$((n + 1)); done\n"
        "lines=$(wc -l <<EOF\n"
        "x\n"
        "y\n"
        "EOF\n"
        ")\n"
        "if [ \"$n\" -eq 3 ] && [ \"$lines\" -eq 2 ]; then echo \"ok $n $lines\"; else echo \"bad\"; exit 1; fi\n"
        "case \"$1\" in hi*) echo \"greeted\";; *) echo \"plain\";; esac\n");
    // "hi" matches hi* (goal.md's run shows "hello", which dash -- the
    // reference -- does not match: it starts with "he"; so the greeting arm
    // is exercised with "hi"; "hello"/"plain" fall through to *).
    Captured captured = RunCaptured("hsh", {"/count.sh", "hi"});
    EXPECT_EQ(captured.out, "ok 3 2\ngreeted\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("hsh", {"/count.sh", "hello"});
    EXPECT_EQ(captured.out, "ok 3 2\nplain\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
    captured = RunCaptured("hsh", {"/count.sh", "plain"});
    EXPECT_EQ(captured.out, "ok 3 2\nplain\n");
    EXPECT_EQ(captured.err, "");
    EXPECT_EQ(captured.status, 0);
}

} // namespace Haisos
