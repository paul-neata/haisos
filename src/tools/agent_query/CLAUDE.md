# agent_query

Query the status of named subagents. On success, returns a JSON array of agent status objects.

## Use cases

1. **Check if a subagent is still running** — call `agent_query` with `names: ["agent_name"]` to get `finished` and `killed` status without waiting.
2. **Peek at a subagent's console** — call `agent_query` with `names` and `return_console: true` to read the current console output even while the agent is still running.
3. **Inspect conversation history** — call `agent_query` with `names` and `return_messages: true` to retrieve the agent's full message history at any point.
4. **Check if an agent is long-running** — call `agent_query` with `names` to see `long_running` status.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to query. |
| `return_console` | `boolean` | No | Whether to include the subagent's console output in the result. |
| `return_messages` | `boolean` | No | Whether to include the subagent's message history in the result. |

## Output format

On success, returns a JSON array:

```json
[{"name":"agent1","starting_time":"...","killed":false,"finished":false,"long_running":false}]
```

For each found agent, the array contains an object with:

| Name | Type | Description |
|------|------|-------------|
| `name` | `string` | The agent name. |
| `starting_time` | `string` | Timestamp when the agent was started. |
| `killed` | `boolean` | Whether the agent was killed. |
| `finished` | `boolean` | Whether the agent has finished. |
| `long_running` | `boolean` | Whether the agent is long-running (keeps waiting for more commands). |
| `console_result` | `string` | Console output (only present if `return_console` was `true`). |
| `messages_result` | `array` | Message history (only present if `return_messages` was `true`). |

If an agent is not found, its entry in the array is `{"name":"...","found":false}`.

If **no** requested agents are found, the response is wrapped as an error:

```json
{"is_error": true, "content": [{"name":"missing","found":false}]}
```

If the `names` argument is missing, the response is:

```json
{"is_error": true, "content": "Missing required field: names"}
```
