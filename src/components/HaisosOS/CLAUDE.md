# HaisosOS

An instance of an operating system: owns a rooted filesystem, a physical
console, and a services layer; starts processes and spawns sub-OS instances.

## Responsibilities

- Assigns PIDs and tracks parent/child relationships between processes (parent
  is resolved from the calling agent, when there is one; top-most processes
  have parent PID 0)
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
  not depend on the parent's lifetime.
- Built via `IFactory::CreateHaisosOS(servicesCreator, physicalConsole,
  rootFileSystem, environment, osProcessId)`. The network/LLM/filesystem
  services are created internally from the `IServicesCreator` rather than being
  passed in pre-built. Every OS can start processes; the
  process-start restriction that `SubOSPermissions` used to carry is gone.
- Holds an **environment** (`GetOsEnvironment()`): plain key/value strings fixed
  at creation and inherited by every process and sub-OS it starts. The LLM
  endpoint/model/API key are read from it (`HAISOS_ENDPOINT`/`HAISOS_MODEL`/
  `HAISOS_API_KEY`), so a sub-OS inherits the LLM configuration for free. It is
  populated from the haisosfile's `ENV` directives -- the host's environment is
  never inherited wholesale.
- `GetOSProcessID()` identifies the process this OS belongs to: 0 for the
  initial OS, and the creating process's pid for a sub-OS. Process ids come from
  `AllocateUniquePid()`, one allocator for the whole program, so these are
  unique across every OS.
- `GetRootFileSystem()` exposes the OS's root directly (what the `os_*` tools
  operate on). An OS cannot step outside that root; `GetFileSystemService()`
  only composes further filesystems on top of it. `GetServicesCreator()` gives
  access to this OS's own sandboxed services.

## Key Classes

- `HaisosOS` - Main implementation of `IHaisosOS`; `CreateHaisosOS(...)` builds the root instance
- `AgentProcess` - `IProcess` backed by an `IAgent`
- `LuaProcess` - `IProcess` backed by an embedded Lua script, running on its own thread; `Kill()` aborts it via a Lua instruction-count hook
- `OSToolFactory` - the OS-level tool set (`os_read_file`, `os_write_file`, `os_list_directory`, `os_start_process`, `os_list_processes`)
