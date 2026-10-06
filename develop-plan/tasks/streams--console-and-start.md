# Task streams--console-and-start: Raw console, console descriptors, stdio at process start, builtins on 1/2

- Rock: streams
- Depends on: fd--descriptor-objects, fd--process-table
- Size: ~850 changed lines in ~24 files
- Plan checked against: develop @ 8fb8324
- PR title: Give every process stdio from StartProcessOptions; builtins write to 1/2

## Goal

After this task:

- The physical console is a plain terminal: `IPhysicalConsole::Write(bytes)`
  writes exactly the bytes given to the host's stdout (no newline added, empty
  writes are no-ops), and the new `IPhysicalConsole::WriteError(bytes)` writes
  them to the host's **stderr**. Output is flushed after every write, so a
  prompt without a newline (`$ `) shows at once.
- The console comes into processes only as **descriptors**
  (`IFileDescriptor`): a console output, a console error and a console input
  descriptor, all terminals (`IsTerminal()` true), raw, untagged, unbuffered;
  plus an **empty input** descriptor (reads return 0 at once, not a terminal).
- `StartProcessOptions` has `stdIn`, `stdOut`, `stdErr` and `interactive`
  (renamed from `interactiveAgent`). `HaisosOS::StartProcess` fills every null
  one with the default -- stdout: console output, stderr: console error, stdin:
  console input when `interactive`, else empty input -- and hands the result to
  the runtime.
- A **builtin** process has these three in slots 0/1/2 of its `IFileIO`
  table before its command runs. `BuiltinContext::Out` writes to slot 1;
  `Error`, the `Try '... --help'` line and the "not treated" reports write to
  slot 2. `BuiltinCommandHost::console` is gone. So `RUN /bin/ls /nope` prints
  its `ls: cannot access ...` line on the host's stderr, and `RUN /bin/echo hi`
  prints `hi` untagged on the host's stdout.
- The `[<name>_<pid>] ` tags are gone everywhere: `AgentConsoleAdapter` (still
  used by agent and Lua processes until `streams--runtime-streams`, and by the
  integration tests) writes `message + "\n"` untagged.
- `RUN -i <program>` sets `interactive` for any runtime (the parser already
  accepts it in front of any program; only `main.cpp`, comments and docs
  change). A builtin run with `-i` gets the console's input as its stdin (no
  builtin reads stdin yet; `builtins--directories` makes `cat` do so).

Agent and Lua processes do **not** yet get slots 0/1/2 or write to them: that
is `streams--runtime-streams`. In this task they are handed the resolved
options and ignore the three descriptors (an agent still reads typed lines
through `AgentConsoleAdapter::ReadLine` when interactive).

**No change may alter what an agent sends to the LLM** -- tool schemas, tool
descriptions, system prompts (including `kInteractiveAgentSystemPrompt`),
message shapes. The LLM cache recordings in `tests/tool/llm_cache_proxy_database/`
are keyed by request body; changing any of these makes every agent test miss
the cache.

## Context

Read first: the root `CLAUDE.md` (Security: `ICurrentProcess` is the only door
out of a process; Creating things; Builtin Commands), `src/components/Console/CLAUDE.md`,
`src/components/BuiltinCommands/CLAUDE.md`, `src/components/HaisosOS/CLAUDE.md`,
`develop-plan/goal.md` (Clarifications, Contracts).

What earlier tasks provide (as if already on `develop`):

- `fd--descriptor-objects`: `interfaces/IFileDescriptor.h` --
  `class IFileDescriptor { virtual ssize_t Read(void* buf, size_t count) = 0;
  virtual ssize_t Write(const void* buf, size_t count) = 0; virtual bool
  IsTerminal() const = 0; }`, results `kIOError` (-1), `kIOBrokenPipe` (-2),
  `kIOInterrupted` (-3). `IFileSystem::OpenFile` / `IFileIO::OpenFile` return
  `std::shared_ptr<IFileDescriptor>` (null on failure); `ReadFile`/`WriteFile`/
  `CloseFile` are gone. If that task gave `IFileDescriptor` further pure
  virtual methods, implement them in the new descriptors below as a
  descriptor that is not a file would (fail with `kIOError` / report nothing).
