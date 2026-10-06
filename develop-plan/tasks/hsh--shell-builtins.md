# Task hsh--shell-builtins: cd, export, unset, readonly, set, shift, test

- Rock: hsh
- Depends on: hsh--pipelines
- Size: ~1000 changed lines in ~12 files
- Plan checked against: develop @ e632275
- PR title: hsh: cd, export, unset, readonly, set, shift and test builtins

## Goal

The shell builtins that work on the shell's own state, as dash has them,
byte for byte:

- `cd [-L|-P] [dir|-]` (`HOME`, `OLDPWD`, `CDPATH`, `PWD` kept up to date,
  `cd -` printing the new directory), through `IFileIO::ChangeDirectory`.
- `export [-p] [name[=value]...]`, `readonly [-p] [name[=value]...]`,
  `unset [-f|-v] name...` -- with dash's listings (`export X='a b'`) and
  errors (`export: 1x: bad variable name`, `unset: R: is read only`).
- `set` -- listing every variable, `set -o` / `set +o` listings, turning
  options on and off (`-e`, `+x`, `-o noclobber`...), the not-treated ones
  reported, and the positional parameters (`set -- a b`).
- `shift [n]`.
- `test` and `[`, with dash's grammar and messages; file tests through
  `IFileIO::Stat`.
