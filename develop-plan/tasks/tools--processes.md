# Task tools--processes: ps, and a process's arguments and start time

- Rock: tools
- Depends on: coreutils--date (`FormatDateTime`, `LocalTimeOf` in `BuiltinDate.h`)
- Size: ~850 changed lines in ~14 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the ps builtin; IProcess reports Args and StartTime

(The planned tools--processes came to ~1500 changed lines with pgrep,
pkill and kill; those are the next task, tools--pgrep-kill, which reuses
this task's process snapshot.)

## Goal

1. `IProcess` reports two more things every process knows at its start:
   `Args()` (the arguments it was started with) and `StartTime()`. So `ps
   -f` can show a command line (`/bin/sleep 100`), `pgrep -f` can match
   one, and `STIME`/`ELAPSED` mean something.
2. A new builtin `ps` (`BUILTIN rootfs ps /bin/ps`) after procps-ng 4.0.4,
   listing the processes of the OS the caller runs in
   (`ICurrentProcess::OS()->GetRunningProcesses()`), in procps's layouts
   byte for byte: the default (`    PID TTY          TIME CMD`), `-f`,
   `-F`, `-l`, `-j`, BSD `u`/`aux` and `ax`, `-o`/`-O` with procps's
   field names, widths and headers, selection by `-e -A -p -C --ppid -u
   -N`, `--sort`, `--no-headers`. What Haisos does not have is shown with
   fixed values, each a documented exception: no terminals (`TTY` `?`),
   no CPU accounting (`TIME` `00:00:00`, `C`/`%CPU` 0), no memory figures
   (`VSZ`/`RSS`/`SZ` 0), one user (`haisos`, uid 0, as `ls`/`stat` show
   it), no process groups or sessions (each process its own).

`sleep 100 &` then `ps` in `hsh` prints

```
    PID TTY          TIME CMD
      4 ?        00:00:00 hsh
      5 ?        00:00:00 sleep
      6 ?        00:00:00 ps
```

## Context

Read first: the root `CLAUDE.md` ("Security: `ICurrentProcess` is the only
door out", "Creating things", "Builtin Commands"),
`src/components/HaisosOS/CLAUDE.md` (parent pids are the OS's id; finished
processes stay in the list until the next `StartProcess` cleans them up),
`interfaces/IProcess.h`, `interfaces/IHaisosOS.h` (`GetRunningProcesses`,
`GetOSProcessID`), `src/components/BuiltinCommands/CLAUDE.md`,
`BuiltinCommand.h`, `BuiltinProcess.h/.cpp`,
`src/components/HaisosOS/AgentProcess.h/.cpp`, `LuaProcess.h/.cpp`,
`HaisosOS.cpp` (`StartAgentProcess`, `StartLuaProcess`,
`StartBuiltinProcess`), `src/tools/os_list_processes/` (another reader of
the process list; unchanged here).

What `IProcess` offers today: `GetPid`, `GetParentPid` (the OS's id for
every process `StartProcess` made -- hsh's children included: process
parentage is being redefined, so `ps` shows that), `Path` (the resolved
program path), `StartingAgentName`, `GetEnvironment`, `TriggerStop`,
`WaitToFinish`, `ExitCode`. Missing for ps/pgrep: the arguments (only
`BuiltinProcess` and `LuaProcess` keep them, privately; an agent's are
folded into its prompt) and a start time (none). Both are cheap to keep
and worth adding: without arguments `ps -f`'s CMD and `pgrep -f` are
useless; without a start time `STIME`, `START`, `ELAPSED`, `pgrep -n/-o`
have nothing to show. A working directory, a terminal, CPU time or
memory are not added: nothing in Haisos measures them.

What earlier tasks provide, as if already on develop (coreutils--date,
`src/components/BuiltinCommands/BuiltinDate.h`):
`std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);`
(GNU date's strftime) and `std::tm LocalTimeOf(int64_t seconds);`.

procps-ng 4.0.4 is in the task container (`ps`, `ps --help all`): compare
headers and column positions with it; only the values differ.

## Changes

### Rules that bite (root CLAUDE.md, restated)

- `ICurrentProcess` is the only door out: `ps` reaches the process list
  only through `context.Process().OS()` (null -> an empty list), never an
  `IHaisosOS` of its own. It only looks: it never stops a process.
- Classes implementing `interfaces/` keep private constructors and
  `static Create()` returning `shared_ptr` (`AgentProcess::Create` gains a
  parameter; nothing else about construction changes).
- Builtins: procps's output byte for byte; every option of procps ps in
  `Options()` (BSD letters, which the table cannot hold next to the Unix
  ones, are listed in the help notes); `--help` from `BuiltinHelpText`;
  `--version` (`ps (HaisosOS builtin) 1.0.0`); `man ps` (the default
  `ManPage()`); registered in `CreateStandardBuiltinCommands()` (the
  `haisos --init` template follows -- never hand-write it); portable C++17,
  no POSIX headers.

### `interfaces/IProcess.h`

Add to `IProcess`, after `Path()`:

```cpp
// The arguments the program was started with -- argv[1] onwards, exactly
// as passed to IHaisosOS::StartProcess (the program path is Path()).
virtual std::vector<std::string> Args() const = 0;

// When the process was started: the wall clock (UTC, as every FileDateTime)
// at its creation. Never changes.
virtual FileDateTime StartTime() const = 0;
```

(`FileDateTime` comes with `IFileIO.h` -> `IFileSystemService.h`, already
included; add `<vector>`.)

### Process classes

- `src/components/BuiltinCommands/BuiltinProcess.h/.cpp`: `Args()` returns
  `m_args`; a `const FileDateTime m_startTime` set from
  `CurrentFileDateTime()` (`FilesystemUtils.h`) in the constructor.
- `src/components/HaisosOS/LuaProcess.h/.cpp`: the same (`m_args` exists).
- `src/components/HaisosOS/AgentProcess.h/.cpp`: `Create` gains
  `std::vector<std::string> args` (after `path`), stored and returned by
  `Args()`; the start time as above. `HaisosOS::StartAgentProcess` passes
  its `args` (they still go into the prompt too, unchanged).
- `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`:
  `FakeProcess` returns `{}` and `FileDateTime{}`.
- Any other `IProcess` implementer the build finds (grep for
  `GetParentPid() const override`): the same.

`os_list_processes` and the `os_*` tools are unchanged.

### New `src/components/BuiltinCommands/BuiltinProcessList.h` / `.cpp`

Shared by ps and, next, pgrep/pkill/kill:

```cpp
struct ProcessSnapshot {
    uint64_t pid = 0;
    uint64_t parentPid = 0;
    std::string path;          // IProcess::Path()
    std::string name;          // "comm": the base name of path, cut to 15 bytes (Linux's TASK_COMM_LEN - 1)
    std::string commandLine;   // path, then each of Args(), joined by single spaces
    FileDateTime startTime;
    bool self = false;         // the process asking
    std::shared_ptr<IProcess> process;  // to TriggerStop (kill, pkill)
};

// The running processes of the caller's OS -- context.Process().OS()->GetRunningProcesses(),
// without those that have finished (WaitToFinish(0) true: Haisos has no
// zombies to show), sorted by pid. Empty when the OS is gone.
std::vector<ProcessSnapshot> SnapshotProcesses(BuiltinContext& context);

// "[[DD-]hh:]mm:ss" (procps etime), "[DD-]hh:mm:ss" (time), "m:ss" (BSD TIME).
std::string FormatElapsed(int64_t seconds);
std::string FormatCpuTime(int64_t seconds);
std::string FormatBsdCpuTime(int64_t seconds);
```

`name` for `/bin/sleep` is `sleep`, for `/agent.md` `agent.md`, for
`/tools/setup.lua` `setup.lua` (as Linux's comm for a script is its file
name). `commandLine` starts with the path, not the name the shell used:
`/bin/sleep 100` where Linux shows `sleep 100` (documented exception: the
argv[0] as typed is not kept).

### New `src/components/BuiltinCommands/commands/ps/Ps.cpp`

`CreatePsCommand()`; `Name()` `ps`, `Version()` `1.0.0`; help summary
`report a snapshot of the current processes`, usage `ps [options]`.

**Argument parsing** is procps's own, not `ParseBuiltinArgs` (ps mixes
three syntaxes). Each argument, left to right:

- `--name[=value]` or `--name value`: a long option (exact names, no
  abbreviation, as procps). Unknown -> error `unknown gnu long option`.
- `-letters`: Unix options, clustered (`-ef`); an option taking a list
  (`-p -q -C -u -U -g -G -s -t -o -O -D`) takes the rest of the cluster,
  else the next argument; none left -> its error (`list of process IDs
  must follow -p`, `list of command names must follow -C`, `list of
  process IDs must follow --ppid`, `format specification must follow -o`,
  `long sort specification must follow --sort`; for the others the same
  shape -- `list of users must follow -u` etc.; procps reads a bare `-u` as
  BSD `u`, an oddity not copied: documented). Unknown letter -> `unsupported SysV option`.
- otherwise: BSD letters, clustered (`aux`); `o O p q t U k` take the rest
  of the cluster or the next argument. Unknown -> `unsupported option
  (BSD syntax)`.
- A list is comma- or blank-separated (`-p 1,2`, `-p "1 2"`, `-p 1 -p 2`
  accumulate). A pid that is not a number -> `process ID list syntax
  error`; 0 or negative -> `process ID out of range`. `-u`/`-U`/`U`
  names: `haisos` or `0` select every process; any other name or number ->
  `user name does not exist` (Haisos has one user).

Every error goes to stderr exactly as procps writes it, exit 1:

```
error: <message>

Usage:
 ps [options]

 Try 'ps --help <simple|list|output|threads|misc|all>'
  or 'ps --help <s|l|o|t|m|a>'
 for additional help text.

For more details see ps(1).
```

`--help` (with an optional following word `simple list output threads misc
all s l o t m a`, consumed and ignored) prints `BuiltinHelpText`, exit 0;
`-V`, `V`, `--version` print `BuiltinVersionText`, exit 0.

**Selection.** No selection option: every process (procps selects the
caller's user and terminal; in Haisos every process shares both --
documented). Union of the given selections: `-e -A -a -d a x T` (all),
`r` (running: only ps itself, the one process whose state is `R`), `-p p
--pid -q q --quick-pid PIDs`, `--ppid PIDs`, `-C NAMES` (exact `name`),
`-u -U U --user --User` (all or the error above); `-N`/`--deselect`
inverts the result. Exit status 1 when nothing was listed, else 0
(verified: `ps -p 999999` prints the header and exits 1).

**Formats.** One format, the last of: default; `-f` (full); `-F` (extra
full); `-l` (long; with `-f`, `-lf`); `-j` (jobs); BSD `u`; BSD with no
format letter (`ax`, `x`, `a`); `-o`/`o`/`--format LIST` (several
accumulate); `-O`/`O LIST` (`pid`, then LIST, then `s`, `tname` (header `TTY`), `time`,
`args` (header `COMMAND`) -- verified: `ps -O comm -p 1` prints `    PID
COMMAND         S TTY          TIME COMMAND` and `      1 systemd         S
?        00:00:30 /sbin/init`). `-O` after another format -> error
`option -O can not follow other format options`. `-o` given -> it wins
over `-f -F -l -j u`. `-P` inserts a `psr` column after
`ppid` when the format has one (`-fP`: `UID          PID    PPID PSR  C
STIME ...`), else after `pid` (`-P`: `    PID PSR TTY          TIME CMD`)
-- verified.

Exact headers (procps-ng 4.0.4, verified) and a Haisos row:

```
default  "    PID TTY          TIME CMD"
         "     12 ?        00:00:00 sleep"
-f       "UID          PID    PPID  C STIME TTY          TIME CMD"
         "haisos        12       3  0 19:00 ?        00:00:00 /bin/sleep 100"
-F       "UID          PID    PPID  C    SZ   RSS PSR STIME TTY          TIME CMD"
         "haisos        12       3  0     0     0   0 19:00 ?        00:00:00 /bin/sleep 100"
-l       "F S   UID     PID    PPID  C PRI  NI ADDR SZ WCHAN  TTY          TIME CMD"
         "0 S     0      12       3  0  80   0 -     0 -      ?        00:00:00 sleep"
-lf      "F S UID          PID    PPID  C PRI  NI ADDR SZ WCHAN  STIME TTY          TIME CMD"
         "0 S haisos        12       3  0  80   0 -     0 -      19:00 ?        00:00:00 /bin/sleep 100"
-j       "    PID    PGID     SID TTY          TIME CMD"
         "     12      12      12 ?        00:00:00 sleep"
u        "USER         PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND"
         "haisos        12  0.0  0.0      0     0 ?        S    19:00   0:00 /bin/sleep 100"
ax, x    "    PID TTY      STAT   TIME COMMAND"
         "     12 ?        S      0:00 /bin/sleep 100"
```

(3 is the OS's id, the parent of every process; 19:00 a start time of
today.)

**Fields** (`-o` names -> header, width, alignment; columns are joined by
one space; the last column is never padded or cut; a field not last is
padded to its width, and a longer value is printed whole; `name=HEADER`
renames the header and takes the rest of that `-o` argument, commas
included; `name=` gives an empty header, and when every header is empty
no header line is printed):

| names | header | width | align | Haisos value |
|-------|--------|-------|-------|--------------|
| `pid` `tgid` | PID / TGID | 7 | right | pid |
| `ppid` | PPID | 7 | right | parent pid (the OS's id) |
| `pgid` `pgrp` `sid` `sess` `lwp` `spid` `tid` | PGID / PGRP / SID / SESS / LWP / SPID / TID | 7 | right | pid (each process its own group, session and only thread) |
| `nlwp` `thcount` | NLWP / THCNT | 4 | right | 1 |
| `user` `euser` `ruser` `suser` `fuser` `uname` | USER / EUSER / RUSER / SUSER / FUSER / USER | 8 | left | `haisos` |
| `uid` `euid` `ruid` `suid` `fuid` | UID / EUID / RUID / SUID / FUID | 5 | right | 0 |
| `group` `egroup` `rgroup` `sgroup` `fgroup` | GROUP / EGROUP / RGROUP / SGROUP / FGROUP | 8 | left | `haisos` |
| `gid` `egid` `rgid` `sgid` `fgid` | GID / EGID / RGID / SGID / FGID | 5 | right | 0 |
| `tty` `tt` `tname` | TT | 8 | left | `?` (in the default/-f/-l/-j formats the header is `TTY`) |
| `stat` | STAT | 4 | left | `R` for ps itself, `S` otherwise |
| `s` `state` | S | 1 | left | as `stat` |
| `etime` | ELAPSED | 11 | right | `FormatElapsed(now - start)` |
| `etimes` | ELAPSED | 7 | right | seconds |
| `time` `cputime` | TIME | 8 | right | `00:00:00` |
| `cputimes` `times` | TIME | 7 | right | 0 |
| `comm` `ucmd` `ucomm` | COMMAND | 15 | left | `name` (default formats: header `CMD`) |
| `args` `command` | COMMAND | 27 | left | `commandLine` |
| `cmd` | CMD | 27 | left | `commandLine` |
| `stime` `start_time` | STIME / START | 5 | left | `HH:MM` if started today (local), else `MonDD` (`Oct06`) this year, else `YYYY` |
| `start` | STARTED | 8 | right | `HH:MM:SS` today, else `%b %d` (`Oct 06`) |
| `bsdstart` | START | 6 | right | `HH:MM` today, else `%b %e` (`Oct  6`) |
| `lstart` | STARTED | 24 | left | `FormatDateTime("%a %b %e %H:%M:%S %Y")` (`Tue Oct  6 01:32:10 2026`) |
| `c` | C | 2 | right | 0 |
| `%cpu` `pcpu` | %CPU | 4 | right | `0.0` |
| `%mem` `pmem` | %MEM | 4 | right | `0.0` |
| `vsz` `vsize` | VSZ | 6 | right | 0 |
| `rss` `rssize` `rsz` | RSS | 5 | right | 0 |
| `sz` | SZ | 5 | right | 0 |
| `ni` `nice` | NI | 3 | right | 0 |
| `pri` | PRI | 3 | right | 19 (in `-l`, the PRI column is 80) |
| `psr` | PSR | 3 | right | 0 |
| `f` `flag` `flags` | F | 1 | right | 0 |
| `wchan` | WCHAN | 6 | left | `-` |
| `addr` | ADDR | (see `-l`) | | `-` |

Times are local (`LocalTimeOf`, `FormatDateTime(..., false)`). Any other
name -> error `unknown user-defined format specifier "NAME"`. Verify each
header and width with `ps -o NAME -p 1` in the container if in doubt.

**Sorting.** `--sort=SPEC` / `k SPEC`: `[+|-]key[,...]`, keys from the
field names (`pid ppid comm ucmd cmd args user uid stime start_time
start lstart etime etimes time ...`; constant fields compare equal); `-`
descends; ties keep pid order; default pid order. Unknown key -> error
`unknown sort specifier`; `--sort` with nothing -> `long sort
specification must follow --sort`.

**Headers.** `--no-headers`, `--no-heading`: none. `--headers` (repeat per
page) -> not treated.

**Width.** Output is never cut (procps cuts only for a terminal): `-w`,
`w`, `--cols`, `--columns`, `--width N`, `--rows`, `--lines` are accepted
and have nothing to do (treated, documented).

**Not treated** (reported with `context.NotTreated(spelling)` -- the
spelling as given, `-H`, `f`, `--forest` -- then ignored): `-H f --forest
-L -T -m m H -M Z --context -c c e n S --cumulative s v X l j L -y -D
--signames -g -G -s --sid -t t --tty --Group --group --headers`.

`Options()` lists every Unix and long option of `ps --help all` (above
`kBuiltinNotTreated` for the untreated), for `--help`'s lines and the
`Not treated arguments:` line; the help notes list the BSD letters
(treated: `a u x r T o O p q U k w V`; not treated: the rest) and the
exceptions in two or three lines.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Register `CreatePsCommand()`; add `BuiltinProcessList.cpp` and
`commands/ps/Ps.cpp` to the `BuiltinCommands` library.

## Tests

- `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`:
  extend `ABuiltinProcessLooksLikeAnyOther`: `Args()` equals the arguments
  passed; `StartTime()` lies between `CurrentFileDateTime()` taken before
  and after `StartProcess`. Add `ps` to `ListsEveryBuiltinSortedWithAVersion`.
- `tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp` (or
  `HaisosOSTest.cpp`, wherever a `.lua` process is started): a Lua process
  reports its `Args()` and a `StartTime()` in range. If an existing test
  starts an `.md` process (with its LLM mock), assert the same for it.
- New `tests/unit/components/BuiltinCommands.unittests/PsTest.cpp` (in that
  directory's `CMakeLists.txt`), on `RunCaptured`. A long-running helper
  process: the fixture's own `WaitCommand`-style builtin is in
  `BuiltinCommandsTest.cpp` only, so start `/bin/hsh -c 'read x'` with an
  input that never ends -- or, simpler, `sleep` if coreutils--names-env's
  `sleep` is on develop (it is earlier in the playbook): `os->StartProcess(
  ..., "/bin/sleep", {"100"}, ...)`, stopped with `TriggerStop()` at the
  end of the test.
  - `PsDefaultListsTheOsProcesses`: with `sleep 100` running, `ps` -> out
    `    PID TTY          TIME CMD\n` then one line per process in pid
    order: `sleep`'s `"%7llu ?        00:00:00 sleep"` and ps's own line
    ending `ps`; status 0.
  - `PsFullFormats`: `-f` -> the header above and sleep's line
    `haisos   ` + pid (7) + ` ` + OS id (7) + `  0 HH:MM ?        00:00:00
    /bin/sleep 100` (build HH:MM from `FormatDateTime("%H:%M", start, false)`
    of the process's `StartTime()`, unless the test straddles midnight);
    `-F`, `-l`, `-lf`, `-j`, `aux`, `ax` headers exactly as above.
  - `PsUserDefinedFormat`: `-o pid,ppid,comm,args -p <sleep pid>` ->
    `    PID    PPID COMMAND         COMMAND\n` + the row (comm padded to
    15, args last, unpadded); `-o pid= -o comm=` -> no header line;
    `-o pid,comm=NAME` -> header `    PID NAME`; `-o etime` gives a line matching
    `^ +[0-9]{2}:[0-9]{2}$` (`std::regex` is fine in tests: the fixture
    uses it);
    `-o bogus` -> the error block with `error: unknown user-defined format
    specifier "bogus"`, status 1.
  - `PsSelection`: `-p <sleep pid>` -> header + that line; `-p 999999` ->
    header only, status 1; `-C sleep` -> sleep's line; `--ppid <os id>` ->
    every process; `-N -C ps` -> every line but ps's own;
    `-u haisos` -> all; `-u root` -> `error: user name does not exist` +
    the block, 1; `-p abc` -> `error: process ID list syntax error`; `-p`
    -> `error: list of process IDs must follow -p`.
  - `PsSortAndHeaders`: two sleeps; `--sort=-pid -o pid --no-headers` ->
    the larger pid first; `k pid` ascending; `--sort=bogus` -> `error:
    unknown sort specifier`.
  - `PsOptionErrors`: `--bogus` -> exactly the error block with `error:
    unknown gnu long option` on stderr, nothing on stdout, status 1; `-K` ->
    `unsupported SysV option`; `Y` -> `unsupported option (BSD syntax)`.
  - `PsNotTreatedAndHelp`: `--forest` -> stderr `Parameter --forest is not
    treated by HaisosOS ps v. 1.0.0\n`, the default listing on stdout;
    `--help` starts `HaisosOS ps version 1.0.0 -`; `--version` -> `ps
    (HaisosOS builtin) 1.0.0\n`.
  - `PsLeavesFinishedProcessesOut`: a process that has finished (e.g.
    `/bin/pwd` waited for) is not listed.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Ps*:BuiltinCommandsTest.ABuiltinProcessLooksLikeAnyOther:BuiltinCommandsTest.ListsEveryBuiltinSortedWithAVersion'
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/HaisosOS/CLAUDE.md`: the `IProcess` bullet mentions
  `Args()` and `StartTime()` (kept by every process class from its
  creation).
- `src/components/BuiltinCommands/CLAUDE.md`: `ps` in the opening list; a
  row (version 1.0.0; treated: the formats, selections, `-o`/`-O` fields,
  `--sort`, `--no-headers`; exceptions: TTY `?`, TIME/C/%CPU/%MEM/VSZ/RSS/SZ
  0, user `haisos` uid 0, every process its own group and session, STAT `R`
  for ps and `S` otherwise, finished processes not listed, the parent of
  every process is the OS (its id, not a listed process), CMD starts with
  the program's path, the default selection is every process, output never
  cut); a short paragraph on `BuiltinProcessList.h` (the snapshot through
  `ICurrentProcess::OS()`).
- Root `CLAUDE.md`: a `ps` row in the Builtin Commands table and `ps` in
  the builtin lists; in the interfaces line nothing changes.

## Acceptance

- [ ] `IProcess::Args()` and `StartTime()` exist with those exact signatures and are implemented by every process class and test fake.
- [ ] `ps` reaches processes only through `context.Process().OS()`; it never stops one.
- [ ] Default, `-f`, `-F`, `-l`, `-lf`, `-j`, `u`, `ax` headers match procps-ng 4.0.4 byte for byte; `-o` field names, headers and widths per the table; last column unpadded.
- [ ] Selections union, `-N`, exit 1 when nothing is listed; procps's error block and exit 1 for every usage error.
- [ ] Every exception in the help notes and the CLAUDE.md row.
- [ ] Registered (the `--init` template follows); CMakeLists; tests green on Linux.

## Out of scope

- `pgrep`, `pkill`, `kill`: tools--pgrep-kill.
- Process trees (`--forest`), threads, terminals, CPU and memory accounting,
  process groups and sessions, making hsh the parent of its children.
- Changing `os_list_processes`.