- `fd--process-table` (its plan: `develop-plan/tasks/fd--process-table.md`;
  read it): the `IFileIO` table -- `GetDescriptor(int fd)`,
  `AddDescriptor(shared_ptr<IFileDescriptor>) -> int` (lowest free slot),
  `Dup`, `Dup2`, `CloseDescriptor`, constants `kStdIn` 0, `kStdOut` 1,
  `kStdErr` 2, `kMaxDescriptors` 1024 (in `interfaces/IFileIO.h`); and
  `ProcessFileIO::ReleaseAllDescriptors()`, which every process class calls
  when its program ends, before it reports finished. If that plan names the
  release method differently, use its name.

Today (read the code):

- `src/components/Console/Console.cpp`: `Write` queues the message; the
  background thread prints it plus `'\n'` to `std::cout`, skipping empty
  messages. `ReadLine` is `std::getline` on `std::cin`, `\r` stripped,
  serialized by `m_readMutex`.
- `src/components/Console/AgentConsoleAdapter.cpp`: tags `[<name>] `.
- `src/components/HaisosOS/HaisosOS.cpp`: `StartBuiltinProcess` builds a
  `BuiltinCommandHost` with `host.console = AgentConsoleAdapter::Create(...)`;
  `StartAgentProcess`/`StartLuaProcess` build an `AgentConsoleAdapter` too;
  `StartProcess` reads `options.interactiveAgent`.
- `src/components/BuiltinCommands/BuiltinCommand.{h,cpp}`: `BuiltinContext`
  takes an `IAgentConsole`, `Out` buffers into lines, one console `Write` per
  line; `Error`/`TryHelp`/`NotTreated` flush and write a line.
- `src/components/BuiltinCommands/BuiltinProcess.{h,cpp}`: holds `m_console`
  from the host; writes `<name>: internal error: ...` there on an exception.
- `src/haisos/main.cpp` line ~366: `options.interactiveAgent = runEntry.interactive;`.

## Changes

### `interfaces/IPhysicalConsole.h`

- `virtual void Write(const std::string& bytes) = 0;` -- rewrite the comment:
  writes exactly these bytes to the host's standard output, in order with every
  other `Write`/`WriteError`, adding nothing (no newline, no label); an empty
  string writes nothing.
- New `virtual void WriteError(const std::string& bytes) = 0;` -- the same,
  to the host's standard error.
