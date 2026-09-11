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
- `CreateSubOS` builds a logically-confined child OS: a filesystem sub-root
  (validated to stay within the parent's root) and, optionally, a tool set
  without `os_start_process`

## Key Classes

- `HaisosOS` - Main implementation of `IHaisosOS`; `CreateHaisosOS(...)` builds the root instance
- `AgentProcess` - `IProcess` backed by an `IAgent`
- `LuaProcess` - `IProcess` backed by an embedded Lua script, running on its own thread; `Kill()` aborts it via a Lua instruction-count hook
- `OSToolFactory` - the OS-level tool set (`os_read_file`, `os_write_file`, `os_list_directory`, `os_start_process`, `os_list_processes`)
