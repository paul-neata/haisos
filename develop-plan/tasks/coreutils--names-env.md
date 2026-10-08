# Task coreutils--names-env: env, which, sleep, true, false and running a program from a builtin

- Rock: coreutils
- Depends on: none
- Size: ~900 changed lines in ~12 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add env, which, sleep, true and false builtins

The task as first listed (with chmod, basename, dirname, realpath) came to
~1500 lines; those four are split off into coreutils--chmod-paths (its own
plan, no dependency on this one). This task keeps everything about finding
and running programs, which later tasks depend on.

## Goal

`env`, `which`, `sleep`, `true` and `false` are builtin commands (placed with
`BUILTIN rootfs <name> /bin/<name>`) behaving as GNU coreutils 9.4's (Ubuntu
24.04) -- `which` as Debian's `which` (debianutils), which is what Ubuntu
has. `env -i A=1 B=2` prints `A=1` and `B=2`; `env A=1 hsh -c 'echo $A'`
runs `hsh` found in `PATH` with `A` set; `sleep 100 &` in `hsh` can be
stopped at once; `which -a ls` prints every `ls` in `PATH`.

Shared contract 5 is introduced here, in `BuiltinRunProgram.h/.cpp`:
finding a program in `PATH` and running it as a child process, waited for --
used later by xargs, find `-exec`, and awk's `system()` and pipes.

## Context

