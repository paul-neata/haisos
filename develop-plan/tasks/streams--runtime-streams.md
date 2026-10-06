# Task streams--runtime-streams: Agents and Lua scripts on their own stdin, stdout and stderr

- Rock: streams
- Depends on: streams--console-and-start
- Size: ~700 changed lines in ~22 files
- Plan checked against: develop @ 0d92271
- PR title: Agents and Lua scripts read slot 0 and write to slots 1 and 2

## Goal

After this task every runtime uses its process's standard streams, like the
builtins already do:

- An **agent** process and a **Lua** process have the resolved
  `StartProcessOptions` streams in slots 0/1/2 of their `IFileIO` before
  anything runs.
- An agent's `IAgentConsole` is an adapter over its process's descriptors:
  what the agent says (every assistant message's text, as the console shows it
  today) goes to **slot 1** as `message + "\n"`; its **diagnostics** go to
  **slot 2** as `message + "\n"`: an LLM, HTTP or parse failure (today printed
  like a reply), `Error: Unknown tool - <name>`, and `Error: the command
  failed: ...`. So `RUN /agent.md` against an unreachable endpoint shows
  `Error: HTTP request failed: ...` on the host's stderr, nothing on stdout.
- Lua's `print` writes its line plus `'\n'` to slot 1; a load or runtime
  error is written to slot 2 (still as `[<path>] Error: <message>` -- the
  `lua: ...` format comes with `streams--exit-codes`).
- An interactive process's input loop (`AgentInputLoop`) reads lines from the
  process's **stdin** (slot 0) instead of the console: by default (`RUN -i`)
  that is the console's input, but any descriptor works, so a pipe or a file
  can feed an interactive agent.
- Nothing inside a process holds the physical console any more:
  `IAgentConsole::ReadLine` is removed (no reader left), and `HaisosOS` no
  longer creates `AgentConsoleAdapter`s.

**No change may alter what an agent sends to the LLM** -- tool schemas, tool
descriptions, system prompts (`kInteractiveAgentSystemPrompt` included), message
shapes, what goes into the history (an LLM error reply is still pushed to the
history exactly as today), and the agent's message buffer (`GetConsoleOutput()`,
which `agent_query`/`agent_wait_to_finish` return to the LLM: keep its
`[<name>] ...` lines byte for byte). The LLM cache recordings
(`tests/tool/llm_cache_proxy_database/`) are keyed by request body.

## Context

