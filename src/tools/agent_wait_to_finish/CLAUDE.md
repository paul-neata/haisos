# agent_wait_to_finish

Wait for named subagents to finish. On success, returns an empty string. On error, returns an error message about agents that could not be found or waited for.

## Use cases

1. **Wait for a background subagent** — call `agent_wait_to_finish` with `names: ["agent_name"]` after starting a subagent with `agent_start`. Blocks until completion. For `oneShot=true` agents, it is fine to omit `timeout_ms`; they finish as soon as they can and this call will return promptly.
2. **Poll agent status with timeout** — call `agent_wait_to_finish` with `names`, `timeout_ms: 0` to check current status without blocking.
3. **Wait with timeout** — call `agent_wait_to_finish` with `names` and `timeout_ms: <value>` to block up to a specified duration, returning immediately if the agent finishes earlier. `timeout_ms` is **required** for a `oneShot=false` agent: such an agent goes back to waiting for commands once it is done, so it never finishes on its own and nothing can tell it to stop. Omitting it there is an error rather than a wait that would never return.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to wait for. |
| `timeout_ms` | `integer` | No | Timeout in milliseconds, capped at 86400000 (24 hours). `0` = poll current status only; omit = wait up to that cap. Required for `oneShot=false` agents. |
| `return_console` | `boolean` | No | Whether to include the subagent's console output in the result. |
| `return_messages` | `boolean` | No | Whether to include the subagent's message history in the result. |

## Output format

On success, returns an empty string.

On error, it sets the `is_error=true` flag.
