# Task hsh--control-flow: hsh compound commands, functions, ., eval, read, set -e

- Rock: hsh
- Depends on: hsh--shell-builtins
- Size: ~1050 changed lines in ~11 files (grown by the two preliminary fixes
  below; finding 3 (`RedirectionScope`'s double-`Apply`) goes to the final
  whole-develop review instead, not this task)
- Plan checked against: develop @ 2294d67
- PR title: hsh: control flow, functions, dot scripts, eval, read and -e

## Goal

`hsh` runs the whole POSIX command language, as dash does:

- `{ ...; }`, `( ... )` (an in-process subshell: nothing it changes leaks),
  `if`/`elif`/`else`, `while`, `until`, `for` (with and without `in`), `case`
  (shell patterns), each with its own redirections (`while read l; do ...; done < f`);
- functions: `f() { ...; }`, called with their own positional parameters,
  prefix assignments for the call only, `return [n]`;
- `break [n]`, `continue [n]`, `.` (dot scripts), `eval`, `read [-r] [-p prompt] name...`
  (reading the shell's stdin one byte at a time, so it never takes more of a
  pipe than its line), `unset -f`;
- `set -e` (errexit) with dash's exceptions for tested commands;
- compound commands and functions in pipelines, `$(...)` and `&` (through the
  machinery of hsh--pipelines);
- acceptance scenario 3 (`/count.sh`: a `for` loop, `$((...))`, a heredoc in
  `$(...)`, `[`, `&&`, `if`, `case`) prints `ok 3 2` and `greeted`.

No "not supported yet" placeholder remains.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (all of
it), `HshShell.h`, `HshSubshell.h`, `HshRedirection.h`, `HshBuiltins.h`,
`HshAst.h`, `HshPattern.h`; the root `CLAUDE.md` ("Security"); the dash
manual's "Compound Commands", "Functions", "Builtins" (`.`, `break`,
`continue`, `eval`, `read`, `return`, `unset`) and `-e` under "Argument List
Processing". Every behaviour below was checked against dash 0.5.12.

What earlier tasks provide (as if on develop; the code wins on names):

