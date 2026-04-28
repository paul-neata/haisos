# agent_stop

Stop or kill one or more named subagents.

## Use cases

1. **Gracefully stop a subagent** — call `agent_stop` with `names: ["agent_name"]` to send a stop signal, allowing the agent to finish its current work.
2. **Force-kill a stuck subagent** — call `agent_stop` with `names` and `kill: true` to immediately terminate the agent.
3. **Clean up multiple agents** — call `agent_stop` with `names: ["agent1", "agent2"]` to stop several agents at once.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `names` | `array[string]` | Yes | List of agent names to stop. |
| `kill` | `boolean` | No | If `true`, kill the agents instead of stopping them gracefully. Default is `false`. |

## Outputs

For each agent in the `names` array, the result contains:

| Name | Type | Description |
|------|------|-------------|
| `name` | `string` | The agent name. |
| `killed` | `boolean` | Whether the agent was killed. |
| `finished` | `boolean` | Whether the agent has finished. |
| `error` | `string` | Error message if the agent was not found. |