- `ReadLine`, `Start`, `Stop` unchanged. Update the class comment ("stdout,
  shared ...") to say stdout and stderr, and that processes reach it only
  through console descriptors (`ConsoleDescriptors.h`).

### `src/components/Console/Console.{h,cpp}`

- The queue holds `(stream, bytes)` pairs (a small struct `ConsoleOutput {
  bool toError; std::string bytes; }`), so stdout and stderr writes keep their
  relative order.
- `Write(bytes)`: if empty, return; post `{false, bytes}`. `WriteError(bytes)`:
  same with `true`.
- `ProcessQueue`: for each item write the bytes as they are
  (`std::cout.write(data, size)` or `std::cerr.write`) and flush that stream
  (`std::cout.flush()`). No `'\n'` is added any more.
- Comments: the console writes raw bytes, stdout and stderr.

### `src/components/Console/ConsoleDescriptors.{h,cpp}` (new; add to `Console`'s `CMakeLists.txt`)

Four `IFileDescriptor` classes, each with a private constructor and
`static std::shared_ptr<X> Create(...)` (root `CLAUDE.md`, "Creating things"):

```cpp
class ConsoleOutputDescriptor : public IFileDescriptor {   // -> IPhysicalConsole::Write
public:
    static std::shared_ptr<ConsoleOutputDescriptor> Create(std::shared_ptr<IPhysicalConsole> console);
    ...
};
class ConsoleErrorDescriptor : public IFileDescriptor {    // -> IPhysicalConsole::WriteError
public:
    static std::shared_ptr<ConsoleErrorDescriptor> Create(std::shared_ptr<IPhysicalConsole> console);
};
class ConsoleInputDescriptor : public IFileDescriptor {    // <- IPhysicalConsole::ReadLine
public:
    static std::shared_ptr<ConsoleInputDescriptor> Create(std::shared_ptr<IPhysicalConsole> console);
};
class EmptyInputDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<EmptyInputDescriptor> Create();
};
```

Behaviour:

- Output / error: `Write(buf, count)`: `count == 0` returns 0; otherwise
  passes `std::string(buf, count)` to `Write` / `WriteError` and returns
  `count`. A null console discards and still returns `count`. `Read` returns
  `kIOError`. `IsTerminal()` true.
- Input: holds a pending-bytes string and an at-end flag under a mutex.
  `Read(buf, count)`: `count == 0` returns 0. Under the mutex: if nothing is
  pending and not at end, call `console->ReadLine()` (blocking, uninterruptible
  -- the documented exception of D7); `nullopt` sets at-end and returns 0;
  a line becomes pending as `line + "\n"`. Copy up to `count` pending bytes
  out, keep the rest for the next `Read`, return the number copied. Once at
  end, every `Read` returns 0. A null console is at end at once. `Write`
  returns `kIOError`. `IsTerminal()` true.
- Empty input: `Read` returns 0 at once, `Write` returns `kIOError`,
  `IsTerminal()` false.

None of them buffers output or adds anything to it.

### `src/components/Console/AgentConsoleAdapter.{h,cpp}`

- `Create(std::shared_ptr<IPhysicalConsole> physicalConsole)` -- the source
  name parameter and the tag go. `Write(message)` calls
  `physicalConsole->Write(message + "\n")`. `ReadLine` unchanged.
- Update the six integration tests that call
  `AgentConsoleAdapter::Create(physicalConsole, "root")` to
  `AgentConsoleAdapter::Create(physicalConsole)`:
  `tests/integration/{Agent.PostAndProcess,AgentQuery,AgentWaitToFinish,Call.get_current_date_time,AgentListRunning,AgentStart}.integrationtest/*.cpp`.

### `interfaces/IHaisosOS.h` -- `StartProcessOptions`

```cpp
struct StartProcessOptions {
    // The new process's standard input, output and error: its descriptors
    // 0, 1 and 2. The same descriptor may be given twice (2>&1). Null means the
    // default: stdout and stderr the OS's console (output / error), stdin the
    // console's input when `interactive`, else an empty input whose reads end
    // at once (as /dev/null).
    std::shared_ptr<IFileDescriptor> stdIn;
    std::shared_ptr<IFileDescriptor> stdOut;
    std::shared_ptr<IFileDescriptor> stdErr;
    // stdin defaults to the console's input; an agent (.md) is interactive
    // (see IAgent::IsInteractive): after its program, every line read from its
    // stdin is posted to it, until it closes itself or its stdin ends.
    bool interactive = false;
};
```

Include `IFileDescriptor.h`. Rename every `interactiveAgent` use:
`src/haisos/main.cpp`, `HaisosOS.cpp`, `BuiltinCommands.cpp`,
`tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`, and the comments
in `AgentProcess.h`, `HaisosFileParser.h`, `IBuiltinCommands.h`,
`src/tools/self_close/CLAUDE.md`, `src/components/HaisosOS/CLAUDE.md`, root
`CLAUDE.md` (`grep -rn interactiveAgent src interfaces tests CLAUDE.md` must
find nothing afterwards).

### `src/components/HaisosOS/HaisosOS.{h,cpp}`

- New members, created in the constructor from `m_physicalConsole` (null
  console allowed): `m_consoleOutput`, `m_consoleError`, `m_consoleInput`
  (`ConsoleOutputDescriptor` / `ConsoleErrorDescriptor` /
  `ConsoleInputDescriptor`) and `m_emptyInput` (`EmptyInputDescriptor`), all
  `std::shared_ptr<IFileDescriptor>`. One set per OS: they hold no per-process
  state. The OS itself keeps `m_physicalConsole` (it creates sub-OSs with it);
  nothing inside a process gets it.
- New private `StartProcessOptions ResolveStandardStreams(const StartProcessOptions& options) const;`
  -- a copy with each null stream replaced by its default (stdin: console input
  if `options.interactive`, else empty input).
- `StartProcess` resolves once, right after its refusals, and passes the
  resolved options on: to `StartBuiltinProcess` (unchanged signature, now the
  resolved options), to `StartAgentProcess` (replace its `bool interactive`
  parameter by `const StartProcessOptions& options`; read `options.interactive`
  where it read `interactive`) and to `StartLuaProcess` (add
  `const StartProcessOptions& options`; unused in this task, see Out of scope).
  Remove the "interactiveAgent only applies to .md programs" debug log:
  `interactive` now means something for every runtime.
- `StartBuiltinProcess`: stop setting `host.console`.
- The name `GetStem(programPath) + "_" + pid` stays (it names the agent and log
  lines); only the console tag that used it goes.

### `interfaces/IBuiltinCommands.h`

- Remove `BuiltinCommandHost::console` and its comment.
- `RunCommand`'s comment: `options.stdIn/stdOut/stdErr` become the process's
  descriptors 0/1/2 (IHaisosOS::StartProcess always fills them); a null one is
  refused (returns null); `options.interactive` needs nothing more from a
  builtin (its stdin already says where input comes from).
- The class comment: drop "stdin, stdout, stderr" from what an OS lacks.

### `src/components/HaisosOS/ProcessFileIO.{h,cpp}`

Add

```cpp
// Places in, out and err in slots 0, 1 and 2 of a table that has no
// descriptor yet. Returns false, changing nothing, if any is null or the table
// is not empty. Called by a process's Create() before its program starts.
bool InstallStandardStreams(std::shared_ptr<IFileDescriptor> in,
                            std::shared_ptr<IFileDescriptor> out,
                            std::shared_ptr<IFileDescriptor> err);
```

Implement it with the table's own operations (three `AddDescriptor` calls that
must return 0, 1, 2), under the table's lock if it has one. If
`fd--process-table` already provides an equivalent, use that instead and say
so in the PR.

