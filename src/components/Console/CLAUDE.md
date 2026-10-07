# Console

Asynchronous, physical console output and line input, the descriptors a process
sees the console through, plus the adapters that give an agent without a
process a view onto it (or onto nothing but memory).

## Responsibilities

- Queues output writes and processes the queue on a background thread, so
  writers never block on the terminal
- Writes raw: `Write(bytes)`/`WriteError(bytes)` put exactly those bytes on the
  host's stdout / stderr, adding no newline, and flush the stream after every
  write, so a prompt without a newline shows at once
- Reads lines of input (`ReadLine`, blocking, from stdin), which reaches an
  interactive process's agent only through the console input descriptor and the
  input loop reading the process's stdin (see `AgentInputLoop` in the HaisosOS
  component)
- Provides the console descriptors (`ConsoleDescriptors.h`), the only way the
  console reaches a process: an OS hands them to it as its descriptors 0, 1
  and 2 (see `StartProcessOptions` in `interfaces/IHaisosOS.h`)
- Provides a standalone in-memory console for callers that don't need a real console

## Key Classes

- `Console` - Main implementation of `IPhysicalConsole`
- `ConsoleOutputDescriptor` / `ConsoleErrorDescriptor` - the console's stdout /
  stderr as an `IFileDescriptor`: writes reach `Write` / `WriteError` exactly
  as given; terminals (`IsTerminal()` true), unbuffered, untagged
- `ConsoleInputDescriptor` - the console's stdin as an `IFileDescriptor`:
  every `ReadLine` line with its `"\n"` restored, then end of file; a terminal,
  and blocking (console input cannot be interrupted)
- `EmptyInputDescriptor` - no input at all: reads end at once, as /dev/null;
  not a terminal. The default stdin of a non-interactive process
- `AgentConsoleAdapter` - `IAgentConsole` that forwards each write to a shared
  `IPhysicalConsole` as one line (`message + "\n"`), untagged -- `Write` to its
  output, `WriteError` to its error. For agents running without an OS (the
  integration tests): a process's agent talks to the process's own descriptors
  instead, and nothing inside a process holds the physical console
- `InMemoryAgentConsole` - standalone `IAgentConsole` that just accumulates
  writes in memory, diagnostics (`WriteError`) alike

## Notes

- **A console writes what it is given and nothing else.** A `Write` never adds
  a newline or a label, and neither does a console descriptor: nothing knows
  (or may claim to know) who is writing. The `[name_pid] ` tags are gone --
  output is raw for every program alike.
- **A console is not a log sink.** Where the log goes is the Logger's business
  (`LogSetConsoleOutput`, `LogRegisterMessageReceiver`) -- creating a console
  never subscribes it to log output, which would put diagnostics into the
  program's own stream.
- **`ReadLine` returns `std::nullopt` at end of input**, never an empty string:
  an empty line is a line. `Console::ReadLine` strips a trailing `\r` and is
  serialized, so two readers never split one line; which reader gets the next
  line is first come, first served. It blocks and cannot be interrupted, which
  is why an interactive process only notices its agent has closed when the next
  line arrives.
