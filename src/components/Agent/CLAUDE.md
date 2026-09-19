# Agent

Manages LLM conversations with parent/child agent relationships. Supports subagents via agent tools.

## Responsibilities

- Maintains conversation history with the LLM
- Handles tool calls returned by the LLM and executes them
- Supports parent/child agent hierarchies for subagent delegation
- Runs each agent on its own thread with a command queue
- Tracks agent lifecycle: running, finished, killed
- Provides console output aggregation from agent execution
- Created only through `Agent::Create()` (the constructor is private and every
  `Agent` is owned by a `shared_ptr`). `Create()` also starts the conversation
  thread, which the constructor deliberately does not: the thread hands
  `shared_from_this()` to every tool it calls, so it may not run until the
  owning `shared_ptr` exists.

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
- Asking an agent to stop (`TriggerStop`) and waiting on it
  (`WaitToFinish(timeoutMs)`; a timeout of 0 does not wait at all, so it
  doubles as "has it finished?")
- Querying status (`Name`, `IsInteractive`, `GetStartTime`, `GetHistory`, `GetConsoleOutput`)
- Navigating hierarchy (`GetParent`, `GetChildren(onlyDirectChildren)`, `AddChild`, `GetDepth`)

`IsInteractive()` says whether the agent goes back to waiting for prompts after
finishing the job it was given -- the way a coding agent's main agent does -- or
finishes outright, which is what a delegated one-shot subagent wants.
`GetDepth()` is the number of parent hops up to the top of the agent tree (0 for
an agent with no parent); `agent_start` uses it to cap subagent recursion.

**Forcing an agent down is deliberately not on `IAgent`.** `TriggerStop()` is
all an outsider gets: it closes the command queue, so the agent takes no new
commands and finishes once whatever it is already doing is done. `Stop()`,
`Kill()`, the untimed `WaitToFinish()`, `IsFinished()` and `IsKilled()` are
public on the concrete `Agent` only, so an agent is killed by whoever owns it --
the process wrapping it, the `LLMService` that created it, or its own
destructor -- and never by another agent or a tool holding an `IAgent` handle.

`AddChild` is **protected** on `IAgent`, with `LLMService` as its only friend:
an agent's children are decided by the one thing that creates agents, not by
another agent or a tool. It stays public on the concrete `Agent`, which nothing
outside this component and its tests can reach anyway.
