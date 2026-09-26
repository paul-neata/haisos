# A tool call hands over the calling agent twice: to CreateTool, then to Call

For every tool call an agent makes (currently in `Agent.cpp`'s tool-call
handling), it does

    auto tool = m_toolFactory->CreateTool(toolName, shared_from_this());
    result = tool->Call(shared_from_this(), args);

so the caller is handed over twice, and a new tool object is built for every
call. What each side does with it:

- `IToolFactory::CreateTool(name, callerAgent = nullptr)`
  (`interfaces/ILLMService.h`): the ToolFactory component's factory uses the
  caller only to refuse a tool when it is null ("caller agent is missing"),
  and keeps nothing; `OSToolFactory` ignores it. At creation it is at most a
  null check.
- `ITool::Call(callerAgent, args)`: the tools that need the caller take it
  here (`agent_start`, `agent_query`, `agent_wait_to_finish`,
  `agent_list_running`, `self_close`, `os_start_process`); the others
  (`get_current_date_time`, `os_read_file`, `os_write_file`,
  `os_list_directory`, `os_list_processes`) ignore it.

To do: say who the caller is once. Either:

- drop `CreateTool`'s caller parameter and keep `Call`'s, where the tools use
  it; the refusals move into the tools' `Call`, which already fails a call
  cleanly; or
- bind the caller when the tool is created, and drop it from `Call`.

As every tool is stateless, each could also be created once, per factory or
per agent, rather than per call. The caller that every tool is handed is also
why an agent can be released last on a tool's thread (see "Creating things" in
the root CLAUDE.md).