Read first: the root `CLAUDE.md` ("Security", "Exit codes", "Builtin
Commands", rule 9), `src/components/BuiltinCommands/CLAUDE.md`,
`src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (how hsh starts
children).

What exists (the model to follow):
- `commands/hsh/HshShell.cpp`: `Shell::LookUpCommand` (PATH lookup: a name
  with `/` taken as is if it exists and is not a directory; otherwise each
  `:`-separated entry of PATH, an empty entry meaning the working directory,
  the first existing non-directory wins), `Shell::ChildEnvironment`,
  `Shell::StartChild` (asks `Process().OS()` at each use and releases it;
  passes the caller's descriptors 0/1/2, an empty slot as
  `Hsh::ClosedDescriptor::Create()` -- never null, which would mean "the
  console"; `interactive = false`; working directory
  `IO().GetCurrentDirectory()`), `Shell::WaitForChild` (`WaitToFinish(50)`
  slices; on a stop, `TriggerStop()` the child once and wait up to 5000 ms
  more). `kDefaultPath` there is dash's default PATH.
- `commands/hsh/HshDescriptors.h`: `Hsh::ClosedDescriptor`.
- `interfaces/IHaisosOS.h`: `StartProcess(environment, programPath, args,
  workingDirectory, options)` -- `programPath` absolute (resolved against the
  OS root, not the caller's directory), null when it cannot be run.
  `interfaces/IProcess.h`: `GetEnvironment()` returns a clone of the
  process's own environment; `ExitCode()`.
- `interfaces/IEnvironment.h`: `GetVariableNames`, `GetVariable`,
  `SetVariable`, `RemoveVariable`. Variables only are the "environment" of
  env; secrets and LLM identifiers are not environment variables and are left
  alone (even by `-i`).
- `BuiltinCommand.h/.cpp`: `ParseBuiltinArgs`, `BeginBuiltin`,
  `BuiltinContext::Flush()`, `ShellEscapeQuoted`. `commands/echo/Echo.cpp`
  handles `--help`/`--version` only as the sole argument -- the model for
  true/false.
- `BuiltinProcess`: `TriggerStop()` sets the flag `context.StopRequested()`
  reads; a stopped builtin exits 143 whatever it returns.

Rules that bite: `ICurrentProcess` is the only door out -- a program is
started only through `context.Process().OS()->StartProcess` (the OS asked for
at each use, never kept), files only through `context.IO()`; GNU output byte
for byte; every option of the real command in `Options()`; `--help` from
`BuiltinHelpText`; registration in `CreateStandardBuiltinCommands()` (feeds
the `haisos --init` template); portable C++17 (no POSIX headers;
`std::this_thread::sleep_for` is fine).

## Changes

### `BuiltinCommand.h` / `BuiltinCommand.cpp` -- stop at the first operand

GNU's `+` getopt mode, which env (and later xargs) needs: options are only
those before the first operand.

```cpp
ParsedBuiltinArgs ParseBuiltinArgs(const std::vector<std::string>& args,
    const std::vector<BuiltinOption>& options, bool stopAtFirstOperand = false);
std::optional<ParsedBuiltinArgs> BeginBuiltin(BuiltinContext& context,
    const IBuiltinCommand& command, int usageErrorStatus, int& exitStatus,
    bool stopAtFirstOperand = false);
```

With `stopAtFirstOperand`, the first argument that is not an option (`-`
alone included) and every argument after it, `--` included, become operands
untouched. `--` before any operand still ends the options and is dropped.
Existing callers are unchanged (default false).

### `src/components/BuiltinCommands/BuiltinRunProgram.h` / `.cpp` (new) -- contract 5

```cpp
namespace Haisos {
// dash's default PATH, used when the environment has none (as hsh does).
inline constexpr const char* kBuiltinDefaultSearchPath =
    "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

// PATH split at ':', every entry kept, empty ones as "" (the working
// directory). PATH is |environment|'s when given, else the caller's own
// (context.Process().GetEnvironment()); kBuiltinDefaultSearchPath's entries
// when that has no PATH.
std::vector<std::string> SearchPathEntries(BuiltinContext& context,
                                           const IEnvironment* environment = nullptr);

// Where |name| runs from, as an absolute path for StartProcess, or nullopt.
// A name holding '/' is taken as is (resolved by context.IO()) if something
// that is not a directory is there. Otherwise each SearchPathEntries entry in
// order: "<entry>/<name>" (no doubled '/'; an empty entry gives "<name>",
// the working directory), the first existing non-directory wins -- a
// builtin, a .md agent, a .lua script, any file (StartProcess decides what
// it can run). An empty name finds nothing. hsh's rules exactly.
std::optional<std::string> FindProgramInPath(BuiltinContext& context, const std::string& name,
                                             const IEnvironment* environment = nullptr);

struct RunProgramOptions {
    // The child's descriptors 0, 1, 2. Null: the caller's own slot 0/1/2;
    // an empty slot goes as Hsh::ClosedDescriptor, never as null.
    std::shared_ptr<IFileDescriptor> stdIn, stdOut, stdErr;
    // Resolved by context.IO(); nullopt: the caller's working directory.
    std::optional<std::string> workingDirectory;
    // The child's environment. Null: a clone of the caller's own
    // (context.Process().GetEnvironment(), which is already a clone).
    std::shared_ptr<IEnvironment> environment;
};

// Starts |programPath| (absolute, e.g. from FindProgramInPath) with |args|
// (argv[1] onwards) through context.Process().OS()->StartProcess -- never
// another way -- and waits for it. Returns its exit code; 127 if it could not
// be started (no OS, or StartProcess returned null), with *started set to
// false (true otherwise) when |started| is given. When the caller is asked to
// stop while waiting, the child is stopped (TriggerStop once, then up to
// 5000 ms more) and 143 is returned if it has not finished.
int RunProgramAndWait(BuiltinContext& context, const std::string& programPath,
                      const std::vector<std::string>& args,
                      const RunProgramOptions& options = {}, bool* started = nullptr);
}
```

`RunProgramAndWait` calls `context.Flush()` **before** starting the child:
the caller's buffered stdout must reach the descriptor before the child
writes to the same one (find `-exec` printing a name, then running a
command, depends on it). `StartProcessOptions::interactive` is false. Waiting
is in `WaitToFinish(50)` slices checking `context.StopRequested()`, as
`Shell::WaitForChild` does; the `shared_ptr<IHaisosOS>` from `OS()` is
released right after `StartProcess`.

`HshShell.cpp`: replace its `kDefaultPath` constant with
`kBuiltinDefaultSearchPath` (include `BuiltinRunProgram.h`), so the two never
differ. Nothing else in hsh changes.

### `commands/env/Env.cpp` (new)

`CreateEnvCommand()`; `env`, `1.0.0`; summary `run a program in a modified
environment`, usage `env [OPTION]... [-] [NAME=VALUE]... [COMMAND [ARG]...]`.

Options (GNU env 9.4): `-i, --ignore-environment`; `-0, --null`;
`-u, --unset=NAME` (Required); `-C, --chdir=DIR` (Required);
`-S, --split-string=S` (Required); not treated: `--block-signal[=SIG]`,
`--default-signal[=SIG]`, `--ignore-signal[=SIG]` (Optional),
`--list-signal-handling`, `-v, --debug`. Usage errors exit **125**
(`BeginBuiltin(..., 125, ..., /*stopAtFirstOperand=*/true)`).

How the environment reaches the child: `env` takes
`context.Process().GetEnvironment()` (already its own clone), edits it --
`-i` removes every variable, `-u NAME` removes one, each leading `NAME=VALUE`
operand sets one -- and hands that object to `RunProgramAndWait` as
`RunProgramOptions::environment`, which passes it to `StartProcess`. The
command is looked up with `FindProgramInPath(context, name, edited.get())`,
i.e. in the **new** environment's PATH, as execvp does after env set it.

Steps:
1. `-S`: split S into words (below) and re-run the parse on: every option
   given except the `-S` ones (re-spelled: `spelling`, then `argument` as its
   own word when it has one), then the words, then the operands. Repeat while
   a `-S` remains (at most 16 rounds).
2. Options in order: `-i`; `-u NAME` -- a NAME that is empty or holds `=`:
   `env: cannot unset q(NAME): Invalid argument`, exit 125; `-0`; `-C DIR`.
3. Operands: a first operand `-` means `-i` and is dropped. Then every
   operand holding `=` with a non-empty name sets a variable, until the
   first that does not: that is COMMAND, the rest its arguments.
4. No COMMAND: `-C` given -> `env: must specify command with --chdir (-C)` +
   Try, 125. Otherwise print every variable `NAME=VALUE`, each followed by
   `\n` (with `-0`, `\0`), **sorted by name** (`IEnvironment` keeps no
   order; GNU prints the environment's own order -- documented), exit 0.
5. COMMAND with `-0`: `env: cannot specify --null (-0) with command` + Try, 125.
6. `-C DIR`: `Stat` must find a directory, else `env: cannot change directory
   to q(DIR): No such file or directory` (or `Not a directory`), 125.
7. Find COMMAND: not found -> `env: q(COMMAND): No such file or directory`,
   127; a directory named with a `/` -> `env: q(COMMAND): Permission
   denied`, 126. Run it with `RunProgramAndWait` (environment = the edited
   one, workingDirectory = DIR); not started -> `env: q(COMMAND): Permission
   denied`, 126; otherwise exit with the child's code.

`q(x)` is `ShellEscapeQuoted(x, /*always=*/true)`.

`-S` splitting (GNU's rules, the subset): words separated by spaces/tabs;
`'...'` literal; `"..."` with backslash escapes inside; outside single
quotes `\\ \" \' \t \n \r \v \f \#` `\$`, `\_` (outside quotes it
separates words like a space -- `printf [%s] x\_y` gives `[x][y]`; inside
`"..."` it is a literal space), and `\c` (ignore the rest of S); `${NAME}` replaced by the variable's value from the
environment being built (unset: empty); `#` at the start of a word starts a
comment to the end. Errors, exit 125: `env: no terminating quote in -S
string`; `env: invalid sequence '\q' in -S`; `env: only ${VARNAME} expansion
is supported, error at: $X`. Example: `env -S 'echo a  b "c d"'` runs echo
with `a`, `b`, `c d`.

