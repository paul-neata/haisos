# agent_start

Starts a new subagent with a user prompt and returns immediately. On success, returns only the agent name string as a plain unquoted string. The name is alphanumeric and may contain underscores and spaces. This tool ONLY starts the agent; it does NOT wait for the agent to finish. Even if the agent finishes quickly, you can still call `agent_query` to check its status and collect output.

## Use cases

1. **Start a one-shot subagent (common choice)** — call `agent_start` with `user_prompt` and `oneShot: true`. The subagent does the delegated job and then finishes on its own. There is no need to stop or monitor it. If you need the response, just call `agent_wait_to_finish` with no `timeout_ms`; it will return as soon as the agent is done.
2. **Start a long-running background subagent** — call `agent_start` with `user_prompt` and `oneShot: false`. The subagent keeps running and waits for more commands. Use this for agents that require async events (like post message) or companion agents.
3. **Start and later collect results** — call `agent_start`, note the returned name, then use `agent_wait_to_finish` or `agent_query` to check status and collect output.
4. **Start with a custom system prompt** — call `agent_start` with `user_prompt` and `system_prompt` to give the subagent specific behavior instructions.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `user_prompt` | `string` | Yes | The user prompt to send to the new subagent. |
| `system_prompt` | `string` | No | Optional system prompt for the subagent. |
| `oneShot` | `boolean` | Yes | If `true`, the subagent finishes after processing its initial task. If `false`, the subagent keeps running and waits for more commands. |

## Output format

On success, returns the generated unique agent name as a plain string, e.g.:

```
abc123def
```

On error, it sets the `is_error=true` flag.
