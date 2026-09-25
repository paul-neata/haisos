# HaisosOS

An instance of an operating system: owns a rooted filesystem, a physical
console, and a services layer; starts processes and spawns sub-OS instances.

## Responsibilities

- Numbers the processes it starts from one program-wide pid allocator (the same
  one behind `IFactory::GetNextGloballyUniquePID()`), so a pid live in one OS can
  never appear in another. A process started here has **this OS as its parent**:
  its parent pid is `GetOSProcessID()`, not 0 (0 means no parent at all, and is
  never allocated). `StartProcess` is deliberately not told which agent called
  it, because a process is opaque (whether it is an agent is its own business,
  and an agent's subagents stay inside it rather than becoming processes), so
  the OS is the most specific parent there is to name.
- Every process is started with an environment and a working directory passed
  by the caller (`StartProcess(environment, programPath, args, workingDirectory, options)`)
  -- the environment typically `GetOsEnvironment()->Clone()`. Neither is taken
  from the OS behind the caller's back: a null environment is refused, and an
  empty working directory means the OS's root. The process keeps both.
  `IProcess::GetEnvironment()` hands out a **clone**, so reading a process's
  environment from outside can never change what the process itself sees.
- A process owns its **working directory**, on its `IFileIO` (`ProcessFileIO`):
  a filesystem has none (see the Filesystem component), so this is the only
  thing a relative path is resolved against, and one process moving never moves
  another. All file access a process does goes through that `IFileIO`, reached
  by `ICurrentProcess::IO()` -- never through `GetRootFileSystem()`, which
  understands absolute paths alone. `ProcessFileIO` fetches the filesystem from
  the OS on every call rather than holding it, so a process handed a narrowed
  OS does its I/O through that OS's root and nothing else.
- **`ICurrentProcess` is the only door out of a process.** Everything a running
  program reaches beyond its own memory it reaches through
  `ICurrentProcess` -- files via `IO()`, everything else via `OS()` --
  tools, agents and Lua scripts alike. So
  every runtime has the same reach, and narrowing one process is a matter of
  handing it a narrower OS (a clone confined by a different root filesystem),
  with no runtime needing to know. The security policy that will exploit this
  lands in a later PR; see the Security section of the root `CLAUDE.md`. The
  reference a process holds on its OS is **weak**: an OS owns its processes, so
  a strong one back would be a cycle neither could escape.
  The wiring is `CurrentProcessHandle`: created before the process, handed to
  `OSToolFactory`, and filled in by `AgentProcess::Create`/`LuaProcess::Create`
  before the process goes live -- which is why an agent process is only given
  its first command *after* its `AgentProcess` exists. Because a tool knows its
  caller, a relative path is resolved against that process's working
  directory.
- `IProcess` is the outside view of a process and `ICurrentProcess` (which
  inherits it) the inside one. From outside you may look, ask it to stop
  (`TriggerStop`) and wait; you may not change its environment or its working
  directory, nor reach the agent running it -- `StartingAgentName()` gives the
  name and nothing more. From inside, `ICurrentProcess` adds
  `IO()`, `AsAgent()` and `OS()`. Neither offers a way to force a process down:
  `TriggerStop()` is all there is, and what actually waits a stubborn thread out
  is the process's own destructor. (An `IOSProcess` carrying a `Kill()` used to
  sit between the two; it was removed because neither runtime could honour it
  any harder than `TriggerStop()` already did, and will come back when there is
  something real for it to do.)
- Neither an OS nor a process is ever destroyed on a runtime thread, although
  one can hold the last reference to both -- an `os_*` tool does, for the length
  of a call. `HaisosOS::Create`, `AgentProcess::Create` and `LuaProcess::Create`
  pass the `DestroyOffRuntimeThreads` deleter, and the agent's, the script's
  and the input loop's threads run inside a `RuntimeThreadScope`, so what is let
  go of there is destroyed on the destruction thread (see "Creating things" in
  the root `CLAUDE.md`). `~HaisosOS` depends on it: it drains its processes --
  asks each to stop, then waits up to 5 s for it -- and on a process's own
  thread, that wait would be for itself.
