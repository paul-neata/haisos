# get_current_date_time

Returns the current date and time.

## Use cases

1. **Get local time** — call `get_current_date_time` with no arguments or `get_gmt: false`. Returns local time in `YYYY-MM-DD HH:MM:SS` format.
2. **Get GMT/UTC time** — call `get_current_date_time` with `get_gmt: true`. Returns GMT/UTC time in ISO 8601 format (`YYYY-MM-DDTHH:MM:SSZ`).

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `get_gmt` | `boolean` | No | If `true`, return GMT/UTC time in ISO 8601 format. If `false` or omitted, return local time. Default is `false`. |

## Outputs

| Name | Type | Description |
|------|------|-------------|
| `is_error` | `boolean` | Always `false` for this tool. |
| `content` | `string` | The current date and time. Local time is formatted as `YYYY-MM-DD HH:MM:SS`. GMT/UTC time is formatted as `YYYY-MM-DDTHH:MM:SSZ`. |
