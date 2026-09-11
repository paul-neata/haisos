# LLMService

Service-layer entry point for creating LLM-backed agents.

## Responsibilities

- Holds the LLM endpoint/model/API key configuration
- Composes a fresh `ILLMCommunicator` + `IToolFactory` (via `IFactory`) for each agent it creates
- Exposes a shared agent-management tool set (`agent_start`, ...) for introspection

## Key Classes

- `LLMService` - Main implementation of `ILLMService`
