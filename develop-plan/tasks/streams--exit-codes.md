# Task streams--exit-codes: Exit codes for every process and for haisos

- Rock: streams
- Depends on: streams--runtime-streams
- Size: ~600 changed lines in ~20 files
- Plan checked against: develop @ 8fb8324
- PR title: Add IProcess::ExitCode and exit haisos with the first failing RUN's

## Goal

Every process has an exit code readable from outside, and `haisos` returns
one:

- `IProcess::ExitCode()` is empty while the process runs, then 0-255:
  - a **builtin**: its `Run` result taken modulo 256 (the low 8 bits, so -1 is
    255); 143 if it was asked to stop (`TriggerStop`) before it finished;
  - a **Lua script**: 0 when it runs to the end; `exit([code])` -- a new
    global -- ends it with that code (0 by default); a load or runtime error
    is printed to its stderr as `lua: <path>:<line>: <message>` (as the
    standalone `lua` prints it, without the traceback) and the code is 1; 143
    when it was stopped;
  - an **agent**: 0, or 1 when its last command failed (an LLM/HTTP/parse
    failure, the round cap reached, or the command threw); 143 when it was
    asked to stop from outside (`IProcess::TriggerStop`). An interactive agent
    ending at end of input, or closing itself with `self_close`, reports its
    last command's outcome, as a shell does.
- `haisos` exits with the code of the **first `RUN` in file order that did not
  exit 0** -- 127 for a `RUN` whose process could not be started -- else 0.
  Its own errors (command line, parse, setup, creating the OS) stay 1, and so
  does an `OUTCOPY` failure when every `RUN` exited 0. A `RUN` still running
  when haisos gives up waiting (the 24 h bound) counts as 143.
- `os_list_processes` shows each process's `exit_code` (null while running).
  Its description and schema are **not** changed.
- 141 (a broken pipe) is part of the vocabulary but nothing produces it yet:
  `pipes--pipe-service` does, through the seam below.

**No change may alter what an agent sends to the LLM** -- tool schemas, tool
descriptions (`os_list_processes`' description stays exactly as it is, though
its result gains a field), system prompts, message shapes. The LLM cache
recordings (`tests/tool/llm_cache_proxy_database/`) are keyed by request body;
no recorded test calls `os_list_processes`.

## Context

