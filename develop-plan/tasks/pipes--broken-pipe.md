# Task pipes--broken-pipe: A write nobody reads ends the program with 141

- Rock: pipes
- Depends on: pipes--pipe-service
- Size: ~350 changed lines in ~14 files (code ~150, tests ~170, docs ~30)
- Plan checked against: develop @ faf4c9e
- PR title: Stop a program quietly with exit code 141 on a broken pipe

## Goal

A builtin, a Lua script or an agent whose write to its stdout or stderr
returns `kIOBrokenPipe` -- the reader of that pipe is gone -- stops quietly
with exit code 141, as a Linux program killed by SIGPIPE does: `yes`-style
producers end when their reader goes away, nothing is printed about it, and
`IProcess::ExitCode()` is 141 even if the process is also asked to stop
afterwards (141 wins over 143).

- `ICurrentProcess::StopForBrokenPipe()` is the one entry point: the
  process's own runtime calls it when a write made **for its program**
  returned `kIOBrokenPipe`. It records `ProcessEnd::BrokenPipe` at that
  runtime's `ProcessEnd` decision point and stops the program the way
  `TriggerStop` would.
- The API itself stops nobody: `IFileDescriptor::Write` and `IFileIO` just
  return `kIOBrokenPipe`. C++ code writing to a descriptor directly (a shell
  writing a heredoc, later) gets the error and carries on; only the runtimes'
  own output paths -- `BuiltinContext`, Lua's `print` and error line,
  `ProcessAgentConsole` -- turn it into a stop (goal.md, D9).

## Context

Read first: the root `CLAUDE.md` ("Security: `ICurrentProcess` is the only door
out of a process"; "Creating things", especially "Nothing that waits for a
runtime thread is destroyed on one"), `develop-plan/goal.md` (the exit-code
clarification), `src/components/HaisosOS/CLAUDE.md`,
`src/components/BuiltinCommands/CLAUDE.md`, `src/components/Agent/CLAUDE.md`.

### What earlier tasks provide (as if already on `develop`)

- fd--descriptor-objects: `IFileDescriptor` and `kIOError`, `kIOBrokenPipe`,
  `kIOInterrupted` (`interfaces/IFileDescriptor.h`).
- fd--process-table: the `IFileIO` table (`IFileIO::kStdOut`, `kStdErr`,
  `GetDescriptor`, ...); `ProcessFileIO::ReleaseAllDescriptors()` called when
  a program ends, before the process reports finished (for an agent through
  `Agent::SetFinishedHook`).
- streams--console-and-start: `StartProcessOptions { stdIn, stdOut, stdErr,
  interactive }`; `BuiltinContext`'s private
  `bool WriteAll(IFileDescriptor* descriptor, const std::string& bytes)` -- the
  one place a builtin's bytes reach a descriptor (loops over partial writes;
  null or negative result stops and returns false; after stdout has failed
  once, later stdout output is dropped), with a comment marking it as the
  broken-pipe seam; stdout block-buffered (4096) when not a terminal, flushed
  by `~BuiltinContext`, which `BuiltinProcess::RunThread` destroys before
  `ReleaseAllDescriptors()`; the `FakeProcess : ICurrentProcess` of the
  `BuiltinContextTest` cases; `tests/mocks/MockFileDescriptor.h`
  (`SetWriteResult(ssize_t)`, `Written()`, `WriteCalls()`).
- streams--runtime-streams: `ProcessAgentConsole` (`src/components/HaisosOS/ProcessAgentConsole.{h,cpp}`),
  the agent's `IAgentConsole`: `Write` -> `message + "\n"` to `kStdOut`,
  `WriteError` -> `message + "\n"` to `kStdErr`, through its
  `CurrentProcessHandle` (`m_process->Get()`, then `IO()->GetDescriptor(fd)`),
  looping over partial writes, with a seam comment for this task; the agent's
  LLM/HTTP/parse failures go to `WriteError`.
  `LuaProcess::WriteToDescriptor(int fd, const std::string& bytes)` (void;
  whole buffer, partial writes looped, a negative result stops), used by
  `LuaPrintTrampoline` (stdout) and for the error line (stderr).
