# Task pipes--pipe-service: Unnamed pipes and interruptible blocking I/O

- Rock: pipes
- Depends on: streams--exit-codes (and through it fd--descriptor-objects, fd--process-table, streams--console-and-start, streams--runtime-streams)
- Size: ~900 changed lines in ~25 files (code ~480, tests ~390, docs ~30)
- Plan checked against: develop @ 0d92271, plus the plans fd--descriptor-objects, fd--process-table, streams--console-and-start, streams--runtime-streams, streams--exit-codes
- PR title: Add IPipeService, IFileIO::CreatePipe and interruptible pipe I/O

## Goal

A process can make an unnamed pipe: `IFileIO::CreatePipe()` places a read end
and a write end, two `IFileDescriptor`s, in the two lowest free slots of its
descriptor table. A pipe is a bounded buffer, as Linux's `pipe(7)`: 64 KiB
unless a capacity is asked for, written into by whoever holds the write end
and read from by whoever holds the read end -- typically two processes, each
given one end as its stdin or stdout through `StartProcessOptions`.

- `Write` blocks while the pipe is full; `Read` blocks while it is empty.
- `Read` returns 0 (end of file) once the write end has been released and the
  buffer drained; `Write` returns `kIOBrokenPipe` once the read end has been
  released. (What a program does about `kIOBrokenPipe` -- stop with 141 -- is
  the next task, `pipes--broken-pipe`; here the result is just returned.)
- A write of up to 4096 bytes is atomic: never interleaved with another
  writer's bytes (POSIX `PIPE_BUF`).
- A `Read` or `Write` blocked on a pipe returns `kIOInterrupted` when the
  process whose runtime thread is blocked is asked to stop (`TriggerStop`;
  for an agent also `self_close`), so a stuck pipeline never wedges
  `~HaisosOS`'s drain. No polling: the wait is woken by the process's stop
  token.

There are no transfer threads (decided by the user): the writer's thread copies
into the buffer, the reader's thread copies out.

## Context

Read first: the root `CLAUDE.md` ("Security: `ICurrentProcess` is the only door
out of a process", "Creating things: private constructors and `Create()`",
including "Nothing that waits for a runtime thread is destroyed on one"),
`develop-plan/goal.md` ("## Clarifications" -- pipes -- and "## Contracts"),
`src/components/ServicesCreator/CLAUDE.md`, `src/components/FileSystemService/CLAUDE.md`
(the model of a stateless service), `src/components/HaisosOS/CLAUDE.md`,
`src/components/BuiltinCommands/CLAUDE.md`, `src/components/Agent/CLAUDE.md`,
`src/components/libheaders/CLAUDE.md`.

### What earlier tasks provide (as if already on `develop`)

- fd--descriptor-objects: `interfaces/IFileDescriptor.h` --
  `class IFileDescriptor { ssize_t Read(void* buf, size_t count); ssize_t Write(const void* buf, size_t count); bool IsTerminal() const; }`
  and the namespace-scope `constexpr ssize_t` results `kIOError` (-1),
  `kIOBrokenPipe` (-2), `kIOInterrupted` (-3), reserved for pipes; the
  `ssize_t` alias for Windows lives there. A descriptor is closed when its last
  `shared_ptr` is released.
- fd--process-table: in `class IFileIO`, `static constexpr int kStdIn = 0,
  kStdOut = 1, kStdErr = 2, kMaxDescriptors = 1024`; `GetDescriptor(int) const`,
  `AddDescriptor(std::shared_ptr<IFileDescriptor>) -> int` (lowest free slot,
  -1 when full or null), `Dup`, `Dup2`, `CloseDescriptor(int) -> int`.
  `ProcessFileIO` holds the table (`m_descriptors`, `m_descriptorsMutex`,
  releases outside the lock), works without an OS, and has
  `ProcessFileIO::ReleaseAllDescriptors()`, called by `BuiltinProcess::RunThread`,
  `LuaProcess::RunThread` and, through `Agent::SetFinishedHook`, by
  `AgentProcess` when the program ends, before the process reports finished.
  fd--process-table explicitly leaves `IFileIO::CreatePipe` to this task.
- streams--console-and-start: `StartProcessOptions { stdIn, stdOut, stdErr,
  interactive }` resolved by `HaisosOS::ResolveStandardStreams`;
  `ProcessFileIO::InstallStandardStreams(in, out, err)`; console descriptors
  (`src/components/Console/ConsoleDescriptors.h`: `ConsoleInputDescriptor` --
  its blocking read stays uninterruptible -- `ConsoleOutputDescriptor`,
  `ConsoleErrorDescriptor`, `EmptyInputDescriptor`); `IPhysicalConsole::WriteError`;
  `BuiltinContext` writing slots 1/2 through its private
  `bool WriteAll(IFileDescriptor* descriptor, const std::string& bytes)`, with
  stdout block-buffered (`kBuiltinOutBufferSize`, 4096) when it is not a
  terminal; `tests/mocks/MockFileDescriptor.h`.
