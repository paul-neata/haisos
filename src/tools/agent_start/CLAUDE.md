# agent_start

Starts a new subagent with a user prompt and optionally waits for it to finish.

## Use cases

1. **Start a fire-and-forget subagent** — call `agent_start` with `user_prompt` only. The subagent runs in the background and the tool returns immediately with its name and start time.
2. **Start and wait for a result** — call `agent_start` with `user_prompt` and `wait_to_finish: true`. The tool blocks until the subagent completes and optionally returns console output or message history.
3. **Start with a custom system prompt** — call `agent_start` with `user_prompt`, `system_prompt`, and `wait_to_finish: true` to give the subagent specific behavior instructions.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `user_prompt` | `string` | Yes | The user prompt to send to the new subagent. |
| `system_prompt` | `string` | No | Optional system prompt for the subagent. |
| `wait_to_finish` | `boolean` | No | If `true`, block until the subagent finishes before returning. Default is `false`. |
| `wait_to_finish_timeout_ms` | `integer` | No | Timeout in milliseconds when `wait_to_finish` is `true`. `0` polls current status; omitting blocks forever. |
| `return_console` | `boolean` | No | Whether to include the subagent's console output in the result. |
| `return_messages` | `boolean` | No | Whether to include the subagent's message history in the result. |

## Outputs

| Name | Type | Description |
|------|------|-------------|
| `name` | `string` | The generated unique name of the subagent. |
| `start_time` | `string` | Timestamp when the subagent was started. |
| `killed` | `boolean` | Whether the subagent was killed. |
| `finished` | `boolean` | Whether the subagent has finished (only `true` if `wait_to_finish` was set). |
| `console_result` | `string` | The subagent's console output (only present if `return_console` was `true` and the agent finished). |
| `messages_result` | `array` | The subagent's message history (only present if `return_messages` was `true` and the agent finished). |
| `error` | `string` | Error message if the call failed. |
