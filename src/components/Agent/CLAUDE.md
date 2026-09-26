# Agent

Manages LLM conversations with parent/child agent relationships. Supports subagents via agent tools.

## Responsibilities

- Maintains conversation history with the LLM
- Handles tool calls returned by the LLM and executes them
- Supports parent/child agent hierarchies for subagent delegation
- Runs each agent on its own thread with a command queue
- Tracks agent lifecycle: running, finished
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
  for user input.
- **Commands are delimited, never rewritten.** Every command -- the whole `.md`
  program `AgentProcess` posts, each line typed into a `RUN -i` session, a
  subagent's prompt from `agent_start` -- goes into the history byte for byte
  between `--- BEGIN USER INPUT ---` / `--- END USER INPUT ---`. A lossy
  sanitizer used to run over them (`SanitizeUserInput`, now deleted): it
  dropped every line containing phrases such as "you are now" or "system:",
  deleted everything between `<` and `>`, and cut at 64 KB. None of these
  texts comes from an untrusted third party -- the program from the
  haisosfile's author, typed lines from the human operator, a subagent's prompt
  from a parent with at least the subagent's power (a subagent gets only the
  agent-management tools) -- while the genuinely untrusted content, tool
  results, was never sanitized, only delimited. The denylist was trivially
  evaded by rewording, and it corrupted code (`a < b and b > c`), markup and
  ordinary prose ("Operating system: Linux").
- **Nothing is truncated or trimmed.** The history keeps every message for the
  agent's whole life, and message content -- an LLM reply, a tool result, the
  copy written to the log -- reaches its destination whole. What the model said
  and what a tool returned is what is kept. The cost is that a long-running
  agent's history grows without bound, and with it the size of every request:
  `MAX_LLM_ROUNDS` caps one command's rounds, nothing caps the conversation.
- A tool call is whatever the LLM sent, so `ExecuteToolCalls` reads it without
  throwing and answers every malformed one with an error result: no string
  name, or arguments that are neither an object nor null (null means none).
  A tool that throws fails its own call ("Error: tool <name> failed: ..."),
  never the agent.
- **A failed command never ends an agent silently.** Whatever a command throws
  -- a tool, the LLM round trip, anything else -- is caught around that one
  command (`ProcessCommand`): it is logged ("Exception in RunThread"), written
  to the agent's console and message buffer as `Error: the command failed:
  ...`, and every tool call it left without a result is given an error one
  (`AnswerUnansweredToolCalls`), because the history is only a valid
  conversation with one result per tool call. An interactive agent then takes
  its next command; a non-interactive one finishes, as it would have anyway.
- Tool descriptions are fetched from the tool factory once in the constructor and cached for the
  agent's lifetime, not rebuilt per LLM round. Tools registered after an agent is constructed are
  invisible to it, so tool registries must be populated before agents are created.

## Public Interface

`IAgent` is defined in `interfaces/ILLMService.h` and provides methods for:
- Sending commands (`Post`, `Send`)
- Asking an agent to stop (`TriggerStop`) and waiting on it
  (`WaitToFinish(timeoutMs)`, the only wait there is; a timeout of 0 does not
  wait at all, so it doubles as "has it finished?")
- Querying status (`Name`, `IsInteractive`, `GetStartTime`, `GetHistory`, `GetConsoleOutput`)
- Navigating hierarchy (`GetParent`, `GetChildren(onlyDirectChildren)`, `AddChild`, `GetDepth`)

`IsInteractive()` says whether the agent goes back to waiting for prompts after
finishing the job it was given -- the way a coding agent's main agent does -- or
finishes outright, which is what a delegated one-shot subagent wants.
`GetDepth()` is the number of parent hops up to the top of the agent tree (0 for
an agent with no parent); `agent_start` uses it to cap subagent recursion.

**Forcing an agent down is deliberately not on `IAgent`.** `TriggerStop()` is
all an outsider gets, and it does two things: it closes the command queue, so no
new command is taken, and it sets a flag the round already in flight notices.
That flag is what makes a stop prompt -- `ExecuteToolCalls` runs no further
tools once it is set, and the conversation round ends rather than spending the
remaining LLM rounds refusing tool calls. Every call still gets a result, an
error one, because the history is only well-formed with one result per tool
call.

**There is no `Kill()`.** An agent cannot be forced down, because its thread
spends its time inside an HTTP call or a tool call that has to be allowed to
return; a flag saying it was killed would change nothing about when it actually
stops. `TriggerStop()` is the whole of it, for owners and outsiders alike --
`AgentProcess::Kill()`, the OS's escalation after a cooperative stop times out,
can do no more than ask again.

`~Agent` is what closes the gap. It triggers a stop and then waits **without a
bound**, in passes of `DESTRUCTION_WAIT_INTERVAL_MS`, logging a warning after
each pass that finds the thread still running. The wait cannot give up: the
thread runs on members the destructor is about to free, so abandoning it is a
use-after-free rather than a timeout. The per-pass log is what keeps a wedged
agent from looking like a silent hang.

That wait is also why an `Agent` is never destroyed on its own thread, though
its thread can hold the last reference to it -- the one it hands the tool it is
calling. `Create()` passes the `DestroyOffRuntimeThreads` deleter and
`RunThread()` runs inside a `RuntimeThreadScope`, so an agent let go of there is
destroyed on the destruction thread once its thread is done; on its own thread,
`~Agent` would wait for itself forever (see "Creating things" in the root
`CLAUDE.md`).

`IsFinished()` is public on the concrete `Agent` only, not on `IAgent`, which
answers the same question through `WaitToFinish(0)`.

`AddChild` is **protected** on `IAgent`, with `LLMService` as its only friend:
an agent's children are decided by the one thing that creates agents, not by
another agent or a tool. It stays public on the concrete `Agent`, which nothing
outside this component and its tests can reach anyway.