- The option behaviours that belong here: `-x` (xtrace: `+ echo a b` on
  stderr before each simple command), `-a` (allexport: every assignment
  exports), `-n` (noexec: a non-interactive shell reads but does not run).

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`,
`HshShell.h`, `HshBuiltins.h`, `HshInvocation.h` (`ShellOptionTable`,
`FindShellOption`), `HshVariables.h`; the root `CLAUDE.md` ("Security";
"Builtin Commands" -- the not-treated report); the dash manual's
"Builtins" section (`cd`, `export`, `readonly`, `set`, `shift`, `test`,
`unset`) and "Argument List Processing". Every message below was checked
against dash 0.5.12; check more with `dash -c '...'`.

What earlier tasks provide (as if on develop; the code wins on names):

- hsh--executor: `Shell` -- `ExecuteSimpleCommand` (words, then
  redirections, then assignments and the command), `Run` (the parse/run
  loop), `State()`, `IO()`, `Process()`, `Context()`, `WriteOut`,
  `WriteErr`, `Report`, `Fail`, `CurrentLine`; `ShellBuiltin { name,
  special, run }` rows in `ShellBuiltins()` with each function declared in
  `HshBuiltins.h` (special: errors through `Fail`, fatal; regular: `Report`
  and status 2); `ShellOptionTable()` (dash's 18 options in `set -o` order,
  `field` null when not treated), `FindShellOption(char)`,
  `FindShellOption(name)`; the fixture `HshShellFixture.h` (`HshShellTest`,
  `Sh`, `ReadRootFile`, `NotTreatedLine(spelling)`).
- hsh--redirections: `RedirectionScope`; hsh--pipelines: in-shell pipeline
  stages run in a `SubshellScope`; `WriteOut` in a subshell ends only the
  subshell on a broken pipe (141).
- hsh--expansion: `ShellVariables` (`Get`, `IsSet`, `Set` -> false when
  read-only, `Unset` -> false when read-only, `Export`, `IsExported`,
  `MakeReadonly`, `IsReadonly`, `Names()` sorted by byte), `ShellOptions`,
  `OptionLetters`, `ShellState::positional`; `IsValidShellName` (hsh--lexer).
- `IFileIO::ChangeDirectory(path) -> 0/-1`, `GetCurrentDirectory()`,
  `ResolvePath`, `Stat(path, FileStatus&)` with `type`, `size`,
  `modificationTime` (`interfaces/IFileIO.h`, `IFileSystemService.h`);
  `IFileIO::GetDescriptor(fd)` and `IFileDescriptor::IsTerminal()`.

Checked against the merged code: `:`, `exec`, `exit`, `false`, `true` and
`wait` are already implemented (`HshBuiltins.h`/`.cpp`, `HshBuiltinExec.cpp`,
`HshBuiltinWait.cpp`, from hsh--executor and hsh--pipelines) and are not
redone here -- the Changes below add only `cd`, `export`, `readonly`, `set`,
`shift`, `test`/`[` and `unset`. `ShellBuiltins()`/`FindShellBuiltin`,
`ShellOptionTable()`/`FindShellOption`, `RedirectionScope`, `SubshellScope`,
`Jobs()`/background lists and the fixture all already match this plan's
references, named exactly as above.

`BackgroundPrelude()` (`HshShell.h`) currently passes the options that are on
(`e u f x C a`) to a background child hsh as an invocation argument, "since
there is no `set` builtin yet". Now that this task adds `set`, that could
change to pass them as `set -eu...` text in the prelude instead -- but this
task does not touch `BackgroundPrelude`/`StartBackgroundShell`/`Hsh.cpp`'s
reading of that invocation argument: keep it as is. Switching it is a small,
separate change (it would also need the child to run the `set` text before
anything else, including any function definitions hsh--control-flow appends)
and is better left to whichever task next touches background lists, or to
the final develop review.

## Changes

All in namespace `Haisos::Hsh`, `src/components/BuiltinCommands/commands/hsh/`,
plain portable C++17.

### Rules that bite (restated)

- `cd` moves the process's own working directory (`IO().ChangeDirectory`):
  children started afterwards start there; file tests use `IO().Stat`.
  Nothing else is reached.
- Not-treated options of `set` are reported with the builtin rule's exact
  line, `Parameter <spelling> is not treated by HaisosOS hsh v. <version>`,
  through `WriteErr` (so it follows the shell's current stderr), and are not
  applied.
- hsh's version becomes `0.4.0`.
- New files in the two `CMakeLists.txt`.

### Preliminary: three fixes from the PR #35 review

1. **An `-o`/`+o` option cluster stops parsing early**
   (`HshInvocation.cpp`, `ParseInvocation`, the `letter == 'o'` branch): after
   taking the option's name from the next whole argument, the loop does
   `break`, dropping any further letters of the *same* cluster (`-oe errexit`
   parses `-o errexit` and silently ignores the `e`); dash keeps parsing them
   (`continue`). Change the `break` to `continue`, and drop the comment above
   it that currently claims the drop is intentional ("the rest of this one,
   if any, is ignored, as dash ignores it" -- it is not).
   - Test: `HshInvocationTest.cpp`, parsing `{"-oe", "nounset", "-c", "x"}`
     expects both `nounset` (named by `-o`) and `errexit` (the trailing
     letter `e`) on.
   - [ ] Acceptance: an `-o`/`+o` not last in a cluster still leaves the
     cluster's remaining letters parsed.

2. **A read-only check for a child command's assignment runs before the
   value is expanded** (`HshShell.cpp`, `ExecuteSimpleCommand`, the "anything
   else is a child process" branch): `IsReadonly(assignment.name)` is checked
   before `ExpandAssignmentValue(assignment.value)` runs; dash expands first
   (so a command substitution in the value still runs, and `$?` reflects it)
   and only then rejects the assignment. Expand the value first, then check.
   - Test: `HshBuiltinsTest.cpp` (or `HshShellTest.cpp`), two `Sh` calls (a
     `Fail` is fatal, so nothing after it in the same script runs):
     `readonly x=1; x=$(pwd >/seen.txt) /bin/true` -> out empty, err
     `hsh: 1: x: is read only\n`, status 2; then `cat /seen.txt` -> `/\n`,
     proving the substitution ran (and wrote the file) before the read-only
     assignment was rejected.
   - [ ] Acceptance: a read-only assignment on a child command still expands
     (and runs any command substitution in) its value before failing.

3. **A child that ignores `TriggerStop` is left running, unlogged**
   (`HshShell.cpp`, `StopChildren`): after the `kStopGraceMs` wait, a child
   whose `WaitToFinish` still returns false is left running with nothing
   logged. Capture each child's pid (`GetPid()`) and path (`Path()`) before
   signalling it, and `LogWarning` the ones `WaitToFinish` times out on.
   - Test: none needed (a log line only).
   - [ ] Acceptance: `StopChildren` logs a warning naming the pid and path of
     any child still running after the grace period.

### `HshShell.h` / `.cpp` -- assignments, xtrace, noexec

```cpp
// Every variable assignment the shell makes goes through here: Set, a
// read-only variable being fatal (Fail("<name>: is read only")), then Export
// when allexport (-a) is on.
void AssignVariable(const std::string& name, const std::string& value);
```

- `ExecuteSimpleCommand` in dash's order, refined: (1) expand the words; (2)
  expand every assignment's value into `(name, value)` pairs, nothing
  assigned yet; (3) **xtrace**: when `options.xtrace`, `WriteErr` the value
  of `PS4` (empty when unset; not expanded -- a documented exception, dash
  expands it), then the items joined by one space -- each pair as
  `name=value`, then each field, all as they are (dash 0.5.12 does not
  quote) -- then `\n`; a bare assignment is traced too (`+ x=1 y=2 3`); (4)
  redirections; (5) assignments and the command, every assignment through
  `AssignVariable` (temporary ones for regular builtins put back as before).
- `Run`: before executing each complete command, when `options.noexec` and
  not `options.interactive`, skip it (parse errors are still reported).
  `set -n` in a script stops the rest from running, as in dash.

### `HshQuote.h` / `.cpp` (new)

```cpp
// A value as dash's `set`, `export -p` and `readonly -p` print it: in single
// quotes, each run of single quotes written as a double-quoted run.
// "plain" -> 'plain', "" -> '', "it's" -> 'it'"'"'s', "'x" -> ''"'"'x',
// "x'" -> 'x'"'", "''" -> ''"''".
std::string ShellSingleQuote(const std::string& value);
```

Algorithm (dash's `single_quote`): repeat { write `'`, the run of non-`'`
bytes (possibly empty), `'`; if no `'` follows, stop; write `"`, the run of
`'` bytes, `"`; stop at the end of the string }.