Help notes: variables printed in name order; `-i` empties the variables only
(secrets and LLM identifiers stay); signal options not treated (Haisos
processes have no signals).

GNU 9.4 transcripts to match:
```
$ env -i A=1 B=2
A=1
B=2
$ env nothingx                     -> env: 'nothingx': No such file or directory   (127)
$ env -C nothing pwd               -> env: cannot change directory to 'nothing': No such file or directory   (125)
$ env -0 echo hi                   -> env: cannot specify --null (-0) with command / Try 'env --help' ...  (125)
$ env -u 'A=B' true                -> env: cannot unset 'A=B': Invalid argument   (125)
$ env -i -u                        -> env: option requires an argument -- 'u' / Try ...   (125)
```

### `commands/which/Which.cpp` (new)

`CreateWhichCommand()`; `which`, `1.0.0`; `Help().basedOn` stays empty but
the help summary says Debian's: summary `locate a command`, usage `which
[-as] args`; notes: `Debian's which (debianutils), not GNU which; --help and
--version are Haisos's (Debian's which has none).` Options: `-a` ("print all
matching pathnames of each argument"), `-s` ("silent: exit status only").

Behaviour, as `/usr/bin/which` (a sh script) on Ubuntu 24.04:
- Parse with `ParseBuiltinArgs` directly (not `BeginBuiltin`): `--help` /
  `--version` as usual. Any other parse error: on stderr `Illegal option -x`
  (the option letter; for an unknown long option `Illegal option --`), then
  on **stdout** `Usage: <context.Process().Path()> [-as] args`, exit 2.
- For each operand: with a `/` in it, print it as given if `Stat` finds a
  regular file (`DirectoryEntryType::File`); otherwise for each
  `SearchPathEntries` entry (an empty one taken as `.`), the candidate is
  literally `entry + "/" + name` (so `./ls`, and `/bin//ls` for an entry
  `/bin/`), printed when `Stat` finds a regular file; without `-a` stop at the
  first. With `-s` nothing is printed.