- streams--exit-codes: `src/components/libheaders/ExitCodes.h` --
  `kExitCodeBrokenPipe` (141), `kExitCodeStopped` (143), `kExitCodeNotStarted`
  (127), `enum class ProcessEnd { Exited, Stopped, BrokenPipe }`,
  `int ExitCodeFor(ProcessEnd end, int programCode)`; `IProcess::ExitCode()`.
  The `ProcessEnd` decision points:
  - `BuiltinProcess::RunThread`, after `Run` returns and the descriptors are
    released: `m_exitCode = ExitCodeFor(m_stopRequested ? ProcessEnd::Stopped : ProcessEnd::Exited, status)`
    under `m_finishedMutex`;
  - the end of `LuaProcess::RunThread`: `exit` requested -> `Exited` with
    `m_exitCodeRequested`; else stopped and not finished on its own ->
    `Stopped`; else error -> 1; else 0. `KillHookTrampoline` raises when
    `IsKillRequested() || m_exitRequested`, re-arming itself on every
    instruction/call/return/line;
  - `AgentProcess::ExitCode()`, computed once after the agent finished and
    latched under a mutex: `m_stopRequested` (set by `AgentProcess::TriggerStop`
    only while not finished) -> `Stopped`; else `Exited` with
    `m_agent->LastCommandFailed() ? 1 : 0`.
- pipes--pipe-service: `IPipeService` (`IHaisosOS::GetPipeService()`),
  `IFileIO::CreatePipe`, the pipe rules, `StopToken`; the test file
  `tests/unit/components/HaisosOS.unittests/HaisosOSPipeTest.cpp` with fixture
  `HaisosOSPipeTest` (in-memory root, builtins in `/bin`, capturing console
  with stdout and stderr apart, unreachable LLM endpoint).

If the code on `develop` names any of these differently, the code wins: use
its names and keep this plan's behaviour.

## Changes

### `interfaces/IProcess.h`

On `ICurrentProcess` (no constant here: 141 is `kExitCodeBrokenPipe` in
`src/components/libheaders/ExitCodes.h`):

```cpp
// Called by this process's own runtime when a write its program made to one
// of its descriptors returned kIOBrokenPipe: the program stops as TriggerStop
// would, quietly -- nothing is printed -- and the process's exit code is 141
// (ProcessEnd::BrokenPipe), whatever else happens afterwards: a later
// TriggerStop does not make it 143. Idempotent. On the inside view because only
// a process decides this about itself, as a Linux program gets SIGPIPE for its
// own write. Code that would rather handle the error (a shell writing a
// heredoc) just gets kIOBrokenPipe from IFileDescriptor::Write and carries on:
// nothing in IFileIO or the pipe calls this.
virtual void StopForBrokenPipe() = 0;
```

Every implementation of `ICurrentProcess` must have it: the three process
classes below, and every test fake (`grep -rn "public ICurrentProcess" src tests`
-- at least the `FakeProcess` of the `BuiltinContextTest` cases, which records
the calls in a counter for the test below).

### `src/components/BuiltinCommands/BuiltinProcess.{h,cpp}`

- `std::atomic<bool> m_brokenPipe{false}`.
- `StopForBrokenPipe()`: set `m_brokenPipe`, then `TriggerStop()` (which sets
  `m_stopRequested` and signals the stop token, so a command checking
  `StopRequested()` finishes early).
- Decision point: `ExitCodeFor(m_brokenPipe ? ProcessEnd::BrokenPipe : m_stopRequested ? ProcessEnd::Stopped : ProcessEnd::Exited, status)`.
  The final flush in `~BuiltinContext` runs before it, so a broken pipe found
  only when the buffered output is written at the end still gives 141.

### `src/components/BuiltinCommands/BuiltinCommand.{h,cpp}` -- `BuiltinContext::WriteAll`

Extend the existing `WriteAll(IFileDescriptor* descriptor, const std::string& bytes)`
(no second overload): when a write returns `kIOBrokenPipe` -- on stdout or
stderr, as SIGPIPE does not care which -- call `m_process.StopForBrokenPipe()`
(once: keep a `bool m_brokenPipe` member) and from then on drop **all**
further output, stdout and stderr alike (the program is dying quietly, so a
diagnostic must not reach stderr either). Replace the seam comment with one
saying what happens. `kIOInterrupted` and other negative results keep
streams--console-and-start's behaviour (stop, return false). No builtin's
version or `--help` changes: what a builtin prints is unchanged.

### `src/components/HaisosOS/LuaProcess.{h,cpp}`

