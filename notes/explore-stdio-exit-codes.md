# Exploration: Standard streams and exit codes for Haisos processes

## Seed

> Add stdin, stdout and stderr, and a program exit code, to Haisos processes. In the end, the API interfaces should support writing a shell that handles the ordinary shell things: pipes (`|`), redirections (`<`, `>`, `>>`, `2>`, `2>&1`), command lists (`&&`, `||`, `;`), heredocs and command substitution (`$(...)`).

## Base

- Branch: `task/fs_special_files_and_builtin_commands`
- Commit: `1920521` (Add DEV filesystem with /dev/null and /dev/zero, and . and .. listings)
- Related notes: none (`notes/` holds only `.gitkeep`)

## The issue

Haisos processes -- builtin commands, Lua scripts and LLM agents -- have no
standard streams and no exit code. Everything they print goes a line at a time
to a tagged console, only interactive agents read input (from that same
console), and whether a program succeeded is known only to a builtin's unit
tests. The seed asks for stdin, stdout, stderr and an exit code on every
process, shaped so that a shell could be written on the interfaces: pipes, the
five redirections, `&&`/`||`/`;`, heredocs and `$(...)`. This exploration looks
at where the streams live in the API, what stands behind them, how a child is
handed them, what each runtime does with them (builtins that must stay
GNU-exact, sandboxed Lua, agents), what an exit code means per runtime, and
which surfaces follow (agent tools, Lua names, the console, the haisosfile,
haisos's own exit code). Done means: every process has descriptors 0/1/2 of its
own and an exit code readable from outside; the interfaces offer pipes,
descriptor duplication, a terminal check and stream passing at start; builtins
behave in pipelines as GNU's do; and tests build each of the seed's shell
features from the interfaces alone -- plus whatever the open aspects add.

## What exists today

- **Processes.** `IHaisosOS::StartProcess(environment, programPath, args, workingDirectory, options)`
  runs a builtin (any path the root filesystem says is one), a `.md` agent or a
  `.lua` script, each on a thread of its own (`AgentProcess` and `LuaProcess`
  in the HaisosOS component, `BuiltinProcess` in BuiltinCommands). Environment
  and working directory are passed, never inherited; the OS is not told who
  calls it and is every process's parent (its own pid -- the comments in
  `IProcess.h` and os_start_process's CLAUDE.md still say 0).
  `StartProcessOptions` has one field, `interactiveAgent`. The OS caps
  concurrent processes and, when destroyed, drains them with `TriggerStop` and
  a bounded wait; each process's destructor then waits without bound.
- **No exit code.** `IProcess` gives pid, parent pid, path, agent name, an
  environment clone, `TriggerStop` and `WaitToFinish(timeoutMs)` (deliberately
  no untimed wait). `IBuiltinCommand::Run` already returns the GNU exit status
  (`ls` 2, the others 1 on failure; 1 when stopped mid-way), but
  `BuiltinProcess` exposes it only to tests ("`IProcess` has no exit status
  yet"). Lua scripts and agents have no notion of one. `haisos` returns 0
  whatever its RUN processes did (1 only for errors of its own: parsing, setup,
  OUTCOPY, nothing started), which is why the four agent haisos tests grep the
  output for `Error:`.
- **Descriptors are the filesystem's, not the process's.** `IFileIO`
  (`ICurrentProcess::IO()`) is POSIX-shaped -- `OpenFile`/`ReadFile`/
  `WriteFile`/`CloseFile` on `int` descriptors -- but passes each descriptor
  straight to the OS's root filesystem (currently in `ProcessFileIO`). Every
  filesystem numbers its own: in-memory and device ones from 3, a disk-backed
  one with the host's real descriptors, mounted ones re-issued from a
  program-wide synthetic range. So descriptors are shared by all processes of an
  OS, and on a `PhysicalFileSystem` root 0, 1 and 2 are the haisos host's own
  stdin, stdout and stderr (`ReadFile(0)` reads the host's stdin, `CloseFile(0)`
  closes it). Only builtins, which are C++, handle raw descriptors today -- tools
  and Lua work by path -- so this is latent, but 0/1/2 cannot be reserved on top
  of it. There is no pipe, dup/dup2, isatty or seek, on `IFileIO` or on
  `IFileSystem`.
- **Output is a line-oriented, tagged console, with no stderr.** Every runtime
  writes lines to an `IAgentConsole`; `AgentConsoleAdapter` prefixes
  `[<name>_<pid>] ` and calls `IPhysicalConsole::Write`, whose implementation
  queues the message and prints it plus `'\n'` on the host's stdout (an empty
  message is skipped). Agents print the content of every assistant message,
  including text next to tool calls, and "Error: Unknown tool" lines; an LLM,
  HTTP or parse failure comes back as reply content `Error: ...` (with
  `LLMResponse::done_reason` set) and is printed like a reply. Lua's `print`
  writes a line; a load or runtime error is written as `[<path>] Error: ...`,
  and since the chunk is named by the bare path, Lua itself shows it as
  `[string "<path>"]:<line>:`. Builtins go through `BuiltinContext` (currently
  in `BuiltinCommand.h`): `Out` buffers into lines, `Error` writes
  `<name>: <message>`, and the Try line and the "not treated" reports go to the
  same console; `BuiltinCommandHost` carries that console ("there is no stdout
  yet").
- **Only interactive agents read input.** `RUN -i` sets `interactiveAgent`; the
  process's input loop (currently `AgentInputLoop`) reads
  `IAgentConsole::ReadLine` -- the physical console, `std::getline` on the host's
  stdin, serialized and uninterruptible -- and posts each line. Other runtimes
  ignore the flag, and `os_start_process` never sets it: "the console's input
  belongs to whoever the haisosfile gave it to".
- **Builtins document the gap.** `cat` reports an error instead of reading stdin
  (no FILE, or `-`); `ls` always prints as GNU ls does to a terminal (columns,
  shell-escape quoting); `IBuiltinCommands` lists stdin, stdout and stderr among
  what Haisos lacks. The root CLAUDE.md requires GNU's output format, wording
  and exit statuses, every option accepted, and a version bump whenever a
  builtin's behaviour changes.
- **Lua sandbox.** Only base, table, string, math, utf8 and coroutine are
  opened; `io`, `os`, `package` and `debug` are absent and `load`/`dofile`/
  `loadfile`/`warn` removed, and LuaProcessTest pins `io` and `os` being nil. A
  stop aborts the script through a latched instruction-count hook. The stock
  `os.exit` (vendored `extern/lua`) calls C `exit()`, which would end the whole
  haisos host.
- **Agents sanitize what they are posted.** The agent's thread passes every
  posted command -- its program included -- through `SanitizeUserInput` (lossy:
  drops lines containing "system:" and the like, deletes everything between `<`
  and `>`, caps at 64 KB), although the OS's agent-starting code says the
  program is deliberately not sanitized. Tool results reach the model verbatim
  between `--- BEGIN TOOL RESULT ---`/`--- END TOOL RESULT ---`.
- **Tools.** `os_start_process` resolves the path against the caller's
  directory, gives the child a clone of the OS's environment and the caller's
  directory, and returns `{pid, path}` at once: no output, no exit code.
  `os_list_processes` reports pid, parent, path, agent name and whether
  finished; a finished process drops out of the OS's list at the next process
  start. The llm_cache_proxy keys its recordings by the SHA-256 of the request
  body, so any change to a tool's schema or description, or to an agent's
  system prompts, invalidates every recorded agent request.
- **The haisosfile.** `RUN [-i] /path args...` splits on whitespace: no quoting,
  no redirection (only `CREATE`/`APPEND` content is quote-aware). `FS <name> DEV`
  mounted at `/dev` gives `/dev/null` and `/dev/zero`, which open with any flags
  so `> /dev/null` works ("Shell commands redirect to /dev/null all the time").
- **Portability.** Processes are plain threads with mutexes and condition
  variables; nothing uses host pipes, so an in-memory pipe behaves the same on
  Linux, Windows and WASM.

## Open aspects

### A1. Is a shell, or more builtins, part of this work -- or only the API?
The seed wants interfaces a shell can be written on "in the end"; how much is
built on them now sets the size of the work and how the API is proven. Variant 3
is what A4's shell-command tool and A10's quoting would serve.
Combinable: yes

1. **API only** -- the interfaces and every runtime using stdio (the decided
   aspects), with unit tests that build pipes, redirections, lists, heredocs and
   `$(...)` through the interfaces as a shell would, from the existing builtins
   (`echo`, `cat` reading stdin, `ls` failing with 2). For: bounded; nothing is
   designed twice. Against: no shell to show for it; `&&`/`||` tests lean on a
   failing `ls` or `cat` for a non-zero code.
2. **`true` and `false`** -- the two trivial GNU commands. For: cheap, the
   natural operands of `&&`/`||`, and the `--init` template picks them up by
   itself. Against: of little use without a shell.
3. **A `sh` builtin** -- a POSIX sh subset: quoting, variables, pipes,
   redirections, lists, heredocs, `$(...)`, `-c`. For: proves the API end to
   end, and gives agents the syntax they know best. Against: a language
   implementation; as a builtin it must also accept every option of the real
   `sh` and have the one `--help` shape.
4. **Text filters** -- `head`, `tail`, `wc`, `grep`, `sort`, `tee`, `tr`. For:
   what makes pipelines worth having. Against: each follows the GNU rules (every
   option listed, untreated ones reported), which adds up.

### A2. What does an agent process write to its stdout?
It decides what `agent.md > out.txt` or `agent.md | next` carries. The rest goes
to stderr (D13), which by default shows on the console too (D4), so the console
looks as today in every variant.
Combinable: no

1. **Last reply only** -- the last text the agent produces for each command
   (each turn, for an interactive one) goes to stdout, earlier text to stderr,
   each held back until the next arrives so nothing is written twice. For: clean
   data in a pipeline, as `claude -p` prints only its final result; the answer
   still counts when it comes in the same message as a tool call such as
   `self_close`. Against: intermediate text reaches the console one LLM round
   late.
2. **Everything it says** -- every assistant text to stdout, as the console
   shows it today. For: simplest. Against: redirected output carries the "let me
   read the file first" chatter.
3. **Explicit writes only** -- stdout carries only what the agent writes on
   purpose through a tool (`os_write_file` on `/dev/stdout`, A9, or a new one);
   all it says goes to stderr. For: exact control over the data. Against:
   relies on the model calling a tool, and each program has to ask for it.

### A3. How does a non-interactive agent get its stdin?
An interactive agent takes stdin a line at a time (D5); a one-shot agent in a
pipeline (`cat doc | summarize.md`) needs the whole input -- untrusted data,
which must reach the model intact.
Combinable: no

1. **Read on demand** -- through a tool (`os_read_file` on `/dev/stdin` if A9
   provides it, or a new one), the agent being told in its system prompts that
   its stdin is connected (the OS knows whether it is a pipe, a file or the
   empty default). For: arrives verbatim as a tool result, delimited as data;
   nothing is read that is not wanted. Against: the model may not read it; the
   prompt and tool change every recorded LLM request.
2. **Given with the program** -- read to the end (capped, as files are at 10 MB)
   and appended to the program like `--- Arguments ---`. For: what `claude -p`
   does with piped input; nothing for the model to learn. Against: the agent
   cannot start until the producer ends; the program passes through the agent's
   lossy `SanitizeUserInput` (drops `<...>` and whole lines, caps at 64 KB), so
   stdin needs a verbatim path of its own; data would sit among the
   instructions.
3. **Not at all** -- only interactive agents read stdin. For: nothing to build.
   Against: an agent can never be a filter.

### A4. What do agents and Lua scripts get for running programs with stdio and exit codes?
Today `os_start_process` returns a pid and nothing more. Agents are the main
users, and Lua gets every `os_*` tool as a global.
Combinable: yes

1. **Capture in os_start_process** -- optional `stdin` text, `stdout`/`stderr`
   as `console` (default), `capture` or a file path (with append), and a `wait`
   with a timeout, returning the exit code and the captured text (capped). For:
   one tool covers "run it and tell me what happened". Against: no pipelines
   without a shell; the schema change re-records every agent request.
2. **Wait by pid** -- `os_wait_process(pid, timeout)` returning the exit code,
   the OS keeping a finished process's code until it is collected (a zombie, in
   Unix terms). For: start now, collect later, as `agent_start`/
   `agent_wait_to_finish` do. Against: a retention policy for codes nobody
   collects.
3. **Shell command tool** -- runs a command line through the `sh` builtin (A1)
   and returns its output and exit code. For: the syntax every model knows;
   pipes and redirections for free. Against: needs the shell.
4. **Descriptor tools** -- pipe, dup2, read, write and close on descriptors, and
   descriptors in `os_start_process`, so a Lua script could be a shell. For:
   full power. Against: unnatural for a model, easy to deadlock, bytes in JSON.
5. **Nothing yet** -- only the C++ interfaces. For: smallest. Against: agents
   gain nothing visible.

### A5. Can an agent choose its own exit code, and how?
Beyond the derived codes (D13), an agent may need to say "the task failed" so
that `check.md && deploy.lua` stops.
Combinable: no

1. **self_close exit code** -- `self_close` takes an optional code, 0 by
   default. For: the tool that already means "I am done". Against: `self_close`
   is every agent's, subagents' too, and is given only the `IAgent`, so the code
   must live on the agent (an `IAgent` change) for the process to read -- and
   `agent_wait_to_finish` would then want to report it.
2. **New os_exit tool** -- a process-level `os_*` tool, backed by an "exit with
   this code" on `ICurrentProcess`, also a Lua global. For: an exit code is a
   process's, not an agent's; one door for every runtime. Against: a second
   "end yourself" tool beside `self_close`, and Lua with two exits.
3. **Derived only** -- For: nothing new to learn. Against: an agent cannot fail
   on purpose.

### A6. Which stdio names do Lua scripts get?
`print` writes to stdout either way (D12); reading stdin, writing to stderr and
exiting with a code need names, and the sandbox tests pin `io` and `os` being
nil.
Combinable: no

1. **Standard io/os subset** -- `io.read`, `io.write`, `io.lines` (without a
   file name), `io.stdin`/`io.stdout`/`io.stderr` with `:read`/`:write`/
   `:lines`/`:close`, and `os.exit`, over the process's descriptors; no
   `io.open`, `io.popen`, `os.execute`, `os.remove`, `os.getenv`. For: the Lua
   everyone knows, models writing a `.lua` included -- the reason the builtins
   copy GNU. Against: `io` and `os` stop being nil, so the boundary is re-pinned
   function by function.
2. **Haisos globals** -- e.g. `read_stdin`, `write_stdout`, `write_stderr`,
   `exit` (or `os_*` tools), with `io` and `os` left nil. For: the sandbox tests
   stand as they are. Against: unfamiliar; scripts written for plain Lua fail.
3. **print and exit only** -- For: least work. Against: no Lua filters, no
   stderr.

### A7. How is a process's output shown on the physical console?
D15 keeps the lines; what stays open is the tag and where stderr ends up on the
host. Variant 3 goes with 1 or 2.
Combinable: yes

1. **Tagged, stderr to host stderr** -- every line keeps its `[<name>_<pid>] `;
   stdout lines go to the host's stdout, stderr lines to its stderr
   (`IPhysicalConsole` learns to write there). For: `haisos 2>/dev/null` hides
   diagnostics as it would a Unix program's. Against: the two host streams
   interleave independently; `--log-to-console` logs share the host's stderr;
   the agent haisos tests' `Error:` grep on stdout no longer sees LLM failures
   unless A8 turns them into an exit code.
2. **Tagged, all on host stdout** -- as today. For: no change. Against: data
   and diagnostics cannot be told apart on the host.
3. **Untagged raw mode** -- a CLI flag (or a single `RUN`) sends the `RUN`
   processes' bytes to the host's stdout and stderr unchanged, as `docker run`
   does. For: haisos as a filter (`haisos | grep`). Against: concurrent `RUN`s
   interleave unlabelled; the console drops empty messages today, which raw
   output must not.

### A8. What exit code does haisos itself return?
Today 0 whatever the RUN processes did.
Combinable: no

1. **First failing RUN** -- the code of the first RUN, in file order, that did
   not exit 0 (127 for one that could not start), else 0; its own errors
   (parsing, setup, OUTCOPY) stay 1. For: with one RUN, its code (as `docker
   run`); with several, any failure fails the run, which CI and the haisos tests
   can rely on. Against: among several failures, the one reported is chosen by
   file order, not time.
2. **Last RUN** -- the last RUN line's code, as a script's. For: the script
   analogy. Against: RUNs run at the same time, not in sequence; earlier
   failures vanish.
3. **Unchanged** -- For: no change. Against: a failed program still looks like
   success.

### A9. Do /dev/stdin, /dev/stdout and /dev/stderr exist?
Scripts and GNU tools use them (`cat /dev/stdin`, `echo x > /dev/stderr`); for
an agent they would be stdio through the file tools it already has (A2, A3).
Combinable: no

1. **Per process in IFileIO** -- a process's `IFileIO` resolves `/dev/stdin`,
   `/dev/stdout`, `/dev/stderr` (and `/dev/fd/N`) to a duplicate of that
   descriptor, whatever is mounted at `/dev`, and the DEV filesystem lists them.
   For: works as on Linux; `os_read_file`/`os_write_file` reach stdio unchanged.
   Against: paths answered above the filesystem (bash does the same for its
   redirections), and listed entries that open only from inside a process.
2. **Not now** -- For: nothing to build. Against: agents need other tools for
   stdio.
3. **Shell-only names** -- only a shell's redirections understand them, as bash
   emulates them. For: contained. Against: `cat /dev/stdin` still fails.

### A10. Does the haisosfile's RUN learn quoting or redirections?
`RUN` splits on whitespace, so `RUN /bin/sh -c 'ls | cat'` cannot even be
written. Variant 2 goes with 3 or 4.
Combinable: yes

1. **Unchanged** -- redirections come through a shell later. For: no DSL
   change. Against: a shell (A1) could not be handed a command line from a
   haisosfile.
2. **Quoted arguments** -- `'...'` and `"..."` in RUN, as `CREATE`/`APPEND`
   content has. For: small; `sh -c` and arguments with spaces become possible.
   Against: a DSL change to document and test.
3. **Redirections on RUN** -- `<`, `>`, `>>`, `2>`, `2>&1` parsed by the
   haisosfile parser, the files opened in the OS before the process starts.
   For: the common case without a shell. Against: a second place that parses
   redirections; Dockerfiles leave them to a shell.
4. **Docker's shell form** -- RUN lines run through a configurable shell
   (`SHELL`), with an exec form `RUN ["..."]` beside it. For: exactly Docker's
   model. Against: needs the shell, and changes what every RUN means.

## Decided aspects

### D1. Where a process's standard streams live
**Chosen:** a descriptor table per process, in its `IFileIO` -- 0, 1 and 2 are
stdin, stdout and stderr; `OpenFile` hands out the lowest free number, as POSIX
does; `ReadFile`/`WriteFile`/`CloseFile` go through the table; the table adds
the counterparts of `pipe()` (D3), `dup()`/`dup2()` and `isatty()` (D10); and a
per-process limit (as `RLIMIT_NOFILE`) is the backstop against a runaway, like
the OS's process cap. Why: `IFileIO` and `IFileSystem` are already `int`-
descriptor POSIX counterparts; the builtins copy GNU tools, which read 0 and
write 1; every redirection a shell makes is a `dup2` (`2>&1`, `n>&m`, a shell
builtin's own `> file` saved and restored), so one mechanism serves them all;
and it ends today's pass-through, in which a descriptor is the filesystem's and
0/1/2 can be the host's. Being on `IO()`, it stays behind `ICurrentProcess`,
the one door.
**Ignored:**
- Stream objects on `ICurrentProcess` (`StdIn()`/`StdOut()`/`StdErr()`) -- two
  I/O worlds side by side (descriptors for files, objects for streams), no
  `dup2` for a shell's own redirections, and the pass-through stays open.
- Reserving 0/1/2 inside today's pass-through -- files would stay
  filesystem-numbered and shared between processes, and `dup2(file, 1)` would
  have nothing to point at.
- A separate descriptor interface beside `IFileIO` -- `OpenFile` would hand out
  descriptors that something else reads; they belong together.
- `IAgentConsole`-like line objects as streams -- lines only: no bytes, no
  partial line (`echo -n`), no end of file.

### D2. What stands behind a descriptor
**Chosen:** a shared, reference-counted open file (POSIX's "open file
description"): several descriptors (`dup`, `2>&1`) and several processes (a
pipeline, a redirected child) hold the same one, and the file, pipe end or
console behind it is released when the last lets go. Its kinds: a filesystem
file (the filesystem and its own descriptor, as today), a pipe end (D3), a
console stream (D15) and an empty input. It is a public type declared beside
`IFileIO` (name and methods are the plan's), because `IHaisosOS::StartProcess`
must be handed a child's streams (D4) and is not told who is calling; `IFileIO`
hands out the open file behind one of the process's descriptors. A descriptor is
a capability: a child gets exactly the open files it is passed, even into a
narrower sub-OS, as with descriptor passing on Unix -- nothing else widens,
since whoever can pass a file could as well copy its bytes into a pipe.
**Ignored:**
- The filesystems' own numbers as the process's descriptors -- they collide
  between filesystems and cannot be shared or counted.
- Pipes as a hidden filesystem ("pipefs") -- an `IFileSystem` with no paths, and
  the counting is needed anyway.
- Checking a passed file against the child's root -- no protection gained (see
  above), and `cmd > file` into a sub-OS would break.

### D3. Pipes
**Chosen:** in-memory, bounded, blocking pipes, made by a `pipe()` counterpart
on `IFileIO` that gives two descriptors. A read waits for data and returns 0
once every write end is closed; a write waits for room and fails (EPIPE) once
every read end is closed. The capacity is Linux's 64 KiB unless chosen when the
pipe is made (Linux sets it with `F_SETPIPE_SZ`), which heredocs and captured
output use (D14).
**Ignored:**
- Host pipes (`pipe()`, `_pipe()`) -- none on WASM, and host descriptors would
  come into the sandbox.
- Pipes made by the OS as objects -- a process would hold streams outside its
  descriptor table, and `IO()` is where its I/O lives.
- Unbounded pipes -- no back-pressure: `yes | head -1` would fill memory.
- Named pipes only (`mkfifo`) -- they need a writable directory, cleanup and a
  new `IFileSystem` operation; they can come later.

### D4. How a child is given its streams
**Chosen:** explicitly, in `StartProcessOptions`: the open files that become the
child's stdin, stdout and stderr, which the starter takes from its own
descriptors (the same one twice is `2>&1`). An unset stream gets a default:
stdout and stderr the OS's console, tagged with the new process's name --
today's output, so `RUN` and `os_start_process` children print as now -- and
stdin an empty input, whose reads end at once like `/dev/null` (D5 says who gets
the console's input). Descriptors 3 and up are not passed: every feature of the
seed maps onto a child's 0/1/2, and a shell's other descriptors stay in its own
table; a map can come when a program needs more.
**Ignored:**
- Inheriting the caller's table, fork-style -- the OS does not know its caller,
  and nothing is inherited implicitly (environment and directory are passed
  too); passing explicitly also makes close-on-exec unnecessary.
- `posix_spawn`-style file-action lists -- more machinery for the same three
  results.
- A start method on `ICurrentProcess` -- a second way to start a process, while
  parentage is still to be redefined.
- A PIPE mode, where `StartProcess` makes the pipe and the process object hands
  out the parent's end (Python's `subprocess.PIPE`) -- puts a stream on the
  child's outside view, and pipelines need explicit pipes anyway.
- Unset meaning closed -- writes failing with EBADF by default would silence
  `RUN`.

### D5. Who reads the console
**Chosen:** only a process handed the console's input as its stdin. `RUN -i`
does so for any runtime (`RUN -i /bin/cat` echoes what is typed), and an
interactive agent's input loop reads lines from its stdin instead of from the
console, so a shell can also feed one from a pipe. `interactiveAgent` keeps
meaning "post each stdin line to the agent and keep it running";
`os_start_process` still never attaches the console. Docker's `-i` ("Keep STDIN
open even if not attached") is the same idea. `IAgentConsole::ReadLine` loses
its last reader.
**Ignored:**
- Every `RUN` process reading the console -- they would race for typed lines,
  and the console's input belongs to whoever the haisosfile gave it to.
- Leaving the input loop on the console -- two input paths, and no piping into
  an interactive agent.

### D6. When a process's descriptors close
**Chosen:** all at once, when its program ends (its runtime's thread
finishing), before it reports finished -- so a pipe's reader sees end of file
when the writer ends, and a waiter never sees a finished process whose output is
still open.
**Ignored:**
- On destruction -- the OS and a shell keep `IProcess` objects past the end, so
  `a | b` would never see end of file.

### D7. Blocking I/O and stopping
**Chosen:** a pipe read or write that is waiting can be interrupted:
`TriggerStop` on the process wakes it and the call fails (as with EINTR), so the
OS's drain and the destructors never hang on a pipe nobody serves. The console's
blocking line read stays the known exception (as for interactive agents today)
unless the console gets a reader thread -- the plan's call.
**Ignored:**
- Plain blocking waits -- a stuck pipeline would wedge shutdown.
- Polling with timeouts -- latency and needless wakeups.

### D8. The exit code
**Chosen:** on `IProcess`, the outside view that a shell or the OS holds: empty
until the process has finished, then 0-255 with the shell's conventions -- the
program's own code, taken modulo 256 as `exit()` does; 143 (128 + SIGTERM) when
it ended because it was asked to stop, `TriggerStop` being Haisos's SIGTERM; 141
on a broken pipe (D9). A `StartProcess` that returns null makes no process and
so no code (a shell reports 127 or 126 itself, D14). Per runtime: a builtin's
`Run` result; a Lua script's per D12 and A6; an agent's per D13 and A5.
`os_list_processes` shows it for finished processes still listed. Bash gives the
same numbers: `PIPESTATUS` 141 for `yes | head -1`, 143 after `kill -TERM`.
**Ignored:**
- Returned by `WaitToFinish` -- its contract is a bool with a timeout.
- A -1 sentinel while running -- a sentinel instead of "no code yet".
- A status struct or a "signaled" flag -- Haisos has no signals, and the 128+n
  numbers already say why, as a shell's `$?` does.
- 1 for a stopped process -- a stop could not be told from a failure.

### D9. Writing to a pipe nobody reads
**Chosen:** `IFileIO`'s write fails (EPIPE) -- the API behaves as POSIX does
with SIGPIPE ignored -- and each runtime then does what a Linux program with
SIGPIPE's default does: it stops quietly with exit code 141 (a builtin through
its stop flag, a Lua script through the kill hook's unwinding, an agent through
`TriggerStop`). C++ code that wants the error instead, like a shell whose
heredoc reader has gone, simply gets -1 and carries on.
**Ignored:**
- Printing "write error: Broken pipe" -- GNU tools never get that far;
  `yes | head -1` is silent on Linux.
- Carrying on -- the producer would never end.
- Stopping the process inside `IFileIO` -- nothing could opt out, not even a
  shell.

### D10. Terminal or not
**Chosen:** an `isatty()` counterpart on `IFileIO`. Console streams are
terminals whatever the host's own stdout is -- today's console output stays
exactly as it is, and tests do not depend on how haisos is run -- while pipes,
files and devices are not. `ls` then does what GNU ls does when stdout is not a
terminal: one name per line, names unquoted, control characters shown as they
are (host GNU ls 9.4: to a pipe `it's`, `plain`, `with space` on three lines;
to a pty `"it's"  plain  'with space'` in columns).
**Ignored:**
- Always a terminal -- `ls | cat` would print columns and quotes, which no GNU
  ls does.
- Following the host's `isatty` -- one haisosfile would print differently in a
  terminal and in CI.

### D11. Builtins
**Chosen:** `BuiltinContext`'s standard output goes to descriptor 1; its errors,
the Try line and the "not treated" reports to descriptor 2 -- GNU puts
diagnostics and usage hints on stderr, and a "not treated" line on stdout would
corrupt the very output a pipeline parses. `cat` reads stdin with no FILE or
with `-`, losing its documented exception. `BuiltinCommandHost` carries the
process's streams instead of a console; the test-only exit status gives way to
`IProcess`'s. Every builtin whose behaviour changes has its version bumped and
its `--help` notes and the table in the BuiltinCommands CLAUDE.md updated, as
the root CLAUDE.md requires.
**Ignored:**
- "Not treated" reports on stdout -- see above.

### D12. Lua basics
**Chosen:** whatever names A6 settles on, a script's stdio goes through its
process's descriptors (`ICurrentProcess::IO()`), so its reach does not change.
`print` writes its line and `'\n'` to stdout. A load or runtime error goes to
stderr as the standalone interpreter prints it -- `lua: <file>:<line>: <message>`
and a traceback, the chunk named `@<path>` so it reads that way -- and the exit
code is 1, as `lua.c` returns `EXIT_FAILURE`; a stopped script exits 143. The
stock `os.exit` is never opened: it calls C `exit()` and would end the whole
haisos host; exiting with a code is a sandboxed replacement that unwinds the
script as the kill hook does.
**Ignored:**
- Keeping `[<path>] Error:` console lines -- not stderr, and not what anyone who
  knows Lua expects.

### D13. Agents: diagnostics and derived exit code
**Chosen:** an agent's diagnostics -- LLM, HTTP and parse failures (today
printed as `Error: ...` replies) and unknown-tool messages -- go to its stderr,
whatever A2 decides about its replies. Unless it chooses a code itself (A5), its
exit code is 0 when it finished normally; 1 when its last command failed (an LLM
call reported an error, the round cap was reached, its thread threw); 143 when
stopped from outside. An interactive agent ending at end of input reports its
last command's outcome, as a shell does. `claude -p` behaves alike (non-zero on
failure). With D8 and A8, the agent haisos tests could check an exit code
instead of grepping for `Error:`.
**Ignored:**
- Always 0 -- `agent.md && next` could never stop on a failed LLM call.
- Diagnostics on stdout -- they would land in whatever the agent's output is
  redirected into.

