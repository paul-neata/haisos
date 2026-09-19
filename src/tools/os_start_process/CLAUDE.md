# os_start_process

Starts a new OS process (a `.md` agent or a `.lua` script, resolved against the
OS's filesystem root). Returns immediately; does not wait for the new process
to finish. The new process runs with a `Clone()` of the OS's environment, so it
inherits what the OS was given and its own edits stay its own. It is top-most
(parent pid 0): the calling agent is deliberately not passed to
`IHaisosOS::StartProcess`, because a process is opaque -- whether it happens to
be an agent is its own business. It starts at the OS's root: a tool has no
handle on the calling process, so there is no working directory to inherit from
it yet.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | `string` | Yes | Path to the `.md` or `.lua` program to run, relative to the OS's filesystem root. |
| `args` | `array` of `string` | No | Optional arguments passed to the process. |

## Output format

On success, returns a JSON object `{"pid": ..., "path": ...}`. On error
(missing argument, process-starting disallowed for this OS, unsupported/unknown
program extension), sets `is_error=true`.
