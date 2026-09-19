# os_read_file

Reads a text file from the calling process's OS (rooted at the OS's mounted
directory), up to a 10 MB cap.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | `string` | Yes | Path to the file to read, resolved against the calling process's working directory. |

## Output format

On success, returns the file contents as a plain string. On error (missing
argument, file not found, path escapes the OS's root), sets `is_error=true`.
