# Task tools--pgrep-kill: pgrep, pkill and kill

- Rock: tools
- Depends on: tools--processes (`BuiltinProcessList.h`: `ProcessSnapshot`, `SnapshotProcesses`; `IProcess::Args`/`StartTime`), base--regex-match (`Regex` component, `RegexSyntax::Extended`), coreutils--names-env (`sleep`, used by the tests only)
- Size: ~850 changed lines in ~8 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the pgrep, pkill and kill builtins

(Split from tools--processes, which came to ~1500 changed lines.)

## Goal

Three new builtins after procps-ng 4.0.4, over Haisos's own processes:

- `pgrep` (`BUILTIN rootfs pgrep /bin/pgrep`): the pids of the processes
  whose name (or, `-f`, command line) matches an extended regular
  expression, with `-l -a -c -d -v -x -i -n -o -O -P -u -F -A -r -w`;
  exit 0 when something matched, 1 when nothing, 2 on a usage error.
- `pkill` (`/bin/pkill`): the same selection, each process asked to stop;
  `-SIGNAL`/`--signal`, `-e` (`sleep killed (pid 12)`), `-c`.
- `kill` (`/bin/kill`): `kill [-s SIG | -SIG] PID...`, `-l [SIG]`, `-L`,
  with procps's messages (`kill: (123): No such process`).

A Haisos process can only be asked to stop (`IProcess::TriggerStop()`), so
every signal whose default action ends a process (TERM, KILL, INT, HUP,
QUIT, ...) does that -- the process then exits 143 whatever the signal (a
documented exception: Linux reports 128+N); signal 0 only checks the
process exists; signals ignored by default (CHLD, CONT, URG, WINCH) do
nothing; STOP/TSTP/TTIN/TTOU (pausing) are reported as not treated. So in
`hsh`: `sleep 100 & pgrep sleep; kill $!; wait $!; echo $?` prints the pid
and `143`. `kill` is a `/bin` program only: hsh has no `kill` builtin, so
job specs (`kill %1`) are not supported (`kill: failed to parse argument:
'%1'`) but `kill $!` is.

## Context

Read first: the root `CLAUDE.md` ("Security", "Builtin Commands",
"Exit codes"), `src/components/BuiltinCommands/CLAUDE.md` (and its `ps`
row from tools--processes), `BuiltinCommand.h` (`ParseBuiltinArgs`,
`BeginBuiltin`, `NotTreated`), `interfaces/IProcess.h` (`TriggerStop`,
`WaitToFinish`), the tools--processes plan
(`develop-plan/tasks/tools--processes.md`) and the base--regex-syntax plan
(the `Regex` API and its error texts).

What earlier tasks provide, as if already on develop:

- tools--processes, `src/components/BuiltinCommands/BuiltinProcessList.h`:
  `struct ProcessSnapshot { uint64_t pid; uint64_t parentPid; std::string
  path; std::string name; /* comm: base name, 15 bytes */ std::string
  commandLine; /* path + args */ FileDateTime startTime; bool self;
  std::shared_ptr<IProcess> process; };` and
  `std::vector<ProcessSnapshot> SnapshotProcesses(BuiltinContext& context);`
  (the running processes of `context.Process().OS()`, finished ones left
  out, pid order); `IProcess::Args()`, `IProcess::StartTime()`.
- base--regex-match, `src/components/Regex/Regex.h` (CMake target
  `Regex`): `Regex::Compile(pattern, RegexOptions{RegexSyntax::Extended,
  ignoreCase}, error)` (null on a bad pattern, `error` glibc's message,
  e.g. `Unmatched ( or \(`), `Search(text, 0, match)`.
- coreutils--names-env: the `sleep` builtin (tests only).

