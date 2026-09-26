# Explore agents as process children

Today there are two parallel hierarchies. The **process tree**: `IHaisosOS::StartProcess` /
`os_start_process` create `ICurrentProcess` children with a pid, a parent pid, an
`IEnvironment` and an `IFileIO`. The **agent tree**: `agent_start` calls
`ILLMService::CreateAgent(prompt, parent)` with an `IAgent` parent (`src/tools/agent_start/`),
so a subagent is *not* a process -- no pid, invisible to `os_list_processes`, no environment
of its own, its lifetime tied to the agent tree (parents hold only weak refs to children).
Only the root agent of a `.md` program gets a process wrapper
(`HaisosOS::StartAgentProcess` / `AgentProcess`).

Idea to explore: make a subagent a **child process** of the calling agent's process
(`AgentProcess::AsAgent()` gives the calling agent from its process). Aspects to think
through before committing to it:

- **Tooling comes free**: an `OSToolFactory` and the `os_*` tools are built per process, so
  a subagent-as-process gets the same reach as any process; today subagents go through the
  caller's `CurrentProcessHandle` instead.
- **Visibility**: subagents would appear in `os_list_processes` with their own pids.
- **Lifetime**: when the calling process finishes, do its subagent processes keep running?
  HaisosOS exits once the `RUN` processes finish -- must decide whether subagent processes
  count, and who waits for them (today `agent_wait_to_finish` does, via the agent tree).
- **Two trees or one**: the agent tree still feeds the agent paths and depth indent of
  `--log-agent-to-file`; if processes carry the hierarchy, does the agent tree collapse
  into the process tree or stay for logging only?
- **Environment/security**: each process `Clone()`s its environment; a subagent becoming a
  process would get a clone too, and a narrowed OS if the caller had one -- matching the
  `ICurrentProcess` door model in the root CLAUDE.md.

Seed for `/explore` or `/todo`.
