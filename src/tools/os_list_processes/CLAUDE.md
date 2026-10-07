# os_list_processes

Lists the OS's currently running processes.

## Arguments

None.

## Output format

On success, returns a JSON array of
`{"pid": ..., "parent_pid": ..., "path": ..., "agent_name": ..., "finished": ..., "exit_code": ...}`
objects. `path` is the program the process was started from; `agent_name` is
the name of the agent running it, and is empty for a process whose runtime is
not an agent. `exit_code` is the process's exit code (see "Exit codes" in the
root `CLAUDE.md`): a number once the process has finished, `null` while it is
still running.
