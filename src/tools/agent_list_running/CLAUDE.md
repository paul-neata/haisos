# agent_list_running

List all currently running subagents by name. On success, returns a comma-separated string of agent names without spaces, like 'agent1,agent2'. Optionally filter by names.

## Use cases

1. **List all active subagents** — call `agent_list_running` with no arguments to get every subagent that is still running.
2. **Check if specific agents are still running** — call `agent_list_running` with `names: ["agent1", "agent2"]` to see which of them are still active.
3. **Monitor agent lifecycle** — call `agent_list_running` before and after `agent_stop` to verify that agents were properly terminated.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | No | Optional filter: only include agents whose names are in this list. |

## Output format

On success, returns a comma-separated string of running agent names without any spaces or other characters:

```
agent1,agent2
```

If no agents match the filter or if no agents are running, returns an empty string.

If an error occurs (e.g., no caller agent), the response is:

```json
{"is_error": true, "content": "no caller agent"}
```
