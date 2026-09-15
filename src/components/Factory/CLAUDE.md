# Factory

Factory for the root concepts: the things that must exist before anything else
can.

## Responsibilities

- Creates a physical console, a disk-backed `PhysicalFileSystem`, an empty
  `IEnvironment`, the services layer (`IServicesCreator`), and the OS itself
  (`IHaisosOS`)
- Hands out no unrooted filesystem: every filesystem it creates is anchored
  somewhere, and an OS receives its root at creation and can never step outside
  it -- it can only compose further filesystems on top of that root
- Knows nothing about agents, LLM communication, or tool factories -- that all
  lives in `ILLMService` (see `src/components/LLMService/`). HTTP clients come
  from `INetworkService`, not from here.

## Key Classes

- `Factory` - Main implementation of `IFactory`
- `CreateFactory()` - Free function to create the root factory
