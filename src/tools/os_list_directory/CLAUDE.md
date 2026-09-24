# os_list_directory

Lists the entries of a directory on the calling process's OS (rooted at the
OS's mounted directory).

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `path` | `string` | No | Path to the directory to list, resolved against the calling process's working directory. Defaults to `"."` (the working directory itself). |

## Output format

On success, returns a JSON array of `{"name": ..., "type": "file"|"dir"}` objects.