Read first: the root `CLAUDE.md` (Security: `ICurrentProcess` is the only door
out of a process; Creating things -- especially "Nothing that waits for a
runtime thread is destroyed on one"), `src/components/HaisosOS/CLAUDE.md`,
`src/components/Agent/CLAUDE.md`, `src/components/Console/CLAUDE.md`,
`src/components/libheaders/CLAUDE.md`, and the plan
`develop-plan/tasks/streams--console-and-start.md`.

What earlier tasks provide (as if already on `develop`):

- `interfaces/IFileDescriptor.h` (`Read`, `Write`, `IsTerminal`; `kIOError`,
  `kIOBrokenPipe`, `kIOInterrupted`).
- The `IFileIO` table: `GetDescriptor(int)`, `AddDescriptor`, `Dup`, `Dup2`,
  `CloseDescriptor`, `kStdIn`/`kStdOut`/`kStdErr`; `ProcessFileIO::ReleaseAllDescriptors()`
  called by each process class when its program ends (fd--process-table).
- `ProcessFileIO::InstallStandardStreams(in, out, err)` (streams--console-and-start).
- `StartProcessOptions { stdIn, stdOut, stdErr, interactive }`, resolved by
  `HaisosOS::ResolveStandardStreams`; `StartAgentProcess(..., const
  StartProcessOptions& options)` and `StartLuaProcess(..., const
  StartProcessOptions& options)` already receive the resolved options and
  ignore the streams (streams--console-and-start).
- `IPhysicalConsole::Write`/`WriteError` raw; console descriptors in
  `src/components/Console/ConsoleDescriptors.h`; `AgentConsoleAdapter::Create(physicalConsole)`
  untagged.
- `tests/mocks/MockFileDescriptor.h` (`Feed`, `EndInput`, `Written`,
  `SetWriteResult`, terminal flag).

Today (read the code): `HaisosOS::StartAgentProcess` creates an
`AgentConsoleAdapter` and passes it to `ILLMService::CreateAgent` and, when
interactive, to `AgentProcess::Create` as `interactiveInput`;
`AgentProcess` builds an `AgentInputLoop(agent, console)`; `AgentInputLoop::Run`
calls `m_console->ReadLine()`; `Agent.cpp` writes to `m_console` at three
places (reply content ~line 469, unknown tool ~358, `OnCommandFailed` ~521);
the LLM error responses are made in `src/components/LLMCommunicator/LLMCommunicator.cpp`
with `done_reason` `"error"`, `"parse_error"` or `"http_error"` and content
`Error: ...`; `LuaProcess` takes an `IAgentConsole` and writes `print` lines
and error lines to it.

## Changes

### `interfaces/ILLMService.h` -- `IAgentConsole`

```cpp
class IAgentConsole {
public:
    virtual ~IAgentConsole() = default;
    // What the agent says: the text of one assistant message. How it is laid
    // out is the console's business (a process's console writes it plus '\n'
    // to the process's stdout).
    virtual void Write(const std::string& message) = 0;
    // A diagnostic about the agent's own running, not something it said: an
    // LLM, HTTP or parse failure, an unknown tool, a failed command. A
    // process's console writes it plus '\n' to the process's stderr.
    virtual void WriteError(const std::string& message) = 0;
};
```

`ReadLine` is removed. Update every implementation: `AgentConsoleAdapter`
(`WriteError` -> `physicalConsole->WriteError(message + "\n")`; drop
`ReadLine`), `InMemoryAgentConsole` (`WriteError` appends like `Write`; drop
`ReadLine`), `tests/mocks/MockAgentConsole.h` (records `WriteError` calls in a
separate list, `GetErrors()`; drop `ReadLine`, `SetInputLines`,
`GetReadLineCalls`), and the test consoles in
`tests/unit/components/Agent.unittests/AgentTest.cpp` (`ConsoleFailingOnUnknownTool`:
throws from `WriteError` on a message containing `Unknown tool`, records the
rest), `tests/unit/tools/agent_start.unittests/AgentStartToolTest.cpp`
(`DummyAgentConsole`). `ILLMService::CreateAgent`/`CreateAgentConsole`
signatures do not change.

### `src/components/Agent/Agent.cpp`

- An anonymous-namespace helper `bool IsErrorResponse(const LLMResponse& r)`:
  `done_reason` is `"error"`, `"parse_error"` or `"http_error"` (the three the
  LLMCommunicator sets on failure; comment that).
- Reply content: if `IsErrorResponse(response)` call `m_console->WriteError(content)`,
  else `m_console->Write(content)`. Everything else in that block (message
  buffer append, pushing the response to the history) is unchanged.
- Unknown tool and `OnCommandFailed`: `WriteError` instead of `Write`; the
  texts and the message-buffer lines are unchanged.

### `src/components/HaisosOS/ProcessAgentConsole.{h,cpp}` (new; add to the `HaisosOS` library)

```cpp
// An agent process's IAgentConsole: what the agent says goes to its process's
// stdout, its diagnostics to its stderr -- through the process's own IFileIO,
// looked up at every write, so a process whose descriptors were replaced or
// released writes wherever its table says (or nowhere).
class ProcessAgentConsole : public IAgentConsole {
public:
    static std::shared_ptr<ProcessAgentConsole> Create(std::shared_ptr<CurrentProcessHandle> process);
    void Write(const std::string& message) override;       // message + "\n" to kStdOut
    void WriteError(const std::string& message) override;  // message + "\n" to kStdErr
private:
    explicit ProcessAgentConsole(std::shared_ptr<CurrentProcessHandle> process);
    std::shared_ptr<CurrentProcessHandle> m_process;
};
```

The console exists before the process (the agent is created first), which is
why it goes through the same `CurrentProcessHandle` the OS tools use: the
handle is filled in by `AgentProcess::Create` before the agent is given its
program, so no write can find it empty. Each write: `m_process->Get()`, then
`IO()->GetDescriptor(fd)`; null at any step -> drop. Write the whole buffer,
looping over partial writes, stopping at a negative result. **Seam for
`pipes--pipe-service`:** a `kIOBrokenPipe` on stdout is where that task makes
the agent stop quietly (exit 141); leave a one-line comment. The temporary
`shared_ptr` to the process may be the last one on the agent's thread; that is
safe only because `AgentProcess` uses the `DestroyOffRuntimeThreads` deleter
and the agent's thread runs in a `RuntimeThreadScope` -- do not change either.

### `src/components/libheaders/DescriptorLineReader.h` (new, header-only)

```cpp
// Reads lines from a descriptor, for a reader that owns its input (it reads
// ahead, up to 4096 bytes at a time, and keeps the rest for the next line).
class DescriptorLineReader {
public:
    explicit DescriptorLineReader(std::shared_ptr<IFileDescriptor> input);
    // The next line without its '\n' (and without a '\r' before it); a last
    // line not ended by '\n' is returned as a line too. nullopt at end of
    // input, or once a read fails (any negative result, kIOInterrupted
    // included) -- from then on, always nullopt. A null input is at end.
    std::optional<std::string> ReadLine();
};
```

A plain helper class, not an interface implementation (public constructor is
fine, like the other libheaders).

### `src/components/HaisosOS/AgentInputLoop.{h,cpp}`

- `Create(std::shared_ptr<IAgent> agent, std::shared_ptr<IFileDescriptor> input)`
  -- refuses a null agent or input as today. The loop reads with a
  `DescriptorLineReader` over `input` instead of `IAgentConsole::ReadLine`.
  Everything else (end of input stops the agent; a line for a closed agent is
  dropped; the destructor waits however long; `RuntimeThreadScope`) is
  unchanged. Comments: "its stdin" instead of "its console"; a console read
  still cannot be interrupted, a pipe read can (D7).

### `src/components/HaisosOS/AgentProcess.{h,cpp}`

- `Create(pid, parentPid, environment, path, workingDirectory, os, selfHandle,
  agent, program, const StartProcessOptions& options)` -- `interactiveInput`
  goes. Order inside `Create`: construct; `m_io->InstallStandardStreams(options.stdIn,
  options.stdOut, options.stdErr)` (false -> log, return null); fill
  `selfHandle`; if `options.interactive`, create the input loop with
  `options.stdIn` (the loop holds its own reference, so releasing the table
  never pulls the descriptor from under a blocked read); post the program;
  start the loop. Hold `m_io` as `std::shared_ptr<ProcessFileIO>`.
- Where fd--process-table releases the descriptors for an agent process must
  stay after the agent's last write (the agent's thread has finished) -- check
  it and keep it so.

