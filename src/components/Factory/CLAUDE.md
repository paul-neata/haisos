# Factory

Low-level factory for real/physical primitives.

## Responsibilities

- Creates real primitives only: a physical console, an HTTP client, and disk-backed filesystems (unrooted and `PhysicalFileSystem`)
- Knows nothing about agents, LLM communication, or tool factories -- that all lives in `ILLMService` (see `src/components/LLMService/`)

## Key Classes

- `Factory` - Main implementation of `IFactory`
- `CreateFactory()` - Free function to create the root factory
