# A builtin's own process calls IBuiltinCommands::RunCommand, not the OS

Today (commit 8515a90) the OS calls `IBuiltinCommands::RunCommand` to *start* a
builtin. `HaisosOS` fills in a `BuiltinCommandHost` -- its OS, a fresh pid,
the parent pid, the path, a console -- and `RunCommand`, in the
BuiltinCommands component, builds the process from it: currently
`BuiltinProcess`, which starts its own thread and there runs
`IBuiltinCommand::Run` with a context built around itself. So the
BuiltinCommands component makes processes, pids and threads, which the OS makes
itself for `.md` agents and `.lua` scripts (`AgentProcess` and `LuaProcess`, in
the HaisosOS component).

Decided direction: `RunCommand` is called by the process the command runs in --
the current process, on its own thread -- to run the command there.

- The OS builds a builtin's process the way it builds the others: a process
  class in the HaisosOS component, with its pid, parent, path, console,
  environment, `IO()` and `OS()`. That process's thread calls `RunCommand`.
- `RunCommand` takes that process -- its `ICurrentProcess`, the only door out of
  a process -- plus the builtin's name and arguments, runs the command to
  completion on the caller's thread and returns its exit status. It starts
  nothing.
- `BuiltinCommandHost` then has nothing left to carry, and the BuiltinCommands
  component knows nothing of processes, pids or threads: it is a library of
  commands.
- To settle: how the command learns that a stop was requested (today a flag the
  process owns, read through `BuiltinContext`), where the exit status goes
  (`IProcess` has none yet: see `notes/explore-stdio-exit-codes.md`), and the
  tests, which could then run a command synchronously against a fake
  `ICurrentProcess`.

Related: `notes/explore-builtin-command-host.md` weighs this among the ways to
do without `BuiltinCommandHost`.