### `HshBuiltinCd.cpp` (new) -- `cd` (regular)

1. Options: `-L`, `-P` (the same here: links are followed by the filesystem,
   there is nothing to resolve), `--` ends them; any other `-x` ->
   `Report("cd: Illegal option -x")`, 2. A lone `-` is the operand.
2. The directory: no operand -> `HOME`; `HOME` unset -> status 0, nothing
   changes. `-` -> `OLDPWD`; `OLDPWD` unset -> the current directory. An
   empty operand -> status 0, nothing changes. Extra operands are ignored
   (dash).
3. `CDPATH`: when the operand does not start with `/`, `.` or `..` (as a
   whole component) and `CDPATH` is set, try `<entry>/<dir>` for each entry
   (split at `:`, an empty entry meaning `.`); the first that
   `ChangeDirectory` accepts wins, and when its entry was not empty the new
   directory is printed (`WriteOut(pwd + "\n")`). Otherwise
   `ChangeDirectory(dir)`.
4. Failure -> `Report("cd: can't cd to <operand>")`, 2.
5. Success -> `OLDPWD` = the old `PWD` value (or the old directory), `PWD` =
   `IO().GetCurrentDirectory()` (both through `AssignVariable`); `cd -`
   prints the new directory. Status 0.

### `HshBuiltinVariables.cpp` (new) -- `export`, `readonly`, `unset`, `shift`

- `export` (special): options `-p` (listing) and `--`; any other ->
  `Fail("export: Illegal option -<c>")`. No operands (or `-p` alone): for each
  of `Names()` that is exported: `export NAME='value'\n`
  (`ShellSingleQuote`) when set, `export NAME\n` when not. Operands: `name=value`
  -> name invalid -> `Fail("export: <name>: bad variable name")`; read-only ->
  `Fail("export: <name>: is read only")`; else set and export. `name` alone:
  invalid -> the same error; else `Export(name)`.
- `readonly` (special): the same with `readonly` in every text and
  `MakeReadonly` for `Export`; `readonly x=1` sets then marks it.
- `unset` (special): options `-f`, `-v` (the last one given wins; default
  `-v`), `--`; any other -> `Fail("unset: Illegal option -<c>")`. `-v`: each
  name invalid -> `Fail("unset: <name>: bad variable name")`; `Unset` false ->
  `Fail("unset: <name>: is read only")`. `-f`: no functions exist yet;
  succeed (hsh--control-flow removes the function). Status 0.
- `shift` (special): `shift [n]`, n default 1, digits only else
  `Fail("shift: Illegal number: <n>")`; n greater than the count ->
  `Fail("shift: can't shift that many")`; else drop the first n positional
  parameters.

### `HshBuiltinSet.cpp` (new) -- `set` (special)

1. No arguments: for each of `Names()` that is set: `NAME='value'\n`
   (`ShellSingleQuote`), in that (byte) order.