### `src/components/HaisosOS/LuaProcess.{h,cpp}`

- `Create(..., std::vector<std::string> args, std::shared_ptr<IToolFactory> toolFactory,
  const StartProcessOptions& options)` -- the `IAgentConsole` parameter and
  `m_console` go. `Create` installs the three streams before `Start()` (false
  -> log, return null).
- A private `void WriteToDescriptor(int fd, const std::string& bytes)` (whole
  buffer, partial writes looped, negative result stops; null descriptor drops;
  pipes seam comment as above).
- `LuaPrintTrampoline`: the joined line plus `"\n"`, one call to
  `WriteToDescriptor(kStdOut, ...)`. Still never lets a C++ exception into Lua.
- Load and runtime errors: the same text as today plus `"\n"` to `kStdErr`.
- Update the `warn` comment in `OpenSafeLuaLibs` (it bypasses the process's
  stderr, not "console tagging") and the class comment (`print()` goes to
  stdout).

### `src/components/HaisosOS/HaisosOS.cpp`

- `StartAgentProcess`: create `ProcessAgentConsole::Create(processHandle)`
  (the handle is created first now) and pass it to `CreateAgent`; pass
  `options` to `AgentProcess::Create`. No `AgentConsoleAdapter` include or use
  left in this file.
- `StartLuaProcess`: no console; pass `options` to `LuaProcess::Create`.
- `CreateAgent` gets exactly the same name, system prompts and `isInteractive`
  as before.

### `src/components/Console/AgentConsoleAdapter.*`, `InMemoryAgentConsole.*`

As above (`WriteError`, no `ReadLine`). `AgentConsoleAdapter` stays for the
integration tests, which use agents without an OS.

## Tests

`tests/unit/components/HaisosOS.unittests/AgentInputLoopTest.cpp` -- rewrite on
`MockFileDescriptor` (fed input) instead of consoles:

- `CreateRefusesAMissingAgentOrInput`.
- `ANotStartedLoopHasNotFinishedAndReadsNothing`.
- `PostsEveryLineThenStopsTheAgentAtEndOfInput` -- `Feed("first\n\nsecond\n")`,
  `EndInput()`: commands `{"first", "", "second"}`, stop triggered.
- `PostsALastLineWithoutNewline` -- `Feed("a\nb")`, `EndInput()`: `{"a", "b"}`.
- `StripsCarriageReturns` -- `"x\r\n"` posts `"x"`.
- `DoesNotReadForAnAgentThatHasAlreadyClosed` and the blocking-read test
  (keep their intent, with `Feed` playing the typed line).

`tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp` --
`RunScript(toolFactory, script, args)` creates a stdout and a stderr
`MockFileDescriptor` (stdin: an empty one, `EndInput()` called) and returns
them with the process (a small struct); replace `console->GetMessages()` by
the lines of the stdout mock's `Written()`. New:

- `LuaProcessTest.PrintWritesItsLineAndNewlineToStdout` -- `print('a', 1)`
  gives exactly `"a\t1\n"`; stderr empty.
- `LuaProcessTest.AScriptErrorGoesToStderr` -- `error('boom')`: stdout empty,
  stderr contains `boom` and ends with `\n`.

`tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`:

