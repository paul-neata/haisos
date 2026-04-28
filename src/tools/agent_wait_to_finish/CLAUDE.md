# agent_wait_to_finish

Wait for one or more named subagents to finish running.

## Use cases

1. **Wait for a background subagent** — call `agent_wait_to_finish` with `names: ["agent_name"]` after starting a subagent with `agent_start` and `wait_to_finish: false`. Blocks until completion.
2. **Poll agent status with timeout** — call `agent_wait_to_finish` with `names`, `timeout_ms: 0` to check current status without blocking. Returns `finished` field showing whether the agent is done.
3. **Collect subagent results** — call `agent_wait_to_finish` with `names`, `return_console: true`, and `return_messages: true` to gather the subagent's full output and conversation history after it finishes.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to wait for. |
| `timeout_ms` | `integer` | No | Timeout in milliseconds. `0` = poll current status only; omit = block forever. |
| `return_console` | `boolean` | No | Whether to include the subagent's console output in the result. |
| `return_messages` | `boolean` | No | Whether to include the subagent's message history in the result. |

## Outputs

For each agent in the `names` array, the result contains:

| Name | Type | Description |
|------|------|-------------|
| `name` | `string` | The agent name. |
| `killed` | `boolean` | Whether the agent was killed. |
| `finished` | `boolean` | Whether the agent has finished. |
| `end_time` | `string` | ISO 8601 end timestamp (only present if `finished` is `true`). |
| `console_result` | `string` | Console output (only present if `return_console` was `true` and agent finished). |
| `messages_result` | `array` | Message history (only present if `return_messages` was `true` and agent finished). |
| `error` | `string` | Error message if the agent was not found. |