2. Options, dash's rules: walk the arguments while one starts with `-` or
   `+`:
   - `--`: consumed; the options end and the positional parameters are
     replaced by what follows, even nothing (`set --` clears them).
   - `-` alone: turns off `xtrace` (and `verbose`), ends the options; what
     follows, if anything, becomes the positional parameters.
   - `-o`/`+o` with a following argument: that option by name
     (`FindShellOption(name)`; null -> `Fail("set: Illegal option -o <name>")`;
     not treated -> report it as `-o <name>` / `+o <name>`, do not apply).
     With no following argument: `-o` prints the settings table,
     `+o` the commands table (below).
   - Letters: `FindShellOption(letter)`; null (also for `c`, `l`, which only
     an invocation takes) -> `Fail("set: Illegal option -<c>")` (`+<c>` for
     `+`); not treated -> report `-<c>` / `+<c>`; else set the field on/off.
3. Remaining arguments, if any (or after `--`/`-`): the new positional
   parameters.

`set -o` prints `Current option settings\n` then, for every row of
`ShellOptionTable()` in order, the name left-aligned in 16 columns followed
by `on` or `off` (`errexit         off`); untreated options are always `off`.
`set +o` prints, for every row, `set -o <name>\n` or `set +o <name>\n`
according to its state. Byte for byte as dash (see the Tests).

### `HshBuiltinTest.cpp` (new) -- `test` and `[` (regular)

Both rows point at one function; `[` requires its last argument to be `]`
(else `Report("[: missing ]")`, 2), which is then dropped. Errors are
`Report("<test or [>: <message>")`, status 2. Result: 0 true, 1 false.

**Grammar** -- POSIX's rules by argument count first, then dash's recursive
descent:

- 0 arguments: false. 1: true when not empty.
- 2: `! x` -> not (1-argument test of x); a unary operator then an operand ->
  that test; otherwise the descent.
- 3: a binary operator in the middle (including `-a` and `-o`) -> that test;
  `! a b` -> not (2-argument test); `( x )` -> 1-argument test of x;
  otherwise the descent.
- 4: `! a b c` -> not (3-argument test); `( a b )` -> 2-argument test;
  otherwise the descent.
- More, or no rule matched -- the descent:
  `oexpr := aexpr { -o aexpr }`; `aexpr := nexpr { -a nexpr }`;
  `nexpr := ! nexpr | primary`; `primary := ( oexpr ) | unary-op operand |
  operand binary-op operand | operand`. A missing `)` -> `closing paren expected`;
  a unary or binary operator with nothing after it -> `<op>: argument expected`;
  tokens left over after the whole expression -> `<the last token used>: unexpected operator`
  (dash names the token before the leftovers: `test 1 2 3` says `1: unexpected operator`).
  For a corner the tests below do not pin, follow dash's `test.c`.

**Operators.** Strings: `=`, `!=`, `<`, `>` (byte order), `-n`, `-z`.
Integers: `-eq -ne -gt -ge -lt -le`; an operand is an optional sign and
decimal digits with blanks around allowed (`" 12 "`), within `intmax_t`;
anything else -> `Illegal number: <operand>`. Files (`IO().Stat`):

| op | true when |
|----|-----------|
| `-e` | `Stat` succeeds |
| `-f` | type `File` (builtins placed on a filesystem are files) |
| `-d` | type `Dir` |
| `-c` | type `CharDevice` |
| `-s` | `size > 0` |
| `-r`, `-w`, `-x`, `-O`, `-G` | `Stat` succeeds -- no permissions or owners yet: everything is `rwx` and owned by `haisos`, as `ls -l` shows (a documented exception) |
| `-b`, `-p`, `-S`, `-g`, `-u`, `-k`, `-h`, `-L` | never: no block devices, FIFOs, sockets, set-id bits; links are followed, never seen |
| `-t fd` | slot fd (digits) of the shell's table holds a terminal |
| `a -nt b` / `a -ot b` | both exist and `modificationTime` is greater / smaller |
| `a -ef b` | both exist and `IO().ResolvePath` gives the same path |

### `HshBuiltins.h` / `.cpp`, `Hsh.cpp`

Rows: `[` (regular), `cd` (regular), `export` (special), `readonly`
(special), `set` (special), `shift` (special), `test` (regular), `unset`
(special) -- the table stays sorted by name. Version `0.4.0`. Help notes:
add to the second line `PS4 is not expanded; -v is accepted, not acted on.`
(still one line).

## Tests

