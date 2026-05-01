# agent_stop

Initiate graceful stop or force-kill on named subagents. On success, returns an empty string. Stopping an already stopped agent is not an error. Use agent_wait_to_finish to confirm completion.

## Use cases

1. **Gracefully stop a subagent** — call `agent_stop` with `names: ["agent_name"]` to send a stop signal, allowing the agent to finish its current work.
2. **Force-kill a stuck subagent** — call `agent_stop` with `names` and `kill: true` to immediately terminate the agent.
3. **Clean up multiple agents** — call `agent_stop` with `names: ["agent1", "agent2"]` to stop several agents at once.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to stop. |
| `kill` | `boolean` | No | If `true`, kill the agents instead of stopping them gracefully. Default is `false`. |

## Output format

On success, returns an empty string.

If the `names` argument is missing or not an array, returns `"Missing required field: names"` as an error.
