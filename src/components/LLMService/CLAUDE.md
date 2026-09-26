# LLMService

Service-layer entry point for creating LLM-backed agents. Constructs the
concrete `Agent`/`LLMCommunicator`/`ToolFactory`/`InMemoryAgentConsole`
classes directly -- `IFactory` has no knowledge of agents at all any more.

## Responsibilities

- Holds the LLM endpoint/model/API key configuration
- Composes a fresh `LLMCommunicator` (tagged with the agent's name, for
  per-agent `[JSON_REQUEST]`/`[JSON_RESPONSE]` log tracing) + `ToolFactory`
  for each agent it creates
- Keeps every agent it creates alive via a strong reference (a parent only
  holds a `weak_ptr` to its children, so nothing else would keep e.g. an
  `agent_start`-created subagent alive once the tool call that created it returns)
- **Ends every agent it created before it goes.** `~LLMService` runs while all
  its members are still alive: under the lock it sets a shutting-down flag --
  from then on `CreateAgent` refuses (logs it, returns null) -- and takes the
  list of agents out; then, outside the lock, it asks every agent to stop (all
  of them before waiting for any), waits for each one to finish (without a
  bound, logging every 5 s, as `~Agent` does), and only then releases them.
  A defaulted destructor used to destroy the lock first and then the agents
  one at a time, each `~Agent` waiting for its thread, while the agents further
  down the list kept running -- and one calling `agent_start` then locked the
  destroyed mutex and changed the list mid-destruction (an agent released
  twice, another never destroyed). Waiting, rather than only releasing,
  matters too: an agent someone else still holds would otherwise outlive the
  service, still running, with tools holding a reference to it. The mutex is
  declared before the list, so it outlives it whatever happens.
- `CreateAgent` checks the flag both before creating an agent and when
  registering it; an agent whose creation raced the shutdown is stopped and
  dropped (null is returned). It registers the agent as its parent's child
  only once it is kept alive here. `agent_start` turns a null agent into an
  error result.
- Is the only friend of the protected `IAgent::AddChild`: an agent's children
  are decided by the one thing that creates agents, not by another agent or a
  tool holding an `IAgent`
- Exposes a shared agent-management tool set (`agent_start`, ...) for introspection
- `CreateAgentConsole()` returns a standalone in-memory `IAgentConsole`

## Key Classes

- `LLMService` - Main implementation of `ILLMService`
