# A `kill` builtin

To do: a `kill` builtin command, so that a process -- an agent through
`os_start_process`, a Lua script, a `RUN` -- can stop another one by its pid,
as on Linux. It follows the rules for every builtin (root CLAUDE.md, "Builtin
Commands"): the reference is util-linux `kill`
(https://man7.org/linux/man-pages/man1/kill.1.html), with its arguments,
output, error messages and exit statuses; `--help` and `--version` have the
common shape; it is registered in `CreateStandardBuiltinCommands()`, so
`haisos --init` lists it, and added to the builtins table.

What it can do today, and what it needs (see `notes/note-process-start-stop.md`):

- Finding the process: through its own `ICurrentProcess` -- `OS()`, then
  `IHaisosOS::GetRunningProcesses()` -- by pid, the pids `os_list_processes`
  shows.
- Signals: `TERM`, the default, is `IProcess::TriggerStop()`, which is only a
  request. `KILL` has nothing behind it yet: nothing can make a process end.
  The other signals (`HUP`, `INT`, `STOP`, `CONT`, `USR1`, ...) have no meaning
  in Haisos, so they are parsed and reported as not treated, as the builtin
  rules want for a value that is not handled.
- `-l`/`--list` and `-L`/`--table` can print the Linux signal names and
  numbers exactly as the real command does.
- To decide: who may kill whom (any process of the same OS, only its own
  descendants, never the OS's top-most ones), whether it waits for the process
  to end (util-linux has `--timeout`), and how a pid that is not there or a
  stop that is refused is reported.

Related: no `ps` builtin exists yet to list pids as `ps` does; only the
`os_list_processes` tool lists processes.