- `std::atomic<bool> m_brokenPipe{false}`.
- `StopForBrokenPipe()`: set `m_brokenPipe`, then `Kill()` (which sets
  `m_killed` and signals the stop token).
- `WriteToDescriptor`: return at once (writing nothing) once `m_brokenPipe` is
  set; when a write returns `kIOBrokenPipe`, call `StopForBrokenPipe()` and
  stop.
- `LuaPrintTrampoline`: after its `WriteToDescriptor` call, if `m_brokenPipe`
  is set, re-arm the hook to fire on the next instruction exactly as
  `KillHookTrampoline` re-arms itself --
  `lua_sethook(L, &KillHookTrampoline, LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1)`
  -- and return 0 normally. Never raise (`luaL_error`) from the trampoline: it
  has C++ locals, and a `longjmp` over them is undefined behaviour. The hook
  then raises before the next instruction and the latched unwinding runs as
  for a kill; `RunThread` sees a kill and writes no `lua:` line.
- Decision point: `m_brokenPipe` -> `ExitCodeFor(ProcessEnd::BrokenPipe, 0)`,
  checked **first**, before "exit requested", "stopped", "error". So the
  error line written to stderr into a broken pipe also ends as 141, as a
  standalone `lua` would by SIGPIPE.

### `src/components/HaisosOS/AgentProcess.{h,cpp}`

- `std::atomic<bool> m_brokenPipe{false}`.
- `StopForBrokenPipe()`: set `m_brokenPipe`, then `m_agent->TriggerStop()`
  (the agent's stop: closes its queue, refuses further tool calls, signals its
  stop token) -- not `AgentProcess::TriggerStop`, which is the stop from
  outside.
- Decision point in `ExitCode()`: `m_brokenPipe` -> `BrokenPipe`, checked
  before `m_stopRequested`. `StopForBrokenPipe` is called on the agent's own
  thread while it writes, so the flag is set before the agent finishes and
  before the code is latched.

### `src/components/HaisosOS/ProcessAgentConsole.{h,cpp}`

- `std::atomic<bool> m_brokenPipe{false}`.
- In its write (both `Write` -> `kStdOut` and `WriteError` -> `kStdErr`):
  return at once once `m_brokenPipe` is set; when a write returns
  `kIOBrokenPipe`, set it and call `StopForBrokenPipe()` on the
  `ICurrentProcess` it got from `m_process->Get()` for this write (already a
  temporary `shared_ptr`; the `DestroyOffRuntimeThreads` deleter of
  `AgentProcess` and the agent thread's `RuntimeThreadScope` keep releasing it
  there safe -- leave both as they are). Replace the seam comment.
- Stderr counts as well as stdout (SIGPIPE does not care which descriptor):
  an agent whose diagnostics go into a pipe nobody reads stops with 141.
- What the agent keeps in its history and message buffer is unchanged: the
  console only decides where bytes go. Nothing an agent sends to the LLM
  changes (LLM recordings stay valid).

### Rules that bite (restated)

- `ICurrentProcess` stays the only door: the runtimes call
  `StopForBrokenPipe` on the process they write for; nothing is handed an
  `IHaisosOS`.
- No new class; no destructor gains a wait; the existing
  `DestroyOffRuntimeThreads` deleters and `RuntimeThreadScope`s stay.
- 141 is spelled only in `ExitCodes.h` (`kExitCodeBrokenPipe`, via
  `ExitCodeFor(ProcessEnd::BrokenPipe, ...)`).

## Tests

The script's filter must be a substring of the executable's name
(case-insensitive) **and** of the gtest `Suite.Name` (case-sensitive).

### `tests/unit/components/HaisosOS.unittests/HaisosOSPipeTest.cpp`

Add to the `HaisosOSPipeTest` fixture:

- `ALuaScriptPrintingIntoAPipeWithNoReaderExits141` -- `/p.lua`:
  `print("a")` then `os_write_file({path = '/after.txt', content = 'x'})`;
  `stdOut` = a write end whose read end was released before the start; wait
  for the process to finish (`WaitToFinish`) before reading anything else:
  `ExitCode()` is 141; `/after.txt` does not exist on the root (the script
  stopped at once); the console captured nothing on stderr.