`tests/unit/components/Hsh.unittests/HshBuiltinsTest.cpp` (new; add to
`add_executable`), `TEST_F(HshShellTest, ...)`, tables through `Sh`, byte for
byte. The fixture's environment is empty unless a test sets a variable on
`os->GetOsEnvironment()` first.

- `Cd`: `cd /docs; pwd` -> `/docs\n`; `cd /docs; echo $PWD $OLDPWD` ->
  `/docs /\n`; `cd /docs; cd sub; pwd` -> `/docs/sub\n`; `cd /docs; cd -`
  -> `/\n`; `cd /docs; cd ..; pwd` -> `/\n`; `HOME=/docs; cd; pwd`;
  `cd; echo $?` (no HOME) -> `0\n`; `cd ""; echo $?` -> `0\n`;
  `cd /nonexist; echo $?` -> err `hsh: 1: cd: can't cd to /nonexist\n`, out
  `2\n`; `cd /notes.txt` likewise; `cd -x; echo $?` -> err
  `hsh: 1: cd: Illegal option -x\n`, `2\n`; `CDPATH=/docs; cd sub; pwd` ->
  `/docs/sub\n/docs/sub\n`; `cd /docs; hsh -c pwd` -> `/docs\n` (children
  start in the new directory).
- `ExportAndReadonly`: `export A=1 B; export -p` -> `export A='1'\nexport B\nexport PWD='/'\n`;
  `x="it's"; export x; export -p` contains `export x='it'"'"'s'`;
  `export 1x=2; echo no` -> err `hsh: 1: export: 1x: bad variable name\n`,
  status 2, out empty; `export -z` -> `hsh: 1: export: Illegal option -z\n`;
  `export y=5; hsh -c 'echo $y'` -> `5\n`; `readonly R=1 Q; readonly -p` ->
  `readonly Q\nreadonly R='1'\n`; `readonly x=1; x=2; echo no` -> err
  `hsh: 1: x: is read only\n`, status 2; `readonly x=1; export x=2` ->
  `hsh: 1: export: x: is read only\n`.
- `Unset`: `x=1; unset x; echo "[$x]"` -> `[]\n`; `readonly R=1; unset R` ->
  `hsh: 1: unset: R: is read only\n`, 2; `unset 1x` -> `unset: 1x: bad variable name`;
  `unset -z` -> `unset: Illegal option -z`; `export E=1; unset E; hsh -c 'echo "[$E]"'`
  -> `[]\n`.
- `SetListsAndPositionals`: with an empty environment, `x='it'"'"'s'; set` has
  the line `x='it'"'"'s'` and `IFS=' \t\n'` (quoted as dash: `IFS=' <TAB><LF>'`);
  `set -- a "b c"; echo $# $2` -> `2 b c\n`; `set a b; echo $1$#` -> `a2\n`;
  `set --; echo $#` -> `0\n`; `set -- -x; echo $1` -> `-x\n`;
  `set - a; echo $1 "[$-]"` -> `a []\n`.
- `SetOptions`: `set -e -u; echo $-; set +eu; echo "[$-]"` -> `ue\n[]\n`;
  `set -o errexit; echo $-` -> `e\n`; `set -z` -> err `hsh: 1: set: Illegal option -z\n`,
  2; `set -o nosuch` -> `set: Illegal option -o nosuch`; `set -c` ->
  `set: Illegal option -c`; `set -m; echo ok` -> err `NotTreatedLine("-m")`,
  out `ok\n`; `set -o` -> exactly `Current option settings\n` + 18 lines
  `errexit         off` ... `debug           off` in table order; `set -e; set -o`
  has `errexit         on`; `set +o` -> 18 lines `set +o errexit` ...;
  `set -C; echo a > /n.txt; echo b > /n.txt` -> `cannot create /n.txt: File exists`.
- `Shift`: `set -- a b c; shift 2; echo $1 $#; shift; echo $#` ->
  `c 1\n0\n`; `set -- a b; shift 3` -> err `hsh: 1: shift: can't shift that many\n`,
  2; `shift x` -> `shift: Illegal number: x`.