Read first: the root `CLAUDE.md` (Creating things -- "Nothing that waits for a
runtime thread is destroyed on one"; Builtin Commands: exit statuses),
`src/components/HaisosOS/CLAUDE.md`, `src/components/BuiltinCommands/CLAUDE.md`,
`src/components/Agent/CLAUDE.md`, `src/tools/os_list_processes/CLAUDE.md`,
`develop-plan/goal.md` (the exit-code clarification), and the plans
`develop-plan/tasks/streams--console-and-start.md` and
`develop-plan/tasks/streams--runtime-streams.md`.

What earlier tasks provide (as if already on `develop`): every process (builtin,
Lua, agent) has slots 0/1/2 installed from the resolved `StartProcessOptions`;
`BuiltinContext` writes to 1/2; `LuaProcess` has
`WriteToDescriptor(int fd, const std::string&)` and writes its errors to slot 2;
the agent's diagnostics go to slot 2 through `ProcessAgentConsole`;
`MockLLMCommunicator::SetDoneReason`; `MockFileDescriptor`; the agent haisos
tests use `spawnSync`.

Today: `BuiltinProcess::ExitStatus()` (tests only; "IProcess has no exit status
yet"); `LuaProcess::RunThread` loads with chunk name `m_path` (so Lua writes
`[string "<path>"]:<line>:`), the kill hook `KillHookTrampoline` latches and
re-raises until the top-level `lua_pcall`; `Agent::ProcessCommand` breaks out
after `MAX_LLM_ROUNDS` with only a log line; `src/haisos/main.cpp` returns 0
after the RUNs whatever they did. There are no `IProcess` mocks in `tests/`
(check with `grep -rn "public IProcess\|public ICurrentProcess" tests`; if one
has appeared, give it `ExitCode()`).

## Changes

### `interfaces/IProcess.h`

```cpp
// Empty until the process has finished; then 0-255, with the shell's
// meanings: the program's own code modulo 256; 143 (128 + SIGTERM) when it
// ended because it was asked to stop (TriggerStop is Haisos's SIGTERM); 141
// (128 + SIGPIPE) when it was stopped by writing to a pipe nobody reads. Once
// set it never changes. A StartProcess that returned null made no process,
// and so no code (a shell reports 127 itself).
virtual std::optional<int> ExitCode() const = 0;
```

Include `<optional>`. Fix the old comment there that says a top-most process
has parent pid 0 only if you touch it anyway (not required).

### `src/components/libheaders/ExitCodes.h` (new, header-only)

```cpp
namespace Haisos {
constexpr int kExitCodeBrokenPipe = 141;   // 128 + SIGPIPE
constexpr int kExitCodeStopped = 143;      // 128 + SIGTERM
constexpr int kExitCodeNotStarted = 127;   // what a shell (and haisos) reports for a program it could not start
// How a process's program ended, as far as its exit code is concerned.
enum class ProcessEnd { Exited, Stopped, BrokenPipe };
// Exited: programCode's low 8 bits (exit(-1) is 255, exit(256) is 0);
// Stopped: 143; BrokenPipe: 141 (programCode ignored).
int ExitCodeFor(ProcessEnd end, int programCode);   // inline
}
```

This is the **seam for `pipes--pipe-service`**: each runtime decides its
`ProcessEnd` in one place (named below); that task adds the broken-pipe case
there (BrokenPipe takes precedence over Stopped, since a broken pipe stops the
process itself). Say so in a comment.

### `src/components/BuiltinCommands/BuiltinProcess.{h,cpp}`

- Replace `ExitStatus()` and `m_exitStatus` by `std::optional<int> ExitCode() const override`
  and a `std::optional<int> m_exitCode` guarded by `m_finishedMutex`.
- In `RunThread`, after `Run` returns (or throws: program code 1, as today) and
  after the descriptors are released, set `m_exitCode =
  ExitCodeFor(m_stopRequested ? ProcessEnd::Stopped : ProcessEnd::Exited, status)`
  under the mutex in the same critical section that sets `m_finished`. That
  line is the builtin's `ProcessEnd` decision point.
- Header comment: drop "IProcess has no exit status yet".

### `src/components/HaisosOS/LuaProcess.{h,cpp}`

- Chunk name: `luaL_loadbufferx(..., ("@" + m_path).c_str(), "t")`, so Lua
  reports positions as `<path>:<line>:`.
- Error output (load or runtime, not a stop and not `exit`): write to slot 2
  exactly `lua: <message>\n`, where `<message>` is the error value as `lua.c`'s
  `msghandler` renders it: a string (or number) as it is; otherwise its
  `__tostring` result if it has one; otherwise `(error object is a <type>
  value)`. No traceback. Example, for `/p.lua` whose line 2 is `error("boom")`:
  `lua: /p.lua:2: boom\n`. Exit code 1. The log lines stay.
- New global `exit([code])`, registered in `RegisterBindings` (the stock
  `os.exit` stays unopened: it calls C `exit()` and would end the whole
  haisos host). Argument: none or nil -> 0; `true` -> 0; `false` -> 1; an
  integer (or a float with an exact integer value) -> that number; anything
  else -> a Lua argument error (`luaL_argerror`), which is an ordinary script
  error. It stores the code in `m_exitCodeRequested`, sets `std::atomic<bool>
  m_exitRequested`, re-arms the hook exactly as `KillHookTrampoline` does on a
  kill (every instruction/call/return/line), and raises (`luaL_error(L, "exit")`)
  -- so `pcall(function() exit(2) end)` cannot swallow it: the latched hook
  keeps raising until the top-level `lua_pcall` returns. `KillHookTrampoline`
  raises when `IsKillRequested() || m_exitRequested`. Keep the "never let a C++
  exception into Lua" rule in the new C function (it only touches atomics and
  Lua API calls; no `std::string` building).
- Decision point (end of `RunThread`, before marking finished, after the
  descriptors are released): `exit` requested -> `ExitCodeFor(Exited,
  m_exitCodeRequested)`; else stop requested and the script did not finish on
  its own -> `ExitCodeFor(Stopped, 0)`; else a load/runtime error -> 1; else 0.
  A script that finished before a late `Kill()` keeps its own code.
  Unexpected C++ exceptions in `RunThread` -> 1. Store in
  `std::optional<int> m_exitCode` under `m_finishedMutex`; `ExitCode()` reads
  it.
- `io` and `os` stay nil; the sandbox comment lists `exit` as the one
  addition and why.

