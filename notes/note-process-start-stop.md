# HaisosOS: refusing starts at shutdown, saying why a start failed, killing a process

**No new process once the OS is shutting down.** `HaisosOS` has a shutting-down
flag (currently `m_shuttingDown`), set first thing in its destructor, and
`IHaisosOS::StartProcess` checks it -- but only once, at its start, outside the
lock that guards the process list. The process is then created, its thread
already running (an agent's starts in `Agent::Create`), and only added to the
list afterwards, in a separate critical section. So a start racing with
shutdown -- an agent calling `os_start_process` while the OS is destroyed --
can still add a process after the destructor took the list, which is why the
destructor drains in passes (currently up to 8) and may give up with processes
still tracked. The same split lets the concurrent-process cap be overshot
(`notes/note-review-low-findings.md`, section 7). `IHaisosOS::CreateSubOS`
does not check the flag at all. To do: set the flag under that lock, and check
it and the cap, and reserve the process's place in the list, under it in one
step, before the process's thread starts; one drain pass then suffices.

**Say why a start failed.** `StartProcess` returns null for every failure: no
environment, shutting down, process limit, a program that is unreadable, empty
or of an unsupported type, the LLM service refusing the agent. The reason goes
only to the log. `os_start_process` answers "Failed to start process: <path>",
and `main` prints the same for a `RUN`. To do: have `StartProcess` report the
reason (an error out-parameter, or a result holding the process or the error),
so the calling agent and the operator see "the OS is shutting down", "process
limit reached", "no such program", ...

**Killing a process -- to think through.** `IProcess::TriggerStop()` is only a
request today. A Lua script is aborted by a hook between Lua instructions, not
inside a tool call; a builtin stops where it polls for it; an agent only closes
its command queue, so a command in flight runs to its end -- an LLM call of up
to 120 s, a tool waiting on a subagent -- and its subagents run on
(`notes/note-review-medium-findings.md`, section 2). At shutdown the destructor
waits 5 s per process, one after the other, then leaves each process to its
own destructor, which joins the thread however long that takes. (The
destructor's comment says such a process is "killed rather than waited on
forever"; nothing kills it -- C++ cannot kill a thread.) Directions:

- a stop token per process that reaches every blocking wait -- tool calls, the
  HTTP request (curl and WinHTTP can abort one), waits on subagents -- with a
  process's children stopped first (every process knows its parent's pid);
- TERM and KILL, as `kill` has them: TERM is today's `TriggerStop`; KILL could
  revoke the process's `ICurrentProcess`, its only door out, so every further
  file or OS call fails at once and nothing it does reaches anything, while its
  thread finishes the call it is in;
- a way to ask for it: an `os_kill_process` tool and a `kill` builtin (GNU
  semantics), deciding who may kill whom;
- only a separate OS process can be killed outright: the heavy option.