procps-ng 4.0.4 is in the task container: `pgrep`, `pkill`, `kill` (Ubuntu's
`/usr/bin/kill` is procps's) -- check wording there when in doubt.

## Changes

### Rules that bite (root CLAUDE.md, restated)

- `ICurrentProcess` is the only door out: processes are found only through
  `SnapshotProcesses(context)` (which goes through
  `context.Process().OS()`), and stopped only through the `IProcess` it
  hands out (`TriggerStop()`, the one thing an outsider may ask). No
  `IHaisosOS` held.
- Builtins: procps's output and exit codes byte for byte; every option of
  the real command in `Options()` (untreated ones `kBuiltinNotTreated`,
  reported); `--help` from `BuiltinHelpText`; `--version` (`pgrep
  (HaisosOS builtin) 1.0.0`, likewise `pkill`, `kill`); `man` pages by
  default; each registered in `CreateStandardBuiltinCommands()` (the
  `haisos --init` template follows -- never hand-write it); sources in
  `CMakeLists.txt`; link `BuiltinCommands` to `Regex` if
  search--grep-core has not already; portable C++17.

### New `src/components/BuiltinCommands/BuiltinSignals.h` / `.cpp`

```cpp
// Linux's signal numbers and names (x86/ARM), on every platform: Haisos's own
// numbering, as kill -l prints it.
struct SignalInfo { int number; const char* name; };  // name without "SIG"
const std::vector<SignalInfo>& LinuxSignals();         // 1 HUP .. 31 SYS
// A signal as written: a number 0..64, or a name with or without "SIG", any
// case ("TERM", "sigterm", "Kill"). nullopt if neither.
std::optional<int> ParseSignal(std::string_view text);
// The name of 1..31 ("TERM"), else empty.
std::string SignalName(int number);

enum class SignalEffect { Probe, Stop, Ignored, NotTreated };
// 0 -> Probe; CHLD CONT URG WINCH -> Ignored; STOP TSTP TTIN TTOU ->
// NotTreated; every other (1..64) -> Stop (their default action ends the
// process; 32..64 are real-time signals, which do too).
SignalEffect EffectOf(int signal);
```

The table: `1 HUP 2 INT 3 QUIT 4 ILL 5 TRAP 6 ABRT 7 BUS 8 FPE 9 KILL 10
USR1 11 SEGV 12 USR2 13 PIPE 14 ALRM 15 TERM 16 STKFLT 17 CHLD 18 CONT 19
STOP 20 TSTP 21 TTIN 22 TTOU 23 URG 24 XCPU 25 XFSZ 26 VTALRM 27 PROF 28
WINCH 29 POLL 30 PWR 31 SYS`.

Sending (shared by kill and pkill), for one process and signal: `Probe`
and `Ignored` do nothing; `Stop` calls `process->TriggerStop()`;
`NotTreated` calls `context.NotTreated(spelling)` (the signal as the user
wrote it: `-STOP`, `--signal STOP`; reported once) and does nothing. None
of them is a failure.

### New `src/components/BuiltinCommands/commands/kill/Kill.cpp`

`CreateKillCommand()`; `Name()` `kill`; summary `send a signal to a
process`; usage `kill [options] <pid> [...]`. Arguments parsed procps's
way (not `ParseBuiltinArgs`):

1. If the first argument is `-X` and `ParseSignal(X)` succeeds (`-9`,
   `-KILL`, `-SIGKILL`, `-kill`, `-0`), it is the signal; drop it.
2. Then, left to right: `-l [SIG]` / `--list[=SIG]` (the argument only
   when attached or the next word is not another option -- `kill -l 9`),
   `-L` / `--table`, `-s SIG` / `--signal SIG` (`--signal=SIG`), `-q VAL`
   / `--queue VAL` (not treated: reported, then sent as a plain signal),
   `-h` / `--help`, `-V` / `--version`, `--` (the rest are pids, so `kill
   -- -1` works). Any other `-...` -> `kill: invalid argument C` (C its
   first letter after the dash, as procps's getopt reports it: `kill -FOO
   1` -> `invalid argument F`; `kill -n 9 1` -> `invalid argument n`, since
   `-n` is bash's, not procps's) followed by the usage block, exit 1.
3. `-l`: no SIG -> `LinuxSignals()` names joined by single spaces with a
   newline after the 16th (`STKFLT`) and after the last; SIG a number with
   a name -> the name; a name -> its number; else `kill: unknown signal
   name SIG` on stderr. Only the first SIG counts. Exit 0. `-L`: each signal
   as `%2d %-8s`, seven per line, the seventh of a line without its padding
   and followed by `\n`, the last line padded and ended by `\n` (verified,
   `cat -A`):
   ```
    1 HUP      2 INT      3 QUIT     4 ILL      5 TRAP     6 ABRT     7 BUS
    8 FPE      9 KILL    10 USR1    11 SEGV    12 USR2    13 PIPE    14 ALRM
   15 TERM    16 STKFLT  17 CHLD    18 CONT    19 STOP    20 TSTP    21 TTIN
   22 TTOU    23 URG     24 XCPU    25 XFSZ    26 VTALRM  27 PROF    28 WINCH
   29 POLL    30 PWR     31 SYS     
   ```
   (the last line ends in five spaces after `SYS`).
4. No pid -> the usage block on stderr, exit 1.
5. Default signal TERM. Each pid operand: not an integer (`abc`, `%1`) ->
   `kill: failed to parse argument: 'abc'`, status 1, go on. Then from
   `SnapshotProcesses`: `N > 0` the process with that pid; `-1` every
   process but kill itself; `-N` (N > 1) the process N (each process is
   its own group); `0` kill itself (its group; it then exits 143). None ->
   `kill: (N): No such process`, status 1. A `-s` name `ParseSignal`
   cannot read -> `kill: (N): Invalid argument` for each pid, status 1
   (procps passes the bad name on as -1 and gets EINVAL -- verified).
   Otherwise send it (above). Exit status: 1 if any pid failed, else 0.

The usage block (procps-ng 4.0.4, verbatim, to stderr, starting with an
empty line):

```

Usage:
 kill [options] <pid> [...]

Options:
 <pid> [...]            send signal to every <pid> listed
 -<signal>, -s, --signal <signal>
                        specify the <signal> to be sent
 -q, --queue <value>    integer value to be sent with the signal
 -l, --list=[<signal>]  list all signal names, or convert one to a name
 -L, --table            list all signal names in a nice table

 -h, --help     display this help and exit
 -V, --version  output version information and exit

For more details see kill(1).
```

`--help` itself prints `BuiltinHelpText` (the one shape), exit 0.
`Options()`: `-s --signal` (SIG), `-l --list` (optional), `-L --table`,
`-q --queue` (not treated), `-h` (as `--help`), `-V`. Help notes: the
signal effects above, exit 143 for any stopping signal, pids only (no job
specs), STOP/CONT family not treated. (The reference page is man7's
`kill(1)`, what `BuiltinReferenceUrl` gives; fine.)

### New `src/components/BuiltinCommands/commands/pgrep/Pgrep.cpp`

Two commands sharing one implementation: `CreatePgrepCommand()` and
`CreatePkillCommand()` (both declared in `BuiltinCommandList.h`). Names
`pgrep`/`pkill`; summaries `look up processes based on name and other
attributes` / `signal processes based on name and other attributes`
(one man page each: man7's `pgrep.1` covers both; the default reference
URL for `pkill` -- `pkill.1` -- exists on man7 too); usage `pgrep
[options] <pattern>` / `pkill [options] <pattern>`.

Options (procps-ng 4.0.4, `pgrep --help` / `pkill --help`; parsed with
`ParseBuiltinArgs` after the pkill pre-pass):

- pkill only, first: if the first argument is `-X` with `ParseSignal(X)`
  valid, it is the signal (`pkill -9 sleep`, `pkill -KILL sleep`).
  Otherwise it is parsed as options (procps's own quirk: `pkill -FOO x` is
  `-F OO` -> `pkill: pidfile not valid`).
- both: `-c --count`, `-f --full`, `-g --pgroup` (nt), `-G --group` (nt),
  `-i --ignore-case`, `-n --newest`, `-o --oldest`, `-O --older SECS`,
  `-P --parent PPIDS`, `-s --session` (nt), `--signal SIG`, `-t
  --terminal` (nt), `-u --euid IDS`, `-U --uid IDS`, `-x --exact`, `-F
  --pidfile FILE`, `-L --logpidfile` (nt), `-r --runstates STATES`, `-A
  --ignore-ancestors`, `--cgroup` (nt), `--ns PID` (nt), `--nslist` (nt),
  `-h --help`, `-V --version`;
- pgrep only: `-d --delimiter STR`, `-l --list-name`, `-a --list-full`,
  `-v --inverse`, `-w --lightweight` (same as without: one thread per
  process);
- pkill only: `-e --echo`, `-H --require-handler` (nt), `-q --queue` (nt).

(nt = `kBuiltinNotTreated`: reported, ignored.)

Logic:

1. Usage errors (stderr, exit 2; procps prints its usage block after some
   of these -- Haisos prints the `Try` line instead, documented): a parser
   error -> `pgrep: <error>` then ``Try `pgrep --help' for more
   information.`` (backquote-quote, procps's form); more than one operand
   -> `pgrep: only one pattern can be provided` + Try; no pattern and none
   of `-P -u -U -F -O -r` (nor the untreated `-g -G -s -t --ns --cgroup`)
   -> `pgrep: no matching criteria specified` + Try; `-n` with `-o` ->
   just the Try line (procps prints only its usage); `-u`/`-U` with a name
   or number other than `haisos`/`0` -> `pgrep: invalid user name: X` +
   Try; `--signal X` unknown -> `Unknown signal "X".` (no prefix) + Try;
   `-F` file missing or without a pid on its first line -> `pgrep: pidfile
   not valid` + Try, exit **1** (verified); a bad regex -> `pgrep: regex
   error: <Regex error>`, exit 2. (`pkill` in place of `pgrep` for pkill.)
2. Pattern: `Regex::Compile(pattern, {Extended, ignoreCase: -i})`; with
   `-x`, the pattern `^(` + pattern + `)$`. Without `-f`, a pattern longer
   than 15 bytes warns first, on stderr: `pgrep: pattern that searches for
   process name longer than 15 characters will result in zero matches`
   and `Try `pgrep -f' option to match against the complete command line.`
   (verified; matching then goes on, so such a pattern finds nothing, exit 1).
3. Candidates: `SnapshotProcesses(context)` minus the process itself
   (`self`). Keep those passing every given filter: `-P` (parentPid in
   the list -- every process's parent is the OS, so `pgrep -P $$` from hsh
   finds nothing: documented), `-u/-U` (all), `-O N` (started at least N
   seconds ago), `-F` (pid in the file), `-r` (state letters: `S` matches
   every candidate, `R` none -- the only running one is the caller), `-A`
   (ancestors: none are listed; a no-op that is correct), and the
   pattern against `name` (or `commandLine` with `-f`), inverted by `-v`.
   `-n`: only the newest (latest `startTime`, ties: highest pid); `-o`:
   the oldest.
4. pgrep output: `-c` -> the count and `\n` (even `0`); else each pid --
   `pid name` with `-l`, `pid commandLine` with `-a` -- joined by the
   delimiter (`-d`, default `\n`), then `\n` (verified: `pgrep -l -d,
   bash` -> `559 bash,11813 bash`). Exit 0 if any matched, else 1.
5. pkill: send the signal (default TERM; `-SIG` or `--signal`) to each
   (above); `-e` -> `<name> killed (pid N)` per process (verified form);
   `-c` -> the count. Exit 0 if any matched, else 1.

Help notes (both): signals stop processes (exit 143) or are ignored as
for kill; the name is the program's base name (15 bytes); `-f` matches
`<program path> <args>`; one user (`haisos`, 0); parents are the OS;
`-g -G -s -t -L -H -q --cgroup --ns` not treated; usage errors end in the
Try line instead of procps's usage text.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Register `CreateKillCommand()`, `CreatePgrepCommand()`,
`CreatePkillCommand()`; add `BuiltinSignals.cpp`,
`commands/kill/Kill.cpp`, `commands/pgrep/Pgrep.cpp`; `Regex` in
`target_link_libraries` if missing.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/PgrepKillTest.cpp`
(in that directory's `CMakeLists.txt`), on `RunCaptured`. Helper
processes: `os->StartProcess(os->GetOsEnvironment()->Clone(),
"/bin/sleep", {"100"}, "/", StartProcessOptions{})`; every test stops
what it started (`TriggerStop` + `WaitToFinish`) at the end.

- `SignalsTableAndParsing` (plain `TEST`s on `BuiltinSignals.h`):
  `ParseSignal("9")`, `"KILL"`, `"SIGKILL"`, `"kill"` -> 9; `"0"` -> 0;
  `"65"`, `"FOO"` -> nullopt; `SignalName(15)` `TERM`; `EffectOf` for 0,
  15, 9, 17, 19.
- `KillListsSignals`: `kill -l` -> `HUP INT QUIT ILL TRAP ABRT BUS FPE
  KILL USR1 SEGV USR2 PIPE ALRM TERM STKFLT\nCHLD CONT STOP TSTP TTIN TTOU
  URG XCPU XFSZ VTALRM PROF WINCH POLL PWR SYS\n`; `-l 9` -> `KILL\n`;
  `-l TERM` -> `15\n`; `-l 64` -> err `kill: unknown signal name 64\n`,
  status 0; `-L` -> the five lines above byte for byte.
- `KillStopsAProcess`: start sleep; `kill <pid>` -> no output, status 0;
  the sleep finishes within the wait and its `ExitCode()` is 143; `kill -9
  <pid>` and `kill -s INT <pid>` likewise; `kill -0 <pid>` -> 0 and it
  keeps running.
- `KillErrors`: `kill 999999` -> err `kill: (999999): No such process\n`,
  1; `kill abc` -> `kill: failed to parse argument: 'abc'\n`, 1; `kill %1`
  likewise; `kill` -> the usage block exactly, 1; `kill -FOO 1` -> `kill:
  invalid argument F\n` + the usage block, 1; `kill -s BOGUS <pid>` ->
  `kill: (<pid>): Invalid argument\n`, 1, the process still running;
  `kill -STOP <pid>` -> err `Parameter -STOP is not treated by HaisosOS
  kill v. 1.0.0\n`, 0, still running.
- `KillFromHsh`: `hsh -c 'sleep 100 & kill $!; wait $!; echo $?'` -> out
  `143\n`.
- `PgrepFindsByNameAndCommandLine`: two sleeps (`100`, `200`); `pgrep
  sleep` -> both pids, ascending, `\n`-terminated; `-l` -> `<pid>
  sleep\n...`; `-a` -> `<pid> /bin/sleep 100\n<pid> /bin/sleep 200\n`;
  `-f 'sleep 2'` -> the second; `-x slee` -> nothing, 1; `-x sleep` ->
  both; `-i SLEEP` -> both; `-c sleep` -> `2\n`; `-c nosuch` -> `0\n`, 1;
  `-d, sleep` -> `<a>,<b>\n`; `-n sleep` -> the second, `-o sleep` the
  first; `-v sleep` -> no sleep pid (and never pgrep's own).
- `PgrepFilters`: `-P <os id> sleep` -> both; `-P 1 sleep` -> 1; `-u
  haisos sleep` -> both; `-u root x` -> `pgrep: invalid user name: root`
  + Try, 2; `-O 3600 sleep` -> nothing, 1; `-F /pidfile` holding the first
  pid -> it.
- `PgrepUsageErrors`: `pgrep` -> `pgrep: no matching criteria
  specified\nTry \`pgrep --help' for more information.\n`, 2; `pgrep a b`
  -> `pgrep: only one pattern can be provided` + Try, 2; `pgrep '('` ->
  `pgrep: regex error: Unmatched ( or \\(\n`, 2; `pgrep -F /missing` ->
  `pgrep: pidfile not valid` + Try, 1; `pgrep --bogus` -> `pgrep:
  unrecognized option '--bogus'` + Try, 2.
- `PkillStopsMatches`: `pkill -e sleep` -> `sleep killed (pid <a>)\nsleep
  killed (pid <b>)\n`, 0, both finish with 143; `pkill -9 -f 'sleep 100'`
  -> only that one; `pkill nosuch` -> 1; `pkill --signal STOP sleep` ->
  the not-treated line, both still running, 0; `pkill --signal FOO sleep`
  -> `Unknown signal "FOO".\nTry \`pkill --help' for more information.\n`,
  2; `pkill -c sleep` -> `2\n`.
- `HelpAndVersion`: `pgrep --help`, `pkill --help`, `kill --help` start
  `HaisosOS <name> version 1.0.0 -`; `--version` lines; add `kill`,
  `pgrep`, `pkill` to `ListsEveryBuiltinSortedWithAVersion` in byte order.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Kill*:BuiltinCommandsTest.Pgrep*:BuiltinCommandsTest.Pkill*:*SignalsTableAndParsing*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `kill`, `pgrep`, `pkill` in
  the opening list and rows (versions 1.0.0; treated options; exceptions:
  stopping signals all end the process with 143, STOP/TSTP/TTIN/TTOU not
  treated, CHLD/CONT/URG/WINCH ignored, pid 0/-N/-1 meanings with each
  process its own group, the name is the program's base name and `-f`
  matches the path plus arguments, one user, parents are the OS, usage
  errors end in the Try line); a line on `BuiltinSignals.h`.
- Root `CLAUDE.md`: rows for `kill`, `pgrep`, `pkill` in the Builtin
  Commands table and the builtin lists; note in the `kill` row that hsh
  has no `kill` builtin (no job specs).

## Acceptance

- [ ] Processes are found only through `SnapshotProcesses` and stopped only with `IProcess::TriggerStop()`; never the caller unless asked (`kill 0`, `kill -1` excludes kill itself).
- [ ] `kill -l`, `-L`, the usage block and every message byte for byte as procps-ng 4.0.4; exit codes 0/1.
- [ ] `pgrep`/`pkill` selection, output, `-e` lines, exit codes 0/1/2 as procps; ERE through the `Regex` component; never matches itself.
- [ ] `hsh -c 'sleep 100 & kill $!; wait $!; echo $?'` prints `143`.
- [ ] Every option of the three commands in `Options()`; untreated ones reported; `--help`/`--version`/`man` work.
- [ ] Registered (the `--init` template follows); CMakeLists; tests green on Linux.

## Out of scope

- `ps` and the `IProcess` additions (tools--processes).
- A `kill` builtin in hsh and job specs; pausing/continuing processes;
  process groups, sessions, terminals, namespaces, cgroups; `sigqueue`
  values.
