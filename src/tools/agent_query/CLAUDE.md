# agent_query

Query the current status of one or more named subagents without blocking.

## Use cases

1. **Check if a subagent is still running** — call `agent_query` with `names: ["agent_name"]` to get `finished` and `killed` status without waiting.
2. **Peek at a subagent's console** — call `agent_query` with `names` and `return_console: true` to read the current console output even while the agent is still running.
3. **Inspect conversation history** — call `agent_query` with `names` and `return_messages: true` to retrieve the agent's full message history at any point.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to query. |
| `return_console` | `boolean` | No | Whether to include the subagent's console output in the result. |
| `return_messages` | `boolean` | No | Whether to include the subagent's message history in the result. |

## Outputs

For each agent in the `names` array, the result contains:

| Name | Type | Description |
|------|------|-------------|
| `name` | `string` | The agent name. |
| `killed` | `boolean` | Whether the agent was killed. |
| `finished` | `boolean` | Whether the agent has finished. |
| `console_result` | `string` | Console output (only present if `return_console` was `true`). |
| `messages_result` | `array` | Message history (only present if `return_messages` was `true`). |
| `error` | `string` | Error message if the agent was not found. |
