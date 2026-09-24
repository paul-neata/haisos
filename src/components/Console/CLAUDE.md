# Console

Asynchronous, physical console output, plus the adapters that give individual
agents/processes a write-only view onto it (or onto nothing but memory).

## Responsibilities

- Queues output messages and processes the queue on a background thread
- Bridges a single agent's writes onto the shared physical console, tagged by name
- Provides a standalone in-memory console for callers that don't need a real console

## Key Classes

- `Console` - Main implementation of `IPhysicalConsole`
- `AgentConsoleAdapter` - `IAgentConsole` that forwards writes to a shared `IPhysicalConsole`, tagged with a source name
- `InMemoryAgentConsole` - standalone `IAgentConsole` that just accumulates writes in memory

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