- `ALuaErrorLineIntoAPipeWithNoReaderExits141` -- `/e.lua`: `error("boom")`;
  `stdErr` = such a write end: wait for the process to finish, then
  `ExitCode()` 141 (not 1), nothing captured.
- `AnAgentWritingIntoAPipeWithNoReaderExits141` -- `/a.md` ("Say hello.");
  `stdErr` = such a write end, stdout default; the unreachable endpoint makes
  the agent write its `Error: HTTP request failed ...` diagnostic to stderr;
  wait for the process to finish, then: `ExitCode()` 141 and nothing on the
  captured stderr or stdout.
Run: `bash ./scripts/test_linux.sh L U HaisosOS`

### `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`

- `BuiltinCommandsTest.EchoIntoAPipeWithNoReaderExits141Quietly` -- a pipe
  from `os->GetPipeService()`; release the read end; start `/bin/echo hello`
  with `options.stdOut` = the write end; release the test's copy; wait;
  `ExitCode()` is 141 and the fixture's console got nothing on stdout or
  stderr.
- `BuiltinCommandsTest.ABrokenStderrAlsoExits141Quietly` -- `ls /nope` with
  `stdOut` and `stdErr` = the same write end of a pipe whose read end is
  released; wait for the process to finish, then: 141 (not 2), nothing
  captured.
- `BuiltinContextTest.ABrokenStdoutStopsTheProcessOnceAndDropsTheRest` --
  (with the existing `BuiltinContextTest` cases and their `FakeProcess`) a
  terminal `MockFileDescriptor` as stdout with `SetWriteResult(kIOBrokenPipe)`:
  `Out("a")`, `Out("b")`, `Error("e")`: the fake's `StopForBrokenPipe` count
  is 1, stdout's `WriteCalls()` is 1, stderr's mock received nothing.

Run: `bash ./scripts/test_linux.sh L U BuiltinCommands` (selects the
`BuiltinCommandsTest` cases) and, for the `BuiltinContextTest` case,
`./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinContextTest.*'`.

### Whole run

`bash ./scripts/build_linux_on_linux.sh`, then `bash ./scripts/test_linux.sh L U`,
then `bash ./scripts/test_linux.sh L "*"` (the haisos agent tests must pass on
the existing LLM recordings).

## Docs

- Root `CLAUDE.md`: in the "Exit codes" paragraph streams--exit-codes added,
  one sentence: 141 when a builtin, Lua script or agent wrote to a pipe nobody
  reads -- it stops quietly, as with SIGPIPE.
- `src/components/HaisosOS/CLAUDE.md`: `StopForBrokenPipe` on the three
  process classes, 141 before 143; Lua's `print` re-arming the kill hook;
  `ProcessAgentConsole` stopping the agent on a broken stdout or stderr.
- `src/components/BuiltinCommands/CLAUDE.md` ("Output"): a write the reader
  is gone for stops the builtin quietly with 141; everything after is dropped.

## Acceptance

- [ ] `ICurrentProcess::StopForBrokenPipe()` exists and is implemented by
      `BuiltinProcess`, `LuaProcess`, `AgentProcess` and every test fake.
- [ ] Each runtime records `ProcessEnd::BrokenPipe` at its existing decision
      point, checked before `Stopped` (and, for Lua, before `exit`/error).
- [ ] No `kExitCodeBrokenPipe` or `141` outside `ExitCodes.h` (tests aside).
- [ ] `BuiltinContext` has one `WriteAll` (the `IFileDescriptor*` one); after
      a broken pipe nothing more is written, stderr included.
- [ ] Lua never raises from `LuaPrintTrampoline`; a broken `print` stops the
      script before its next instruction, with no `lua:` line.
- [ ] `ProcessAgentConsole` stops the agent on `kIOBrokenPipe` from either
      descriptor; history, message buffer and LLM requests unchanged.
- [ ] Nothing in `IFileIO`, `ProcessFileIO` or the pipe calls
      `StopForBrokenPipe`.
- [ ] The new tests pass under the commands above; all unit and haisos tests
      pass.

## Out of scope

- The pipes themselves, `StopToken`, interruption: `pipes--pipe-service`.
- Printing "write error: Broken pipe" (GNU tools never get that far).
- A shell's handling of `kIOBrokenPipe` on its own writes (hsh--executor).
- A `self_close` exit code; `os_start_process` passing streams or reporting
  exit codes (goal.md, Out of scope).
