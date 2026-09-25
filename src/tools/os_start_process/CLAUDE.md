# os_start_process

Starts a new OS process (a `.md` agent, a `.lua` script, or a builtin command
placed on the root filesystem, such as `/bin/ls`; resolved against the OS's
filesystem root). Returns immediately; does not wait for the new process
to finish. The new process runs with a `Clone()` of the OS's environment, so it
inherits what the OS was given and its own edits stay its own. It is top-most
(parent pid 0): the calling agent is deliberately not passed to
`IHaisosOS::StartProcess`, because a process is opaque -- whether it happens to
be an agent is its own business. A relative `path` is resolved against the
calling process's working directory, and the new process starts in that same
directory -- the way a shell would.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | `string` | Yes | Path to the `.md` or `.lua` program, or the builtin command, to run. A relative path is resolved against the calling process's working directory. |
| `args` | `array` of `string` | No | Optional arguments passed to the process. |

## Output format

On success, returns a JSON object `{"pid": ..., "path": ...}`. On error
(missing argument, process-starting disallowed for this OS, unsupported/unknown
program extension), sets `is_error=true`.
