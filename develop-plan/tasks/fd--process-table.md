# Task fd--process-table: A descriptor table per process on IFileIO

- Rock: fd
- Depends on: fd--descriptor-objects
- Size: ~650 changed lines in ~20 files
- Plan checked against: develop @ 0d92271 (after fd--descriptor-objects)
- PR title: Give every process a descriptor table on IFileIO

## Goal

Every process holds its open files the way a POSIX process does: in a table
of numbered slots on its `IFileIO`. Slot 0 is stdin, 1 stdout, 2 stderr, then
3 and up. There are at most 1024 slots.

- `IFileIO::OpenFile` returns the `std::shared_ptr<IFileDescriptor>` itself,
  without placing it in the table. `ReadFile`, `WriteFile` and `CloseFile`
  disappear from `IFileIO`, as they did from `IFileSystem`.
- Placing a descriptor (`AddDescriptor`, lowest free slot), duplicating one
  (`Dup`, lowest free slot; `Dup2`, a chosen slot replaced atomically),
  looking one up (`GetDescriptor`) and closing a slot (`CloseDescriptor`) are
  table operations.
- When a process's program ends -- a builtin's `Run` returning, a Lua
  script's chunk ending, an agent's conversation thread ending -- every
  descriptor in its table is released **before the process reports finished**.
  A pipe's reader will then see end of file when its writer's program ends
  (pipes come later).

In this task slots 0, 1 and 2 stay empty: `streams--console-and-start` fills
them. Nothing a user or an agent sees changes. `cat`, `os_read_file` and
`os_write_file` behave exactly as before.

## Context

Read first:
- the root `CLAUDE.md`: "Security: `ICurrentProcess` is the only door out of a
  process", "Creating things", and "Nothing that waits for a runtime thread is
  destroyed on one";
