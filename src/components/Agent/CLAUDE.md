# Agent

Manages LLM conversations with parent/child agent relationships. Supports subagents via agent tools.

## Responsibilities

- Maintains conversation history with the LLM
- Handles tool calls returned by the LLM and executes them
- Supports parent/child agent hierarchies for subagent delegation
- Runs each agent on its own thread with a command queue
- Tracks agent lifecycle: running, finished, killed
- Provides console output aggregation from agent execution

## Key Classes

- `Agent` - Main implementation of `IAgent`
- `AgentMessageBuffer` - Internal per-agent message storage

## Notes

- Tool-call results are treated as untrusted content: they are added to the history verbatim
  (never filtered, so file contents and JSON stay intact) but wrapped in
  `--- BEGIN TOOL RESULT ---` / `--- END TOOL RESULT ---` delimiters, the same convention used
  for user input. Oversized results are truncated before the delimiters are added.
- Tool descriptions are fetched from the tool factory once in the constructor and cached for the
  agent's lifetime, not rebuilt per LLM round. Tools registered after an agent is constructed are
  invisible to it, so tool registries must be populated before agents are created.

## Public Interface

`IAgent` is defined in `interfaces/ILLMService.h` and provides methods for:
- Sending commands (`Post`, `Send`)
- Stopping, killing, and waiting on agents (`Stop`, `Kill`, `WaitToFinish` (with and without a timeout))
- Querying status (`Name`, `IsFinished`, `IsKilled`, `IsLongRunning`, `GetStartTime`, `GetHistory`, `GetConsoleOutput`)
- Navigating hierarchy (`GetParent`, `GetChildren`, `AddChild`, `GetDepth`)
