# Console

Asynchronous, physical console output and line input, plus the adapters that
give individual agents/processes a view onto it (or onto nothing but memory).

## Responsibilities

- Queues output messages and processes the queue on a background thread
- Reads lines of input (`ReadLine`, blocking, from stdin), which is what feeds an
  interactive agent (see `AgentInputLoop` in the HaisosOS component)
- Bridges a single agent's writes onto the shared physical console, tagged by name
- Provides a standalone in-memory console for callers that don't need a real console

## Key Classes

- `Console` - Main implementation of `IPhysicalConsole`
- `AgentConsoleAdapter` - `IAgentConsole` that forwards writes to a shared `IPhysicalConsole`, tagged with a source name, and reads lines from it (untagged)
- `InMemoryAgentConsole` - standalone `IAgentConsole` that just accumulates writes in memory; it has nothing to read, so `ReadLine` reports end of input at once

## Notes

- **A console writes what it is given and nothing else.** `IPhysicalConsole` has
  one `Write(message)`: it does not know who is writing to it and labels
  nothing. Tagging by source happens in `AgentConsoleAdapter`, the one place
  that knows the name, so only the writers that want a label get one and a
  message can never be tagged twice.
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
