# agent_wait_to_finish

Wait for named subagents to finish. On success, returns an empty string. On error, returns an error message about agents that could not be found or waited for.

## Use cases

1. **Wait for a background subagent** — call `agent_wait_to_finish` with `names: ["agent_name"]` after starting a subagent with `agent_start`. Blocks until completion.
2. **Poll agent status with timeout** — call `agent_wait_to_finish` with `names`, `timeout_ms: 0` to check current status without blocking.
3. **Wait with timeout** — call `agent_wait_to_finish` with `names` and `timeout_ms: <value>` to block up to a specified duration, returning immediately if the agent finishes earlier.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to wait for. |
| `timeout_ms` | `integer` | No | Timeout in milliseconds. `0` = poll current status only; omit = block forever. |
| `return_console` | `boolean` | No | Whether to include the subagent's console output in the result. |
| `return_messages` | `boolean` | No | Whether to include the subagent's message history in the result. |

## Output format

On success, returns an empty string.

If the `names` argument is missing or invalid, the response is:

```json
{"is_error": true, "content": "Missing required field: names"}
```

If one or more agents are not found, the response is:

```json
{"is_error": true, "content": "agent1 not found, agent2 not found"}
```