- streams--runtime-streams: `ProcessAgentConsole` (the agent's
  `IAgentConsole`, writing to slots 1/2 through the `CurrentProcessHandle`);
  `LuaProcess::WriteToDescriptor(int fd, const std::string&)`;
  `src/components/libheaders/DescriptorLineReader.h` (a failed read,
  `kIOInterrupted` included, ends its input); `AgentInputLoop::Create(agent, input)`
  reading the process's stdin descriptor, stopping the agent at end of input.
- streams--exit-codes: `IProcess::ExitCode()`; `src/components/libheaders/ExitCodes.h`
  (`kExitCodeBrokenPipe` 141, `kExitCodeStopped` 143, `kExitCodeNotStarted`
  127, `enum class ProcessEnd { Exited, Stopped, BrokenPipe }`, `ExitCodeFor`);
  a builtin or Lua script stopped by `TriggerStop` exits 143.

If the code on `develop` names any of these differently, the code wins: use
its names and keep this plan's behaviour.

### What exists at 0d92271

- `IServicesCreator` (`interfaces/IServicesCreator.h`) creates the
  filesystem, network and LLM services; `ServicesCreator` implements it, one
  `Create*` per service, each returning `X::Create()`.
- `HaisosOS::Create` creates its network and LLM services from its services
  creator; a sub-OS is `HaisosOS::Create` again with the creator it is given.
- `BuiltinProcess`, `LuaProcess` and `Agent` each run their program on one
  thread opened by a `RuntimeThreadScope`; `TriggerStop` sets a flag
  (`BuiltinProcess::m_stopRequested`, `LuaProcess::Kill` -> `m_killed`,
  `Agent::m_stopRequested` + closing its command queue). `self_close` calls
  `Agent::TriggerStop`. `AgentInputLoop` runs a second runtime thread for an
  interactive agent, holding the `IAgent`.
- `src/components/libheaders/` is header-only, tested by
  `tests/unit/components/libheaders.unittests/` (links `gtest_main Logger`).

## Changes

### `interfaces/IPipeService.h` (new)

```cpp
#pragma once
#include <cstddef>
#include <memory>
#include "IFileDescriptor.h"

namespace Haisos {

// Linux's default pipe capacity (pipe(7)): what CreatePipe(0) gives.
constexpr size_t kDefaultPipeCapacity = 65536;
// Writes of up to this many bytes are atomic (POSIX PIPE_BUF).
constexpr size_t kPipeAtomicWriteSize = 4096;

struct PipeEnds {
    std::shared_ptr<IFileDescriptor> readEnd;
    std::shared_ptr<IFileDescriptor> writeEnd;
};

class IPipeService {
public:
    virtual ~IPipeService() = default;
    virtual PipeEnds CreatePipe(size_t capacity = 0) = 0;
    virtual size_t OpenPipeCount() const = 0;
};

}
```

