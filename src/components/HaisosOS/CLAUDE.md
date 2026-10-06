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
  OS does its I/O through that OS's root and nothing else. The same `IFileIO`
  holds the process's **descriptor table**: its open files by number, as a
  POSIX process holds them -- slots 0, 1 and 2 holding its stdin, stdout and
  stderr (installed from the resolved `StartProcessOptions` before the program
  runs; see below), 3 and up ordinary, at most
  `IFileIO::kMaxDescriptors` (1024) in all. `OpenFile` hands back the
  descriptor itself without numbering it; placing it in the table
  (`AddDescriptor`, lowest free slot), duplicating it (`Dup`, lowest free slot;
  `Dup2`, a chosen slot replaced atomically), looking it up (`GetDescriptor`)
  and closing a slot (`CloseDescriptor`) are table operations, and none of them
  needs the OS.
- When a process's program ends, every descriptor in its table is released
  **before the process reports finished** -- whoever sees `WaitToFinish` return
  true finds the table already empty, which is what will let a pipe's reader
  see end of file when its writer's program ends. Each process class calls
  `ProcessFileIO::ReleaseAllDescriptors()` (not on `IFileIO`, so no program can
  call it): a builtin's `Run` returning, in `BuiltinProcess::RunThread`; a
  script's chunk ending, in `LuaProcess::RunThread`; and an agent's
  conversation thread ending, through `Agent::SetFinishedHook`, which
  `AgentProcess::Create` sets. Every drop happens outside the table's mutex.