### `src/components/BuiltinCommands/BuiltinCommands.cpp`, `BuiltinProcess.{h,cpp}`

- `RunCommand`: refuse (log error, return null) when `options.stdIn`,
  `stdOut` or `stdErr` is null; drop the `interactiveAgent` debug log; pass
  `options` to `BuiltinProcess::Create`.
- `BuiltinProcess::Create(host, environment, command, args, workingDirectory,
  const StartProcessOptions& options)`: after constructing, before `Start()`,
  call `m_io->InstallStandardStreams(options.stdIn, options.stdOut,
  options.stdErr)`; on false log an error and return null. Hold `m_io` as
  `std::shared_ptr<ProcessFileIO>` (fd--process-table may already).
- Remove `m_console`. The `<name>: internal error: <what>` line on an
  exception is written, plus `'\n'`, to slot 2 (`m_io->GetDescriptor(kStdErr)`,
  skipped if null).
- In `RunThread` the `BuiltinContext` must be destroyed (its final flush done)
  **before** `ReleaseAllDescriptors()` is called; if fd--process-table placed
  that call inside the context's scope, move it after.
- The `DestroyOffRuntimeThreads` deleter and the `RuntimeThreadScope` stay as
  they are (root `CLAUDE.md`: never destroy a thread-owning object on a
  runtime thread).

