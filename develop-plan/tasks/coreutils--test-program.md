# Task coreutils--test-program: test and [ as programs, sharing hsh's evaluator

- Rock: coreutils
- Depends on: coreutils--sort (`GnuQuote` in `BuiltinText.h`)
- Size: ~650 changed lines in ~9 files (of which ~330 a move out of `HshBuiltinTest.cpp`)
- Plan checked against: develop @ ccb9dbe
- PR title: Add /bin/test and /bin/[ sharing hsh's test evaluator

(Split off the planned coreutils--du-cmp-test, which came to ~1250 changed
lines; du and cmp are coreutils--du-cmp.)

## Goal

`test` and `[` exist as programs (`BUILTIN rootfs test /bin/test`, `BUILTIN
rootfs [ /bin/[`), behaving and failing exactly as GNU coreutils 9.4's
`/usr/bin/test` and `/usr/bin/[` -- whose messages differ from dash's
(`test: invalid integer 'a'`, `test: missing argument after 'b'`, `[: missing
']'`). They are what `env test ...`, `find ... -exec test ...` and `xargs
test` run; inside `hsh`, `test`/`[` stay the shell's own builtins, unchanged.

The evaluator is written once: hsh's (`commands/hsh/HshBuiltinTest.cpp`)
moves into a shared `BuiltinTestExpression` with a dialect, `Dash` (hsh's
grammar and messages, byte for byte as today) or `Gnu` (coreutils' grammar
and messages). Both dialects share the operator primaries -- file tests,
string and integer comparisons -- so `-f`, `-d`, `-nt`, ... and their
Haisos exceptions (no permissions, users or links) are decided in one place.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md` (and the hsh row's `test`
exceptions), `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`,
`commands/hsh/HshBuiltinTest.cpp` (all of it: dash's evaluator, its ops table,
`TestEvaluation`, `BuiltinTest`), `commands/hsh/HshBuiltins.h`,
`commands/hsh/HshShell.h` (`Report`, `Exists`, `IO()`), `commands/hsh/HshNumber.h`
(`Atomax10`), and `tests/unit/components/Hsh.unittests/HshBuiltinsTest.cpp`
`TestAndBracket` -- the guard that hsh's behaviour and output do not change.
Also `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`
`EveryBuiltinsHelpHasTheSameShape` and `EveryBuiltinHasAVersion`.

What earlier tasks provide, as if on develop: coreutils--sort's
`std::string GnuQuote(std::string_view text);` (`BuiltinText.h`) -- GNU's
`quote()` in the C locale, which coreutils' test uses around every word in
its messages.

## Changes

### New `src/components/BuiltinCommands/BuiltinTestExpression.h` / `.cpp`

Namespace `Haisos`:

```cpp
enum class TestDialect { Dash, Gnu };

// test / [ over args[begin, end) (the operands only: no command name, and
// for [ no closing ]). Files through |io| (the caller's process's IFileIO --
// the only door to files). A syntax error is reported once through |report|,
// as the message without any "<command>: " prefix ("a: unexpected operator",
// "invalid integer 'a'"), and gives 2. Otherwise 0 (true) or 1 (false).
// |command| is "test" or "[" (dash's messages do not name it; kept for the
// callers' prefixes and future use).
int EvaluateTestExpression(TestDialect dialect, const std::string& command,
                           const std::vector<std::string>& args, size_t begin, size_t end,
                           IFileIO& io, const std::function<void(const std::string&)>& report);
```

Dash dialect: move `TestToken`, `TestOpType`, `TestOp`, `kTestOps`,
`TestSyntaxError` and `TestEvaluation` here (anonymous namespace), unchanged
in logic, with three substitutions: `m_shell.Report(...)` becomes
`report(...)` with the `m_command + ": "` prefix dropped (the hsh caller adds
it back, so the text hsh prints is identical); `m_shell.Exists(p)` becomes
`io.Stat(p, status) == 0`; `m_shell.IO()` becomes `io`. Keep using
`Hsh::Atomax10` (include `commands/hsh/HshNumber.h`) for dash's integers.

Shared primaries (file scope in the `.cpp`, used by both dialects): one
function for the unary file tests, taking the operator letter and operand --
exactly today's hsh semantics: `-e -f -d -c -s` from `io.Stat`; `-r -w -x -O
-G` "it exists"; `-h -L -b -p -S -u -g -k` false; plus `-N` (GNU only:
exists and modification time > access time). `-ef`: both exist and
`io.ResolvePath` equal.

Gnu dialect: a port of coreutils 9.4 `src/test.c` (`posixtest`, `expr`,
`or`, `and`, `term`, `binary_operator`, `unary_operator`, `one_argument`,
`two_arguments`, `three_arguments`, `beyond`, `find_int`), as a class like
`TestEvaluation` with `argc` = `end`, `pos` starting at `begin`:

- `posixtest(n)`: 1 arg -> non-empty; 2 -> `two_arguments`; 3 ->
  `three_arguments`; 4 -> `! three` if the first is `!`, `( two )` if first
  `(` and fourth `)`, else `expr`; 5+ -> `expr`. After it, `pos != argc` ->
  `extra argument 'X'` (X = args[pos]).
- `two_arguments`: `! x` -> `x` empty; a 2-char word starting with `-` ->
  `unary_operator`; else `beyond()`.
- `three_arguments`: middle is a binary operator -> `binary_operator`; first
  `!` -> negated `two_arguments`; `( x )` -> x non-empty; middle `-a`/`-o` ->
  `expr`; else `'M': binary operator expected` (M = middle word).
- `term`: leading `!`s (each toggles; running out -> `beyond`); `(`: count up
  to 4 words before a `)` (none within 4: the rest), `posixtest` of them, then
  require `)` (`')' expected` at the end, `')' expected, found 'X'`
  otherwise); `-l STR OP ...` with OP binary -> binary with a length on the
  left; 3+ words left with the second binary -> `binary_operator`; a 2-char
  word starting with `-` -> `unary_operator`; else the word's non-emptiness.
- `and`: terms joined by `-a`; `or`: ands joined by `-o`; `expr`: nothing
  left -> `beyond`, else `or`.
- `beyond()`: `missing argument after 'L'` with L the LAST argument
  (args[end-1]), whatever was being parsed.
- Binary operators (`binop`): `= != == -nt -ot -ef -eq -ne -lt -le -gt -ge`
  -- not `<` or `>` (GNU test: `test b \> a` is `'>': binary operator
  expected`). Integers: `find_int` -- leading blanks, an optional `+` or `-`,
  at least one digit, trailing blanks, nothing else (else `invalid integer
  'X'`); compared as decimal strings of any length (sign, then length after
  leading zeros, then digits: `99999999999999999999 -gt 1` is true -- no
  overflow); `-l STR` on either side is the string's length. `-nt`: left
  exists and (right does not, or left's modification time is later); `-ot`:
  right exists and (left does not, or left's is earlier); `-nt/-ot/-ef` with
  `-l` -> `-nt does not accept -l` (etc.).
- `unary_operator`: the letters `e r w x N O G f d s S c b p L h u g k t n z`;
  `-t FD`: `find_int`, then whether descriptor FD of `io` is a terminal;
  any other letter -> `'-q': unary operator expected`; an operator with no
  operand after it -> `beyond`.
- Quoting in messages: `GnuQuote(word)` (`'b'`; `a'b` gives `'a\'b'`).

### `src/components/BuiltinCommands/commands/hsh/HshBuiltinTest.cpp`

Keeps only `BuiltinTest` (its `[: missing ]` check is dash's and stays
here), now calling `EvaluateTestExpression(TestDialect::Dash, command, args,
1, end, shell.IO(), [&](const std::string& m) { shell.Report(command + ": " + m); })`.
hsh's output, statuses and documented exceptions do not change; no hsh
version bump. (Move the code with `git mv`-friendly edits: the new `.cpp`
should read as the old file's evaluator, so the diff stays reviewable.)

### New `src/components/BuiltinCommands/commands/test/Test.cpp`

`CreateTestCommand()` and `CreateBracketCommand()`: two command objects
sharing one implementation, names `test` and `[`, version `1.0.0`, empty
`Options()`. `Help()`: summary "check file types and compare values", usage
`test EXPRESSION`, `test`, `[ EXPRESSION ]`, `[ ]`, `[ OPTION`; `basedOn`
empty for `test` and `"test"` for `[` (man7 has no `[` page); notes: the
operators in a few lines, `--help`/`--version` only as `[`'s sole argument,
and the exceptions.

`Run` (test.c `main`), not `BeginBuiltin` -- test takes no options:
1. `[` only: exactly one argument `--help` / `--version` -> help / version
   text, exit 0. The last argument must be exactly `]` (unlike dash, which
   accepts any word starting with `]`), else `[: missing ']'` on stderr,
   exit 2; it is then dropped.
2. No operands left -> exit 1.
3. `EvaluateTestExpression(TestDialect::Gnu, name, args, 0, n, context.IO(),
   [&](const std::string& m) { context.Error(m); })` -- `Error` prefixes
   `test: ` or `[: `.

So `test --help` prints nothing and exits 0 (a non-empty string), as GNU's.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Declare `CreateTestCommand()` and `CreateBracketCommand()`; add both to
`CreateStandardBuiltinCommands()` (the `haisos --init` template then shows
`# BUILTIN rootfs [ /bin/[` and the test line; make sure
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` passes with the name `[`).
Add `BuiltinTestExpression.cpp` and `commands/test/Test.cpp` to the library.

### `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`

`EveryBuiltinsHelpHasTheSameShape` and `EveryBuiltinHasAVersion` run every
builtin with `--help`/`--version`; GNU `test` takes neither. Skip `test`
there with a comment (as `EveryBuiltinsManPageIsItsHelp` skips hsh), and in
`EveryBuiltinsHelpHasTheSameShape` check `test`'s help text through
`BuiltinHelpText` directly instead of running it, so its shape is still
checked.

Rules that bite: files only through `context.IO()` / `shell.IO()`;
`--help` from `BuiltinHelpText`; GNU's messages; portable C++17.

Documented exceptions (the `test` row and `--help` notes; the same as hsh's
`test`): `-r -w -x -O -G` only test existence; `-h -L` always false (no
links); `-b -p -S -u -g -k` false; `-ef` compares resolved paths (no inode
numbers).

## Tests

New `tests/unit/components/BuiltinCommands.unittests/TestTest.cpp` (in that
directory's `CMakeLists.txt`), on `RunCaptured` (`RunCaptured("[", {...})`
runs `/bin/[`). Expected messages and statuses are GNU coreutils 9.4's,
checked with `LC_ALL=C /usr/bin/test`; verify further cases in the container.

- `TestProgramTruthValues` (status 0 / 1, no output): `x` 0; `''` 1; no
  operand 1; `-f /notes.txt` 0; `-d /notes.txt` 1; `-e /none` 1; `-s /docs/a.md` 0;
  `a = a` 0; `a == a` 0; `a != a` 1; `' 12 ' -eq 12` 0; `+1 -eq 1` 0;
  `99999999999999999999 -gt 1` 0; `-l abc -eq 3` 0; `! ! a` 0; `-z` 0 (one
  argument); `-t` 0; `'(' -n a ')' -a '(' -z '' ')'` 0; `a = b -o c` 0;
  `/notes.txt -nt /none` 0 (GNU: newer than a missing file); `/none -ot /notes.txt` 0;
  `/notes.txt -ef ./notes.txt` 0 (working directory `/`); `-h /notes.txt` 1;
  `--help` 0 with no output.
- `TestProgramSyntaxErrors` (status 2, exact stderr):
  `1 -eq a` -> `"test: invalid integer 'a'\n"`; `a b` -> `"test: missing argument after 'b'\n"`;
  `a b c` -> `"test: 'b': binary operator expected\n"`; `a b c d e` -> `"test: extra argument 'b'\n"`;
  `'(' a` -> `"test: missing argument after 'a'\n"`; `'(' a ')' b` -> `"test: extra argument 'b'\n"`;
  `'(' a -a b c ')'` -> `"test: ')' expected, found 'c'\n"`; `a -a` -> `"test: missing argument after '-a'\n"`;
  `1 -lt` -> `"test: missing argument after '-lt'\n"`; `-n a b` -> `"test: 'a': binary operator expected\n"`;
  `-q a` -> `"test: '-q': unary operator expected\n"`; `a -q b` -> `"test: '-q': binary operator expected\n"`;
  `b '>' a` -> `"test: '>': binary operator expected\n"`; `-- -eq -1` -> `"test: invalid integer '--'\n"`.
- `BracketProgram`: `[ a ]` 0; `[ a` -> err `"[: missing ']'\n"`, status 2; `[ a b ]` ->
  `"[: missing argument after 'b'\n"`, status 2; `[ ]` 1; `[ -n x ]foo` -> `"[: missing ']'\n"`
  (unlike hsh's `[`); `[ --help` prints the help (first line starts `HaisosOS [ version 1.0.0 - `),
  `[ --version` prints `"[ (HaisosOS builtin) 1.0.0\n"`.
- `TestProgramReachedThroughPath`: an hsh `-c 'env ...'` is not available yet,
  so run `/bin/hsh -c '/bin/test -d /docs && echo yes; /bin/[ 1 -eq 2 ]; echo $?'`
  -> `"yes\n1\n"` (hsh runs a slash path as a program, not its builtin).

The existing `HshShellTest.TestAndBracket` and every other Hsh test must pass
unmodified -- that is the proof the move changed nothing in hsh.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.TestProgram*:BuiltinCommandsTest.BracketProgram*:*TestAndBracket*'
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

(The script's filter matches test executable names, so `BuiltinCommands`
is the narrowest it takes; the direct run narrows to this task's tests.
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` lives in
`CliParser.unittests`.) Add the new builtin names to the exact list in
`ListsEveryBuiltinSortedWithAVersion` (`BuiltinCommandsTest.cpp`), in byte
order.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: rows for `[` and `test`
  (version, GNU grammar, `--help`/`--version` only for `[` alone, the
  exceptions); a sentence that `BuiltinTestExpression` is the one evaluator
  of hsh's `test`/`[` (dialect Dash) and of the programs (dialect Gnu).
- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: where the
  evaluator now lives.
- Root `CLAUDE.md`: rows for `[` and `test` in the Builtin Commands table,
  and in the builtin lists.

## Acceptance

- [ ] `EvaluateTestExpression` with `TestDialect { Dash, Gnu }` is the only test evaluator; hsh's `BuiltinTest` calls it with `Dash`; the hsh tests pass unmodified.
- [ ] The Gnu dialect follows coreutils' `posixtest`/`expr`/`term` structure and every message above byte for byte; integers of any length; `-l`; no `<`/`>`.
- [ ] File primaries shared by both dialects; exceptions as hsh's.
- [ ] `test` takes no options; `[` takes `--help`/`--version` alone and needs a final `]` exactly.
- [ ] Generic help/version tests adjusted only to skip running `test`; registered; init template test green; docs rows.

## Out of scope

- Changing hsh's `test`/`[` in any way (dash's messages stay).
- bash's `[[ ]]`, `-v`, `-R`, `<`/`>` in test.