- Exit 0 when every operand was found; 1 if any was not, or there were no
  operands.

### `commands/sleep/Sleep.cpp` (new)

`CreateSleepCommand()`; `sleep`, `1.0.0`; summary `delay for a specified
amount of time`; usage `sleep NUMBER[SUFFIX]...`; notes `SUFFIX: s
(seconds, the default), m (minutes), h (hours), d (days); NUMBER may be
fractional, several are added.` No options.
- No operand: `sleep: missing operand` + Try, 1.
- Each operand: `std::strtod` from its start (it skips leading whitespace and
  takes decimal, exponent, hex and `inf`/`infinity`); invalid when nothing
  was read, the value is NaN or negative, or what follows is anything but
  nothing or exactly one of `s m h d`. Each invalid operand:
  `sleep: invalid time interval q(arg)` (GNU 9.4: `sleep x y 0` reports `'x'`
  and `'y'`, then one Try line), exit 1 -- after all are checked, before
  sleeping.
- The sum (seconds; `inf` sleeps until stopped) is slept in slices of at most
  50 ms, checking `context.StopRequested()` each time, so a stop ends it at
  once (exit 143 from the process). Exit 0.

### `commands/true/True.cpp` and `commands/false/False.cpp` (new)

`CreateTrueCommand()`, `CreateFalseCommand()`; `1.0.0`; no options; summary
`do nothing, successfully` / `do nothing, unsuccessfully`; usage
`true [ignored command line arguments]` and `true OPTION` (false alike);
notes `--help and --version count only as the sole argument.` As GNU: when
the only argument is `--help` or `--version` print it; any other arguments
are ignored. `true` exits 0; `false` exits **1** -- after `--help` and
`--version` too (GNU 9.4: `false --help` exits 1).

### Registration and build

`BuiltinCommandList.h`: declare and register the five factories
(alphabetical). `CMakeLists.txt`: `BuiltinRunProgram.cpp`,
`commands/env/Env.cpp`, `commands/false/False.cpp`,
`commands/sleep/Sleep.cpp`, `commands/true/True.cpp`,
`commands/which/Which.cpp`.

## Tests

New files in `tests/unit/components/BuiltinCommands.unittests/`, added to its
`CMakeLists.txt`: `EnvTest.cpp`, `WhichSleepTrueFalseTest.cpp`. All
`TEST_F(BuiltinCommandsTest, ...)` on `RunCaptured`, exact stdout, stderr and
status. Environments: `auto env = factory->CreateEnvironment();
env->SetVariable("PATH", "/bin");` passed as `RunCaptured`'s last argument.

- `ParseStopsAtFirstOperandWhenAsked`: `ParseBuiltinArgs({"-i", "A=1", "-u",
  "x"}, envOptions, true)` -> one option, operands `A=1 -u x`; without the
  flag `-u x` is parsed as an option. (Build a small option table in the test.)
- `EnvPrintsSortedVariables`: env with `B=2`, `A=1`, `PATH=/bin`: `env` ->
  `A=1\nB=2\nPATH=/bin\n`; `env -i A=1 B=2` -> `A=1\nB=2\n`; `env -i -0 A=1`
  -> `A=1\0`; `env -u B` drops B; `env - A=1` as `-i`.
- `EnvRunsACommandWithTheEditedEnvironment`: `env -i PATH=/bin A=1 hsh -c
  'echo $A'` -> out `1\n`, 0; `env A=1 env` -> its listing holds `A=1`.
- `EnvReturnsTheExitCode`: `env hsh -c 'exit 3'` -> 3; `env false` -> 1.
- `EnvLooksUpInTheNewPath`: process PATH `/nowhere`; `env PATH=/bin echo hi`
  -> `hi\n`.
- `EnvErrors`: `env nothingx` -> `env: 'nothingx': No such file or
  directory\n`, 127; `env /docs` -> `env: '/docs': Permission denied\n`, 126;
  `env /notes.txt` -> 126 Permission denied (StartProcess refuses a .txt);
  `env -C /nothing pwd` -> 125 with the chdir line; `env -C /docs` -> `env:
  must specify command with --chdir (-C)\n` + Try, 125; `env -0 echo hi` ->
  125; `env -u A=B true` -> 125; `env -i -u` -> `env: option requires an
  argument -- 'u'\n` + Try, 125.
