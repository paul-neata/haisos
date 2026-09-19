# HaisosOS

An instance of an operating system: owns a rooted filesystem, a physical
console, and a services layer; starts processes and spawns sub-OS instances.

## Responsibilities

- Numbers the processes it starts from one program-wide pid allocator (the same
  one behind `IFactory::GetNextGloballyUniquePID()`), so a pid live in one OS can
  never appear in another. Processes started here are top-most (parent PID 0) --
  `StartProcess` is deliberately not told which agent called it, because a
  process is opaque (whether it is an agent is its own business, and an agent's
  subagents stay inside it rather than becoming processes). Process parentage
  will be redefined along with that split.
- Every process is started with an environment passed by the caller
  (`StartProcess(environment, programPath, args)`) -- typically
  `GetOsEnvironment()->Clone()`. It is never taken from the OS behind the
  caller's back: a null one is refused. The process keeps it
  (`IProcess::GetEnvironment()`).
- Dispatches `StartProcess` by file extension: `.md` starts an LLM agent (its
  content becomes the agent's prompt); `.lua` starts an embedded Lua script
  (via the vendored `extern/lua` interpreter, see `LuaProcess`)
- Merges the OS's own tools with an agent-backed process's LLM tools via
  `CompositeToolFactory`; a Lua process gets the OS's tools directly, each
  exposed as a Lua global function returning `(content, is_error)` (JSON
  results are handed back as Lua tables); `print()` routes to the process's
  console
- Lua processes run sandboxed: only a restricted subset of the Lua standard
  library is opened, and host-access libraries (`io`, `os`, `package`,
  `debug`) plus the file-loading globals are deliberately removed, so a script
  cannot touch the real disk, environment, or native libraries. Consequently
  **all** filesystem and process access from Lua must go through the `os_*`
  tool globals. This is a security boundary, not an oversight -- the exact set
  of libraries and globals is defined by the library-opening helper in
  `LuaProcess.cpp`, which is the ground truth.
- `CreateSubOS` takes the same arguments as `IFactory::CreateHaisosOS`, because a
  sub-OS is an ordinary OS. What confines it is the root filesystem the caller
  hands it -- typically this OS's root narrowed with
  `IFilesystemService::CreateSubFileSystem` -- rather than a permissions struct.
  Give it its own services creator via `IServicesCreator::Clone()`, so it does
  not depend on the parent's lifetime, and its own environment via
  `IEnvironment::Clone()`. It carries the parent OS's pid rather than taking one
  of its own: a sub-OS is the same OS seen through a narrower root.
- Built via `IFactory::CreateHaisosOS(servicesCreator, physicalConsole,
  rootFileSystem, environment)`, which allocates the new OS's pid itself; the
  concrete entry point is `HaisosOS::Create(..., osProcessId)` (the constructor
  is private and every `HaisosOS` is owned by a `shared_ptr`). The network and
  LLM services are created internally from the `IServicesCreator` rather than
  being passed in pre-built. Every OS can start processes; the process-start
  restriction that `SubOSPermissions` used to carry is gone.
- Holds an **environment** (`GetOsEnvironment()`, an `IEnvironment` fixed at
  creation; creating an OS without one fails). The LLM endpoint/model/API key
  are read from its variables (`HAISOS_ENDPOINT`/`HAISOS_MODEL`/
  `HAISOS_API_KEY`), so a sub-OS handed a `Clone()` of it gets the LLM
  configuration for free. It is populated from the haisosfile's `ENV`
  directives -- the host's environment is never inherited wholesale.
- `GetOSProcessID()` is the pid identifying this OS, allocated when it was
  created. Every sub-OS spawned from it shares the same id. 0 is never
  allocated; it means "no parent".
- `GetRootFileSystem()` exposes the OS's root (what the `os_*` tools operate
  on). An OS cannot step outside that root, but it can compose further
  filesystems on top of it, through the filesystem service that
  `GetServicesCreator()` -- this OS's own sandboxed services -- creates.

## Key Classes

- `HaisosOS` - Main implementation of `IHaisosOS`; `HaisosOS::Create(...)` builds an instance
- `AgentProcess` - `IProcess` backed by an agent. It holds the concrete `Agent`,
  not an `IAgent`: stopping and killing are deliberately off `IAgent` (see the
  Agent component's CLAUDE.md), and a process is exactly the thing that has to
  be able to do both
- `LuaProcess` - `IProcess` backed by an embedded Lua script, running on its own thread; `Kill()` aborts it via a Lua instruction-count hook
- `OSToolFactory` - the OS-level tool set (`os_read_file`, `os_write_file`, `os_list_directory`, `os_start_process`, `os_list_processes`)