- `AnInteractiveAgentIsFedConsoleLinesUntilInputEnds` keeps passing (the
  default stdin is the console's input; `ReadLineCalls()` still 2).
- `HaisosOSTest.AnInteractiveAgentReadsAGivenStdin` -- `interactive` with
  `stdIn` a `MockFileDescriptor` fed `"from a pipe\n"` and ended: the history
  mentions `from a pipe`; the scripted console's `ReadLineCalls()` is 0.
- `HaisosOSTest.AnAgentsLLMFailureGoesToStderr` -- the unreachable endpoint,
  `stdOut`/`stdErr` mocks: stderr contains `Error: HTTP request failed`,
  stdout does not contain `Error:`.
- `HaisosOSTest.AScriptPrintsToAGivenStdout` -- a `.lua` printing `hi` with a
  `stdOut` mock: it holds `hi\n`.

`tests/unit/components/Agent.unittests/AgentTest.cpp`:

- Assertions on `"Error: the command failed: ..."` and `Unknown tool` move
  from `GetMessages()` to `GetErrors()`; `GetConsoleOutput()` assertions stay
  byte for byte.
- `AgentTest.AnErrorResponseIsWrittenAsAnError` -- `MockLLMCommunicator` gains
  `SetDoneReason(const std::string&)` (put in the response it returns); with
  `"http_error"` and message `Error: x`, the console's `GetErrors()` has
  `Error: x` and `GetMessages()` is empty; with an empty done reason the reply
  is in `GetMessages()`.

`tests/unit/components/Console.unittests/ConsoleTest.cpp`: drop the
`ReadLine` tests of the adapter and in-memory console; add
`AgentConsoleAdapterTest.WriteErrorGoesToTheHostsStderr` (recording console:
`WriteError("e")` arrives as `WriteError("e\n")`).

Haisos tests `tests/haisos/agent_{start,query,list_running,wait_to_finish}.haisostest/*.js`:
LLM failures now reach stderr, which `execSync` does not return. Switch to
`spawnSync(haisosPath, ['haisosfile'], { encoding: 'utf8', timeout: ..., cwd: tmpDir })`,
print stdout, and fail if `/Error:/` matches stdout **or** stderr, or if
`result.error` is set. (`streams--exit-codes` adds the exit status check.)

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U Agent
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```
(The filter must be a substring of the test executable's name, case-insensitive -- `HaisosOS.unittests`, `Agent.unittests`, ... -- and is also passed as `--gtest_filter=*<filter>*`, case-sensitive; so filter by component, not by suite name.)

The haisos agent tests replay recorded LLM traffic; if one misses the cache,
something changed what the agent sends -- that is a bug in this task, not a
recording to refresh.

## Docs

- `src/components/HaisosOS/CLAUDE.md`: agent and Lua processes get slots
  0/1/2; `ProcessAgentConsole` (Key Classes); `AgentInputLoop` reads stdin;
  Lua `print` to stdout, errors to stderr; replace "routes to the process's
  console".
- `src/components/Agent/CLAUDE.md`: diagnostics (`WriteError`) vs replies
  (`Write`); the "failed command" note says the line goes to the console's
  error stream.
- `src/components/Console/CLAUDE.md`: `IAgentConsole` has no `ReadLine`;
  the adapter's `WriteError`.
- `src/components/libheaders/CLAUDE.md`: `DescriptorLineReader.h`.
- Root `CLAUDE.md`: the `RUN -i` paragraph -- every line read from its stdin
  (the console's input unless given); the `-L` / console descriptions need no
  change.

## Acceptance

- [ ] `grep -rn "ReadLine" src/components/HaisosOS src/components/Agent interfaces/ILLMService.h`
      finds no `IAgentConsole::ReadLine` use.
- [ ] `HaisosOS.cpp` does not use `AgentConsoleAdapter`; no process holds the
      physical console.
- [ ] LLM/HTTP/parse failures, unknown tools and failed commands go to stderr;
      replies to stdout; the message buffer and history are unchanged.
- [ ] Agent and Lua slots 0/1/2 are installed before the agent gets its
      program / before the script's thread starts.
- [ ] New classes: private constructor and `Create()` returning `shared_ptr`
      (`ProcessAgentConsole`); new files listed in `CMakeLists.txt`.
- [ ] No tool schema, description, system prompt or message shape changed;
      haisos agent tests pass on the existing recordings.
- [ ] Build, unit tests and haisos tests green.

## Out of scope

- Exit codes, the `lua: <path>:<line>: <message>` format, Lua `exit()`:
  `streams--exit-codes`.
- A non-interactive agent reading its stdin; Lua `io`/`os`; `os_start_process`
  passing streams (goal.md, Out of scope).
- The broken-pipe stop (141): `pipes--pipe-service` (seams named above).
