# ToolFactory

Creates tool instances by name, including context-aware tools like `agent_start`.

## Responsibilities

- Maintains a registry of available tools
- Creates tool instances on demand, passing the calling agent when needed
- Provides tool descriptions and JSON schemas for LLM tool registration

## Key Classes

- `ToolFactory` - Main implementation of `IToolFactory`