### `src/components/Agent/Agent.{h,cpp}`

- New on the concrete `Agent` only (not `IAgent`, no interface change):
  `bool LastCommandFailed() const;` backed by `std::atomic<bool>
  m_lastCommandFailed{false}`.
- `ProcessCommand` sets it false at its start; true when a response is an
  error (`IsErrorResponse`, from streams--runtime-streams) or when the round cap
  is exceeded (also write `Error: the command reached the maximum of <n> LLM
  rounds` with `m_console->WriteError` there -- a diagnostic; it does not go to
  the history or the message buffer); `OnCommandFailed` sets it true.
- No change to what is sent to the LLM or kept in the history/message buffer.

### `src/components/HaisosOS/AgentProcess.{h,cpp}`

- `TriggerStop()`: if the process has not finished yet (`WaitToFinish(0)`
  false), set `std::atomic<bool> m_stopRequested`; then ask the agent to stop as
  today. The input loop's own `agent->TriggerStop()` at end of input does not
  go through here, so it does not count as a stop from outside.
- `ExitCode()`: `nullopt` until `WaitToFinish(0)`; then, computed once and
  latched under a mutex (the decision point): `m_stopRequested` ->
  `ExitCodeFor(Stopped, 0)`; else `ExitCodeFor(Exited, m_agent->LastCommandFailed() ? 1 : 0)`.

### `src/tools/os_list_processes/OSListProcessesTool.cpp`

Add `{"exit_code", ...}` to each entry: the number, or JSON null while the
process runs. Do **not** change `ToolDefaultDescription` or the schema. Update
`src/tools/os_list_processes/CLAUDE.md`.

### `src/haisos/main.cpp`

- Keep, per `RUN` entry in file order, either its process or "not started".
- After the waits: for each entry in order, its code is 127 if not started,
  else `process->ExitCode().value_or(kExitCodeStopped)` (a process still
  running after the bounded wait is about to be stopped by the OS's
  destruction); the result is the first non-zero code, else 0.
