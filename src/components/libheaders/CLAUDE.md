# libheaders

Header-only C++ utilities. Not a formal component.

## Contents

- `SynchronizedQueue.h` - Thread-safe queue
- `SynchronizedQueueEx.h` - Extended synchronized queue with additional features
- `DestroyOffRuntimeThreads.h` - keeps whatever waits for a runtime thread in
  its destructor from being destroyed on one (see "Creating things" in the root
  `CLAUDE.md`). `RuntimeThreadScope` marks the thread it is declared on as a
  runtime thread -- and names it in every log line -- for as long as it lives.
  The `DestroyOffRuntimeThreads<T>` deleter destroys an object on the spot,
  unless it is released on a runtime thread: then it hands it to the
  `DestructionThread`, one thread for the whole program, started on first use
  and drained at exit, which may wait for anything. Both a hand-off and each
  destruction there are logged.
- `CrtInvalidParameterAsError.h` - on Windows, keeps the Microsoft C runtime
  from ending the whole program when one of its functions is handed an invalid
  parameter (a descriptor that is not open, a `strftime` conversion it does not
  know, a time `localtime_s` will not take): while one is in scope, the calling
  thread's invalid parameter handler just returns, so the function fails with
  its documented error value instead. Put one in scope around every CRT call
  whose arguments come from outside -- `windows/WindowsFilesystem.cpp` and
  `ls`'s time formatting do. Elsewhere it does nothing.