### `src/components/BuiltinCommands/BuiltinCommand.{h,cpp}` -- `BuiltinContext`

- Constructor: `BuiltinContext(ICurrentProcess& process, const IBuiltinCommand& command,
  const std::vector<std::string>& args, const std::atomic<bool>& stopRequested)`
  -- the console parameter goes. It takes `m_out = m_io->GetDescriptor(kStdOut)`,
  `m_err = m_io->GetDescriptor(kStdErr)` and `m_outIsTerminal = m_out &&
  m_out->IsTerminal()` once, at construction.
- **The output rule** (state it in the header comment):
  - stdout to a terminal is unbuffered: each `Out(text)` is written at once,
    as one write of `text` (nothing is held back, so a partial line such as a
    prompt shows immediately);
  - stdout to anything else (a file, a pipe, a device) is block-buffered:
    `Out` appends to `m_outBuffer`, which is written when it reaches
    `kBuiltinOutBufferSize` (4096 bytes), before anything is written to
    stderr, and when the command ends (`~BuiltinContext` calls `Flush()`);
  - stderr is unbuffered: every message is one write.
  So output reaches its descriptor no later than the command's end, and a
  terminal never waits for a newline.
- `Out(text)`, `Flush()` as above. `Error(message)`: `Flush()`, then write
  `<name>: <message>\n`. `TryHelp()`: `Flush()`, then `Try '<name> --help' for
  more information.\n` to stderr. `NotTreated(spelling)`: dedup as today,
  `Flush()`, then `Parameter <spelling> is not treated by HaisosOS <name> v.
  <version>\n` to stderr. The texts are unchanged byte for byte apart from the
  added `\n` the console used to add.
- A private `bool WriteAll(IFileDescriptor* descriptor, const std::string& bytes)`
  is the one place a builtin's bytes reach a descriptor: loops over partial
  writes; a null descriptor or a negative result stops and returns false. After
  stdout has failed once, later stdout output is dropped. **Seam for
  `pipes--pipe-service`:** a `kIOBrokenPipe` result here is where that task
  makes the command stop quietly with exit code 141; leave a one-line comment
  saying so.
- Update the class comment (no more "there is no stdout or stderr yet").

### `src/haisos/main.cpp`, `src/haisos/HaisosFileParser.{h,cpp}`

- `options.interactive = runEntry.interactive;`
- `HaisosFileParser.h`: comment on `HaisosFileRunEntry::interactive` points to
  `StartProcessOptions::interactive`.
- The `--init` template text in `GetHaisosFileTemplate` (comments only): `RUN -i
  <program>` gives the program the console's input as its stdin; an agent run
  so is interactive -- after its program each typed line is sent to it, until
  it closes itself (with its self_close tool) or input ends. Keep the
  `# RUN -i /chat.md` example line. Do not touch the generated `BUILTIN` lines
  (root `CLAUDE.md` rule 9).

## Tests

New mock `tests/mocks/MockFileDescriptor.h` (`Haisos::Mocks::MockFileDescriptor`,
public constructor like the other mocks): `explicit MockFileDescriptor(bool isTerminal = false)`;
`Write` appends to a string under a mutex and counts calls (returns `count`,
or a result forced with `SetWriteResult(ssize_t)`); `Read` hands out bytes
given with `Feed(const std::string&)`, blocking until there are some or
`EndInput()` was called, then 0; accessors `Written()`, `WriteCalls()`.
`streams--runtime-streams` reuses it.

`tests/unit/components/Console.unittests/ConsoleTest.cpp` (the recording
console records `Write` and `WriteError` calls separately, plus one combined
ordered list):

- `ConsoleTest.WriteAndWriteErrorDoNotAddNewlines` -- a started `Console`
  given `Write("a")`, `WriteError("b")`, `Write("")` does not crash (host
  output is not captured; keep it a smoke test like `ProcessesMessages`).