- The "no process could be started" path keeps its message and returns 127
  (the first RUN's code) instead of 1.
- `OUTCOPY` failure: 1 only if the result is still 0.
- Log the chosen code (`LogInfo("Haisos finished with exit code %d (from RUN %s)", ...)`).
- `--help` text: one line saying how the exit status is chosen.

## Tests

`tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`:
the `Run` helper reads `process->ExitCode()` (expect it set after the wait)
instead of `ExitStatus()`; every existing status assertion keeps its value.
New `BuiltinCommandsTest.ExitCodeIsEmptyUntilFinishedThenTheStatus` -- `ls
/nope`: after the wait, `ExitCode()` is 2; `pwd` gives 0.

`tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp`:

- `LuaProcessTest.ARuntimeErrorPrintsLuaStyleAndExitsOne` -- script name
  `test_lua.lua`, line 2 `error("boom")`: stderr exactly
  `lua: test_lua.lua:2: boom\n`, `ExitCode()` 1.
- `LuaProcessTest.ASyntaxErrorPrintsLuaStyleAndExitsOne` -- stderr starts with
  `lua: test_lua.lua:1:`.
- `LuaProcessTest.ANonStringErrorIsDescribed` -- `error({})`: stderr
  `lua: (error object is a table value)\n`.
- `LuaProcessTest.ExitEndsTheScriptWithItsCode` -- `print('a') exit(3)
  print('b')`: stdout `a\n`, code 3; `exit()` -> 0; `exit(false)` -> 1;
  `exit(256 + 7)` -> 7; `exit(-1)` -> 255.
- `LuaProcessTest.ExitCannotBeCaughtByPcall` -- `pcall(function() exit(4) end)
  print('after')`: code 4, stdout empty.
- `LuaProcessTest.AStoppedScriptExits143` -- `while true do end`, `TriggerStop()`,
  wait: 143.
- `LuaProcessTest.ExitCodeIsEmptyWhileRunning` -- before the stop above:
  `ExitCode()` is `nullopt`.
- `LuaProcessTest.IoAndOsStayNil` (exists or add): unchanged.

`tests/unit/components/Agent.unittests/AgentTest.cpp`:
`AgentTest.LastCommandFailedFollowsTheLastCommand` -- an interactive agent
with `MockLLMCommunicator`: an error done reason sets it, a following normal
reply clears it; `OnCommandFailed` (throw on call) sets it.

`tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`:

- `HaisosOSTest.AnAgentWhoseLLMCallFailsExitsOne` -- the unreachable
  endpoint: `ExitCode()` 1 after finishing.
- `HaisosOSTest.AStopAfterFinishingKeepsTheExitCode` -- same agent, finished,
  then `TriggerStop()`: still 1.
- `HaisosOSTest.AScriptExitCodeIsVisibleFromOutside` -- a `.lua` with `exit(5)`: 5.
- `HaisosOSTest.OsListProcessesShowsExitCodes` -- start a `.lua` that loops
  (`while true do end`) and one with `exit(5)`; wait for the second; build
  `OSToolFactory::Create(handle)` with a `CurrentProcessHandle` set to the
  finished process (its `OS()` is still this OS), create and call
  `os_list_processes`: the finished one has `"exit_code": 5`, the looping one
  `null`. (Do it before any further `StartProcess`: that drops finished
  processes from the list.) Then stop the looping one.

Haisos tests:

- `tests/haisos/agent_{start,query,list_running,wait_to_finish}.haisostest/*.js`:
  also fail unless `result.status === 0`; keep the stderr/stdout `Error:` check.
- `tests/haisos/builtins.haisostest/builtins.haisostest.js`: the `ls /nope`
  run expects status 2.
- New `tests/haisos/exit_codes.haisostest/exit_codes.haisostest.js` (picked up
  by `scripts/internal/test.js` automatically; no LLM, `FS rootfs MEM`):
  `RUN /bin/ls /nope` -> status 2, stdout empty, stderr the `ls:` line;
  `/p.lua` = `print("to stdout")` + `error("boom")` -> status 1, stdout
  `to stdout\n`, stderr `lua: /p.lua:2: boom\n`; `/e.lua` = `exit(3)` -> 3;
  `RUN /nope.lua` (missing) -> 127; two RUNs, `/bin/echo ok` then `/e.lua` ->
  3; `/bin/echo ok` alone -> 0.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U BuiltinCommands
bash ./scripts/test_linux.sh L U Agent
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```
(The filter must be a substring of the test executable's name, case-insensitive -- `HaisosOS.unittests`, `Agent.unittests`, ... -- and is also passed as `--gtest_filter=*<filter>*`, case-sensitive; so filter by component, not by suite name.)

## Docs

- Root `CLAUDE.md`: a short "Exit codes" paragraph (under the DSL or Command-
  Line Arguments): how `haisos` chooses its status, 127/143/141, Lua `exit()`
  and the `lua:` error line; the `os_list_processes` row unchanged.
- `src/components/HaisosOS/CLAUDE.md`: `ExitCode()` per runtime, Lua `exit`
  and error format, the sandbox addition.
- `src/components/BuiltinCommands/CLAUDE.md`: `BuiltinProcess` -- exit code
  instead of the test-only `ExitStatus()`.
- `src/components/Agent/CLAUDE.md`: `LastCommandFailed()`.
- `src/components/libheaders/CLAUDE.md`: `ExitCodes.h`.
- `src/tools/os_list_processes/CLAUDE.md`: `exit_code`.

## Acceptance

- [ ] `IProcess::ExitCode()` implemented by `BuiltinProcess`, `LuaProcess`,
      `AgentProcess`; `ExitStatus()` gone (`grep -rn ExitStatus src tests` empty).
- [ ] Codes: builtin Run % 256 / 143; Lua 0 / exit code / 1 / 143; agent
      0 / 1 / 143; set exactly once, before the process reports finished.
- [ ] Lua errors read `lua: <path>:<line>: <message>`; `exit` survives
      `pcall`; `io`, `os` still nil.
- [ ] `haisos` returns the first failing RUN's code in file order, 127 for one
      not started, 1 for its own errors.
- [ ] `ExitCodes.h` is the one place 141/143/127 are spelled; each runtime has
      one `ProcessEnd` decision point (the broken-pipe seam).
- [ ] No tool schema, description, system prompt or message shape changed.
- [ ] Build, unit tests, haisos tests green.

## Out of scope

- Producing 141 on a broken pipe: `pipes--pipe-service`.
- A `self_close` exit code, `os_start_process` returning or waiting for exit
  codes, `os_wait_process` (goal.md, Out of scope).
- Lua `io`/`os`, tracebacks on stderr.