- `EnvChdir`: `env -C /docs pwd` -> `/docs\n`.
- `EnvSplitString`: `env -S 'echo a  b "c d"'` -> `a b c d\n`; `env -S
  'echo "x\_y"'` -> `x y\n`; `env -S 'echo a#b #c'` -> `a#b\n`; `env -S "'unterminated"` -> `env: no terminating
  quote in -S string\n`, 125.
- `EnvFlushesBeforeTheChild`: covered by find later; here: `hsh -c 'env echo
  a; echo b'` with stdout a file -> `a\nb\n`.
- `EnvStopStopsTheChild`: start `env sleep 100` with `os->StartProcess`
  (PATH=/bin), `TriggerStop()`, `WaitToFinish(3000)` true, exit code 143.
- `WhichFindsInPath`: PATH `/bin` -> `which ls` -> `/bin/ls\n`, 0; `which -a
  ls` with PATH `/bin:/bin` -> two lines; `which nothingx ls` -> `/bin/ls\n`,
  1; `which` -> 1, nothing; `which -s ls` -> nothing, 0; `which /bin/ls` ->
  `/bin/ls\n`; `which /docs` -> 1; PATH `:/bin` from working directory
  `/bin` -> `which ls` -> `./ls\n`.
- `WhichBadOption`: `which -x ls` -> err `Illegal option -x\n`, out `Usage:
  /bin/which [-as] args\n`, 2.
- `SleepSumsAndValidates`: `sleep 0.05 0.01s 0m 0h 0d` -> 0 in under 2 s;
  `sleep x y 0` -> `sleep: invalid time interval 'x'\nsleep: invalid time
  interval 'y'\nTry 'sleep --help' for more information.\n`, 1; `sleep 1x`,
  `sleep '0 '` invalid; `sleep ' 0'` valid; `sleep` -> missing operand.
- `SleepIsStoppedPromptly`: start `sleep inf`, `TriggerStop()`,
  `WaitToFinish(1000)` true, exit 143.
- `TrueAndFalse`: `true -x --bogus` -> 0, nothing; `false x` -> 1, nothing;
  `false --help` -> the help text, status 1; `true --help x` -> nothing, 0.

Update `BuiltinCommandsTest.cpp`: `ListsEveryBuiltinSortedWithAVersion` --
add `"env"`, `"false"`, `"sleep"`, `"true"`, `"which"` in sorted position;
`EveryBuiltinsHelpHasTheSameShape` and `EveryBuiltinHasAVersion` expect
status 0 for every builtin -- make them expect 1 for `false` (GNU's), 0 for
the rest. `EveryUntreatedOptionIsAcceptedAndReported` runs `env
--block-signal /docs` etc.: the report must be printed (it is, before env
tries `/docs` and exits 126).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Env*:BuiltinCommandsTest.Which*:BuiltinCommandsTest.Sleep*:BuiltinCommandsTest.TrueAndFalse:BuiltinCommandsTest.ParseStops*'
bash ./scripts/test_linux.sh L U
```
(`test_linux.sh L U` also runs `Hsh.unittests`, which must stay green after
the `kDefaultPath` change.)

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list; rows for
  `env`, `false`, `sleep`, `true`, `which` (1.0.0) with their exceptions;
  under "Key Classes", `BuiltinRunProgram` (`FindProgramInPath`,
  `SearchPathEntries`, `RunProgramAndWait`: how a builtin runs another
  program -- only through `ICurrentProcess::OS()`, flushing its stdout
  first) and the `stopAtFirstOperand` parse mode.
- Root `CLAUDE.md`: rows for the five commands; the command lists.

## Acceptance

- [ ] Contract 5 exactly as declared above; programs are started only through `context.Process().OS()->StartProcess`, the OS not kept.
- [ ] `RunProgramAndWait` flushes the caller's stdout first, stops the child on a stop, returns 127 when not started.
- [ ] env's child gets the edited environment; lookup uses its PATH.
- [ ] sleep and a waiting env end within a second of `TriggerStop()`.
- [ ] Every case in Tests prints GNU 9.4's (Debian which's) bytes and status.
- [ ] Generic builtin tests (with the `false` exception), `--init` template test, Hsh tests: green.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- chmod, basename, dirname, realpath (coreutils--chmod-paths).
- xargs, find `-exec`, awk `system()` (search, awk rocks: they call contract 5).
- Signals (`--block-signal` etc.), `env --argv0` (not in 9.4).
