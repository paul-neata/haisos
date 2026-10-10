# Task search--xargs: xargs

- Rock: search
- Depends on: coreutils--names-env (contract 5: `BuiltinRunProgram.h`, `stopAtFirstOperand`), coreutils--sort (`BuiltinText.h`), coreutils--rm-rmdir (`BuiltinPrompt`), search--find-actions (`OpenEmptyInput` in `BuiltinRunProgram.h`)
- Size: ~570 changed lines in ~7 files
- Plan checked against: develop @ a092a28
- PR title: Add the xargs builtin

## Goal

`xargs` is a builtin (`BUILTIN rootfs xargs /bin/xargs`) behaving as GNU
xargs 4.9.0 (findutils, Ubuntu 24.04): `find . -name '*.h' -print0 | xargs
-0 wc -l` (acceptance scenario 1) works as on Linux, with GNU's input
parsing, limits, messages and exit codes 123/124/125/126/127. Programs are
run only through contract 5, i.e. `ICurrentProcess::OS()->StartProcess`.

## Context

Read first: root `CLAUDE.md` ("Security", "Exit codes", "Builtin
Commands", rule 9), `src/components/BuiltinCommands/CLAUDE.md`,
`commands/cat/Cat.cpp` (reading stdin, stopping).

What earlier tasks provide (all landed on develop; the headers in
`src/components/BuiltinCommands/` are the authority):
- `BuiltinRunProgram.h` (coreutils--names-env):
  `std::optional<std::string> FindProgramInPath(BuiltinContext&, const std::string& name, const IEnvironment* environment = nullptr)`;
  `struct RunProgramOptions { std::shared_ptr<IFileDescriptor> stdIn, stdOut, stdErr; std::optional<std::string> workingDirectory; std::shared_ptr<IEnvironment> environment; }`;
  `int RunProgramAndWait(BuiltinContext&, const std::string& programPath, const std::vector<std::string>& args, const RunProgramOptions& = {}, bool* started = nullptr)`
  (flushes the caller's stdout first; stops the child when the caller is
  stopped; 127 and `*started = false` when it could not start);
  `ParseBuiltinArgs(args, options, bool stopAtFirstOperand)` and
  `BeginBuiltin(context, command, usageErrorStatus, exitStatus, bool stopAtFirstOperand)`.
  And from search--find-actions (#65), in the same header
  (`src/components/BuiltinCommands/BuiltinRunProgram.h`):
  `std::shared_ptr<IFileDescriptor> OpenEmptyInput(BuiltinContext& context);`
  -- the read end of a pipe whose write end is already released, so an input
  at its end at once (what `/dev/null` gives); **null if no pipe could be
  made**. An options slot left null means the caller's own descriptor
  (`RunProgramAndWait`), so a null from `OpenEmptyInput` must never be passed on.
- `BuiltinPrompt.h` (coreutils--rm-rmdir): `BuiltinPrompt(context)`, `bool Ask(const std::string&)`.
- `BuiltinText.h` (coreutils--sort): `std::string GnuQuote(std::string_view)`.
- `IProcess::GetEnvironment()` (a clone of the process's environment),
  `IEnvironment::GetVariableNames`/`GetVariable`/`SetVariable`.

Rules that bite: programs only through `RunProgramAndWait`; files only
through `context.IO()`; GNU's output and messages byte for byte (C
locale); every option of GNU xargs in `Options()`; `--help` from
`BuiltinHelpText`; registered in `CreateStandardBuiltinCommands()` (the
`--init` template); portable C++17; stops promptly on `TriggerStop()`
(reading input and waiting for a child).

## Changes

### `commands/xargs/Xargs.cpp` (new)

`CreateXargsCommand()`; `xargs`, `1.0.0`; summary `build and execute
command lines from standard input`; usage `xargs [OPTION]... COMMAND
[INITIAL-ARGS]...`. `BeginBuiltin(context, *this, 1, status,
/*stopAtFirstOperand=*/true)`; the first operand is COMMAND (none: `echo`),
the rest INITIAL-ARGS.

Options (GNU 4.9.0's, all treated; ids of your choosing): `-0, --null`;
`-a, --arg-file=FILE`; `-d, --delimiter=CHARACTER`; `-E END` (short only,
Required); `-e, --eof[=END]` (OptionalAttached); `-I R` (short, Required);
`-i, --replace[=R]` (OptionalAttached); `-L, --max-lines=MAX-LINES`;
`-l[MAX-LINES]` (short, OptionalAttached); `-n, --max-args=MAX-ARGS`;
`-o, --open-tty`; `-P, --max-procs=MAX-PROCS`; `-p, --interactive`;
`--process-slot-var=VAR`; `-r, --no-run-if-empty`; `-s, --max-chars=MAX-CHARS`;
`--show-limits`; `-t, --verbose`; `-x, --exit`. Descriptions: a few words
from GNU's `--help`.

**Option checks** (exit 1 unless said):
- `-n 0` / `-L 0`: `xargs: value 0 for -n option should be >= 1` (`-L`
  likewise) + Try; `-P x`: `xargs: invalid number "x" for -P option` + Try;
  `-s 0`: `xargs: value 0 for -s option should be >= 1` (no Try), then -s is
  1, so every item fails to fit: `xargs: cannot fit single argument within
  argument list size limit`, 1; `-s` above the limit below: `xargs: value N
  for -s option should be <= LIMIT`, then LIMIT is used (exit as normal).
- `-L` then `-n`: `xargs: warning: options --max-lines and --max-args/-n are
  mutually exclusive, ignoring previous --max-lines value`; `-n` then `-L`:
  `xargs: warning: options --max-args and -L are mutually exclusive,
  ignoring previous --max-args value`; the last wins.
- `-E` with `-0` or `-d`: `xargs: warning: the -E option has no effect if -0
  or -d is used.`
- `-d` value: one character, or `\n \t \a \b \f \r \v \\`, `\xHH`, `\0` /
  octal `\NNN`; else `xargs: Invalid input delimiter specification ab: the
  delimiter must be either a single character or an escape sequence
  starting with \.`
- `-a FILE` unopenable: `xargs: Cannot open input file 'nosuch': No such
  file or directory` (GnuQuote).

**Limits** (documented: Haisos has no ARG_MAX; Linux's default is used):
ARG_MAX 2097152; env = sum over the process environment's variables of
`len(NAME) + 1 + len(VALUE) + 1`; LIMIT = 2097152 - 2048 - env; the buffer
(default -s) is min(131072, LIMIT). `--show-limits` writes to **stderr**:
```
Your environment variables take up <env> bytes
POSIX upper limit on argument length (this system): <LIMIT>
POSIX smallest allowable upper limit on argument length (all systems): 4096
Maximum length of command we could actually use: <LIMIT - env>
Size of command buffer we are actually using: <buffer>
Maximum parallelism (--max-procs must be no greater): 2147483647
```
then goes on as usual. A command line's size is `sum(len(arg) + 1)` over
command, initial args and items (`printf 'a b c d\n' | xargs -s 11 echo`
runs `echo a b c` then `echo d`).

**Reading items** (from `-a FILE` or descriptor 0, in chunks, stopping on
`StopRequested`/`kIOInterrupted`):
- `-0`: items end at NUL; `-d C`: items end at C; no quote, backslash or
  EOF-string processing; an empty item is kept.
- Default: items separated by blanks (space, tab) and newlines; `'...'` and
  `"..."` quote (no escapes inside); `\` takes the next byte literally; a
  newline inside quotes: `xargs: unmatched single quote; by default quotes
  are special to xargs unless you use the -0 option` (`double quote`),
  then the items read so far are run and xargs exits 1. Empty lines give
  nothing. With `-E END` (or `-eEND`), an unquoted item equal to END ends
  the input.
- `-L N` / `-l[N]` (default 1): at most N non-blank lines per command; a
  line ending in a blank continues on the next (`'a b \nc d\ne\n'` with
  `-L 1` -> `a b c d`, then `e`).
- `-I R` / `-i[R]` (default `{}`): each non-empty line is one item, leading
  blanks removed, inner and trailing blanks kept (`'  x y\n'` -> `x y`;
  quotes and backslashes still processed); one command per item, with
  every occurrence of R in every initial argument replaced by it (an
  argument without R stays); implies `-x` and `-L 1`. `-i -I X`: the last
  wins.

**Running** each command line: `FindProgramInPath(context, COMMAND,
environment)`; not found: `xargs: COMMAND: No such file or directory`
(COMMAND as given, unquoted), exit 127 at once. `-t` (and `-p`) first write
the line -- arguments joined by single spaces -- to stderr, `-t` with
`\n`. `-p`: if descriptor 0 is a terminal, `BuiltinPrompt::Ask(line + "
?...")` (a no skips the command); otherwise write the line, then `xargs:
failed to open /dev/tty for reading: No such device or address`, exit 1
(documented: the terminal is standard input when it is one). The child's
stdin is `OpenEmptyInput(context)` -- **if that returns null, the child must
never get xargs's own descriptor 0** (where `-p` answers and the items come
from): that run fails with `xargs: cannot make an empty input for COMMAND`
(GnuQuote'd), exit 1 at once -- or with `-o` xargs's descriptor 0 when it
is a terminal (documented). `--process-slot-var=VAR`: the child's
environment is a clone of xargs's with VAR=`0`. `-P N` is accepted and
validated; commands always run one after the other (documented), so the
slot is always 0. `RunProgramAndWait` gives the code c:
- `*started == false`: `xargs: COMMAND: Permission denied`, exit 126 at once;
- c == 255: `xargs: COMMAND: exited with status 255; aborting`, exit 124 at once;
- 128 < c < 255 (a stopped program, e.g. 143): `xargs: COMMAND: terminated
  by signal <c - 128>`, exit 125 at once (documented: Haisos reports
  signals as 128+n exit codes);
- 1 <= c <= 125: remember, go on; xargs ends with 123.
No items at all: the command runs once with the initial arguments, unless
`-r`. Too long: an item that cannot fit even alone -> `xargs: argument line
too long`, exit 1; with `-x`, a line that `-n`/`-L` would have filled but
that does not fit -> `xargs: argument list too long`, exit 1.

Help notes: `-P` runs one at a time; ARG_MAX is Linux's default; `-p`/`-o`
use standard input as the terminal; signal exits are 128+n codes.

### `commands/find/FindActions.cpp` (the same gap in `-ok`)

~line 737, `options.stdIn = OpenEmptyInput(run.context);` leaves `stdIn`
null when no pipe could be made, and `RunProgramAndWait` then gives the
child find's own standard input (where the `-ok` answers come from). Fix it
with the same rule: when `OpenEmptyInput` returns null, report
`find: cannot make an empty input for 'COMMAND'` (via `GnuQuote`) and fail
that action (return false), without starting the child.

### Registration and build

`src/components/BuiltinCommands/BuiltinCommandList.h`: `CreateXargsCommand()`
declared after `CreateWhichCommand()` and added to
`CreateStandardBuiltinCommands()` after `CreateWhichCommand()` (alphabetical,
last). `src/components/BuiltinCommands/CMakeLists.txt`:
`commands/xargs/Xargs.cpp` after `commands/which/Which.cpp`.
`tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt`: `XargsTest.cpp`
in `add_executable` after `WhichSleepTrueFalseTest.cpp`.

## Tests

`tests/unit/components/BuiltinCommands.unittests/XargsTest.cpp` (new, in
the CMakeLists; includes `BuiltinCommandsFixture.h`, as `FindActionsTest.cpp`
does), `TEST_F(BuiltinCommandsTest, Xargs...)` on `RunCaptured`,
input through its third argument, an environment holding `PATH=/bin`
through its last. Each expected output checked with `LC_ALL=C xargs` in the
container.

- `XargsDefaultsToEcho`: stdin `a b\nc\n` -> `a b c\n`; `-n1` -> three lines; quotes and backslashes (`a 'b c' "d e" f\ g` with -n1 -> four items).
- `XargsUnmatchedQuote`: `a 'b\n` -> stderr the message, stdout `a\n`, exit 1.
- `XargsNullAndDelimiter`: `-0 -n1` on `a\0b c\0`; `-d, -n1` on `a,b,c`; `-d '\n'`; `-d ab` (message, 1); `-0 -E x` warning.
- `XargsLinesAndReplace`: `-L 2`, `-L 1` with a trailing blank, `-I {} echo [{}] {}`, `-I % echo '<%>'` on `  x y\n`, `-i echo [{}]`, `-L 1 -n 2` and `-n 2 -L 1` warnings.
- `XargsEofString`: `-E END` on `a\nb\nEND\nc\n` -> `a b\n`; `-eEOF`.
- `XargsSizes`: `-s 11 echo` on `a b c d`; `-s 8` on `aaaa bbbb` -> `argument line too long`, 1; `-x -s 12 -n 2` -> `argument list too long`, 1; `-s 0`; `-n 0`; `-P x`; `--show-limits` with a known environment (`PATH=/bin` only: env 10 bytes -> exact six lines on stderr).
- `XargsExitCodes`: `false` -> 123; `hsh -c 'exit 255'` with `-n1` on two items -> the aborting line, 124, only one run; `nosuchcmd` -> 127; `/docs` -> `xargs: /docs: Permission denied`, 126; a child stopped (`hsh -c 'exit 143'`) -> `terminated by signal 15`, 125.
- `XargsVerboseAndEmpty`: `-t -n2 echo` on `a b c` -> stderr `echo a b\necho c\n`; empty input -> `echo hi` runs once, `-r` not.
- `XargsPromptWithoutTerminal`: `-p echo` with stdin a file -> stderr `echoxargs: failed to open /dev/tty for reading: No such device or address\n` (the line has no newline: GNU's), exit 1.
- `XargsArgFileAndSlotVar`: `-a` a file `/proj/args` = `x y\n` with `echo` -> `x y\n`; `-a nosuch`; `--process-slot-var=SLOT hsh -c 'echo $SLOT'` -> `0\n`.
- `XargsChildGetsEmptyInput`: `xargs -I X hsh -c 'cat; echo X'` on stdin `q\nmore\n` -> `q\nmore\n` (two runs; `cat` reads nothing in either, rather than the rest of xargs's input).
- `XargsNoEmptyInputNeverPassesOwnStdin`: unless a pipe failure can be
  injected in the fixture (then: `-I X hsh -c 'cat'` on `q\n` prints the
  message, exit 1, and `q` is never read by the child), checked by reading
  the code: the only `OpenEmptyInput` use has a null branch. Likewise for
  find's `-ok` (next to `OpenEmptyInputReadsNothing` in `FindActionsTest.cpp`).
- `XargsIsStoppedPromptly`: `xargs sleep` fed `100\n`, `TriggerStop()`, 143 within 1 s.


Update `ListsEveryBuiltinSortedWithAVersion` in `BuiltinCommandsTest.cpp` with `"xargs"` (after `"which"`).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Xargs*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list; an `xargs`
  1.0.0 row (exceptions: `-P` runs one at a time, ARG_MAX is Linux's
  default 2097152, `-p`/`-o` take standard input as the terminal, a child's
  exit code above 128 is reported as a signal).
- Root `CLAUDE.md`: Builtin Commands table row and the command lists.
- `src/components/BuiltinCommands/CLAUDE.md`, the `BuiltinRunProgram.h` entry:
  `OpenEmptyInput` returns null when no pipe could be made, and its callers
  (find's `-ok`, xargs) fail the run rather than pass the caller's stdin.

## Acceptance

- [ ] When `OpenEmptyInput` returns null, neither xargs nor find's `-ok` starts a child on the caller's own stdin (message, failed run).
- [ ] Programs run only through `RunProgramAndWait`; files only through `context.IO()`.
- [ ] Every message, output and exit status in this plan is byte for byte GNU xargs 4.9.0's (C locale), checked in the container.
- [ ] `find ... -print0 | xargs -0 wc -l` works in an `hsh -c` pipeline.
- [ ] `xargs` registered; `--init` template and generic builtin tests green; both CLAUDE.md files updated.

## Out of scope

- Parallel `-P` runs; a real `/dev/tty`; find (search--find-tests, search--find-actions).
