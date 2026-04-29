# agent_start

Starts a new subagent with a user prompt and returns immediately. On success, returns only the agent name string. This tool ONLY starts the agent; it does NOT wait for the agent to finish.

## Use cases

1. **Start a short-running subagent (common choice)** — call `agent_start` with `user_prompt` and `long_running: false` (or omit it, since false is the default). The subagent does the delegated job and then finishes on its own.
2. **Start a long-running background subagent** — call `agent_start` with `user_prompt` and `long_running: true`. The subagent keeps running and waits for more commands.
3. **Start and later collect results** — call `agent_start`, note the returned name, then use `agent_wait_to_finish` or `agent_query` to check status and collect output.
4. **Start with a custom system prompt** — call `agent_start` with `user_prompt` and `system_prompt` to give the subagent specific behavior instructions.

## Arguments

| Name | Type | Required | Description |
|------|------|----------|-------------|
| `user_prompt` | `string` | Yes | The user prompt to send to the new subagent. |
| `system_prompt` | `string` | No | Optional system prompt for the subagent. |
| `long_running` | `boolean` | No | If `true`, the subagent keeps running and waits for more commands. If `false` (default), the subagent finishes after processing its initial task. |

## Output format

On success, returns the generated unique agent name as a plain string, e.g.:

```
abc123def
```

On error, the response is wrapped:

```json
{"is_error": true, "content": "error message"}
```