- `src/components/HaisosOS/CLAUDE.md`;
- `src/components/Agent/CLAUDE.md`;
- `develop-plan/goal.md` "## Contracts": the `IFileIO` descriptor table bullet
  and the last bullet ("A process's descriptors are all released when its
  program ends"). They are binding.

What `fd--descriptor-objects` provides, as if already on develop:
- `interfaces/IFileDescriptor.h`: `class IFileDescriptor` with
  `ssize_t Read(void*, size_t)`, `ssize_t Write(const void*, size_t)`,
  `bool IsTerminal() const`, and the namespace-scope constants `kIOError`
  (-1), `kIOBrokenPipe` (-2), `kIOInterrupted` (-3).
- `IFileSystem::OpenFile(path, flags[, mode])` returns
  `std::shared_ptr<IFileDescriptor>` (null on failure). `IFileSystem` has no
  `ReadFile`/`WriteFile`/`CloseFile`.
- `src/components/Filesystem/FilesystemUtils.h`:
  - `bool ReadWholeDescriptor(IFileDescriptor&, std::string&)`;
  - `bool ReadWholeFile(IFileSystem&, const std::string&, std::string&)`
    (descriptor-based);
  - `bool ReadWholeFile(IFileIO&, const std::string&, std::string&)`, still on
    `IFileIO`'s `int` API -- this task changes it.
- `src/components/HaisosOS/ProcessFileIO.h/.cpp`: still implements the `int`
  API with an interim map, `std::map<int, std::shared_ptr<IFileDescriptor>>
  m_openFiles` and `m_openFilesMutex` (numbers from 3). This task replaces it.

The code this task changes:
- `interfaces/IFileIO.h`;
- `src/components/HaisosOS/ProcessFileIO.*`;
- `src/components/BuiltinCommands/BuiltinProcess.h/.cpp` (`RunThread`);
- `src/components/HaisosOS/LuaProcess.h/.cpp` (`RunThread`);
- `src/components/HaisosOS/AgentProcess.h/.cpp` (`Create`);
- `src/components/Agent/Agent.h/.cpp` (`RunThread`);
- `src/components/BuiltinCommands/commands/Cat.cpp` (`CatFile`);
- `src/tools/os_write_file/OSWriteFileTool.cpp`;
- `src/components/Filesystem/FilesystemUtils.h`.

Lua and agents reach files only through the `os_*` tools, so they have no
other `IFileIO` call sites. `os_list_directory` uses only `ReadDirectory`;
`ls`, `mkdir` and `pwd` use `Stat`/`ReadDirectory`/`CreateDirectory`. None of
these changes.

How the tests are selected: the name filter of `scripts/test_linux.sh` must be
a substring of the executable's name (case-insensitive) **and** of the gtest
`Suite.Name` (case-sensitive, passed as `--gtest_filter=*<filter>*`).

## Changes

### `interfaces/IFileIO.h`

- `#include "IFileDescriptor.h"`.
- Inside `class IFileIO`, at the top of the public section, add exactly:
  ```cpp
  // The standard slots of the descriptor table, and its size (as RLIMIT_NOFILE).
  static constexpr int kStdIn = 0;
  static constexpr int kStdOut = 1;
  static constexpr int kStdErr = 2;
  static constexpr int kMaxDescriptors = 1024;
  ```
  Callers write `IFileIO::kStdOut`; `streams--*` and `builtins--*` use these
  names.
- Replace the four `int`-descriptor methods with:
  ```cpp
  // As open(), resolving |pathname| against the working directory: the open
  // file, or null on failure. It is NOT placed in the descriptor table; pass it
  // to AddDescriptor (or Dup2 it in) to give it a number.
  virtual std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) = 0;
  virtual std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) = 0;
  ```
  Delete `CloseFile`, `ReadFile` and `WriteFile`.
- Add the table, with this section comment and these exact signatures:
  ```cpp
  // --- The descriptor table ---
  // The process's open files by number, as a POSIX process holds them: 0 stdin,
  // 1 stdout, 2 stderr, then 3 and up, at most kMaxDescriptors slots. A slot
  // holds a shared_ptr, so several slots (and several processes) may hold the
  // same open file -- one position, closed when the last holder releases it.
  // Every one is released when the process's program ends.

  // The descriptor in slot |fd|, or null if the slot is empty or out of range.
  virtual std::shared_ptr<IFileDescriptor> GetDescriptor(int fd) const = 0;
  // Places |descriptor| in the lowest free slot and returns it; -1 if the table
  // is full or |descriptor| is null.
  virtual int AddDescriptor(std::shared_ptr<IFileDescriptor> descriptor) = 0;
  // As dup(): places slot |fd|'s descriptor in the lowest free slot as well and
  // returns it; -1 if |fd| is empty or out of range, or the table is full.
  virtual int Dup(int fd) = 0;
  // As dup2(): makes slot |newFd| hold slot |oldFd|'s descriptor, releasing
  // what |newFd| held, in one step; returns newFd. -1 if |oldFd| is empty or
  // out of range or |newFd| is out of range (then nothing changes). With
  // oldFd == newFd and oldFd holding a descriptor, returns newFd and changes nothing.
  virtual int Dup2(int oldFd, int newFd) = 0;
  // As close(): empties slot |fd|, releasing its descriptor; 0, or -1 if it was
  // empty or out of range.
  virtual int CloseDescriptor(int fd) = 0;
  ```
- Rewrite the class comment's "--- The IFileSystem operations ---"
  paragraph: `OpenFile` hands back the open file (the `IFileSystem`
  counterpart's result) without numbering it.
- `CreatePipe` is **not** added here: `pipes--pipe-service` adds it.

### `src/components/HaisosOS/ProcessFileIO.h/.cpp`

- Remove the interim `m_openFiles` map, its mutex, and the `int`
  `OpenFile/ReadFile/WriteFile/CloseFile`.
- `OpenFile` (both overloads):
  `auto fs = RootFileSystem(); return fs ? fs->OpenFile(ResolvePath(pathname), flags[, mode]) : nullptr;`
- The table is the private members:
  ```cpp
  mutable std::mutex m_descriptorsMutex;
  // Index = slot; null = free. Grown on demand, never beyond IFileIO::kMaxDescriptors.
  std::vector<std::shared_ptr<IFileDescriptor>> m_descriptors;
  ```
- Implement the five overrides as `IFileIO.h` describes them:
  - "lowest free" is the first null entry, else the next index while
    `size() < kMaxDescriptors`;
  - `Dup2` to a slot beyond `size()` grows the vector up to `newFd + 1`,
    filling with nulls.
  - **Release outside the lock:** `Dup2`, `CloseDescriptor` and
    `ReleaseAllDescriptors` move the `shared_ptr`(s) they drop into a local
    variable under the lock and let them go only after unlocking. A
    descriptor's destructor may later block or call back (a pipe end waking
    its peer, a console flushing), and it must never run under the table's
    mutex.
- Add a public method **not** on `IFileIO`, which the process classes call:
  ```cpp
  // Releases every descriptor in the table (the table is left empty and still
  // usable). Called by the process classes when their program ends, before they
  // report finished. Releases outside the lock.
  void ReleaseAllDescriptors();
  ```
- The table works without an OS: `GetDescriptor`, `AddDescriptor`, `Dup`,
  `Dup2`, `CloseDescriptor` and `ReleaseAllDescriptors` never call
  `RootFileSystem()`. The tests rely on this (`ProcessFileIO::Create` with an
  empty `weak_ptr`).
- The class comment says it now also owns the process's descriptor table.

### `src/components/Filesystem/FilesystemUtils.h`

- `ReadWholeFile(IFileIO& io, ...)` becomes: open with `kFileOpenReadOnly`,
  return false on null, return `ReadWholeDescriptor(*file, outContent)`. It is
  the same shape as the `IFileSystem` overload.
- Remove the comment that called it interim.

### `src/components/BuiltinCommands/commands/Cat.cpp`

`CatFile`:
- `auto handle = io.OpenFile(file, kFileOpenReadOnly);`
- if `!handle`: `Permission denied`, as today;
- read with `handle->Read(...)`. A negative result is `Input/output error`,
  as today;
- remove `io.CloseFile(fd)`: the descriptor is released at end of scope.

Output and messages are byte-for-byte unchanged, so **no version bump** (the
root rule bumps only on a behaviour change) and no `--help` change.

### `src/tools/os_write_file/OSWriteFileTool.cpp`

- `auto file = io.OpenFile(path, ...)`. On null, keep the same `ToolResult`
  text "Failed to open file for writing: <path>". The `LogWarning` drops the
  `fd=%d`.
- `ssize_t written = file->Write(...)`, then `file.reset()`. Keep the same
  failure text.

The tool's schema, description and results do not change: LLM recordings stay
valid. `OSReadFileTool.cpp` needs no change (it calls
`ReadWholeFile(*context.io, ...)`).

### Releasing the table when a program ends

The rule restated: a process's descriptors are all released when its program
ends, **before it reports finished**. Whoever sees `WaitToFinish` return true
must find the table already empty.

- `src/components/BuiltinCommands/BuiltinProcess.h/.cpp`:
  - the member becomes `std::shared_ptr<ProcessFileIO> m_io`. Forward-declare
    `class ProcessFileIO;` in the header; the .cpp already includes
    `ProcessFileIO.h`. `IO()` still returns it as `std::shared_ptr<IFileIO>`.
  - In `RunThread`, call `m_io->ReleaseAllDescriptors();` after
    `m_exitStatus = status;` and before taking `m_finishedMutex` to set
    `m_finished`.
- `src/components/HaisosOS/LuaProcess.h/.cpp`:
  - `std::shared_ptr<ProcessFileIO> m_io` (the header already includes
    `ProcessFileIO.h`).
  - In `RunThread`, call `m_io->ReleaseAllDescriptors();` after the final
    `lua_close` cleanup and before the block setting `m_finished`. Wrap it in
    try/catch(...): it must run on every path out, like the finished-marking.
- `src/components/Agent/Agent.h/.cpp`: a hook on the concrete `Agent` only.
  `IAgent` is unchanged, so subagents and the LLMService do not see it.
  ```cpp
  // Runs |hook| once, on the agent's own thread, when its conversation thread
  // ends -- before the agent reports finished (WaitToFinish). If the thread has
  // already ended, runs it at once on the calling thread. Exceptions from the
  // hook are caught and logged.
  void SetFinishedHook(std::function<void()> hook);
  ```
  - New private members: `std::mutex m_finishedHookMutex`,
    `std::function<void()> m_finishedHook`, and `bool m_finishedHookTaken = false`.
  - At the end of `RunThread`, before the block that sets `m_finished`:
    1. under `m_finishedHookMutex`, set `m_finishedHookTaken = true` and move
       the hook out;
    2. after unlocking, run it if non-empty, in try/catch, logging with
       `LogError`.
  - `SetFinishedHook`, under the same mutex: if `m_finishedHookTaken`, unlock
    and run the hook now (same try/catch); else store it.
- `src/components/HaisosOS/AgentProcess.h/.cpp`:
  - the member becomes `std::shared_ptr<ProcessFileIO> m_io`.
  - In `Create`, right after `selfHandle->Set(process)` and **before**
    `process->m_agent->Post(program)`, call
    `process->m_agent->SetFinishedHook(...)` with a lambda that captures a
    `std::weak_ptr<ProcessFileIO>` of `process->m_io`, locks it and calls
    `ReleaseAllDescriptors()`.
  - The program of an agent process is its agent's conversation, so the table
    is released when that ends. For an interactive process, the input loop
    holds no descriptor in this task. `streams--console-and-start` makes the
    loop keep its own reference to its stdin descriptor, so releasing the
    table never pulls it from under the loop.

The release runs on runtime threads. It must not destroy anything that waits
for a runtime thread. Descriptors own no threads, so this holds; keep it so.

### Root rules that bite

- `ICurrentProcess::IO()` stays the only way a process reaches files: the
  table lives on `IFileIO`, behind it.
- `ReleaseAllDescriptors` and `SetFinishedHook` are **not** on any interface
  in `interfaces/`. Programs cannot call them.
- `IFileIO` still has no `Mount`/`Unmount`.
- Test fakes implementing `IFileDescriptor` have private constructors and a
  static `Create` returning `shared_ptr`.
- New test `.cpp` files go on their executable's `add_executable` line in
  that folder's `CMakeLists.txt`.

## Tests

Adapt the existing uses:
- `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`,
  `FileIOReadsAndWritesThroughTheWorkingDirectory`:
  `auto file = io->OpenFile("note.txt", ...)`; `ASSERT_NE(file, nullptr)`;
  `file->Write(...)`; `file.reset()`.
- `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`,
  `AProcessCanReadButNotWriteABuiltin`:
  `EXPECT_EQ(io->OpenFile("/bin/ls", kFileOpenWriteCreateTruncate, kFileCreateMode), nullptr)`.

New file `tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp`,
added to `HaisosOS.unittests` in `tests/unit/components/HaisosOS.unittests/CMakeLists.txt`.
- Its suite is **`HaisosOSDescriptorTableTest`**, so the filter `HaisosOS`
  selects it.
- It builds `ProcessFileIO::Create(std::weak_ptr<IHaisosOS>(), "/")`.
- It uses a test fake, `ReleaseCountingDescriptor`:
  - `Create(std::shared_ptr<std::atomic<int>> releases)`;
  - the destructor increments `*releases`;
  - `Read` returns 0, `Write` returns `count`, `IsTerminal` is false.
- Hold no other reference to a fake whose release a test counts.

| Test | Sets up | Asserts |
|------|---------|---------|
| `AddDescriptorTakesTheLowestFreeSlot` | empty table | three adds return 0, 1, 2; after `CloseDescriptor(1)` the next add returns 1; `AddDescriptor(nullptr)` returns -1 |
| `GetDescriptorReturnsWhatIsThereOrNull` | one add at 0 | `GetDescriptor(0)` is it; `GetDescriptor(1)`, `(-1)`, `(kMaxDescriptors)` are null |
| `DupSharesTheDescriptorInTheLowestFreeSlot` | A at 0 | `Dup(0)` == 1 and `GetDescriptor(1) == GetDescriptor(0)` (same pointer); `Dup(5)` == -1, `Dup(-1)` == -1 |
| `Dup2ReplacesAndReleasesWhatWasThere` | A at 0, B at 1 (B's counter) | `Dup2(0, 1)` == 1, B released once, slot 1 is A; `Dup2(0, 0)` == 0 and nothing changes; `Dup2(0, 500)` == 500 and `GetDescriptor(500)` is A; `Dup2(7, 1)` == -1 with slot 1 still A; `Dup2(0, kMaxDescriptors)` == -1 |
| `CloseDescriptorReleasesWhenTheLastSlotLetsGo` | A at 0, `Dup(0)` -> 1 | `CloseDescriptor(0)` == 0 and A not released; `CloseDescriptor(1)` == 0 and A released; `CloseDescriptor(1)` again == -1; `CloseDescriptor(-1)` == -1 |
| `TheTableHoldsAtMostkMaxDescriptors` | `kMaxDescriptors` adds | all succeed (0 .. 1023); one more `AddDescriptor` and a `Dup(0)` return -1 |
| `ReleaseAllDescriptorsEmptiesTheTable` | three fakes added | after `ReleaseAllDescriptors()` all three are released, every `GetDescriptor` is null, and the next add returns 0 |
| `OpenFileFailsWithoutAnOS` | no OS | `OpenFile("/x", kFileOpenReadOnly)` is null, and the table is still empty (`GetDescriptor(0)` null) |

In `HaisosOSTest.cpp` (suite `HaisosOSTest`), add:

| Test | Sets up | Asserts |
|------|---------|---------|
| `OpenFileHandsBackAnUnnumberedDescriptor` | `BuildOS()`; start `script.lua`; `io = process->IO()`; `auto file = io->OpenFile("x.txt", kFileOpenWriteCreateTruncate, kFileCreateMode)` | `file` not null; `io->GetDescriptor(0..3)` all null (not placed); `AddDescriptor(file)` == 0; `io->GetDescriptor(0)->Write("hi", 2)` == 2; after releasing, `x.txt` on disk holds `hi` |
| `ALuaScriptsDescriptorsAreReleasedWhenItEnds` | write `spin.lua` = `while true do end` into the test root; start it; `AddDescriptor` a counting fake into its `IO()` | not released while running (check after ~50 ms); `TriggerStop()`, `WaitToFinish(kProcessWaitMs)` true; then released == 1 (already, at the moment `WaitToFinish` returned) |
| `AnAgentsDescriptorsAreReleasedWhenItsConversationEnds` | a test console `BlockingPhysicalConsole` (new in this file: `ReadLine` waits on a condition variable until `Release()` and then returns `std::nullopt`; `Write` discards); `BuildOS(console)`; start `hello.md` with `options.interactiveAgent = true`; add a counting fake to its `IO()` | not released while the console blocks; `console->Release()`; `WaitToFinish(kProcessWaitMs)` true; released == 1. Shape the console like `ScriptedPhysicalConsole`, and release it on every path (a scope guard), or a failed assertion leaves the input loop blocked and the test hangs in teardown |

In `BuiltinCommandsTest.cpp` (suite `BuiltinCommandsTest`), add:

| Test | Sets up | Asserts |
|------|---------|---------|
| `ABuiltinsDescriptorsAreReleasedWhenItsCommandReturns` | a test `IBuiltinCommand` (`Name` "wait", `Version` "1", empty `Options`, a minimal `Help`) whose `Run` loops `while (!context.StopRequested())` sleeping 1 ms, then returns 0; `BuiltinProcess::Create(host{os, pid 1, parentPid 1, programPath "/wait"}, environment, command, {}, "/")`; add a counting fake to its `IO()` | not released while running; `TriggerStop()`; `WaitToFinish(kWaitMs)` true; released == 1 |

`tests/unit/components/Agent.unittests/AgentTest.cpp` (suite `AgentTest`,
built like `PostAndWaitToFinish`, non-interactive) gets two new tests:

| Test | Sets up | Asserts |
|------|---------|---------|
| `TheFinishedHookRunsOnceBeforeTheAgentReportsFinished` | `SetFinishedHook` incrementing an atomic, set before `Post` | after `WaitToFinish(...)` returns true the count is exactly 1 |
| `AFinishedHookSetAfterTheEndRunsAtOnce` | agent posted and waited to finish | `SetFinishedHook` runs the hook before it returns (count 1) |

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U BuiltinCommands
bash ./scripts/test_linux.sh L U Agent
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```

Before finishing, check these:
- `grep -rn "CloseFile\|m_openFiles" src interfaces tests` must find nothing.
- `grep -rn "io\.ReadFile\|io\.WriteFile\|io->ReadFile\|io->WriteFile" src tests`
  must find nothing.

## Docs

- `src/components/HaisosOS/CLAUDE.md`:
  - the "A process owns its working directory" bullet gains the descriptor
    table: slots, `Dup`/`Dup2`, `kMaxDescriptors`, `OpenFile` not placing;
  - a new bullet says every process class calls
    `ProcessFileIO::ReleaseAllDescriptors()` when its program ends, before
    reporting finished:
    - a builtin's `Run` returning, in `BuiltinProcess::RunThread`;
    - a script ending, in `LuaProcess::RunThread`;
    - an agent's conversation thread ending, through `Agent::SetFinishedHook`,
      set by `AgentProcess::Create`;
  - the `ProcessFileIO` key-class line drops "interim".
- `src/components/Agent/CLAUDE.md`: a note on `SetFinishedHook` (concrete
  `Agent` only, runs once on the agent's thread before it reports finished,
  or at once if already finished).
- Root `CLAUDE.md`, "Security": in the paragraph starting "All file access
  goes through `ICurrentProcess::IO()`", add one sentence. `IFileIO` also
  holds the process's descriptor table (0 stdin, 1 stdout, 2 stderr, ...),
  so whatever a process has open, it holds there, behind the same door.

## Acceptance

- [ ] `IFileIO` matches the signatures above exactly, including the four `static constexpr` constants. It has no `ReadFile`, `WriteFile`, `CloseFile` or `CreatePipe`.
- [ ] `OpenFile` never places a descriptor. `AddDescriptor`/`Dup` take the lowest free slot. `Dup2` replaces in one step. Every drop happens outside the table's mutex.
- [ ] `kMaxDescriptors` (1024) is enforced. Out-of-range slots are refused with -1, or null for `GetDescriptor`.
- [ ] `ReleaseAllDescriptors` is called by `BuiltinProcess`, `LuaProcess` and (through `Agent::SetFinishedHook`) `AgentProcess`. Each call comes before the process can be seen finished, as the three release tests show.
- [ ] `cat`'s output, messages and version are unchanged. `os_write_file`/`os_read_file` results and schemas are unchanged.
- [ ] No new method on any interface beyond the `IFileIO` table. `IAgent` is unchanged.
- [ ] The build is green. `L U` and `L H` pass.

## Out of scope

- Filling slots 0, 1 and 2 at start: `StartProcessOptions::stdIn/stdOut/stdErr`,
  console and empty-input descriptors (`streams--console-and-start`).
- `IFileIO::CreatePipe` and pipe descriptors (`pipes--pipe-service`).
- Builtins reading slot 0 or writing slots 1 and 2; Lua `print` or agents
  writing to descriptors (`streams--*`, `builtins--*`).
- Exit codes (`streams--exit-codes`).
- Passing descriptors to children; close-on-exec (not needed: nothing is
  inherited).