Document on the interface, in comments: what a pipe is (bounded, blocking,
unidirectional; both ends are descriptors, closed when their last holder lets
go); `capacity` 0 means `kDefaultPipeCapacity`, any other value is taken as
given (a heredoc asks for the size of its text); the full blocking / end of
file / broken pipe / atomic write / interruption rules (from "The pipe's
rules" below, in prose), including that a thread with no stop token (the
haisos main thread, a test's thread) is never interrupted;
`OpenPipeCount()` is the number of pipes this service created that still have
at least one end held (for tests and diagnostics); a pipe does not depend on
the service that made it -- it keeps working after the service is gone and
may be handed to a process of another OS (a sub-OS). `CreatePipe` never
fails (both ends are always non-null).

### `interfaces/IServicesCreator.h`

Add `#include "IPipeService.h"` and, after `CreateFileSystemService`:
`virtual std::shared_ptr<IPipeService> CreatePipeService() = 0;` -- comment:
each call is a new, independent service; an OS creates one for itself.

### `interfaces/IHaisosOS.h`

Add `virtual std::shared_ptr<IPipeService> GetPipeService() = 0;` after
`GetServicesCreator()`. Comment: the OS's own pipe service, created from its
services creator when the OS is created (a sub-OS has its own); never null;
reached by a process through `ICurrentProcess::OS()` -- in practice through
`IFileIO::CreatePipe`, which places both ends in the caller's table.

### `interfaces/IFileIO.h`

Add, after `CloseDescriptor` in the descriptor-table section (include
`<utility>`; `<optional>` is already there):

```cpp
// As pipe(): makes a pipe through the OS's pipe service (see IPipeService)
// and places its read end and write end in the two lowest free slots, read end
// first. Returns {readSlot, writeSlot}, or nullopt -- the table left as it was
// -- when fewer than two slots are free or the OS is gone. capacity 0 is
// kDefaultPipeCapacity.
virtual std::optional<std::pair<int, int>> CreatePipe(size_t capacity = 0) = 0;
```

### `src/components/libheaders/StopToken.h` (new, header-only)

The per-runtime-thread stop token that makes a blocked pipe call
interruptible without polling. Three classes, namespace `Haisos`:

```cpp
class StopToken {
public:
    static std::shared_ptr<StopToken> Create();
    // Idempotent. Sets the flag, then runs every registered callback once.
    void RequestStop();
    bool StopRequested() const;
    // The token installed on the calling thread by a StopTokenScope, or null.
    static std::shared_ptr<StopToken> Current();
private:
    StopToken() = default;
    friend class StopCallback;
    // std::mutex m_mutex; std::atomic<bool> m_stopRequested{false};
    // std::map<uint64_t, std::function<void()>> m_callbacks; uint64_t m_nextId = 0;
};

// Registers onStop with token for its own lifetime (as C++20 std::stop_callback).
class StopCallback {
public:
    StopCallback(std::shared_ptr<StopToken> token, std::function<void()> onStop);
    ~StopCallback();
    StopCallback(const StopCallback&) = delete;
    StopCallback& operator=(const StopCallback&) = delete;
};

// Installs token as StopToken::Current() on the calling thread for its
// lifetime, restoring the previous one after.
class StopTokenScope {
public:
    explicit StopTokenScope(std::shared_ptr<StopToken> token);
    ~StopTokenScope();
    StopTokenScope(const StopTokenScope&) = delete;
    StopTokenScope& operator=(const StopTokenScope&) = delete;
};
```

Rules (write them as comments too):
- `Current()` is a `thread_local std::shared_ptr<StopToken>` behind a static
  function (as `RuntimeThreadScope::Flag()` does in `DestroyOffRuntimeThreads.h`).
- `StopCallback` with a null token does nothing. With a token whose stop is
  already requested, it runs `onStop` at once, on the calling thread, before
  returning. Otherwise it registers `onStop` under the token's mutex.
- `RequestStop()`: under the token's mutex, return if already requested; set
  the atomic flag; run every registered callback **while still holding the
  token's mutex**. Holding it is what lets `~StopCallback` (which takes the same
  mutex to unregister) guarantee that once it returns, its callback is
  neither running nor will ever run -- so a callback may capture raw pointers
  to the waiter's state.
- Consequence, documented: a callback must not call into the same token (no
  `RequestStop`, no new `StopCallback` on it), and may take only locks that are
  never held while a `StopCallback` on that token is constructed or destroyed.
  The pipe obeys this: it constructs its `StopCallback` before taking its own
  mutex and destroys it after releasing it.
- No threads, no timers, no logging. Not an interface implementation, but in
  the house style anyway: private constructor, `Create()`, held by
  `shared_ptr` (the process and the thread-local both hold it).

### The runtimes install and signal their token

Every runtime thread gets the token of the process it runs for; `TriggerStop`
signals it.

- `src/components/BuiltinCommands/BuiltinProcess.h/.cpp`: member
  `std::shared_ptr<StopToken> m_stopToken = StopToken::Create();`.
  `RunThread` declares `StopTokenScope stopTokenScope(m_stopToken);` right
  after its `RuntimeThreadScope`. `TriggerStop()` sets `m_stopRequested`, then
  calls `m_stopToken->RequestStop()`.
- `src/components/HaisosOS/LuaProcess.h/.cpp`: the same member;
  `StopTokenScope` right after the `RuntimeThreadScope` in `RunThread`;
  `Kill()` sets `m_killed`, then calls `m_stopToken->RequestStop()`
  (`TriggerStop` already calls `Kill`). Lua `exit()` (streams--exit-codes)
  does not touch the token: it ends the script by unwinding, not by blocking.
- `src/components/Agent/Agent.h/.cpp`: the same member, plus a public
  `std::shared_ptr<StopToken> GetStopToken() const;` on the concrete `Agent`
  (not on `IAgent`). `RunThread` installs it after its `RuntimeThreadScope`;
  `TriggerStop()` calls `m_stopToken->RequestStop()` after setting
  `m_stopRequested` and closing the queue. So `self_close`, the input loop's
  stop at end of input and `AgentProcess::TriggerStop` all interrupt a blocked
  write of the agent (`ProcessAgentConsole` writing to a full stdout pipe) --
  an agent asked to stop no longer waits on a full pipe; what could not be
  written is dropped.