- hsh--parser: `BraceGroup { body }`, `Subshell { body }`, `IfCommand {
  branches (IfBranch { condition, body }), elseBody }`, `LoopCommand {
  condition, body }` (kind `While` or `Until`), `ForCommand { variable, hasIn,
  words, body }`, `CaseCommand { subject, items (CaseItem { patterns, body }) }`,
  `FunctionDefinition { name, body (CommandPtr) }`; `Command::redirections`
  on compound commands (a function definition's are on its body);
  `Parser`/`ParseNext`/`ParseProgram`.
- hsh--arith-glob: `MatchPattern(pattern, text)`.
- hsh--expansion: `Expander::ExpandWords`, `ExpandToString` (case subject),
  `ExpandPattern` (case patterns).
- hsh--executor: `Shell::ExecuteList/AndOr/Pipeline/Command/SimpleCommand`,
  `Fail`, `Report`, `WriteErr`, `LookUpCommand`, `ShellExit`, `ShellStopped`,
  `ShellBuiltin` table, `NotYet` (still used for the compound kinds and
  function definitions in `ExecuteCommand`), `Invocation`, the fixture
  `HshShellFixture.h`.
- hsh--redirections: `RedirectionScope` (`Apply` -> optional error message,
  `Restore`, `Keep`).
- hsh--pipelines: `SubshellScope` (with a comment where the function table and
  the depths are to be added), `Shell::RunSubshell(body)`, `InSubshell()`,
  `IsChildStage(command)` (with a comment where functions are to be excluded).
- hsh--shell-builtins: `Shell::AssignVariable(name, value)`; `unset -f`
  succeeding without effect; `test`/`[`.
- `OpenFailureReason(io, path, creating)`; `ReadWholeDescriptor`.

## Changes

All in namespace `Haisos::Hsh`, `src/components/BuiltinCommands/commands/hsh/`,
plain portable C++17.

### Rules that bite (restated)

- `.` reads its file through `IO()` (`OpenFile`, `ReadWholeDescriptor`), and
  `read` reads the shell's slot 0 -- nothing else is reached.
- A subshell is in-process (decided in hsh--pipelines): no thread, no child
  hsh. It is the cheaper and exact choice -- dash's fork copies the whole
  shell state, which `SubshellScope` saves and restores. Only a background
  list that needs the shell (`{ ...; } &`, `( ... ) &`, `f &`) runs in a child
  `hsh -c` (hsh--pipelines), which sees only exported variables (documented).
- hsh's version becomes `0.5.0`.
- New files in the two `CMakeLists.txt`.

### Preliminary fixes (do these first; three review findings against PRs #36/#37)

Small, independent of the rest; fix and test each before starting the control
flow below -- loops and functions are what will actually exercise them.

1. **A child stage's `$(...)` can deadlock on an earlier in-shell stage**
   (`HshShell.cpp`, `ExecutePipelinedStages`/`RunStage`, around the pass-1/
   pass-2 split, and `IsChildStage`). `IsChildStage` only looks at the first
   word's `LiteralText`; it does not notice a `CommandSubstitution` anywhere
   in the command's words. So `: | /bin/echo $(cat)` makes stage 1 (`/bin/echo
   $(cat)`) a child stage: `RunStage` places its stdin (the real, bounded pipe
   from stage 0, `:`) and calls `ExecuteCommand`, which expands `$(cat)` --
   in pass 1, before stage 0 has run in pass 2 -- so `cat` blocks reading a
   pipe nothing has written to yet, and pass 2 (which would run `:` and close
   the write end) never gets to start: a deadlock. Fix `IsChildStage` to also
   return false for a `SimpleCommand` holding a `CommandSubstitution`
   anywhere in its words (a small recursive walk of the `Word`/`WordPart`
   tree, `DoubleQuoted` included) -- add it next to the "exclude functions"
   change `IsChildStage` gets below, in the same function. (Re-expanding it
   only once earlier stages could have run is the other option the review
   named; excluding it from child-stage status is the smaller change and
   leaves the pass-1/pass-2 ordering untouched.)
   - Test (`HshPipelineTest.cpp`, existing file): a new case running
     `: | /bin/echo $(cat)` off a background thread (`std::async`) with a
     bounded wait (`wait_for(std::chrono::seconds(5))`; fail the test with a
     clear message, not hang, if it is not done by then) -- expect it to
     finish, printing an empty line. Name it
     `ChildStageCommandSubstitutionDoesNotDeadlock`.
   - [ ] Acceptance: `ChildStageCommandSubstitutionDoesNotDeadlock` passes
     inside its bounded wait (it would hang past it before the fix).

2. **A subshell's background job leaks its processes into `m_liveChildren`**
   (`HshSubshell.cpp`, `SubshellScope`). A `Job` started inside a subshell
   (`( /bin/true & )`, or later `f() { ...; } &` called from one) is recorded
   in the subshell's own `m_jobs` (saved/restored like everything else) and
   its processes are added to `Shell::m_liveChildren` (`StartChild`), which
   `SubshellScope` does not save or restore. `~SubshellScope` puts the outer,
   pre-subshell `m_jobs` back, dropping that job -- and with it the only way
   `wait` could ever reach those processes -- while they stay in
   `m_liveChildren` for the life of the shell: `( /bin/true & )` run
   repeatedly grows it without bound. Fix `SubshellScope`'s destructor to
   remove from `m_shell.m_liveChildren` (or wait for) every process of a job
   that is about to be dropped -- every job present in the subshell's
   `m_jobs` but not in the saved, pre-subshell list -- symmetric with how it
   already restores everything else a subshell must not leak.
   - Test (new case in `HshShellTest.cpp`, or a new `HshSubshellTest.cpp` if
     the implementer prefers a file of its own): run `( /bin/true & )` three
     times through `Sh` (no loop needed to reach this) and check, through
     whatever the fix exposes for tests (a small test-only accessor on
     `Shell` is fine, e.g. a count of `m_liveChildren`), that the count after
     the third does not exceed the count after the first. Name it
     `SubshellBackgroundJobDoesNotLeakLiveChildren`.
   - [ ] Acceptance: `SubshellBackgroundJobDoesNotLeakLiveChildren` passes;
     the live-child count does not grow run over run.

(A third finding -- `RedirectionScope::Save` in `HshRedirection.cpp` dedups
across the whole scope, not per `Apply` -- goes to the final whole-develop
review instead of this task.)

### `HshShell.h` / `.cpp` -- control flow

New exception types (beside `ShellExit`):

```cpp
// break n / continue n: unwinds to the loops (levels counts them).
struct LoopControl { bool isBreak; int levels; };
// return n: unwinds to the function call or the dot script running.
struct FunctionReturn { int status; };
```

New members (private unless said; the names are the implementer's except
where other code uses them):

```cpp
std::map<std::string, CommandPtr> m_functions;
int m_loopDepth = 0;            // loops running in the current function (or top level)
int m_functionDepth = 0;        // function calls running
int m_dotDepth = 0;             // dot scripts running
int m_errexitSuppressed = 0;    // > 0 inside a tested context (see "errexit")
public: int LoopDepth() const; int FunctionDepth() const; int DotDepth() const;
public: bool HasFunction(const std::string& name) const;
public: void RemoveFunction(const std::string& name);
```

`BackgroundPrelude()` (hsh--pipelines) appends, after its `set` line, the
`sourceText` of every function in `m_functions` (`FunctionDefinition::sourceText`,
the whole definition as written, from hsh--parser), each followed by `\n`, so
a background child hsh has the functions: `f() { echo in-bg; }; f & wait`
prints `in-bg`.

`SubshellScope` additionally saves and restores `m_functions`,
`m_loopDepth`, `m_functionDepth`, `m_dotDepth`, `m_errexitSuppressed`.
`RunSubshell` also catches `LoopControl` (the subshell ends, status 0: `for i
in 1 2; do (break); echo $i; done` prints both) and `FunctionReturn` (the
subshell ends with its status).

`ExecuteCommand` for every kind but `Simple` and `FunctionDefinition`:
`RedirectionScope scope(*this)`; an error from `Apply` -> `Report` it,
status 2 (compound commands are not special builtins); then by kind:

- `BraceGroup`: `ExecuteList(body)`.
- `Subshell`: `RunSubshell([&] { return ExecuteList(body); })`.
- `If`: for each branch, its condition run as a tested context
  (`++m_errexitSuppressed` around it); status 0 -> return `ExecuteList(body)`.
  No branch taken: `elseBody` if any, else status 0.
- `While` / `Until`: `++m_loopDepth`; loop: the condition as a tested
  context; stop when it fails (`While`) / succeeds (`Until`); run the body;
  the status is the last body's, 0 if it never ran. `LoopControl` from the
  body: break with `levels > 1` -> leave this loop and rethrow with
  `levels - 1`; break -> leave this loop; continue with `levels > 1` -> leave
  this loop and rethrow `continue` with `levels - 1`; continue -> next
  iteration (the condition again). `--m_loopDepth` on every way out.
- `For`: the words: `hasIn` -> `ExpandWords(words)`; else the positional
  parameters. For each: `AssignVariable(variable, word)` (read-only is fatal:
  `r: is read only`), then the body, with the same `LoopControl` handling.
  Status: the last body's, 0 if none ran.
- `Case`: `subject = ExpandToString(subject)`; the first item with a pattern
  for which `MatchPattern(ExpandPattern(pattern), subject)` holds runs its body
  (an empty body: status 0); patterns of later items are not expanded. No
  match: 0.
- `FunctionDefinition` (no redirection scope of its own): `m_functions[name] = body`,
  status 0.
- Remove `NotYet` and every use of it.

#### Functions

- Lookup order in `ExecuteSimpleCommand` (POSIX): special builtin, then
  function, then regular builtin, then `PATH`. So a function may override
  `cd` or `test`, not `exit`.
- A call: save the positional parameters, set them to `fields[1..]`; the
  prefix assignments are made for the call only, as for a regular builtin
  (`x=a f` sees `x=a`, afterwards `x` is as before); save `m_loopDepth` and
  set it to 0 (dash: a `break` inside a function does not reach the caller's
  loop); `++m_functionDepth`; `ExecuteCommand(*body)` (the body's
  redirections apply on every call); `FunctionReturn r` -> its status. Put
  everything back on every way out (RAII). `$0` does not change.
- `IsChildStage` returns false for a function name.
- `unset -f name`: `RemoveFunction(name)` (in `HshBuiltinVariables.cpp`).

#### errexit (`set -e`)

In `ExecuteAndOr`: every pipeline but the last runs as a tested context
(`++m_errexitSuppressed` around it); so does a negated pipeline. After the
**last** pipeline of the list has run (not a short-circuited one), if its
status is non-zero, `options.errexit` is on, `options.interactive` is off, it
is not negated and `m_errexitSuppressed == 0`: `throw ShellExit{status}`.
Conditions of `if`/`elif`/`while`/`until` are tested contexts; a function
called inside one inherits it (dash: `f && :` runs all of `f` even if a
command in it fails). A subshell that exits through errexit gives its status
to the subshell command, which may then trigger errexit outside.

#### `break`, `continue`, `return` (`HshBuiltinFlow.cpp`, new; all special)

- `break [n]`, `continue [n]`: n defaults to 1, must be digits and at least
  1, else `Fail("break: Illegal number: <n>")` (`continue: ...`). Outside a
  loop (`m_loopDepth == 0`): status 0, nothing happens. Else
  `throw LoopControl{isBreak, std::min(n, m_loopDepth)}` (`break 5` in two
  loops leaves both).
- `return [n]`: n defaults to `$?`, digits only, else
  `Fail("return: Illegal number: <n>")`. Inside a function or a dot script:
  `throw FunctionReturn{n & 0xFF}`; outside both: `throw ShellExit{n & 0xFF}`
  (dash ends a script or `-c` string on a top-level `return`).

#### `.` and `eval` (`HshBuiltinFlow.cpp`; both special)

- `. file [args]`: no operand -> status 0. A name with a `/` is used as
  given; otherwise the first `PATH` entry holding it (as `LookUpCommand`,
  without the runnable distinction); none -> `Fail(".: <file>: not found")`.
  `OpenFile` failure -> `Fail(".: cannot open <file>: <reason>")`. Further
  operands are ignored (dash does not set `$1`). Run the text as a script:
  `++m_dotDepth`; `Parser(text, {1, false, "end of file"})` and `ParseNext`
  one complete command at a time, `ExecuteList` each; a syntax error ->
  `throw ShellError("<file>: " + errorMessage, errorLine)` (dash:
  `hsh: 2: ./bad.sh: Syntax error: "fi" unexpected`); `FunctionReturn r` ->
  its status ends the script. Status: the last command's, 0 if none.
- `eval args...`: the arguments joined with single spaces; empty -> status 0.
  Parse with `Parser(text, {CurrentLine(), false, "end of file"})` one
  complete command at a time, running each; a syntax error ->
  `Fail("eval: " + errorMessage)` (dash: `hsh: 1: eval: Syntax error: "fi" unexpected`).
  Status: the last command's, 0 if none ran.

#### `read` (`HshBuiltinRead.cpp`, new; regular)

1. Options: `-r`; `-p prompt` (the next argument); `--` ends them; any other
   -> `Report("read: Illegal option -<c>")`, 2. No names -> `Report("read: arg count")`,
   2. An invalid name -> `Report("read: <name>: bad variable name")`, 2.
2. The prompt is written to stderr (`WriteErr`) only when slot 0 is a
   terminal (dash).
3. **Byte-wise input**: read slot 0 with `Read(&c, 1)` until a newline or
   the end (0, or an empty slot); `kIOInterrupted` -> `throw ShellStopped`;
   any other negative result ends the input. One byte at a time is what
   keeps `read` from taking bytes of a pipe beyond its line, so the next
   command reading the same stdin starts right after it
   (`{ read a; cat; } < f` prints the rest of `f`).
4. Without `-r`: a backslash before a newline joins the lines (both dropped,
   reading goes on); a backslash before any other byte makes that byte
   literal (kept, and protected from field splitting); a trailing lone
   backslash at the end of input is dropped. With `-r` backslashes are plain.
5. Splitting by `IFS` (unset: space, tab, newline; empty: no splitting --
   the whole line goes to the first name): leading IFS white space is
   skipped; each name but the last takes one field (IFS white space runs
   collapse; one non-white IFS character, with the white space around it,
   delimits once); the last name takes the rest of the line with its leading
   and trailing (unprotected) IFS white space removed, inner delimiters kept.
   Names left over are set empty. Every assignment through `AssignVariable`.
6. Status 0 when a newline ended the line, 1 at the end of input (the
   variables are set even then).

dash vectors (input -> variables): `a b c\n` with `x y` -> `a`, `b c`;
`  a\ b  c  \n` -> `a b`, `c`; the same with `-r` -> `a\`, `b  c`;
`a\<newline>b\n` with `x` -> `ab`; `noeol` (no newline) -> `noeol`, status 1;
empty input -> status 1; `IFS=:` and `a:b:c\n` with `x y` -> `a`, `b:c`;
`a b\n` with `x` -> `a b`.

### `HshBuiltins.h` / `.cpp`, `Hsh.cpp`

Rows: `.` (special), `break` (special), `continue` (special), `eval`
(special), `read` (regular), `return` (special). Version `0.5.0`.

## Tests

No fixed sleeps anywhere (the three preliminary tests above included): wait on
`wait`/a bounded `wait_for`, or poll with a bound, never a bare
`std::this_thread::sleep_for` hoping a race resolves -- a regression must fail
the test, not hang it. No new `HaisosOS.unittests` tests (0): every test below
and above is in `Hsh.unittests`.

`tests/unit/components/Hsh.unittests/HshControlFlowTest.cpp` (new; add to
`add_executable`), `TEST_F(HshShellTest, ...)`, tables through `Sh`, byte for
byte (multi-line scripts written with `\n` in the C++ string); every test is
named below -- implement each one, do not skip any for time:

- `GroupsAndSubshells`: `{ echo a; echo b; } > /g.txt; cat /g.txt` ->
  `a\nb\n`; `x=1; (x=2; cd /docs; echo $x); echo $x; pwd` -> `2\n1\n/\n`;
  `(exit 3); echo $?` -> `3\n`; `{ echo in; } | wc -l` -> `1\n`;
  `( echo e >&2 ) 2>/e.txt; cat /e.txt` -> `e\n`.
- `IfWhileUntil`: `if true; then echo y; else echo n; fi`; `if false; then :;
  elif true; then echo elif; fi`; `if false; then :; fi; echo $?` -> `0\n`;
  `i=0; while [ $i -lt 3 ]; do i=$((i+1)); echo $i; done` -> `1\n2\n3\n`;
  `until true; do :; done; echo $?` -> `0\n`; `while false; do :; done; echo $?`
  -> `0\n`.
- `ForAndCase`: `for f in a b c; do echo $f; done`; `for x in; do echo no; done; echo $?`
  -> `0\n`; `set -- p q; for x; do echo $x; done` -> `p\nq\n`;
  `for f in /docs/*; do echo $f; done` -> `/docs/a.md\n/docs/sub\n`;
  `case abc in a*) echo m;; esac`; `case x in y) ;; esac; echo $?` -> `0\n`;
  `case "a b" in "a b") echo q;; esac`; `x='*'; case a in "$x") echo lit;; $x) echo glob;; esac`
  -> `glob\n`; `case b in a|b) echo or;; esac`; `case z in (z) echo paren;; esac`;
  `readonly r=1; for r in a; do :; done; echo no` -> err `hsh: 1: r: is read only\n`, 2.
- `BreakContinueReturn`: `for i in 1 2 3; do [ $i = 2 ] && break; echo $i; done`
  -> `1\n`; `for i in 1 2; do for j in a b; do break 5; done; echo $i; done; echo end`
  -> `end\n`; `for i in a b; do continue 2; echo no; done; echo $i` -> `b\n`;
  `for i in 1 2 3; do (break); echo $i; done` -> `1\n2\n3\n`; `break; echo $?`
  -> `0\n`; `for i in 1; do break 0; done` -> err `hsh: 1: break: Illegal number: 0\n`, 2;
  `f(){ break; }; for i in 1 2; do f; echo $i; done` -> `1\n2\n`;
  `f(){ return 7; echo no; }; f; echo $?` -> `7\n`; `return 3; echo no` ->
  status 3; `return x` -> `return: Illegal number: x`, 2.
- `Functions`: `f(){ echo "$# $1"; set -- z; }; set -- a b; f x; echo "$1"` ->
  `1 x\na\n`; `f(){ x=in; }; x=out; f; echo $x` -> `in\n`;
  `f(){ echo "x=$x"; }; x=a f; echo "after=[$x]"` -> `x=a\nafter=[]\n`;
  `f() { echo $0 $1; }; f a` -> `hsh a\n`; `cd() { echo mine; }; cd /` ->
  `mine\n`; `f() { echo out; } > /f.txt; f; cat /f.txt` -> `out\n`;
  `f() { echo piped; }; f | wc -l` -> `1\n`; `f(){ :; }; unset -f f; f` ->
  `hsh: 1: f: not found`, 127; `x=$(f(){ echo sub; }; f); echo $x; f` ->
  `sub\n` then `f: not found` (the definition did not leak).
- `BackgroundCompoundCommandsAndFunctions`: `f() { echo in-bg; }; f & wait`
  -> `in-bg\n`; `{ echo a; echo b; } > /bg.txt & wait $!; cat /bg.txt` ->
  `a\nb\n`; `(exit 4) & wait $!; echo $?` -> `4\n`;
  `for i in 1 2; do echo $i; done | wc -l & wait` -> `2\n`; `x=1; { echo "[$x]"; } & wait`
  -> `[]\n` (unexported: the documented exception).
- `DotAndEval`: `/src.sh` = `echo sourced $1` / `return 4` / `echo no`:
  `. /src.sh x; echo $?` -> `sourced\n4\n`; `.; echo $?` -> `0\n`;
  `. nosuchfile; echo no` -> err `hsh: 1: .: nosuchfile: not found\n`, 2;
  `/bad.sh` = `echo in` / `fi`: `. /bad.sh; echo after` -> out `in\n`, err
  `hsh: 2: /bad.sh: Syntax error: "fi" unexpected\n`, 2; `PATH=/docs; . a.md`
  with `/docs/a.md` = `alpha`: err `hsh: 1: alpha: not found\n` (found in
  PATH and run); `eval "echo a; echo b"; eval; echo $?` -> `a\nb\n0\n`;
  `x='echo $y'; y=v; eval $x` -> `v\n`; `eval "fi"; echo after` -> err
  `hsh: 1: eval: Syntax error: "fi" unexpected\n`, 2.
- `Read`: each dash vector above as `printf`-free input through the `input`
  parameter of `Sh`: e.g. input `a b c\n`, script `read x y; echo "[$x][$y]"`
  -> `[a][b c]\n`; `read -r x y` on `  a\ b  c  \n` -> `[a\][b  c]\n`;
  `read x; echo "st=$? [$x]"` on `noeol` -> `st=1 [noeol]\n`; `read` ->
  err `hsh: 1: read: arg count\n`, out per script; `read 1x` ->
  `read: 1x: bad variable name`; `read -z x` -> `read: Illegal option -z`;
  `read -p "P> " x; echo $x` on `q\n` -> out `q\n`, err empty (stdin is not a
  terminal); `read a; cat` on `l1\nl2\n` -> `l2\n` (byte-wise: cat gets the
  rest); `while read l; do echo "<$l>"; done < /notes.txt` ->
  `<one>\n<two>\n<>\n<>\n<three>\n`; `cat /notes.txt | while read l; do echo $l; done | wc -l`
  -> `5\n` (an in-shell stage between two child stages).
- `Errexit`: `set -e; false || echo or; if false; then :; fi; ! true; false && x; echo still`
  -> `or\nstill\n`; `set -e; f(){ false; echo inf; }; f && :; echo after; false; echo no`
  -> `inf\nafter\n`, status 1; `RunCaptured("hsh", {"-e", "/e.sh"})` with
  `/e.sh` = `echo a` / `false` / `echo b` -> out `a\n`, status 1;
  `set -e; (false); echo no` -> status 1, out empty.
- `NestedEverywhere`: `x=$(for i in 1 2; do echo $i; done); echo $x` ->
  `1 2\n`; `{ while read l; do echo $l; done; } <<E` / `h1` / `E` -> `h1\n`;
  `for i in a b; do echo $i; done | cat | while read j; do echo "<$j>"; done`
  -> `<a>\n<b>\n` (in-shell stages on both ends of a child: the pipe rule).
- `AcceptanceScenarioThree`: `/count.sh` with exactly the text of goal.md's
  scenario 3; `RunCaptured("hsh", {"/count.sh", "hello"})` -> out
  `ok 3 2\ngreeted\n`, err empty, status 0; with `"plain"` instead of `hello`
  -> `ok 3 2\nplain\n`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```

## Docs

- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a section
  "Control flow" -- each compound command, `LoopControl`/`FunctionReturn`
  and where they stop (loops, calls, dot scripts, `RunSubshell`), the
  function table and lookup order, why subshells are in-process, errexit's
  tested contexts, `.`/`eval` parsing one complete command at a time,
  `read`'s byte-wise input; the builtin list; new files; the placeholder
  section goes.
- `src/components/BuiltinCommands/CLAUDE.md`: `hsh` row -- 0.5.0, control
  flow, functions, `.`, `eval`, `read`, `-e`.
- Root `CLAUDE.md`: the `hsh` row describes the whole language.

## Acceptance

- [ ] Every `CommandKind` is executed; `NotYet` is gone (`grep -rn "not supported yet" src/components/BuiltinCommands/commands/hsh` finds nothing).
- [ ] Compound commands apply their redirections; a subshell changes nothing outside it (variables, functions, directory, descriptors).
- [ ] break/continue/return/exit unwind exactly as the tests show; a `break` in a function does not leave the caller's loop.
- [ ] `read` reads one byte at a time and splits as dash; errexit honours tested contexts.
- [ ] Scenario 3 passes as a unit test; all unit and haisos tests pass.

## Out of scope

- The interactive mode, prompts, the man page, the haisos tests of the six
  scenarios, the `--init` `ENV PATH=/bin` line (hsh--interactive).
- `local`, `getopts`, `trap`, `type`, `command`, aliases, `select`, `[[`.
