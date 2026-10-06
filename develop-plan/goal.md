# Develop: Standard streams, pipes, exit codes and the hsh shell

## Metadata
- Created: 2026-10-05T07:24:52Z
- Base: master @ f582be0

## Settings
- Task models: kimi-k3:cloud
- Attempts per model: 2
- CI fix rounds: 3
- Task timeout minutes: 120
- Review model: opus
- Helper model: sonnet

## Goal
Every Haisos process -- builtin, Lua script or agent -- has a stdin, a stdout
and a stderr of its own, and an exit code readable from outside. A process
holds its open files as `shared_ptr<IFileDescriptor>` in a descriptor table of
its own (0, 1, 2 the standard streams), can `Dup`/`Dup2` them, make unnamed
pipes (from an `IPipeService`), and hand any of them to a child as its
standard streams. On top of that comes **`hsh`**, the Haisos SHell: a builtin
reimplementing POSIX `sh` (modelled on dash, Linux's `/bin/sh`) with quoting
and escaping, parameters, pipelines, every usual redirection, heredocs,
command lists, `$(...)`, arithmetic, globbing, control flow and functions --
good enough to be the program a haisosfile `RUN`s, interactively (`RUN -i
/bin/hsh`) or with a script or `-c`. A new `wc` builtin, `cat` reading stdin and
`ls` printing one name per line into a pipe make pipelines worth having, and
a new `man` builtin shows a manual page for every builtin -- for `hsh` a full
one, a short guide to its language. The
console is a plain terminal: output reaches the host raw and untagged, stderr
on the host's stderr, and `haisos` exits with the code of its first failing
`RUN`.

Also in this develop, already on `develop` (b5b6b0a, task
`links--follow-anywhere`, marked obsolete): symbolic links (and Windows
junctions) inside a physical filesystem are followed wherever they lead, as
the host follows them; Haisos keeps no link bookkeeping, and `DELETE` treats a
link like what it points at. See the root `CLAUDE.md` (Security).

## Clarifications
- Q: Where does this work go? -- A: Folded into develop 0.4 (the links work, done directly on `develop`, rides along).
- Q: How far do `shared_ptr<IFileDescriptor>` objects replace `int` descriptors? -- A: Everywhere: `IFileIO` **and** `IFileSystem`. Every filesystem's `OpenFile` returns its own descriptor object; `ReadFile`/`WriteFile`/`CloseFile(int)` disappear from both interfaces; a mount passes the inner filesystem's descriptor up untouched, so `MountPoints`' synthetic-descriptor translation and `AllocateSyntheticFd` go. A descriptor is closed when its last `shared_ptr` is released.
- Q: The process's table? -- A: Numbered slots (0 stdin, 1 stdout, 2 stderr, then 3...) holding `shared_ptr<IFileDescriptor>`, on the process's `IFileIO`. `OpenFile` returns the descriptor without placing it; placing, `Dup` (lowest free slot), `Dup2` (a chosen slot, replacing atomically) and closing a slot are table operations. A per-process limit of 1024 slots (as `RLIMIT_NOFILE`).
- Q: How do pipes move data? -- A: A direct bounded buffer per pipe (as Linux's `pipe(7)`): `Write` copies in and blocks while the pipe is full, `Read` copies out and blocks while it is empty; no transfer threads. `IPipeService` (a service, from `IServicesCreator`) creates and tracks them; every pipe has the same capacity, `kDefaultPipeCapacity` = 64 KiB, unless a capacity is asked for at creation (heredocs). Read returns 0 once every write end is released; Write fails with broken pipe once every read end is released.
- Q: Does a process still get `IPhysicalConsole`? -- A: No (decided after evaluation). The console stays at the OS's edge: the OS turns it into console descriptors (output, error, input), which become a process's 0/1/2 by default. Nothing inside a process -- runtime, builtin, tool -- holds the console or an `IAgentConsole` onto it; `BuiltinCommandHost::console` goes, and an agent's or Lua script's `IAgentConsole` becomes an adapter writing to its process's descriptors 1 and 2. `CreateHaisosOS`/`CreateSubOS` keep taking the console.
- Q: How does console output look? -- A: Always raw: a console descriptor is a plain terminal for every process -- the bytes written reach the host as they are, untagged and unbuffered (a prompt `$ ` shows at once). The `[name_pid] ` tags go.
- Q: Where does stderr go on the host? -- A: To the host's stderr: `IPhysicalConsole` gains `WriteError` beside `Write` (both write exactly the bytes given, adding no newline), and the console's error descriptor uses it.
- Q: How far do Lua scripts and agents go? -- A: Basics. Lua: `print` writes to stdout; a load or runtime error goes to stderr as `lua: <path>:<line>: <message>` and the exit code is 1; a global `exit([code])` ends the script with that code (0 by default) by unwinding as the stop hook does; `io` and `os` stay nil. Agents: what they say goes to stdout (as the console shows it today), their diagnostics (LLM/HTTP/parse errors, unknown tools) to stderr; exit code 0, or 1 when the last command failed. An interactive process reads its input lines from its stdin, so `RUN -i` works for every runtime and `echo hi | /chat.md` style feeding is possible. `os_start_process`, every tool schema and every system prompt stay as they are, so no LLM recording is invalidated.
- Q: Exit codes? -- A: `IProcess::ExitCode()` is empty until the process has finished, then 0-255: the program's own code modulo 256; 143 when it ended because it was asked to stop (`TriggerStop`, Haisos's SIGTERM); 141 when it was stopped by a broken pipe (a write to a pipe nobody reads makes a builtin, a Lua script or an agent stop quietly with 141, as SIGPIPE does). `haisos` exits with the code of the first `RUN`, in file order, that did not exit 0 (127 if it could not start), else 0; its own errors (parse, setup, OUTCOPY) stay 1.
- Q: Which `sh` does hsh copy? -- A: dash (Debian/Ubuntu's `/bin/sh`), decided: its options, its builtins, its error message shapes with `hsh` as the name (`hsh: 1: nope: not found`), exit codes 126/127 for a command found but not runnable / not found. Its `--help` references `https://man7.org/linux/man-pages/man1/dash.1.html`, a documented exception to the man-page rule. No line editing, history or completion (a later develop).
- Q: How much of the language? -- A: Commands and control flow: quoting and escaping (`'...'`, `"..."`, `\`), parameters and their expansions (`$VAR`, `${VAR}`, `${VAR:-w}` and the other POSIX forms, `$?`, `$$`, `$!`, `$#`, `$@`, `$*`, `$0`-`$9`), field splitting, globbing (`*`, `?`, `[...]`), `$(...)` and backquotes, arithmetic `$((...))`, pipelines with `!`, every POSIX redirection (`<`, `>`, `>|`, `>>`, `<>`, `n>&m`, `n<&m`, `n>&-`, `<<`, `<<-`) plus `&>` and `<<<` (bash's, cheap and common), lists (`;`, `&&`, `||`, `&`), `{ ...; }` and `( ... )`, `if`/`while`/`until`/`for`/`case`, functions; the shell's own builtins (`:`, `.`, `break`, `continue`, `cd`, `eval`, `exec`, `exit`, `export`, `false`, `read`, `readonly`, `return`, `set`, `shift`, `test`/`[`, `true`, `unset`, `wait`); everything else is found in `PATH` (unset: dash's default) and started through `IHaisosOS::StartProcess` -- builtins, `.md` agents and `.lua` scripts alike.
- Q: Builtin layout? -- A: Each builtin moves into a directory of its own under `src/components/BuiltinCommands/commands/<name>/`; `hsh` spans several files there.
- Q: `wc`? -- A: GNU `wc` byte for byte: `-c -m -l -L -w`, `--files0-from=F`, `--total=WHEN`, the column width rules, stdin with no FILE or `-`, `wc: x: No such file or directory`. `-m` counts UTF-8 characters (Haisos is UTF-8 throughout).
- Q: `man`? -- A: A new builtin, after man-db's `man`: `man <name>` prints the page of the builtin of that name; every option of the real `man` is accepted, the rest reported as not treated. Pages are compiled in, one per builtin, kept beside the builtin (`IBuiltinCommand::ManPage()`): for every builtin but `hsh` it is exactly its `--help` text; `hsh`'s is a complete page -- invocation and options, then a short section each on quoting, parameters and expansions, pipelines, redirections (every form), heredocs, lists, `if`/`while`/`until`/`for`/`case`, functions, the shell builtins, exit codes, with a one-line example each. Output is plain text (no pager, no formatting codes: a pager needs a terminal mode Haisos does not have yet); an unknown page prints `No manual entry for <name>` on stderr and exits 16, as man-db does. `man` reads the pages from the builtin registry it is compiled with (program data, not something reached outside the process), so it needs no OS access.
- Q: Unicode data for `wc -m`/`-L`? -- A: Compact hand-written tables (exact for Latin, Cyrillic, Greek, CJK, emoji; approximate widths for rarer scripts, a documented exception), in a library of their own: the `Unicode` component (`src/components/Unicode/`), linked by BuiltinCommands.
- Q: A background command that needs the shell itself (`{ a; b; } &`, `f &`, a builtin)? -- A: Started as a child `hsh -c` running its source text (with the shell's options and function definitions prepended, positional parameters passed); `$!` is its pid. Unexported variables do not reach it (a documented exception). The parser keeps `ListItem::sourceText` and `FunctionDefinition::sourceText` for it.
- Q: Smaller defaults taken while planning (decided, not asked) -- A: `fd--descriptor-objects` stays one task of ~1100 lines (an `IFileSystem` change cannot be split green without a throw-away API); an in-memory file removed while open makes its descriptors fail, as today; `haisos` returns 127 when no `RUN` could start, a `RUN`'s non-zero code wins over an `OUTCOPY` failure, a `RUN` still running after the 24 h wait counts as 143; an agent reaching the LLM round cap fails its command (exit 1, a line on stderr); `ls` on a terminal follows GNU's shell-escape quoting exactly (control characters as `'a'$'\001''b'`, `]` and `{` unquoted); `man -k`/`-f` are treated over builtin names; hsh is interactive when stdin and stderr are terminals (as dash), prints PS1/PS2/PS4 unexpanded, and accepts `-v` without acting on it; redirection descriptor numbers are single digits, as dash.
- Q: `ls` into a pipe? -- A: As GNU `ls` when stdout is not a terminal: one name per line, no quoting. Console descriptors are terminals whatever the host's own stdout is, so console output stays as today; pipes, files and devices are not.

## Contracts
The names every task uses; the interfaces in `interfaces/` are the truth once
written, and a task plan that disagrees is fixed, not the interface.

- `interfaces/IFileDescriptor.h`: `class IFileDescriptor` -- `ssize_t Read(void* buf, size_t count)`, `ssize_t Write(const void* buf, size_t count)`, `bool IsTerminal() const`. Read returns the bytes read, 0 at end of file; both return a negative `IOResult` on failure: `kIOError` (-1), `kIOBrokenPipe` (-2, a write to a pipe with no reader), `kIOInterrupted` (-3, the calling process was asked to stop while blocked). A pipe, console input or console output may block; nothing else does. Released by its last holder = closed.
- `IFileSystem::OpenFile(path, flags[, mode])` and `IFileIO::OpenFile(...)` return `std::shared_ptr<IFileDescriptor>` (null on failure). `ReadFile`, `WriteFile`, `CloseFile` are removed from both.
- `IFileIO` descriptor table: `GetDescriptor(int fd) -> shared_ptr<IFileDescriptor>` (null if none), `AddDescriptor(shared_ptr) -> int` (lowest free slot, -1 when full), `Dup(int fd) -> int`, `Dup2(int oldFd, int newFd) -> int` (returns newFd; replacing what was there), `CloseDescriptor(int fd) -> int` (0 / -1), `CreatePipe(size_t capacity = 0) -> std::optional<std::pair<int,int>>` (read slot, write slot; 0 = the default capacity); constants `kStdIn` 0, `kStdOut` 1, `kStdErr` 2, `kMaxDescriptors` 1024, as static members (`IFileIO::kStdOut`). A process's descriptors are released by `ProcessFileIO::ReleaseAllDescriptors()` (an agent's through `Agent::SetFinishedHook`).
- `interfaces/IPipeService.h`: `struct PipeEnds { std::shared_ptr<IFileDescriptor> readEnd, writeEnd; }`, `class IPipeService { PipeEnds CreatePipe(size_t capacity = 0); size_t OpenPipeCount() const; }`, `kDefaultPipeCapacity` = 65536, `kPipeAtomicWriteSize` = 4096. `IServicesCreator::CreatePipeService()`; each `HaisosOS` creates one and exposes it as `IHaisosOS::GetPipeService()`.
- `StartProcessOptions` gains `std::shared_ptr<IFileDescriptor> stdIn, stdOut, stdErr` (null: the defaults -- console output / console error for stdout / stderr, and an empty input, whose reads return 0 at once, for stdin unless `interactive`). `interactiveAgent` is renamed `interactive`: stdin defaults to the console's input, and an agent is interactive.
- `IProcess::ExitCode() const -> std::optional<int>`; `src/components/libheaders/ExitCodes.h` holds `kExitCodeBrokenPipe` 141, `kExitCodeStopped` 143, `kExitCodeNotStarted` 127, `enum class ProcessEnd { Exited, Stopped, BrokenPipe }` and `ExitCodeFor()`; `ICurrentProcess::StopForBrokenPipe()` is how a runtime ends itself on `kIOBrokenPipe` (141 wins over 143).
- `IAgentConsole` gains `WriteError` and loses `ReadLine`; an agent's console is a `ProcessAgentConsole` writing to its process's slots 1 and 2.
- Blocking pipe calls are interrupted through `src/components/libheaders/StopToken.h` (`StopToken`, `StopCallback`, `StopTokenScope`): each runtime thread carries its process's token.
- `IPhysicalConsole::Write(bytes)` writes exactly the bytes (no newline added); new `WriteError(bytes)` writes to the host's stderr; `ReadLine` unchanged.
- A process's descriptors are all released when its program ends, before it reports finished, so a pipe's reader sees end of file when its writer's program ends.

## Acceptance scenarios
Each haisosfile starts with this preamble (left out below):
```
FS rootfs MEM
ROOT rootfs
ENV PATH=/bin
CREATE_DIR /bin
BUILTIN rootfs hsh /bin/hsh
BUILTIN rootfs cat /bin/cat
BUILTIN rootfs echo /bin/echo
BUILTIN rootfs ls /bin/ls
BUILTIN rootfs man /bin/man
BUILTIN rootfs wc /bin/wc
CREATE /abc.txt multiline END
one
two
three
END
```

1. **A pipeline and a redirection.**
   `RUN /bin/hsh -c 'cat /abc.txt | wc -l; ls /bin | wc -l > /n.txt; cat /n.txt'`
   prints `3` and `6`, each on its own line, untagged; `haisos` exits 0.
2. **stderr, `||` and exit codes.**
   `RUN /bin/hsh -c 'ls /nope 2>/err.txt || echo "failed: $?"; cat /err.txt; cat < /abc.txt >> /out.txt 2>&1 && wc -c /out.txt'`
   prints `failed: 2`, `ls: cannot access '/nope': No such file or directory`
   and `14 /out.txt`. A second haisosfile with `RUN /bin/ls /nope` writes the
   `ls:` line to the host's stderr (nothing on stdout) and `haisos` exits 2;
   with `RUN /bin/hsh -c 'exit 3'` it exits 3.
3. **A script with control flow, a heredoc and `$(...)`.**
   `CREATE /count.sh` holding
   ```
   n=0
   for f in a b c; do n=$((n + 1)); done
   lines=$(wc -l <<EOF
   x
   y
   EOF
   )
   if [ "$n" -eq 3 ] && [ "$lines" -eq 2 ]; then echo "ok $n $lines"; else echo "bad"; exit 1; fi
   case "$1" in hi*) echo "greeted";; *) echo "plain";; esac
   ```
   and `RUN /bin/hsh /count.sh hello`: prints `ok 3 2` and `greeted`; exit 0.
4. **An interactive shell.** `RUN -i /bin/hsh`: the host shows the prompt
   `$ ` before each line typed. `echo hi | wc -c` prints `3`; `cd /bin; ls |
   wc -l` prints `6`; `nosuch` prints `hsh: 3: nosuch: not found` on the
   host's stderr; `exit 4` ends the shell, and `haisos` exits 4.
5. **Lua through stdio.** `/p.lua` holding `print("to stdout")` then
   `error("boom")`, run as `RUN /bin/hsh -c '/p.lua 2>/e.txt | wc -l; cat /e.txt'`:
   prints `1` and `lua: /p.lua:2: boom`; `RUN /p.lua` on its own makes
   `haisos` exit 1.
6. **Manual pages.** `RUN /bin/man wc` prints exactly what `RUN /bin/wc
   --help` prints; `RUN /bin/hsh -c 'man hsh | wc -l'` prints a number well
   above the help's, and `RUN /bin/man hsh` shows sections on quoting,
   expansions, pipelines, redirections, heredocs, lists, `if`, `while`, `for`,
   `case` and functions; `RUN /bin/man nosuch` prints `No manual entry for
   nosuch` on the host's stderr and `haisos` exits 16.

## Out of scope
- Line editing, history, completion (a later develop); job control (`fg`, `bg`, `jobs`, Ctrl-C, signals, process groups, `trap`).
- `/dev/stdin`, `/dev/stdout`, `/dev/stderr`, `/dev/fd/N`; named pipes (`mkfifo`); seek; poll/select; non-blocking I/O.
- `os_start_process` passing stdio, capturing output or waiting for an exit code; a `self_close` exit code; Lua `io`/`os`; a non-interactive agent reading its stdin; any tool schema or system prompt change.
- Running a file by its `#!` line or a `.sh` extension (run scripts as `hsh script.sh`); `PATH` lookup outside hsh.
- Process parentage: children an hsh starts are still the OS's (parent = the OS's pid).
- More text filters (`head`, `tail`, `grep`, `sort`, `tee`, `tr`, `printf`) beyond `wc`; `alias`, `getopts`, `ulimit`, `umask`, `times`, `command`, `type`, `hash`, `local`, `printf` as hsh builtins.