- `AgentConsoleAdapterTest.WritesUntaggedLines` (replaces
  `TagsWithItsSourceNameExactlyOnce`): `Write("hello")` reaches the physical
  console as exactly `"hello\n"`.
- `ConsoleDescriptorTest.OutputWritesExactlyTheBytes` -- `Write("ab\0c", 4)`
  (with a NUL) and `Write("$ ", 2)` reach `Write` as those exact strings;
  returns are 4 and 2; `IsTerminal()` true.
- `ConsoleDescriptorTest.ErrorGoesToWriteError` -- nothing reaches `Write`.
- `ConsoleDescriptorTest.InputGivesLinesWithNewlinesThenEndOfFile` -- scripted
  lines `{"hi", ""}`: reads with a 2-byte buffer give `"hi"`, `"\n"`, `"\n"`,
  then 0, and 0 again.
- `ConsoleDescriptorTest.EmptyInputEndsAtOnce` -- `Read` 0, `IsTerminal()` false,
  `Write` returns `kIOError`.
- `ConsoleDescriptorTest.NullConsoleDiscardsAndEnds`.

`tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`:

- `CapturingConsole`: `Write` appends to `m_out` and to `m_all`, `WriteError`
  to `m_err` and `m_all`; `TakeLines()` splits `m_all` on `'\n'` (no empty
  last element for a trailing newline; a trailing partial line is kept), then
  clears all three; add `TakeOut()`/`TakeErr()` returning the raw strings. The
  tag-stripping goes. Existing tests keep passing unchanged otherwise.
- `BuiltinCommandsTest.ErrorsGoToStderrOnly` -- `ls /nope`: out is empty, err
  is exactly `ls: cannot access '/nope': No such file or directory\n`.
- `BuiltinCommandsTest.OutputIsRawAndUntagged` -- `echo -n abc` gives out
  exactly `abc`; `echo hi` gives `hi\n`.
- `BuiltinCommandsTest.NotTreatedReportsGoToStderr` -- `mkdir -m 755 /m`:
  out empty, err holds `Parameter -m is not treated by HaisosOS mkdir v. `.
- `BuiltinCommandsTest.GivenStdoutReceivesTheOutput` -- open `/out.txt` on
  the in-memory root (`root->OpenFile(..., kFileOpenWriteCreateTruncate,
  kFileCreateMode)` as the fd tasks left it), pass it as `options.stdOut`, run
  `echo hello`; release the descriptor; the file holds `hello\n`, the console
  got nothing on stdout.
- `BuiltinCommandsTest.SameDescriptorForStdoutAndStderr` -- one
  `MockFileDescriptor` as both `stdOut` and `stdErr`, `ls /docs /nope`: its
  `Written()` contains both the `ls: cannot access '/nope'` line and `a.md`
  (do not assert their order).
- `BuiltinCommandsTest.RunCommandRefusesWhatItCannotRun`: add a case with a
  valid host and environment but null streams -> null.
- `BuiltinContextTest.TerminalStdoutIsWrittenAtOnce` and
  `BuiltinContextTest.OtherStdoutIsBufferedUntilFullOrEnd`: a small
  `FakeProcess : ICurrentProcess` in the test file whose `IO()` is a
  `ProcessFileIO::Create({}, "/")` with `InstallStandardStreams` of three
  `MockFileDescriptor`s; the command is the `echo` object from
  `CreateStandardBuiltinCommands()`. Terminal: after `Out("$ ")` the mock
  holds `$ `. Not a terminal: after `Out("x")` it holds nothing; after
  `Out(std::string(5000, 'y'))` it holds at least 4096 bytes; after the
  context is destroyed, all 5001. Also: `Error("e")` after `Out("x")` on a
  non-terminal writes `x` to stdout before `echo: e\n` reaches stderr.

`tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`:

- `ScriptedPhysicalConsole` gains `WriteError` (no-op) .
- `options.interactiveAgent` -> `options.interactive`.
- Rename `InteractiveAgentOnlyAppliesToAgentPrograms` to
  `AnInteractiveScriptIsNotAnAgent` (a `.lua` with `interactive` runs and
  finishes; it reads nothing, so `ReadLineCalls()` stays 0).
- `HaisosOSTest.ABuiltinWithoutGivenStreamsWritesToTheConsole` -- an OS with a
  recording physical console (and builtins: `m_factory->CreateBuiltinCommands()`,
  `echo` placed with `CreateBuiltinConfigurator`), `StartProcess` of
  `/bin/echo hi` with default options: the console's `Write` got `hi\n`.
- `HaisosOSTest.AGivenStdoutBypassesTheConsole` -- the same with a
  `MockFileDescriptor` as `options.stdOut`: it holds `hi\n`, the console's
  `Write` got nothing.
- (No builtin reads stdin yet, so the interactive default for builtins is
  covered by `ResolveStandardStreams` being the only place defaults are
  chosen; `builtins--directories` tests it through `cat`.)

`tests/integration/*` -- the six `AgentConsoleAdapter::Create` lines only.

`tests/haisos/builtins.haisostest/builtins.haisostest.js`: the pwd check
becomes an exact match of the whole output, `"/\n"`; add `RUN /bin/echo -n
abc` expecting exactly `abc`; add a run of `RUN /bin/ls /nope` with
`spawnSync` (stdio piped) expecting stdout `""` and stderr containing
`ls: cannot access '/nope': No such file or directory` (do not assert the exit
status yet: `streams--exit-codes` does).

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Console
bash ./scripts/test_linux.sh L U BuiltinCommands
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```
(The filter must be a substring of the test executable's name, case-insensitive -- `HaisosOS.unittests`, `Agent.unittests`, ... -- and is also passed as `--gtest_filter=*<filter>*`, case-sensitive; so filter by component, not by suite name.)

## Docs

- Root `CLAUDE.md`: the `RUN -i` paragraph (any program; stdin is the
  console's input; an agent is interactive) and the DSL example comment
  (`RUN -i /chat.md` stays as the example); the `IPhysicalConsole` mention in
  the interfaces list stays.
- `src/components/Console/CLAUDE.md`: raw stdout/stderr, no newline added,
  flushed per write; the four descriptors (Key Classes) and that they are how
  a process sees the console; `AgentConsoleAdapter` untagged.
- `src/components/BuiltinCommands/CLAUDE.md`: "How a builtin gets run" step 2
  (no console; the resolved streams), the "Output" section rewritten with the
  output rule above and slots 1/2.
- `src/components/HaisosOS/CLAUDE.md`: the `StartProcessOptions` paragraph
  (streams, defaults, `interactive`).

## Acceptance

- [ ] `grep -rn "interactiveAgent" src interfaces tests CLAUDE.md` finds nothing.
- [ ] `grep -rn "host.console\|BuiltinCommandHost::console" src tests` finds nothing.
- [ ] No `[name_pid] ` tag is produced anywhere (`AgentConsoleAdapter` untagged).
- [ ] `Console` adds no newline; `WriteError` goes to `std::cerr`; both flush.
- [ ] Every new class implementing an interface has a private constructor and
      `Create()` returning `shared_ptr`.
- [ ] A builtin's descriptors 0/1/2 are installed before its thread starts.
- [ ] Builtin messages are byte for byte as before plus the `\n`.
- [ ] No tool schema, tool description, system prompt or message shape changed.
- [ ] New files are in their `CMakeLists.txt`; build and unit tests green; haisos tests green.

## Out of scope

- Agent and Lua processes getting slots 0/1/2, writing to them, diagnostics on
  stderr, the input loop reading slot 0: `streams--runtime-streams`.
- Exit codes: `streams--exit-codes`.
- `cat` reading stdin, `ls` one-per-line when stdout is not a terminal:
  `builtins--directories`.
- Pipes and the broken-pipe stop (141): `pipes--pipe-service`.
