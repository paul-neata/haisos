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

## Public Interface

`IAgent` is defined in `interfaces/IAgent.h` and provides methods for:
- Sending commands (`Post`, `Send`)
- Stopping and killing agents (`Stop`, `Kill`)
- Querying status (`IsFinished`, `IsKilled`, `GetHistory`, `GetConsoleOutput`)
- Navigating hierarchy (`GetParent`, `GetChildren`, `GetDepth`)