- `TestAndBracket` (status of each; `[` forms end in `]`; from dash):
  `[ 1 -eq 1 ]` 0; `[ a -eq 1 ]` 2 with `hsh: 1: [: Illegal number: a\n`;
  `[ 1 -eq ]` 2 `[: -eq: argument expected`; `[ 1 = 1` 2 `[: missing ]`;
  `test` 1; `test ""` 1; `test -n` 0; `test -z` 0; `[ ! ]` 0; `[ ]` 1;
  `[ -e ]` 0; `test -f /notes.txt` 0; `test -f /docs` 1; `test -d /docs` 0;
  `test -e /nope` 1; `test -s /notes.txt` 0; `test -x /bin/ls` 0;
  `test -L /docs` 1; `[ -d /docs -a -e /nope ]` 1;
  `[ \( 1 = 1 \) -o 1 = 2 ]` 0; `test 1 -gt 2` 1; `test 5 -ge 5` 0;
  `test a \< b` 0; `test a != b` 0; `test " 12 " -eq 12` 0;
  `test 9999999999999999999999 -eq 1` 2 `test: Illegal number: 9999999999999999999999`;
  `test 1 -eq 1x` 2; `test 1 2 3` 2 `test: 1: unexpected operator`;
  `[ x -foo y ]` 2 `[: x: unexpected operator`; `test a b` 2
  `test: a: unexpected operator`; `test \( a` 2 `test: closing paren expected`;
  `test -n a -a -z ""` 0; `test x = x -a ! y = z` 0; `test = = =` 0;
  `test -e -e` 1; `test ! ! a` 0; `test "" -a a` 1; `test ! a = b` 0;
  `test -t 1` 1 (stdout is a file); `test /notes.txt -ef /docs/../notes.txt` 0.
- `Xtrace`: `set -x; echo "a b" c; x=1 y="2 3"; x=1 /bin/echo hi > /o.txt`
  -> out `a b c\n`, err `+ echo a b c\n+ x=1 y=2 3\n+ x=1 /bin/echo hi\n`;
  `PS4='>> '; set -x; :` -> err `>> :\n`.
- `AllexportAndNoexec`: `set -a; v=1; hsh -c 'echo $v'` -> `1\n`;
  `set -n; echo no` -> out empty, status 0; through
  `RunCaptured("hsh", {"-n", "-c", "echo no; fi"})` -> err the syntax error,
  2; `{"-n", "-c", "echo no"}` -> nothing, 0.
- `ABrokenPipeInAnInShellStageEndsOnlyThatStage`: `/big.txt` of 200000
  bytes; `x=$(cat /big.txt); set | /bin/echo x; echo $?` -> out `x\n0\n`,
  status 0 (`set` writes more than the pipe holds; `/bin/echo` never reads;
  the in-shell stage dies with 141, the shell goes on).
- `ABrokenPipeAtTheTopLevelEndsTheShell`: a pipe from the OS's pipe service
  with its read end released as stdout, a `MockFileDescriptor` as stderr:
  `/bin/hsh -c 'set; /bin/echo after'` -> `ExitCode()` 141, stderr mock empty.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: the builtin list
  (special/regular) with these; `AssignVariable` as the one way to assign;
  xtrace, allexport and noexec; `ShellSingleQuote`; the `test` grammar and the
  file-test table with its exceptions; new files.
- `src/components/BuiltinCommands/CLAUDE.md`: `hsh` row -- 0.4.0; the
  builtins; exceptions: `PS4` not expanded, `-v` accepted not acted on,
  `test -r/-w/-x/-O/-G` true for anything that exists, `-h/-L` never.
- Root `CLAUDE.md`: the `hsh` row lists the shell builtins so far.

## Acceptance

- [ ] Every builtin above is in the table with the right special/regular
      flag; special-builtin errors are fatal, regular ones status 2.
- [ ] Every assignment goes through `AssignVariable`; `-a` exports; `-x`
      traces before redirections; `-n` stops execution in a script.
- [ ] Listings and messages match the tests byte for byte.
- [ ] `cd` uses only `IFileIO::ChangeDirectory`; file tests only `IFileIO::Stat`.
- [ ] Not-treated options of `set` are reported, not applied.
- [ ] All unit and haisos tests pass.

## Out of scope

- `unset -f`, functions (hsh--control-flow); `read`, `.`, `eval`, `-e`
  behaviour (hsh--control-flow); `getopts`, `local`, `type`, `command`,
  `printf`, `umask`, `ulimit`, `times`, `trap`, `alias` (goal.md: out of
  this develop); `cd` printing via `CDPATH` beyond dash's rule; `-v` input
  echoing.
