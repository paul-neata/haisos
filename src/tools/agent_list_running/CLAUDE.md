# agent_list_running

List all currently running subagents. Optionally filter by names.

## Use cases

1. **List all active subagents** — call `agent_list_running` with no arguments to get every subagent that is still running.
2. **Check if specific agents are still running** — call `agent_list_running` with `names: ["agent1", "agent2"]` to see which of them are still active.
3. **Monitor agent lifecycle** — call `agent_list_running` before and after `agent_stop` to verify that agents were properly terminated.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | No | Optional filter: only include agents whose names are in this list. |

## Outputs

The result is an array of entries, one for each running agent:

| Name | Type | Description |
|------|------|-------------|
| `name` | `string` | The agent name. |
| `start_time` | `string` | Timestamp when the agent was started. |

If no agents match the filter or if no agents are running, the result is an empty array.