- `StartProcessOptions` says how to run a program. Its one field for now,
  `interactiveAgent`, applies to `.md` programs only (ignored otherwise): the
  agent is created interactive, gets an extra system prompt telling it that
  further messages are lines typed on the console and that `self_close` ends the
  session, and its `AgentProcess` owns an `AgentInputLoop` reading its console.
  The loop posts each line to the agent while `WaitToFinish(0)` says it is still
  running; it ends when a line arrives for an agent that has closed (that line
  is dropped), or at end of input, when it asks the agent to stop. An
  interactive process is finished only once both the agent and the loop are.
  `os_start_process` never asks for it: the console's input belongs to whoever
  the haisosfile gave it to.
- `StartProcess` first asks the root filesystem `IsBuiltinCommand(path)`: a
  path naming a builtin runs it -- whatever its extension -- through the
  `IBuiltinCommands` the OS was created with (`StartBuiltinProcess` fills in a
  `BuiltinCommandHost` and calls `RunCommand`; see the BuiltinCommands
  component). That `IBuiltinCommands` is private to the OS, deliberately not
  exposed. An OS created with none (null) cannot start a builtin. Otherwise
  it dispatches by file extension: `.md` starts an LLM agent (its
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
- `CreateSubOS` takes the same arguments as `IFactory::CreateHaisosOS` --
  including the `IBuiltinCommands`, right after the root filesystem, typically
  the parent's own -- because a sub-OS is an ordinary OS. What confines it is the root filesystem the caller
  hands it -- typically this OS's root narrowed with
  `IFileSystemService::CreateSubFileSystem` -- rather than a permissions struct.
  Give it its own services creator via `IServicesCreator::Clone()`, so it does
  not depend on the parent's lifetime, and its own environment via
  `IEnvironment::Clone()`. It carries the parent OS's pid rather than taking one
  of its own: a sub-OS is the same OS seen through a narrower root.
- Built via `IFactory::CreateHaisosOS(servicesCreator, physicalConsole,
  rootFileSystem, builtinCommands, environment)`, which allocates the new OS's pid itself; the
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

- `HaisosOS` - Main implementation of `IHaisosOS`; `HaisosOS::Create(...)` builds an instance. Tracks its processes as `IProcess`, the outside view -- all it asks of them is to stop, wait and name themselves, and all `IBuiltinCommands::RunCommand` hands back
- `AgentProcess` - `ICurrentProcess` backed by an agent. It holds the concrete
  `Agent` because it owns the agent's lifetime: an agent cannot be forced down
  (see the Agent component's CLAUDE.md), so a wedged agent process is waited out
  by `~Agent` rather than aborted. `AgentProcess::Create` refuses a null agent
  or environment, so the rest of the class assumes both. It also posts the
  program to the agent -- only once the process exists, so a tool can find it --
  and then starts the input loop of an interactive process
- `AgentInputLoop` - the thread that feeds an interactive agent the lines typed
  on its console (see `StartProcessOptions` above). Its destructor waits the
  thread out however long it takes, since a blocked `ReadLine` cannot be
  interrupted
- `LuaProcess` - `ICurrentProcess` backed by an embedded Lua script, running on
  its own thread. Its `Kill()` aborts the script via a Lua instruction-count
  hook -- an interpreter really can be interrupted mid-instruction -- and
  `TriggerStop()` simply calls it, since a script has no command queue to close
- `ProcessFileIO` - the `IFileIO` behind `ICurrentProcess::IO()`: the OS's root filesystem plus this process's working directory. Built as a library of its own (`ProcessFileIO` in `CMakeLists.txt`), so a runtime living outside this component -- `BuiltinProcess` -- gives its processes the same I/O without linking all of `HaisosOS`
- `OSToolFactory` - the OS-level tool set (`os_read_file`, `os_write_file`, `os_list_directory`, `os_start_process`, `os_list_processes`), built once per process and bound to it
