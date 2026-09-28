# Factory

Factory for the root concepts: the things that must exist before anything else
can.

## Responsibilities

- Creates a physical console, a disk-backed `PhysicalFileSystem` jailed at a
  directory (`CreatePhysicalFileSystem`, which takes it in any form a
  physical path may have: see `src/components/Filesystem/PhysicalPath.h`),
  the host's whole disk as one filesystem (`CreateFullPhysicalFileSystem`: a
  `PhysicalFileSystem` at `/` on Linux, `WindowsFullPhysicalFileSystem` on
  Windows, where `/c/x` is `C:\x`), an empty
  `IEnvironment`, the services layer (`IServicesCreator`), the builtin
  commands (`IBuiltinCommands`) and what places them (`IBuiltinConfigurator`),
  and the OS itself (`IHaisosOS`, handed its `IBuiltinCommands` right after its
  root filesystem)
- Hands out no unrooted filesystem: every filesystem it creates is anchored
  somewhere -- the full physical filesystem at the host's own root, the rest
  at a directory of it -- and an OS receives its root at creation and can
  never step outside it -- it can only compose further filesystems on top of
  that root
- Knows nothing about agents, LLM communication, or tool factories -- that all
  lives in `ILLMService` (see `src/components/LLMService/`). HTTP clients come
  from `INetworkService`, not from here.

## Key Classes

- `Factory` - Main implementation of `IFactory`
- `CreateFactory()` - Free function to create the root factory
