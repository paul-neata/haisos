# libheaders

Header-only C++ utilities. Not a formal component.

## Contents

- `SynchronizedQueue.h` - Thread-safe queue
- `SynchronizedQueueEx.h` - Extended synchronized queue with additional features
- `SanitizeUserInput.h` - Best-effort stripping of prompt-injection patterns from
  free-form text before it reaches an LLM. It is a **lossy denylist**: it drops
  whole lines matching known injection phrases, deletes everything between `<`
  and `>`, and caps the result at 64KB. That makes it wrong for anything that
  must survive verbatim -- file contents, tool output, or a program's own text
  would be silently corrupted -- and, being a denylist, it is easily evaded by
  rewording. Treat it as one defence among several, not a guarantee.
- `DestroyOffRuntimeThreads.h` - keeps whatever waits for a runtime thread in
  its destructor from being destroyed on one (see "Creating things" in the root
  `CLAUDE.md`). `RuntimeThreadScope` marks the thread it is declared on as a
  runtime thread -- and names it in every log line -- for as long as it lives.
  The `DestroyOffRuntimeThreads<T>` deleter destroys an object on the spot,
  unless it is released on a runtime thread: then it hands it to the
  `DestructionThread`, one thread for the whole program, started on first use
  and drained at exit, which may wait for anything. Both a hand-off and each
  destruction there are logged.
