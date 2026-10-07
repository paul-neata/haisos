# PipeService

The `IPipeService` (`interfaces/IPipeService.h`): makes unnamed pipes -- a
bounded, blocking, unidirectional byte channel, as Linux's `pipe(7)`, whose
two ends are ordinary `IFileDescriptor`s. Typically one process's stdout is
another's stdin: an end is handed to a process through
`StartProcessOptions`, or `IFileIO::CreatePipe` places both ends in the
caller's descriptor table.

## The pipe's rules

- Bounded (64 KiB, `kDefaultPipeCapacity`, unless a capacity is asked for):
  `Write` blocks while the pipe is full, `Read` blocks while it is empty.
- `Read` returns 0 (end of file) once the write end has been released and the
  buffer drained; `Write` returns `kIOBrokenPipe` once the read end has been
  released.
- A write of up to `kPipeAtomicWriteSize` (4096, POSIX `PIPE_BUF`, capped at
  the capacity) is atomic: never interleaved with another writer's bytes. A
  larger write goes in parts and may interleave.
- A `Read` or `Write` blocked on a pipe returns `kIOInterrupted` when the
  process whose runtime thread is blocked is asked to stop (`TriggerStop`; for
  an agent also `self_close`). The wait is woken by the calling thread's stop
  token (`src/components/libheaders/StopToken.h`), never polled. A thread
  carrying no token (the haisos main thread, a test's thread) is never
  interrupted.
- A call that does not have to block goes through even after a stop was
  requested; only a call that would block is interrupted (EINTR's rule).

There are no transfer threads: the writer's thread copies into the buffer, the
reader's thread copies out. Each end object is the one and only descriptor
object for its side (`Dup`, `Dup2` and children share the same `shared_ptr`),
so the buffer tracks two booleans, not counts. The ring is allocated lazily
(a pipe nobody writes to costs nothing) and freed once nobody can read it.

No `DestroyOffRuntimeThreads` deleter anywhere here: nothing owns a thread and
no destructor waits (`CloseReadEnd`/`CloseWriteEnd` only notify), so an end
may be released on any thread -- runtime threads and
`ProcessFileIO::ReleaseAllDescriptors()` included.

## Key Classes

- `PipeService` - the `IPipeService`; stateless apart from the count of pipes
  still open. The count is a `shared_ptr<std::atomic<size_t>>` shared with the
  `PipeBuffer`s (a buffer's constructor increments it, its destructor
  decrements it), so a pipe outliving its service decrements a live counter.
- `PipeBuffer` - one pipe's ring and wait sets, shared by its two ends.
- `PipeReadEnd` / `PipeWriteEnd` - the ends as `IFileDescriptor`s;
  `IsTerminal()` false, the wrong-direction call fails with `kIOError`.
