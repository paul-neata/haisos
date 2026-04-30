# Factory

Dependency injection factory.

## Responsibilities

- Creates all component instances (Console, HTTPClient, LLMCommunicator, ToolFactory, Agent, HaisosEngine)
- Owns the agent lifecycle by keeping shared references
- Configures system callbacks for components

## Key Classes

- `Factory` - Main implementation of `IFactory`
- `CreateFactory()` - Free function to create the root factory