### D14. The shell's features need nothing more
**Chosen:** each one is built from D1-D8: `<`, `>`, `>>`, `2>` -- the shell
opens the file in its own `IFileIO` and passes it (D4); `2>&1` -- one open file
as the child's stdout and stderr; `|` -- a pipe, write end to one child and read
end to the next, the shell closing its own copies; `&&`, `||`, `;` --
`WaitToFinish` and the exit code; `$(...)` -- a pipe read to end of file while
the child runs, then its exit code, trailing newlines stripped by the shell;
heredocs -- a pipe sized to the text (D3), filled and closed by the shell before
it starts the reader, as bash 5.1+ does for heredocs that fit a pipe. A failed
start is `$?` 127 or 126 by the shell's own `Stat` of the path (missing, or
there but refused), and `PATH` lookup is `Stat`/`IsBuiltinCommand` on each
directory, so `StartProcess` needs no error code. No poll/select, non-blocking
mode or seek is needed: none of the seed's features reads two streams at once.
One fix rides along: the in-memory filesystem puts an `O_APPEND` handle at the
end only when it is opened, not before every write as POSIX does, so
`a >> log` and `b >> log` would overwrite each other there.
**Ignored:**
- Heredocs through a temporary file (bash's fallback for large ones) -- needs a
  writable directory the OS may not have.
- Heredocs through a default-sized pipe written while the child runs -- a
  single-threaded shell deadlocks in `$(cmd <<EOF)` once both directions pass a
  pipe's capacity.
- A memory-file descriptor for heredocs (`memfd_create`, `fmemopen`) -- needs
  seek, or a second kind of object for what a sized pipe already does.
- poll/select -- only for reading two streams at once (a child's stdout and
  stderr together), which a capturing tool can avoid with sized pipes or two
  readers.
- An error code from `StartProcess` -- the shell's own `Stat` gives it both
  codes, and the log already says why.

### D15. Console streams
**Chosen:** a console stream turns bytes into lines: each complete line is one
tagged console write, a final unterminated line is written when the last
descriptor on it closes (`echo -n` still shows), and reading one yields a typed
line with its `'\n'`, then 0 at end of input -- today's output exactly, whatever
A7 decides about tags and the host's stderr.
**Ignored:**
- One console write per `WriteFile` -- a line written in pieces would come out
  as several tagged lines.

### D16. Left out
**Chosen:** not part of this work -- job control (a job table, `fg`/`bg`,
Ctrl-C, signals, process groups), parentage (a shell's children stay the OS's),
`os_start_process` handing on the caller's environment instead of the OS's,
named pipes and seek. The API still allows `&` (start without waiting) and
subshells (in the shell's own process with `dup`/`dup2`, or as a child shell).
**Ignored:**
- Doing any of them now -- none is needed by the seed's features, and each is a
  design of its own.

## Checked

- Root `CLAUDE.md`; the CLAUDE.md of HaisosOS, BuiltinCommands, Console,
  Filesystem, FileSystemService, Agent, Environment, Factory, LLMService,
  ToolFactory, libheaders; of os_start_process, os_list_processes, os_read_file,
  os_write_file, self_close, agent_start; of `tests/tools/llm_cache_proxy`.
- `interfaces/`: `IProcess.h`, `IHaisosOS.h`, `IFileIO.h`,
  `IFileSystemService.h`, `IBuiltinCommands.h`, `IPhysicalConsole.h`,
  `ILLMService.h`, `IEnvironment.h`, `IFactory.h`, `IServicesCreator.h`,
  `ILLMCommunicator.h`.
- HaisosOS component: the OS (start dispatch, drain, process cap), the agent
  process and its input loop, the Lua process (sandbox, kill hook, `print`,
  error reporting, chunk name), the process file I/O (descriptor pass-through),
  the OS tool factory; libheaders `CurrentProcessHandle.h`,
  `SanitizeUserInput.h`; `os_tools_common` and every `os_*` tool's source.
- BuiltinCommands: the context, parser and `BeginBuiltin`, the builtin process
  (exit status for tests), the registry, and `cat`, `echo`, `ls` (terminal
  format, quoting, exit statuses), `mkdir`, `pwd`.
- Console (queue, empty messages skipped, `getline`), the agent console adapter,
  the in-memory console; Filesystem: mount routing and synthetic descriptors,
  the device, in-memory (`O_APPEND` at open only) and physical filesystems and
  the POSIX/Windows backends (raw host descriptors); the Agent's round loop,
  console writes, sanitizing and stop; LLMCommunicator error paths; Logger
  (writes to the host's stderr).
- `src/haisos/main.cpp` (RUN start, wait, exit code), the haisosfile parser
  (RUN tokenizing, `-i`, the DEV template text), `CMakeLists.txt`.
- Tests: BuiltinCommands.unittests (tag-stripping capture console, exit status
  hook), HaisosOS.unittests (Lua sandbox pins, input loop, OS tests),
  `tests/haisos/*.haisostest` (console tags; the agent tests grep `Error:`
  because haisos exits 0).
- Git: the last 15 commits (af38643 builtins and stat, 1920521 DEV
  filesystem), the history of `notes/` (nothing but `.gitkeep`).
- `extern/lua`: `loslib.c` (`os.exit` calls `exit()`), `lua.c` (`lua: <msg>` and
  traceback on stderr, `EXIT_FAILURE`), `liolib.c` (standard files on `FILE*`),
  `lobject.c` (chunk names: `@path` vs `[string "..."]`).
- External: GNU coreutils manual (ls `-C` the default only when stdout is a
  terminal, one per line otherwise; width from the terminal, `COLUMNS`, else 80);
  man7 ls(1) (`--color=auto`, control characters as-is unless a terminal); the
  host's GNU ls 9.4 to a pipe and to a pty; bash on the host (`PIPESTATUS` 141
  for `yes | head -1`, 143 after `kill -TERM`, 127 for a missing command,
  `$(...)` stripping trailing newlines, `$?` 1 after `x=$(false)`, stdout and
  stderr order of `ls`/`cat` under `2>&1 | cat`); bash 5.1's NEWS as quoted on
  bug-bash (heredocs through a pipe when smaller than the pipe buffer, else a
  temporary file); Linux pipe(7)/fcntl(2) (64 KiB default capacity,
  `F_SETPIPE_SZ`); Docker `run -i` ("Keep STDIN open even if not attached");
  Claude Code headless docs (`claude -p` takes piped stdin as context and prints
  only the final result).