- `src/components/HaisosOS/AgentInputLoop.h/.cpp`: `Create` becomes
  `Create(std::shared_ptr<IAgent> agent, std::shared_ptr<IFileDescriptor> input, std::shared_ptr<StopToken> stopToken = nullptr)`,
  the token kept as a member; `Run` installs it with a `StopTokenScope` after
  its `RuntimeThreadScope` (a null token installs nothing interruptible).
  `AgentProcess::Create` passes `agent->GetStopToken()` (its `m_agent` is the
  concrete `Agent`). Effect: an interactive agent whose stdin is a pipe stops
  reading as soon as it closes itself or is asked to stop -- the read returns
  `kIOInterrupted`, `DescriptorLineReader` reports end of input, and the loop
  ends as at end of input. Console input stays uninterruptible (goal.md D7:
  the console's blocking read is the known exception).

### `src/components/PipeService/` (new component)

Files: `PipeService.h`, `PipeService.cpp`, `Pipe.h`, `Pipe.cpp`,
`CMakeLists.txt`, `CLAUDE.md`.

`Pipe.h` / `Pipe.cpp` -- the pipe itself:

```cpp
class PipeBuffer {
public:
    static std::shared_ptr<PipeBuffer> Create(size_t capacity, std::shared_ptr<std::atomic<size_t>> openPipes);
    ~PipeBuffer();                                   // decrements *openPipes
    ssize_t Read(void* buf, size_t count);
    ssize_t Write(const void* buf, size_t count);
    void CloseReadEnd();
    void CloseWriteEnd();
    size_t Capacity() const;
private:
    PipeBuffer(size_t capacity, std::shared_ptr<std::atomic<size_t>> openPipes);  // increments *openPipes
    // std::mutex m_mutex; std::condition_variable m_canRead, m_canWrite;
    // std::vector<char> m_storage (the ring, sized to capacity on the first write
    // that copies bytes in, freed when the read end closes); size_t m_head = 0, m_size = 0;
    // bool m_readEndOpen = true, m_writeEndOpen = true; size_t m_capacity;
};

class PipeReadEnd : public IFileDescriptor {
public:
    static std::shared_ptr<PipeReadEnd> Create(std::shared_ptr<PipeBuffer> buffer);
    ~PipeReadEnd() override;                         // buffer->CloseReadEnd()
    ssize_t Read(void* buf, size_t count) override;  // buffer->Read
    ssize_t Write(const void* buf, size_t count) override; // kIOError: not open for writing
    bool IsTerminal() const override;                // false
private:
    explicit PipeReadEnd(std::shared_ptr<PipeBuffer> buffer);
};

class PipeWriteEnd : public IFileDescriptor {
public:
    static std::shared_ptr<PipeWriteEnd> Create(std::shared_ptr<PipeBuffer> buffer);
    ~PipeWriteEnd() override;                        // buffer->CloseWriteEnd()
    ssize_t Read(void* buf, size_t count) override;  // kIOError: not open for reading
    ssize_t Write(const void* buf, size_t count) override; // buffer->Write
    bool IsTerminal() const override;                // false
private:
    explicit PipeWriteEnd(std::shared_ptr<PipeBuffer> buffer);
};
```

Each end object is the one and only descriptor object for its side: `Dup`,
`Dup2` and passing it to a child share the same `shared_ptr`, so "every read
end released" is exactly "the `PipeReadEnd` was destroyed". That is why the
buffer tracks two booleans, not counts. The ring is allocated lazily so a pipe
nobody writes to costs no 64 KiB, and freed once nobody can read it.

**The pipe's rules** (`PipeBuffer::Read` / `Write`; all under `m_mutex`):

`Read(buf, count)`:
1. `count == 0`: return 0 at once.
2. `token = StopToken::Current()`; construct `StopCallback wake(token, f)`
   where `f` locks `m_mutex`, then `notify_all` on both `m_canRead` and
   `m_canWrite` (the lock makes a wake between the waiter's check and its wait
   impossible to miss). Construct it **before** locking `m_mutex`, and let it be
   destroyed **after** the lock is released (declare it first).
3. Lock; loop:
   - `m_size > 0`: copy `n = min(count, m_size)` bytes out (wrapping around
     the ring), `m_size -= n`, `notify_all` on `m_canWrite`, return `n`.
     Data is returned even when a stop has been requested; a read never waits
     for more once some is there.
   - `!m_writeEndOpen`: return 0 (end of file).
   - `token && token->StopRequested()`: return `kIOInterrupted`.
   - otherwise `m_canRead.wait(lock)` and loop.

`Write(buf, count)`:
1. `count == 0`: return 0 at once (as Linux, even with no reader).
2. The same `StopCallback` as `Read`.
3. `atomic = count <= std::min(kPipeAtomicWriteSize, m_capacity)`;
   `written = 0`. Lock; loop:
   - `!m_readEndOpen`: return `written > 0 ? written : kIOBrokenPipe`.
   - `room = m_capacity - m_size`; `need = atomic ? count : 1`.
   - `room >= need`: allocate `m_storage` if still empty; copy
     `n = min(room, count - written)` bytes in (wrapping), `m_size += n`,
     `written += n`, `notify_all` on `m_canRead`; if `written == count`
     return `written`; else loop (the pipe is now full, so the next pass
     waits).
   - `token && token->StopRequested()`: return
     `written > 0 ? written : kIOInterrupted`.
   - otherwise `m_canWrite.wait(lock)` and loop.
   A write that does not have to wait goes through even after a stop was
   requested -- only a call that would block is interrupted (EINTR's rule).
   A write larger than the room left is written in parts, blocking between
   them; parts of two large writes may interleave, a write of
   `<= min(4096, capacity)` bytes never is split. (Capping the atomic size at
   the capacity keeps a 5-byte heredoc pipe from blocking a 10-byte write
   forever.) A partial count followed by the caller's next write (the
   `WriteAll` loops) gets the negative result on that next call.

`CloseReadEnd()`: lock; `m_readEndOpen = false`; drop the buffered bytes and
free `m_storage` (nobody can read them); `notify_all` on `m_canWrite`.
`CloseWriteEnd()`: lock; `m_writeEndOpen = false`; `notify_all` on
`m_canRead`. Neither waits for anything, so ends may be released on any
thread -- runtime threads, and `ProcessFileIO::ReleaseAllDescriptors()`
included: no `DestroyOffRuntimeThreads` deleter is needed (say so in a
comment and in the CLAUDE.md). Log at `LogVerboseDebug` on create/close and
`LogDebug` when a write finds no reader or a call is interrupted -- nothing at
higher levels (pipelines make many pipes).

`PipeService.h` / `.cpp`:

```cpp
class PipeService : public IPipeService {
public:
    static std::shared_ptr<PipeService> Create();
    ~PipeService() override;
    PipeEnds CreatePipe(size_t capacity = 0) override;
    size_t OpenPipeCount() const override;
private:
    PipeService();
    std::shared_ptr<std::atomic<size_t>> m_openPipes;
};
```

`CreatePipe`: capacity 0 -> `kDefaultPipeCapacity`; build one `PipeBuffer`
(its constructor increments `*m_openPipes`, its destructor decrements it) and
both ends around it. The counter is shared with the buffers, so a pipe
outliving the service decrements a live counter. The service holds nothing
else (no list of pipes). Repeat the interface's default (`= 0`) on the
override.

`CMakeLists.txt`:
`add_library(PipeService STATIC PipeService.cpp Pipe.cpp)`, include
directories `${CMAKE_SOURCE_DIR}/interfaces` and
`${CMAKE_SOURCE_DIR}/src/components/PipeService`, link `Logger`,
`cxx_std_17` -- shaped like `src/components/FileSystemService/CMakeLists.txt`.

`CLAUDE.md`: what the component is (the `IPipeService`; stateless apart from
the open-pipe counter), the rules above in short, why no transfer threads and
no `DestroyOffRuntimeThreads`, how interruption works (`StopToken` from
libheaders; a thread with none is not interruptible), and Key Classes
(`PipeService`, `PipeBuffer`, `PipeReadEnd`, `PipeWriteEnd`).

### Wiring

- `CMakeLists.txt` (root): `add_subdirectory(src/components/PipeService)`
  before `src/components/ServicesCreator`.
- `src/components/ServicesCreator/ServicesCreator.h/.cpp`: override
  `CreatePipeService()` returning `PipeService::Create()`; include
  `src/components/PipeService/PipeService.h`. Its `CMakeLists.txt`: add
  `PipeService` to `target_link_libraries`.
- `src/components/HaisosOS/HaisosOS.h/.cpp`: `HaisosOS::Create` calls
  `servicesCreator->CreatePipeService()` beside the network service and passes
  it to the private constructor (new parameter
  `std::shared_ptr<IPipeService> pipeService`, after `llmService`); member
  `m_pipeService`; `GetPipeService()` override returns it. A sub-OS gets its
  own through `HaisosOS::Create`. Nothing else in the OS uses it.
- `src/components/HaisosOS/ProcessFileIO.h/.cpp`: override
  `std::optional<std::pair<int, int>> CreatePipe(size_t capacity = 0) override;`:
  1. `os = m_os.lock()`; null -> nullopt. `service = os->GetPipeService()`;
     null -> nullopt.
  2. `ends = service->CreatePipe(capacity)`.
  3. `r = AddDescriptor(ends.readEnd)`; `r < 0` -> nullopt (the pipe is
     released with `ends`).
  4. `w = AddDescriptor(ends.writeEnd)`; `w < 0` -> `CloseDescriptor(r)`,
     nullopt.
  5. return `{r, w}`.
  `AddDescriptor` takes the lowest free slot, so this gives the two lowest
  free slots, read end first. Unlike the other table operations it needs the
  OS. The process reaches the service through its OS -- `ICurrentProcess::OS()`,
  the one door -- and nothing in a process holds a pipe service of its own.

### Rules that bite (restated)

- `ICurrentProcess` is the only door out of a process: `CreatePipe` reaches
  the pipe service through `OS()`; no process, runtime or tool holds an
  `IPipeService` or an `IHaisosOS` of its own. Pipe ends are descriptors in
  the process's table like any other. No tool or Lua global gains pipes
  (goal.md: no tool schema change).
- Every class implementing an interface (`PipeService`, `PipeReadEnd`,
  `PipeWriteEnd`) has private constructors and a static `Create()` returning
  `shared_ptr`; so do `PipeBuffer` and `StopToken`. Nothing is held by value
  or `unique_ptr`.
- `DestroyOffRuntimeThreads`: not needed for anything new -- no new class owns
  a thread or waits in its destructor. Do not add a wait to any destructor,
  and keep the existing deleters and `RuntimeThreadScope`s as they are.
- New files are listed in their `CMakeLists.txt`; the new test executable is
  added to `tests/unit/CMakeLists.txt`.
- Paths in comments and docs relative to the repo root.

## Tests

The script's filter must be a substring of the executable's name
(case-insensitive) **and** of the gtest `Suite.Name` (case-sensitive). Every
helper thread is joined; every "it blocks" check is "has not returned after
~100 ms" followed by unblocking it -- never an unbounded wait that could hang
the run.

### `tests/unit/components/PipeService.unittests/` (new executable)

`CMakeLists.txt`: `add_executable(PipeService.unittests PipeServiceTest.cpp)`,
link `gtest_main PipeService`, `cxx_std_17`; add
`add_subdirectory(components/PipeService.unittests)` to
`tests/unit/CMakeLists.txt`.

`PipeServiceTest.cpp`, fixture `PipeServiceTest` (holds `PipeService::Create()`):

- `BytesComeOutInTheOrderTheyWentIn` -- writes "ab", "cd", "ef"; reads of 4
  then 4 give "abcd", "ef" (a read returns what is there, short).
- `ReadBlocksWhileEmpty` -- a thread reads; after 100 ms it has not returned;
  write "x"; it returns 1 with "x".
- `WriteBlocksWhileFullAndResumesAsTheReaderDrains` -- capacity 8; write 8
  returns 8; a thread writes 4 more and has not returned after 100 ms; read 4;
  the thread returns 4; the next reads give the 12 bytes in order.
- `ALargeWriteGoesInPartsAndBlocksBetweenThem` -- capacity 16, a thread
  writes 100 bytes; the main thread reads until 100 bytes have arrived, in
  order; the write returns 100.
- `ReadReturnsEndOfFileOnceTheWriteEndIsReleasedAndDrained` -- write "hi",
  release the write end; read gives "hi", then 0, then 0 again.
- `ReleasingTheWriteEndWakesABlockedReader` -- a thread blocks in `Read`;
  release the write end; it returns 0.
- `WriteFailsWithBrokenPipeOnceTheReadEndIsReleased` -- release the read end;
  `Write` returns `kIOBrokenPipe`; a write of 0 bytes returns 0.
- `ReleasingTheReadEndWakesABlockedWriter` -- capacity 4, full; a thread
  writes 4 more and blocks; release the read end; it returns `kIOBrokenPipe`.
- `SmallWritesFromTwoWritersAreNeverInterleaved` -- capacity 4096; two
  threads each write 500 chunks of 4096 bytes ('A'*4096 and 'B'*4096) through
  the same write end; the main thread reads everything (until both threads are
  done and the write end released, then until 0) and checks every aligned
  4096-byte block is all 'A' or all 'B', with 500 of each.
- `CapacityZeroMeansTheDefault` -- `CreatePipe()`: 65536 bytes go in without
  blocking, one more byte blocks (thread, 100 ms, then drain one byte).
- `TheCapacityAskedForIsTheCapacity` -- `CreatePipe(10)`: 10 bytes go in at
  once, an 11th blocks; a 20-byte write on a 10-byte pipe completes while the
  reader drains (the atomic size is capped at the capacity).
- `ABlockedReadIsInterruptedByItsThreadsStopToken` -- a thread installs a
  `StopTokenScope` with a fresh `StopToken` and reads an empty pipe; after
  100 ms `RequestStop()`; the read returns `kIOInterrupted` within 1 s.
- `ABlockedWriteIsInterruptedByItsThreadsStopToken` -- the same on a full
  pipe (capacity 4): returns `kIOInterrupted`; a partly done write (8 bytes on
  a 4-byte pipe, read 4 while it waits) returns the part written instead.
- `AnotherThreadsStopDoesNotInterruptAReader` -- reader A (token a) blocked;
  `b->RequestStop()` for another token b; A has still not returned after
  100 ms; a write wakes it with data.
- `AStopAlreadyRequestedInterruptsOnlyACallThatWouldBlock` -- token stopped
  before the calls: a read with data returns the data; a read on an empty pipe
  returns `kIOInterrupted` at once; a write with room goes through.
- `ManyPipesAreIndependentAndCounted` -- create 1000 pipes; `OpenPipeCount()`
  is 1000; write the index into each and read it back from each; release only
  the read ends -> still 1000; release everything -> 0.
- `APipeOutlivesItsService` -- make a pipe, release the service, write and
  read through the ends, release them (no crash).
- `EndsAreNotTerminalsAndWorkOneWay` -- `IsTerminal()` false on both; `Read`
  on the write end and `Write` on the read end return `kIOError`.

Run: `bash ./scripts/test_linux.sh L U PipeService`

### `tests/unit/components/libheaders.unittests/StopTokenTest.cpp` (new)

Add to the `add_executable` of `libheaders.unittests`. `TEST(StopTokenTest, ...)`:

- `CurrentIsNullWithoutAScope`.
- `AScopeInstallsAndRestoresTheToken` -- nested scopes restore the outer
  one; another thread sees null.
- `RequestStopRunsEachCallbackOnce` -- two callbacks, two `RequestStop()`
  calls: each ran once; `StopRequested()` true.
- `ACallbackRegisteredAfterTheStopRunsAtOnce`.
- `ADestroyedCallbackNeverRuns` -- register, destroy, stop: not run.
- `ANullTokenCallbackDoesNothing`.

Run (the suite name cannot contain the executable's name, so call it
directly): `./output/linux/libheaders.unittests --gtest_filter='StopTokenTest.*'`

### `tests/unit/components/HaisosOS.unittests/HaisosOSPipeTest.cpp` (new)

Add to `HaisosOS.unittests`'s `add_executable`. Fixture `HaisosOSPipeTest`
(the name contains `HaisosOS`, so the filter below selects it): an in-memory
root (`CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem()`),
`/bin` with every builtin placed by `CreateBuiltinConfigurator()`, a small
capturing `IPhysicalConsole` of its own (`Write` and `WriteError` captured
separately; `ReadLine` returns nullopt), an environment with `HAISOS_ENDPOINT`
`http://127.0.0.1:9999/api/chat` (unreachable, as `HaisosOSTest`'s
`kUnreachableEndpoint`) and `HAISOS_MODEL` `llama3`, and the OS from
`CreateFactory()->CreateHaisosOS(...)`. Process waits are bounded (30 s).
`pipes--broken-pipe` adds more cases to this file.

- `EachOSHasAPipeServiceOfItsOwn` -- `GetPipeService()` non-null and the same
  on two calls; a sub-OS's is a different object.
- `CreatePipeTakesTheTwoLowestFreeSlots` -- `ProcessFileIO::Create(os, "/")`
  (an empty table); add three placeholders (the ends of a pipe from the
  service and a dup) -> 0, 1, 2; `CloseDescriptor(1)`; `CreatePipe()` returns
  `{1, 3}`; `OpenPipeCount()` went up by one.
- `CreatePipeMovesBytesFromTheWriteSlotToTheReadSlot` -- write "hello" via
  `GetDescriptor(w)->Write`, read it via `GetDescriptor(r)`; `CloseDescriptor(w)`
  -> the next read returns 0; `CloseDescriptor(r)` -> `OpenPipeCount()` back
  where it was.
- `CreatePipeFailsWhenFewerThanTwoSlotsAreFree` -- fill the table to
  `IFileIO::kMaxDescriptors - 1` slots (one `AddDescriptor`, then `Dup`s);
  `CreatePipe()` is nullopt; `AddDescriptor` then returns
  `kMaxDescriptors - 1` (the slot was given back); `OpenPipeCount()` is
  unchanged.
- `CreatePipeWithoutAnOSFails` -- `ProcessFileIO::Create(std::weak_ptr<IHaisosOS>(), "/")`
  -> nullopt.
- `AReaderSeesEndOfFileWhenTheWritersProgramEnds` -- pipe from the service;
  start `/bin/echo hi` with `options.stdOut` = the write end; release the
  test's copy; read until 0: "hi\n"; `ExitCode()` 0.
- `ABuiltinBlockedOnAFullPipeStopsWhenAskedTo` -- start `/bin/echo` with one
  argument of 100000 'x' and `stdOut` = a write end (the test keeps the read
  end, unread, and releases its copy of the write end); `WaitToFinish(200)` is
  false (blocked); `TriggerStop()`; `WaitToFinish(5000)` is true;
  `ExitCode()` is 143 (`kExitCodeStopped`); the read end then yields exactly
  65536 bytes, then 0.
- `AStopOfAnInteractiveAgentInterruptsItsPipedStdin` -- `/a.md` ("Say
  hello.") with `interactive = true` and `stdIn` = a read end whose write end
  the test keeps open and silent; let it fail its LLM call (unreachable);
  `TriggerStop()`; the process finishes within 5 s. Release the test's write
  end on every path (a scope guard), or a failed assertion leaves the input
  loop blocked and the test hangs in teardown.

Run: `bash ./scripts/test_linux.sh L U HaisosOS`

### `tests/unit/components/ServicesCreator.unittests/ServicesCreatorTest.cpp`

- `ServicesCreatorTest.CreatePipeServiceCreatesWorkingPipes` -- two calls give
  two different services; a pipe from one carries "x"; `OpenPipeCount()` 1,
  then 0 after releasing both ends.

Run: `bash ./scripts/test_linux.sh L U ServicesCreator`

### Whole run

`bash ./scripts/build_linux_on_linux.sh`, then `bash ./scripts/test_linux.sh L U`
(every unit test, the existing ones unchanged), then `bash ./scripts/test_linux.sh L "*"`.

## Docs

- Root `CLAUDE.md`: Directory Structure -- add `PipeService/` to the
  component list; the `interfaces/` line -- add `IPipeService.h [PipeEnds]`;
  the component table -- a **PipeService** row ("`IPipeService`: unnamed
  pipes -- bounded, blocking, one-way, both ends descriptors; no threads");
  the ServicesCreator row -- add `IPipeService`; in "Security" one sentence:
  a process makes pipes only through `IFileIO::CreatePipe`, which reaches the
  OS's pipe service through `OS()`.
- `src/components/PipeService/CLAUDE.md` (new, above).
- `src/components/ServicesCreator/CLAUDE.md`: `CreatePipeService`.
- `src/components/HaisosOS/CLAUDE.md`: the OS creates its `IPipeService`
  (`GetPipeService()`); `ProcessFileIO::CreatePipe`; every runtime thread
  (builtin, Lua, agent, input loop) carries its process's `StopToken`, so a
  blocked pipe call is interrupted by `TriggerStop`.
- `src/components/libheaders/CLAUDE.md`: `StopToken.h` -- `StopToken`,
  `StopCallback`, `StopTokenScope`, the callback rules.
- `src/components/BuiltinCommands/CLAUDE.md`: `BuiltinProcess` installs the
  stop token.
- `src/components/Agent/CLAUDE.md`: `Agent::GetStopToken()`, installed on the
  agent's thread, signalled by `TriggerStop` (and so by `self_close`).

## Acceptance

- [ ] `interfaces/IPipeService.h` matches goal.md's Contracts exactly
      (`PipeEnds`, `IPipeService::CreatePipe(size_t capacity = 0)`,
      `OpenPipeCount() const`, `kDefaultPipeCapacity` 65536).
- [ ] `IServicesCreator::CreatePipeService()`, `IHaisosOS::GetPipeService()`,
      `IFileIO::CreatePipe(size_t capacity = 0) -> std::optional<std::pair<int,int>>`
      exist; `ServicesCreator`, `HaisosOS`, `ProcessFileIO` override them.
- [ ] No thread, timer, sleep or timed wait in `src/components/PipeService/`
      or `StopToken.h`; waits are `condition_variable::wait` woken by data,
      room, a released end or a `StopCallback`.
- [ ] `StopCallback` is constructed before and destroyed after the pipe's
      mutex is held; `RequestStop` runs callbacks under the token's mutex.
- [ ] Atomic writes: `count <= min(4096, capacity)` is copied in one piece.
- [ ] `BuiltinProcess`, `LuaProcess`, `Agent` and `AgentInputLoop` install a
      `StopTokenScope`; `BuiltinProcess::TriggerStop`, `LuaProcess::Kill`,
      `Agent::TriggerStop` call `RequestStop()`.
- [ ] Private constructors + `Create()` for every new class; no new
      destructor waits.
- [ ] New CMake targets and test files registered; the `PipeService`,
      `HaisosOS` and `ServicesCreator` filters select the new tests; all unit
      tests pass.
- [ ] Docs updated as listed.

## Out of scope

- Stopping a program with exit code 141 on `kIOBrokenPipe`
  (`ICurrentProcess::StopForBrokenPipe`): `pipes--broken-pipe`.
- Named pipes, `/dev/fd/N`, poll/select, non-blocking I/O, `F_SETPIPE_SZ`
  after creation, a cap on pipe capacity or on the number of pipes (the
  descriptor limit bounds them per process).
- Making the console's blocking line read interruptible.
- Pipelines, heredocs and redirections in a shell (hsh--executor), `cat`
  reading stdin and `ls` into a pipe (builtins rock).
- Passing descriptors through `os_start_process`; any tool schema or system
  prompt change.
