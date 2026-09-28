# An agent's program reaches the LLM as a user message, not a system one

How an agent's conversation is sent today (`src/components/Agent/Agent.cpp`):

- `role: "system"`: only the fixed prompts the agent was created with
  (`m_systemPrompts`). For a `.md` process, `HaisosOS`
  (`src/components/HaisosOS/HaisosOS.cpp`) gives "You are a helpful AI
  assistant.", plus `kInteractiveAgentSystemPrompt` for `RUN -i`. A subagent
  gets `agent_start`'s optional `system_prompt`
  (`src/tools/agent_start/AgentStartTool.cpp`), or none.
- `role: "user"`: every command, wrapped by `Agent::ProcessCommand` in
  `--- BEGIN USER INPUT ---` / `--- END USER INPUT ---`. That includes the
  agent's program -- the `.md` file's content with its `--- Arguments ---`,
  posted as the first command by `AgentProcess::Create` -- as well as a
  subagent's `user_prompt` and each line typed to an interactive agent.
- `role: "tool"`: tool results, wrapped in `--- BEGIN TOOL RESULT ---` markers.

So the program -- the instructions that define the agent, written by whoever
set up the OS -- reaches the model as if a user had typed it, framed as "user
input" and on the same footing as the lines typed after it, while the actual
system prompt is a generic line. `kInteractiveAgentSystemPrompt` even describes
the program as coming before "every further user message".

To do: send the program (and its arguments) as a `system` message, after the
fixed prompts, and keep `user` for what a user actually says -- the lines typed
to an interactive agent, and a subagent's `user_prompt`, which is the task its
parent gives it. Then reword `kInteractiveAgentSystemPrompt` and the delimiter
comments in `Agent::ProcessCommand`, and check that the models in use honour a
second system message: some chat templates keep only one.
