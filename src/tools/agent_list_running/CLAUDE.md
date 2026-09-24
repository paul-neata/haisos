# agent_list_running

List all currently running subagents by name. On success, returns a comma-separated string of agent names, like 'agent1,agent2'. Names are plain strings without quotes; each name is alphanumeric and may contain underscores and spaces. Optionally filter by names.

## Use cases

1. **List all active subagents** — call `agent_list_running` with no arguments to get every subagent that is still running.
2. **Check if specific agents are still running** — call `agent_list_running` with `names: ["agent1", "agent2"]` to see which of them are still active.
3. **Monitor agent lifecycle** — call `agent_list_running` between rounds of work to see which subagents are still going. A subagent cannot be stopped from here: a `oneShot=true` one finishes by itself, and a `oneShot=false` one runs until the agent that started it is gone.

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

On error, it sets the `is_error=true` flag.
