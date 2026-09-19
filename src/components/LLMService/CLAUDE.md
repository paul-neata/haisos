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
- Is the only friend of the protected `IAgent::AddChild`: an agent's children
  are decided by the one thing that creates agents, not by another agent or a
  tool holding an `IAgent`
- Exposes a shared agent-management tool set (`agent_start`, ...) for introspection
- `CreateAgentConsole()` returns a standalone in-memory `IAgentConsole`

## Key Classes

- `LLMService` - Main implementation of `ILLMService`
