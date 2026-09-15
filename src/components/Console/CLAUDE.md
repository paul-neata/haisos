# Console

Asynchronous, physical console output, plus the adapters that give individual
agents/processes a write-only view onto it (or onto nothing but memory).

## Responsibilities

- Queues output messages and processes the queue on a background thread
- Optionally registers as a log message receiver
- Bridges a single agent's writes onto the shared physical console, tagged by name
- Provides a standalone in-memory console for callers that don't need a real console

## Key Classes

- `Console` - Main implementation of `IPhysicalConsole`
- `AgentConsoleAdapter` - `IAgentConsole` that forwards writes to a shared `IPhysicalConsole`, tagged with a source name
- `InMemoryAgentConsole` - standalone `IAgentConsole` that just accumulates writes in memory
