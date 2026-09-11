# os_start_process

Starts a new OS process (a `.md` agent or a `.lua` script, resolved against the
OS's filesystem root) as a child of the calling process. Returns immediately;
does not wait for the new process to finish.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | `string` | Yes | Path to the `.md` or `.lua` program to run, relative to the OS's filesystem root. |
| `args` | `array` of `string` | No | Optional arguments passed to the process. |

## Output format

On success, returns a JSON object `{"pid": ..., "name": ...}`. On error
(missing argument, process-starting disallowed for this OS, unsupported/unknown
program extension), sets `is_error=true`.