- Every process reports an **exit code** (`IProcess::ExitCode()`), empty while
  it runs and a shell-style 0-255 once finished, latched and never changing --
  a `TriggerStop()` landing after the finish changes nothing. The meanings are a
  shell's: the program's own code modulo 256, 143 (128 + SIGTERM) for a stop,
  141 (128 + SIGPIPE) for a write into a pipe whose reader is gone; the
  constants and `ExitCodeFor` live
  in `src/components/libheaders/ExitCodes.h`, and each runtime turns its end
  into a code in one place, where it reports finished. A builtin reports its
  command's status; a Lua script its `exit()` argument, 1 on an error, 143 when
  its kill hook fired before the chunk ran out; an agent 1 when its last
  command failed (`Agent::LastCommandFailed()`), else 0, and 143 when it was
  stopped before finishing -- `AgentProcess::TriggerStop()` latches "stopped"
  only while the process is still running. Every runtime also implements
  `ICurrentProcess::StopForBrokenPipe()`, called by that runtime's own output
  path (`BuiltinContext`, Lua's `print` and error line, `ProcessAgentConsole`)
  when a write of the program's bytes returned `kIOBrokenPipe`: the flag it
  latches is checked **before** "stopped" at the decision point (141 wins over
  a `TriggerStop` asked for afterwards, and, for Lua, before `exit()` and an
  error's 1 too), and the program is then stopped the way `TriggerStop` would
  stop it -- quietly, nothing printed. The API stops nobody: `Write` and
  `IFileIO` only return the error, for code (a shell's heredoc, later) that
  would rather handle it. In Lua a broken `print` does not raise from the
  trampoline -- it has C++ locals a `longjmp` may not cross -- but re-arms the
  kill hook to fire on the next instruction, so the unwinding runs as for a
  kill and no `lua:` line is written; `ProcessAgentConsole` stops the agent on
  a broken pipe from **either** descriptor, history and message buffer
  unchanged.
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
- `StartProcessOptions` says how to run a program: the standard streams
  (`stdIn`/`stdOut`/`stdErr`, the process's descriptors 0/1/2) and
  `interactive`. `StartProcess` resolves every null stream right after its
  refusals, one place for the defaults (`ResolveStandardStreams`): stdout the
  OS's console output, stderr its console error (console descriptors, see the
  Console component), stdin the console's input when `interactive` is set, else
  an empty input whose reads end at once. Every runtime then gets the three
  streams as slots 0/1/2 of its table, installed before its program starts --
  before the agent is given its program, the script's thread begins or the
  builtin's thread starts, so a runtime's first output already reaches them.
  What a runtime writes there: a builtin writes its stdout and stderr; an agent
  writes its replies to slot 1 and its diagnostics (an LLM, HTTP or parse
  failure, an unknown tool, a failed command) to slot 2, through
  `ProcessAgentConsole`; a Lua script's `print` writes its line to slot 1 and a
  load or runtime error goes to slot 2, one line `lua: <message>` where the
  message is rendered as the standalone interpreter's does (a position prefix
  when there is one, a non-string error object described), without a traceback. For a `.md` program,
  `interactive` also makes the agent interactive: it gets an extra system
  prompt telling it that further messages are lines typed on the console and
  that `self_close` ends the session, and its `AgentProcess` owns an
  `AgentInputLoop` reading its stdin (slot 0). The loop posts each line to the
  agent
  while `WaitToFinish(0)` says it is still running; it ends when a line
  arrives for an agent that has closed (that line is dropped), or at end of
  input, when it asks the agent to stop. An interactive process is finished
  only once both the agent and the loop are. `os_start_process` never asks for
  it: the console's input belongs to whoever the haisosfile gave it to.
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
  results are handed back as Lua tables); `print()` writes its line plus `\n`
  to the process's stdout (descriptor 1), and a load or runtime error goes to
  its stderr (descriptor 2) as `lua: <path>:<line>: <message>` plus a `\n` and
  ends the script with exit code 1 (the load refusal of precompiled bytecode
  has no position to name, so that line is just `lua: <message>`)
- The Lua <-> JSON bridge behind those functions (`ToJson`/`PushJson` in
  `LuaProcess.cpp`) must survive whatever a script passes or a tool returns,
  since a script is untrusted and so is any file it reads. Strings keep every
  byte, NULs included, both ways (and in the `arg` table). Tables may nest at
  most `kMaxJsonNestingDepth` (100) levels: arguments nested deeper, or
  containing themselves (a cycle), are refused with `(message, true)` -- the
  tool is not called and the script goes on -- and a JSON result nested deeper
  is handed back as its text, not as tables. Both converters reserve Lua stack
  space at every level with `lua_checkstack`, which only reports failure;
  never `luaL_checkstack` there, whose Lua error would `longjmp` across C++
  frames
- Lua processes run sandboxed: only a restricted subset of the Lua standard
  library is opened, and host-access libraries (`io`, `os`, `package`,
  `debug`) plus the file-loading globals are deliberately removed, so a script
  cannot touch the real disk, environment, or native libraries. Consequently
  **all** filesystem and process access from Lua must go through the `os_*`
  tool globals. The stock `os.exit` goes with `os` -- it would call C `exit()`
  on the whole haisos process -- and the sandbox's one replacement is a global
  `exit([code])`: it asks for the script to end (the same kill-hook machinery a
  stop uses, so a `pcall` cannot swallow it) and the process then exits with
  its code (no argument or `nil` 0, `true` 0, `false` 1, else an integer, as
  `os.exit` takes them). This is a security boundary, not an oversight -- the
  exact set of libraries and globals is defined by the library-opening helper
  in `LuaProcess.cpp`, which is the ground truth.
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
- `GetPipeService()` is the OS's own pipe service (`IPipeService`, see the
  PipeService component), created from its services creator when the OS is
  created -- `HaisosOS::Create` makes one beside the network and LLM services,
  so a sub-OS gets its own too. Nothing in the OS uses it directly: a process
  reaches it through `ProcessFileIO::CreatePipe()`, which makes a pipe through
  `OS()` (the one door) and places the read and write ends in the two lowest
  free slots of the caller's descriptor table, read end first.
