# os_write_file

Writes a text file on the calling process's OS (rooted at the OS's mounted
directory), creating or overwriting it by default, or appending when
`append=true`.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | `string` | Yes | Path to the file to write, resolved against the calling process's working directory. |
| `content` | `string` | Yes | The content to write to the file. |
| `append` | `boolean` | No | If `true`, append instead of overwriting. Defaults to `false` (also when null). Anything but a JSON boolean -- the string `"true"`, say -- is an error, and nothing is written. |

## Output format

On success, returns `"OK"`. On error (missing or wrongly typed argument, path
escapes the OS's root, write failure), sets `is_error=true`.