- Every runtime thread -- a builtin's command, a Lua script, an agent's
  conversation and an interactive process's input loop -- carries its
  process's `StopToken` (`src/components/libheaders/StopToken.h`), installed
  with a `StopTokenScope` right after its `RuntimeThreadScope` and signalled
  by `TriggerStop()` (`BuiltinProcess::TriggerStop`, `LuaProcess::Kill`,
  `Agent::TriggerStop`; a Lua `exit()` does not touch it -- it ends by
  unwinding, not by blocking). So a pipe `Read`/`Write` blocked on a process's
  behalf returns `kIOInterrupted` as soon as the process is asked to stop, and
  a stuck pipeline never wedges `~HaisosOS`'s drain. Console input stays
  uninterruptible (D7).

## Key Classes

- `HaisosOS` - Main implementation of `IHaisosOS`; `HaisosOS::Create(...)` builds an instance. Tracks its processes as `IProcess`, the outside view -- all it asks of them is to stop, wait and name themselves, and all `IBuiltinCommands::RunCommand` hands back
- `AgentProcess` - `ICurrentProcess` backed by an agent. It holds the concrete
  `Agent` because it owns the agent's lifetime: an agent cannot be forced down
  (see the Agent component's CLAUDE.md), so a wedged agent process is waited out
  by `~Agent` rather than aborted. `AgentProcess::Create` refuses a null agent
  or environment, so the rest of the class assumes both. It also posts the
  program to the agent -- only once the process exists, so a tool can find it --
  and then starts the input loop of an interactive process
- `AgentInputLoop` - the thread that feeds an interactive agent the lines read
  from its stdin (see `StartProcessOptions` above), line by line through a
  `DescriptorLineReader`. Its destructor waits the
  thread out however long it takes: a line being read cannot be abandoned, and
  a read blocked on the console's input cannot be interrupted (D7), though a
  pipe's can
- `ProcessAgentConsole` - an agent process's `IAgentConsole`: `Write` (a reply)
  goes to the process's descriptor 1, `WriteError` (a diagnostic) to descriptor
  2, each plus a `\n`. Created before the process, so it reaches it through the
  same `CurrentProcessHandle` the OS tools use, and looks the descriptor up on
  every write: a process whose table was replaced or released writes wherever
  it says, or nowhere. A write that returns `kIOBrokenPipe` -- either
  descriptor's pipe -- calls the process's `StopForBrokenPipe()` (the agent
  stops, exit code 141, quietly) and drops every line after
- `LuaProcess` - `ICurrentProcess` backed by an embedded Lua script, running on
  its own thread. Its `Kill()` aborts the script via a Lua instruction-count
  hook -- an interpreter really can be interrupted mid-instruction -- and
  `TriggerStop()` simply calls it, since a script has no command queue to
  close. `StopForBrokenPipe()` calls it too, after latching the flag that makes
  the exit code 141 (checked first at the decision point: a broken pipe beats
  `exit()`, a stop and an error); a `print` whose write hit the pipe never
  raises from its trampoline but re-arms the hook to fire before the next
  instruction, so the unwinding is the one a kill runs, with no `lua:` line.
  The same hook serves the global `exit()`: the hook's C trampoline
  re-arms it on every call, return and line, so neither a stop nor an `exit()`
  can be caught and ignored by a `pcall`. Both also stop the script from inside
  any coroutine: a hook set is per Lua thread, so the latch is armed on the
  main thread as well, and `coroutine.resume`/`coroutine.wrap` are wrapped to
  arm each resuming thread as control comes back to it (a nested resumer
  included), so the resumer stops before its next instruction. The chunk is loaded as text with its
  path for a name (`"@" + path`), so error messages come out `path:line:` as
  the standalone interpreter's do
- `ProcessFileIO` - the `IFileIO` behind `ICurrentProcess::IO()`: the OS's root filesystem plus this process's working directory, and its owner of the descriptor table (the process's open files by number; see the bullets above). Built as a library of its own (`ProcessFileIO` in `CMakeLists.txt`), so a runtime living outside this component -- `BuiltinProcess` -- gives its processes the same I/O without linking all of `HaisosOS`
- `OSToolFactory` - the OS-level tool set (`os_read_file`, `os_write_file`, `os_list_directory`, `os_start_process`, `os_list_processes`), built once per process and bound to it
