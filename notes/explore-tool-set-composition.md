# Exploration: Composing tool sets -- joining them, and narrowing one to fewer tools

## Seed

> An agent's tools come from an `IToolFactory` (`interfaces/ILLMService.h`:
> create a tool by name, say whether it has one, list the tools and their
> descriptions). Two are joined today by `CompositeToolFactory` (in the
> ToolFactory component) -- the agent-management tools and, for a process of an
> OS, the OS's `os_*` tools -- which merges exactly two and lists a name both
> have twice. Explore the ways to compose tool sets: joining two or more (and
> what happens when names clash), and making a tool set from another without one
> tool or without several -- e.g. a subagent, a restricted process or an OS
> without `os_start_process`, or an agent with only read-only tools -- and where
> such compositions would be expressed: C++ interfaces, `agent_start`'s
> parameters, the haisosfile, an agent's program.

## Base

- Branch: `task/code_review_max`
- Commit: `8515a90` (Add full physical filesystem and Windows drive-letter paths)
- Related notes:
  - `notes/note-review-low-findings.md` -- section 6 records the join listing a
    name twice and its stale "shared across every process" comment (settled by
    D1); section 9 the root `CLAUDE.md`'s stale sentence on what the OS's tool
    set holds.
  - `notes/note-tool-caller-twice.md` -- `IToolFactory::CreateTool` and
    `ITool::Call` both take the calling agent. Whatever `CreateTool` ends up
    taking, the join and the filter forward it (D2). It lists `os_start_process`
    among the tools using the caller; that tool names the parameter but ignores
    it -- A2's first variant would give it a use: the calling agent's own
    narrowing.
  - `notes/explore-valid-shared-ptr.md` -- its A4 would replace
    `CreateAgent`'s null `additionalTools` by an empty `IToolFactory`: the join
    of nothing, or a set narrowed to nothing, is that object; its D3 makes the
    join's operands valid pointers; its D2 puts vocabulary types in
    `interfaces/`, where a selection type goes (D3 here).
  - `notes/note-review-medium-findings.md` -- section 6: agents can write the
    whole default root, and the fix proposed is a read-only root with a
    writable work directory -- the enforcing half of D5; subagent fan-out is
    unbounded (a set without `agent_start` is one bound). Section 2: stopping
    subagents.
  - `notes/explore-stdio-exit-codes.md` -- its A4 weighs a shell-command tool,
    a second way to start processes that a denylist written today would not
    drop (D3, A5); its A5 an `os_exit` tool. Every new tool has to fit the
    selections, and any groups.
  - `notes/explore-builtin-command-host.md` -- a builtin is handed its process
    and reaches `process.OS()`: a future builtin that starts processes (`env`,
    `timeout`, a shell) is a starter A3 must account for.
  - `notes/note-process-start-stop.md` and `notes/note-kill-builtin.md` -- an
    `os_kill_process` tool and a `kill` builtin, and "who may kill whom":
    another capability a selection would govern. `StartProcess` returning null
    for every failure is also where a refused selection's reason would be lost.
  - `notes/note-agent-program-role.md` -- the program is posted whole as the
    first user message; a program declaring its tools (A4's frontmatter
    variant) would have to be cut out of that text first.
  - `notes/explore-llm-communicator-place.md` (added while this was written)
    -- its D4 has HaisosOS hold its agents as `IAgent` and stop linking the
    Agent library, the only way HaisosOS reaches the ToolFactory library today
    (D8 here); its A4 (a model per agent, a subagent inheriting its parent's
    unless `agent_start` names another; or per process, from its environment)
    is the same inherit-and-narrow shape as A1 and A2 here, and would share
    their `CreateAgent` and `agent_start` changes.
  - Being written at the same time, not in `notes/` when this was written:
    explorations of tool-parameter encodings for several LLM protocols (tool
    names must be unique there, and match `^[a-zA-Z0-9_-]+$`: D1), of tool
    messages that make agents finish sooner (texts naming other tools: A6), and
    of making every agent a child of a process (a subagent with a process of
    its own would be narrowed as a process: A1, A2).

## The issue

The seed interprets the user's words "tools interface ways to join two tools
interfaces, or to create one tools-set without one toll or without multiple
tools". A program's tools are an `IToolFactory`: Haisos has two such sets -- the agent-management
tools every agent gets from its LLM service, and the OS tools every process of
an OS gets -- and one join, of exactly two, used for agent processes. This
exploration looks at what joining any number of sets means (duplicate names,
order, which operand answers), what narrowing a set means (allowlist, denylist,
groups; enforced where), and what makes a narrowed set a restriction rather than
a suggestion: whether a narrowed program can start something with more tools
than it has, how narrowing relates to the root filesystem and to the rule that
narrowing a process means handing it a narrower OS, at which levels (agent,
process, OS) a narrowing is kept, and where it is written (C++, `agent_start`,
the haisosfile, the program). Below, a *selection* says which tools a set keeps,
and a *ceiling* is a selection that bounds a program and everything it starts.
Done means: one join (any number of sets, each name once, a clash refused) and
one filter (a dropped tool neither listed nor creatable) exist with unit tests;
the levels and places chosen here can express the seed's examples -- a
subagent, a process or an OS without `os_start_process`, an agent with only
read-only tools -- and nothing a narrowed program starts gets a tool it lacks;
the documents and the stale comments say so; the unit tests pass.

## What exists today

- **The interface.** `IToolFactory` (`interfaces/ILLMService.h`):
  `CreateTool(name, callerAgent)`, `HasTool(name)` (to be cheap: "called on
  every tool invocation"), `GetAvailableTools()` and
  `GetAvailableToolDescriptions()` (name, description, JSON schema). An `ITool`
  has only `Call(callerAgent, args)` and `GetParametersSchema()`: its name and
  description belong to the set that makes it.
- **Two sets, disjoint by name.**
  - The agent tools (ToolFactory component, currently `ToolFactory`):
    `get_current_date_time`, `agent_start`, `agent_wait_to_finish`,
    `agent_query`, `agent_list_running`, `self_close`. `LLMService::CreateAgent`
    makes a fresh one per agent; the one `ILLMService::GetToolFactory()` hands
    out -- "shared by every agent this service creates", its comment says -- is
    used by tests only. These tools refuse to be created without a calling
    agent, and a registry made without an `ILLMService` still lists
    `agent_start` but never creates it.
  - The OS tools (HaisosOS component, currently `OSToolFactory`):
    `os_read_file`, `os_write_file`, `os_list_directory`, `os_list_processes`,
    `os_start_process`, built once per process around a `CurrentProcessHandle`,
    so each acts through its own process's `IO()` and `OS()`.
- **The join.** Currently `CompositeToolFactory` (ToolFactory component):
  exactly two operands, "shared" and "owned"; `CreateTool` asks the first one
  first, `HasTool` is true if either has the name, and both lists are
  concatenated, so a name in both is listed twice. Its header calls the first
  operand the OS's set "shared across every process", which the OS set had
  stopped being by the time it was merged (commit `39b17ff`). No unit test
  covers it. Its one user is `CreateAgent`: given `additionalTools`, it joins
  them (first) with the agent's own set.
- **Who gets what.**
  - A `.md` process: the OS tools, then the agent tools -- eleven, in that
    order.
  - A `.lua` process: the OS tools only, each a Lua global of its name ("The
    LLM tool set ... belongs to agents", in the OS's Lua start path).
  - A builtin: no tools; the command uses its process's `IO()` and `OS()`.
  - A subagent (`agent_start`): the agent tools only (`additionalTools` null).
    The Agent `CLAUDE.md` builds on it: a subagent's prompt goes in verbatim
    because it comes "from a parent with at least the subagent's power (a
    subagent gets only the agent-management tools)".
  - A process started by `os_start_process`: its runtime's full set, whatever
    its starter had. `IHaisosOS::StartProcess` is not told who calls;
    `StartProcessOptions` holds only `interactiveAgent`; the child gets a clone
    of the OS's environment.
- **How a set is used.** The agent reads `GetAvailableToolDescriptions()` once,
  at construction ("tool registries must be populated before agents are
  created", Agent `CLAUDE.md`) and sends it in order with every request (a
  `tools` array of `function` entries). For each tool call (currently
  `ExecuteToolCalls` in `Agent.cpp`) it calls `CreateTool(name, self)` for
  whatever name the model sent, listed or not; no tool gives an
  `Error: Unknown tool - <name>` result. A Lua script gets a global per name in
  `GetAvailableTools()` and a `CreateTool(name, nullptr)` per call. So the
  lists are what a program is shown, `CreateTool` what it can get, and only the
  join calls `HasTool`.
- **Texts naming other tools.** `agent_start`'s description sends the model to
  `agent_wait_to_finish`, `agent_query` and `agent_list_running`; an
  interactive agent's system prompt (currently `kInteractiveAgentSystemPrompt`
  in `HaisosOS.cpp`) tells it to call `self_close`; `os_start_process`'s
  mentions `/bin/ls`.
- **Limits.** `agent_start` refuses a caller at depth 5 (currently
  `MAX_SUBAGENT_DEPTH`) but is still shown to it; nothing bounds breadth.
- **Starting processes from inside** goes only through `os_start_process` (an
  agent's tool, a Lua global). A builtin could call `process.OS()->StartProcess`
  but none does, and a builtin only runs when something starts it. The
  haisosfile's `RUN` calls `StartProcess` itself.
- **Security model.** Root `CLAUDE.md`: `ICurrentProcess` is the only door out
  of a process; narrowing a process is handing it a narrower OS; the policy
  comes later. What exists is filesystem confinement for a whole OS (`RO`,
  `SUB`, `MEM`, `COMPOSED`, `MOUNT`): every `RUN` shares one OS.
  `IHaisosOS::CreateSubOS` exists, called by tests only; the per-process
  narrowed OS is not built ("A narrowed per-process OS would be passed here
  instead", in the OS's agent start path).
- **History.** Until commit `db90664`, `SubOSPermissions::allowStartProcess`
  gave a sub-OS's processes no `os_start_process` and made its `StartProcess`
  refuse; the root OS always allowed it. It went with the struct: "What
  confines it is the root filesystem it is handed ... rather than a permissions
  field the OS enforces itself", leaving "allowStartProcess, which let a sub-OS
  be created without the os_start_process tool" with "no replacement yet".
  `os_start_process`'s `CLAUDE.md` still lists "process-starting disallowed for
  this OS" among its errors, and gives the child parent pid 0.
- **Build.** The ToolFactory library links every agent tool, `agent_start`
  included, so `agent_start` cannot use code in it without a link cycle. The
  Agent library links the ToolFactory library (no Agent source includes it), so
  HaisosOS, linking Agent, links it too.
- **Tests.** Six hand-written `IToolFactory` doubles (Agent, LuaProcess,
  ServicesCreator and agent_start tests). `CreateAgent` is called by the OS,
  `agent_start`, six integration tests and the ServicesCreator tests, and
  implemented by the agent_start tests' service double. The LLM cache proxy
  keys a recording by the SHA-256 of the request body, tool list included, in
  order; only `.gitkeep` is committed.
- **Outside Haisos.**
  - Anthropic's API rejects duplicate tool names (400, "tools: Tool names must
    be unique."), and OpenAI-compatible gateways have returned the same error
    (openclaw #14775). Claude Code's own subagents hit it, the parent's tools
    apparently merged with the subagent's defaults once more
    (anthropics/claude-code #10668). Names are `[a-zA-Z0-9_-]` only -- up to
    128 characters for Anthropic, 64 for OpenAI -- so no dots, and Anthropic
    advises prefixing a tool by its service.
  - Claude Code subagents: `tools` is an allowlist ("Inherits every tool
    available to subagents if omitted"), `disallowedTools` a denylist: "If both
    are set, `disallowedTools` is applied first ... A tool listed in both is
    removed"; `mcp__<server>` patterns; MCP tools named
    `mcp__<server>__<tool>`; at the depth limit the Agent tool is withheld.
  - OpenAI Agents SDK: static filters of allowed and blocked tool names; an
    empty allowlist once exposed every tool (openai-agents-js #1901); duplicate
    names across MCP servers are an error.
  - A last-writer-wins registry let an MCP tool replace a sandboxed built-in of
    the same name (factor-q #177); fixed with namespaces and refused
    duplicates.
  - OpenBSD `pledge(2)`: "Subsequent calls ... can reduce the subsystems which
    still work, but previously revoked subsystems cannot be re-activated",
    `execpromises` for the programs a process runs, promises named by
    capability (`rpath`, `wpath`, `cpath`, `proc`, `exec`). Linux capability
    bounding sets and seccomp filters are likewise inherited and only narrowed.
  - Anthropic prompt caching caches the prefix tools, then system, then
    messages; "Modifying tool definitions ... invalidates the entire cache".
  - Dockerfile `RUN` takes flags before its command: `--mount`, `--network`
    (`none`: no network for that step), `--security`, `--device`.

## Open aspects

### A1. At which levels can a tool set be narrowed?
The seed's examples sit at three levels: a subagent (inside its parent's
process), a process, an OS. A level is where a narrowing is kept and applied;
what is started below it inherits it (D4). Levels combine: what a program gets
is the OS's ceiling, narrowed by its process's, narrowed by its agent's.
Combinable: yes

1. **Per agent** -- `CreateAgent` takes a selection (D7), `agent_start`
   narrows its subagents, and a `.md` program is narrowed through its agent.
   For: subagents need it whatever else is chosen -- they share their parent's
   process and OS, which cannot tell them apart; the smallest step. Against: a
   `.lua` process cannot be narrowed; an agent narrowed below its process that
   keeps `os_start_process` is refused (D4) unless A3 exists.
2. **Per process** -- `StartProcessOptions` gains a ceiling, applied to
   whatever the runtime offers (the agent's set through `CreateAgent`, the Lua
   globals) and handed on to the process's children (A3). For: a `RUN`, or a
   child an agent or script starts, narrowed on its own, Lua included, with no
   sub-OS; `StartProcessOptions` is already "how to run a program". Against:
   inheritance rests on A3's choice; one more thing each start path applies.
3. **Per OS** -- an OS is created with a ceiling for all its processes (a
   parameter of `IFactory::CreateHaisosOS` and `IHaisosOS::CreateSubOS`, a
   sub-OS's never wider than its creator's): "an OS without `os_start_process`"
   literally. The OS gives its processes no `os_start_process` but does not
   refuse `StartProcess` itself: it cannot tell its creator's `RUN` from a call
   from inside, and nothing inside reaches `StartProcess` but through that tool.
   For: inherited by construction, whatever starts what; the root `CLAUDE.md`'s
   own model; what `allowStartProcess` was for. Against: the haisosfile builds one
   OS, so this narrows every `RUN` at once; narrowing one `RUN` alone takes a
   sub-OS (its own LLM service and process table, its processes absent from the
   other's `os_list_processes`) or the per-process narrowed OS that is not
   built; commit `db90664` dropped an OS-level switch for "what you give an OS"
   -- though a ceiling given at creation is given, as the builtin commands are.

### A2. Do subagents get OS tools, and by default which?
Today a subagent has only the agent tools, so "a subagent without
`os_start_process`" has nothing to remove. A subagent with OS tools acts as its
process: same pid, same working directory, same `IO()`. Should every agent
become a child of a process (being explored separately), subagents would be
narrowed at A1's process level instead.
Combinable: no

1. **Inherit, narrow on request** -- a subagent gets its caller's whole set
   unless `agent_start`'s selection narrows it, Claude Code's default. The
   registry `CreateAgent` already makes per agent is given the agent's OS part
   and selection, so the `agent_start` it creates can hand them on, narrowed
   (D7). For: subagents read and write files themselves instead of the parent
   pasting contents into prompts; the convention models know. Against: every
   existing subagent gains `os_write_file` and `os_start_process`, a behaviour
   change; the Agent `CLAUDE.md`'s trust sentence is rewritten (its subset
   argument still holds: D4); a subagent narrower than its process that keeps
   `os_start_process` needs its own ceiling handed on (A3's first variant via
   the calling agent the tool ignores today, whose selection `IAgent` would
   then expose) or is refused (D4).
2. **Opt in** -- no selection keeps today's default, the agent tools; an
   explicit selection narrows the caller's whole set, so it can include OS
   tools the caller has. For: nothing changes unless asked; least privilege by
   default. Against: the model has to know to ask; "no selection" then means
   less than "an empty `without`", one more rule for the description to teach.
3. **Never** -- subagents keep the agent tools only; file work is delegated to
   a `.md` process through `os_start_process`. For: nothing changes. Against:
   the seed's subagent example has nothing to narrow; the helper process gets
   its runtime's full set unless A1's process level narrows it.

### A3. How does a process's ceiling reach the processes it starts?
Needed only with A1's process level. D4 says a child gets no more than its
starter; `StartProcess` is not told who calls, so something must carry the
ceiling across it.
Combinable: no

1. **os_start_process passes it** -- the process's OS tool set is built with
   its ceiling and gives it to the `os_start_process` it creates, which starts
   the child with that ceiling narrowed by what the call asks for; C++ callers
   of `StartProcess` pass what they mean. For: the only way a program starts a
   process today, so nothing escapes now; no `ICurrentProcess` change.
   Against: a builtin that starts processes (`env`, `timeout`, a shell -- none
   yet) cannot read its process's ceiling and would start children with none.
2. **A narrowed OS per process** -- the process's `OS()` is a narrowed OS
   carrying the ceiling (the root `CLAUDE.md`'s "narrowed clone"), and every
   `StartProcess` through it applies it. For: one place, nothing to remember,
   builtins included; the same object the root `CLAUDE.md`'s per-user root
   needs. Against: not built; its ownership is a design of its own (a process
   holds its OS weakly, because the OS owns its processes) that belongs to the
   security-policy work.
3. **ICurrentProcess exposes it** -- the ceiling is readable from inside the
   process, and every starter -- `os_start_process`, a builtin -- passes it on.
   For: builtins can do it right. Against: still every starter remembering;
   `ICurrentProcess` grows, with every process class and fake.

### A4. Where are narrowings written?
The seed names C++, `agent_start`, the haisosfile and the program. C++ is D7's
and D8's; which of the rest apply follows from A1's levels. Every place obeys
D3 and D4.
Combinable: yes

1. **RUN flags** -- e.g. `RUN --tools=... --without-tools=... /agent.md`
   (spelling the plan's), in front of the path as `-i` is, as Dockerfile `RUN`
   takes `--network=none`. For: per `RUN`, where the haisosfile says what runs;
   fits A1's agent and process levels directly. Against: with A1's OS level
   alone, a flagged `RUN` needs a sub-OS of its own.
2. **agent_start parameters** -- two optional arrays (absent and `[]` told
   apart, as the tools' argument reader already allows), naming only tools the
   caller has: anything else is an error result, and nothing starts. For: the
   model narrows per delegation, from the names in its own tool list. Against:
   the schema and description grow, and every request carries them.
3. **An OS-wide directive** -- e.g. `TOOLS without os_start_process`, one
   ceiling for the haisosfile's OS. For: "an OS without `os_start_process`" in
   one line. Against: needs A1's OS level; narrows every `RUN`.
4. **Program frontmatter** -- a `.md` program declares the tools it wants in a
   front block (`tools:` / `without-tools:`, as Claude Code's agent files do),
   narrowing within its ceiling. For: least privilege set by whoever wrote the
   program, travelling with it; a program can only narrow itself. Against:
   programs are posted whole today, so the block must be recognised and cut
   out; a small parser; `.lua` programs get no equivalent.
5. **os_start_process parameters** -- the starter narrows the child it starts.
   For: an orchestrating agent or script can start a read-only worker. Against:
   needs A1's process level; one more schema to learn.
6. **Named tool sets** -- declared like `FS` (`TOOLSET readonly only
   os_read_file os_list_directory`) and used by name in the places above. For:
   one definition, reused. Against: a directive and a namespace for what a flag
   says in one line.

### A5. Are there names for groups of tools?
With names alone, "read-only" is a list that misses any tool added later --
safely for an allowlist, silently for a denylist -- and a family whose
descriptions refer to each other can be split (A6).
Combinable: yes (2 with 3)

1. **Names only** -- For: nothing to define. Against: as above; a second
   process-starting tool (the shell tool of `notes/explore-stdio-exit-codes.md`
   A4) would slip past every `without os_start_process`.
2. **Capability groups** -- a few names for what tools can do, as pledge's
   promises: e.g. `read` (`os_read_file`, `os_list_directory`,
   `os_list_processes`), `write` (`os_write_file`), `process`
   (`os_start_process`), `agents` (the `agent_*` tools). For: "only `read`" is
   the seed's "only read-only tools", a new tool joins its group, families stay
   whole. Against: a taxonomy to keep and document; each new tool is classed by
   the most it can do (a shell tool is `process`).
3. **Prefix patterns** -- `os_*`, `agent_*`, as Claude Code's
   `mcp__<server>__*`. For: no taxonomy; the prefixes exist. Against: a prefix
   says where a tool comes from, not what it can do: `os_*` mixes reading,
   writing and starting, and `self_close` and `get_current_date_time` match
   none.

### A6. What about texts that rely on a tool the set lacks?
`agent_start`'s description advises `agent_wait_to_finish`, `agent_query` and
`agent_list_running`; an interactive agent's system prompt says to close with
`self_close`. A carelessly narrowed set keeps advice the model cannot follow.
These texts are also being reworked (tool messages that make agents finish
sooner, explored separately).
Combinable: no

1. **Set-aware texts** -- a description or prompt leaves out what names a tool
   the set lacks. For: any selection stays coherent. Against: texts assembled
   per set, in texts that are in flux.
2. **Refuse such selections** -- an interactive agent without `self_close`, or
   `agent_start` without `agent_wait_to_finish`, is an error. For: fixed texts,
   simple rules. Against: a rule per text, kept in step by hand; less freedom.
3. **Families only** -- the agent tools go and come as one group (A5),
   `self_close` with interactivity. For: no dangling advice by construction.
   Against: needs groups; `agent_list_running` alone cannot go.
4. **Leave them** -- For: nothing to build. Against: following the advice costs
   a round and an unknown-tool error; an interactive agent without `self_close`
   cannot end its session before end of input.

### A7. Does Haisos withhold tools it knows will fail?
An agent at the depth limit is shown `agent_start`, which always refuses it,
and the other `agent_*` tools, which serve only children it cannot have.
Combinable: no

1. **Withhold** -- such an agent is created without the `agent_*` tools, as
   Claude Code withholds its Agent tool at its depth limit; `agent_start`'s
   own check stays as the backstop. For: no round spent on a refusal; a first
   use of D2 and D7. Against: agents at that depth see a different list.
2. **Keep** -- For: no change. Against: the refusal round stays.

### A8. What are the composition operations called?
A matter of taste, read wherever a set is built.
Combinable: no

1. **Decorator classes** -- `CompositeToolFactory` kept for the join, now of
   any number, and a filtering class beside it (e.g. `FilteredToolFactory`),
   each with its `Create()`. For: the codebase's shape; the known name stays.
   Against: "factory" names for set algebra.
2. **Free functions** -- e.g. `JoinToolFactories(...)` and
   `NarrowToolFactory(set, selection)` returning an `IToolFactory`, their
   classes hidden, as `CreateServicesCreator()` hides its class. For: reads as
   what it does. Against: a second style beside `Create()`.
3. **Rename to IToolSet** -- `IToolFactory` becomes `IToolSet` (the user's
   "tools-set"), and the operations read as set operations. For: names what
   composition acts on; fits tools made once per set rather than per call
   (`notes/note-tool-caller-twice.md`). Against: a rename across `interfaces/`,
   three implementations, six test doubles and the documents, for a name.

## Decided aspects

### D1. Joining sets
**Chosen:** one join of any number of sets, over `IToolFactory` as it is. Each
name is listed once, in operand order and in each operand's own order within
it, so an agent process's list stays as today (OS tools first) and no request
changes; `HasTool` and `CreateTool` go to the operand that has the name. A name
two operands both have is an error: logged, and the composition refused, as a
`Create()` refuses a bad argument today; a unit test pins that the built-in sets
never clash. Replacing a tool on purpose is written as dropping it (D2) and
joining the replacement. The operands are peers: the "shared"/"owned" roles and
their stale comment go. Why: providers reject duplicate names (Anthropic's 400),
and Claude Code's subagents hit it, apparently by adding a parent's tools to the
defaults once more -- the very shape of `CreateAgent` handed a parent's whole
set as `additionalTools`; today's sets are disjoint by construction, so a clash is a
wiring bug, which silent precedence hides, and in a registry built that way an
outside tool replaced a sandboxed built-in (factor-q #177); the project reports
mistakes rather than absorbing them (a duplicate `FS` name, a `BUILTIN` onto a
taken path). A tool name is also a Lua global, and none of Haisos's clashes with
Lua's.
**Ignored:**
- Listing a name twice, as today -- rejected by providers, ambiguous to a model
  otherwise.
- The first or the last operand winning silently -- hides wiring bugs, and is
  how shadowing happens.
- Namespacing every set now -- every tool is Haisos's own and already prefixed
  by family (`os_`, `agent_`); tools from outside (MCP) would get a prefix of
  their own the day they come, with `_` or `-` as the separator, the only
  punctuation a name may hold.
- A binary join nested for more -- the seed asks for two or more, and nesting
  only spreads the clash check.

### D2. Narrowing a set
**Chosen:** a filter over any set, applied to all four methods: a dropped name
is not listed, `HasTool` says no, `CreateTool` returns nothing. A model calling
it anyway gets today's unknown-tool error result; a Lua script finds the global
nil. The filter forwards whatever `CreateTool` takes
(`notes/note-tool-caller-twice.md` may drop the caller). `IToolFactory` stays as
it is: composing from outside works on every set, test doubles included. Why:
the agent calls `CreateTool` for every name the model sends, listed or not, and
the Lua bridge does per call; a tool hidden only from the lists is one guessed
name away.
**Ignored:**
- Hiding from the lists only -- above.
- Refusing only when called -- the model is shown a tool it cannot use, as with
  the service-less agent registry's `agent_start`, and `agent_start` at the
  depth limit, today.
- Each registry filtering itself, as `allowStartProcess` did inside the OS's
  set -- a copy per registry, and none for a joined set.
- Composition methods on `IToolFactory` -- three implementations and six test
  doubles would each implement them.

### D3. What a selection says
**Chosen:** two lists, as Claude Code's `tools`/`disallowedTools` and the OpenAI
Agents SDK's allowed/blocked names: `only` -- absent means every tool, empty
means none -- and `without`; `without` is applied first, and a name in both is
dropped. Narrowing a ceiling further intersects: the `only` lists intersect
(absent standing for every tool), the `without` lists unite. A name no Haisos
tool has is an error where it is written (the haisosfile at start-up,
`agent_start` as an error result); whether a real tool that a runtime never
offers (`agent_start` for a `.lua`) is an error or a no-op is the plan's call.
When an interface takes it, it is a value type in `interfaces/` beside
`StartProcessOptions` (`notes/explore-valid-shared-ptr.md`, D2); its name is the
plan's. Why: the seed's examples need both shapes ("without
`os_start_process`", "only read-only tools"); an empty allowlist read as "no
filter" exposed every tool in openai-agents-js (#1901); a typo in a denylist
leaves the tool on, and the haisosfile already refuses names it does not know
(`BUILTIN`, `-- key=value`).
**Ignored:**
- An allowlist only -- "all but `os_start_process`" re-lists ten tools, and
  again whenever one is added.
- A denylist only -- cannot say "only these": a tool added later slips in.
- Unknown names ignored -- a silent hole, or a silently missing tool.

### D4. Nothing gets more than its starter
**Chosen:** whatever levels A1 picks, a narrowing is inherited downwards and
never widened: a subagent's set is within its caller's, a process started by a
narrowed program within its starter's, a sub-OS's ceiling within its creator's.
Asking for more is an error (`agent_start` naming a tool its caller lacks).
Where no ceiling is carried across `StartProcess` (A3), a selection that
narrows a program below its process yet keeps `os_start_process` is refused:
its `.md` or `.lua` child would get every OS tool, and `/bin/mkdir` would write
regardless. Why: the Agent `CLAUDE.md` passes a subagent's prompt verbatim
because its parent has "at least the subagent's power"; pledge ("previously
revoked subsystems cannot be re-activated", `execpromises` for what it runs),
capability bounding sets and seccomp filters only ever narrow; Claude Code's
subagents draw on their parent's pool.
**Ignored:**
- Children free of their starter's narrowing -- every narrowing one
  `os_start_process` away from void.

### D5. Read-only
**Chosen:** "only read-only tools" is an allowlist -- `os_read_file`,
`os_list_directory`, `os_list_processes`, plus whichever non-OS tools are
wanted -- never "without `os_write_file`", which keeps `os_start_process`: a
builtin such as `mkdir` writes through its own process's `IO()`, and a `.md` or
`.lua` child gets every OS tool. What makes an OS read-only for every runtime,
builtins included, is its root filesystem (`RO`, `SUB`), which the haisosfile
already builds and `notes/note-review-medium-findings.md` (section 6) wants in
the `--init` template. The two together are the whole of it: the root refuses
writes, and the set spares the model a tool that can only fail.
**Ignored:**
- Deriving the set from the root -- writability varies within a composed root
  (a `MEM` filesystem mounted into an `RO` one), so a tool cannot be judged
  useless up front.
- Read-only by denylist -- above.

### D6. Composed once
**Chosen:** a program's set is composed when the program is created and never
changes after. Why: the agent reads its tool list once, at construction, by
documented contract; the Lua globals are bound once; providers cache the prompt
prefix starting with the tools, and changing a tool definition invalidates the
whole cache; recordings are keyed by the request.
**Ignored:**
- A tool through which a running program narrows itself, pledge's model -- its
  list would change mid-conversation, with the history naming tools no longer
  defined; a program wanting fewer tools says so at its start (A4's
  frontmatter).

### D7. An agent's set is narrowed by the service that creates it
**Chosen:** `ILLMService::CreateAgent` takes a selection -- defaulting to every
tool, so no caller has to change -- which the service applies to the whole set
it composes: its own agent tools and `additionalTools` alike. The registry it
already makes per agent can then carry what that agent's `agent_start` needs to
narrow a subagent (A2). Why: any of the eleven tools can then be dropped (an
agent without `agent_start`; A7) while composing stays in the one component
that creates agents; `agent_start` passes data, needing no
composition code -- which it could not take from the ToolFactory library, since
that library links it -- and the OS keeps handing its set over as today.
**Ignored:**
- `CreateAgent` taking the complete set -- every caller composes it, and
  `agent_start` would need its caller's whole set: through `IAgent`, a
  "deliberately observe-and-talk-to handle" that parents and tools hold alike,
  or through a back-reference into the join that holds the registry itself.
- Agent tools never narrowed, only the OS part (filtered by the OS before
  handing it over) -- no agent without `agent_start`, and no A7.

### D8. What else follows
**Chosen:**
- The join and the filter form a library of their own in the ToolFactory
  component, beside the agent tools' registry (as `ProcessFileIO` sits beside
  the OS in the HaisosOS folder), needing only `interfaces/` and the Logger.
  LLMService links it for agents; HaisosOS links it if A1 narrows processes, to
  filter a Lua script's OS set with the same code, without linking the agent
  tools. HaisosOS reaches the ToolFactory library today only through the Agent
  library's link, which no Agent source uses and which
  `notes/explore-llm-communicator-place.md` removes (its D4 drops HaisosOS's
  link to Agent; its D5 has the build follow the code). Nothing the agent
  tools' library links needs them (D7).
- Tests, in `tests/unit/components/ToolFactory.unittests/` (none cover the
  join today): each name listed once and in order; a clash refused; the real
  agent and OS sets joined without one; a dropped tool neither listed, nor
  reported by `HasTool`, nor created; absent versus empty `only`; `without`
  first; intersection; unknown names. An Agent test: a dropped tool the model
  calls anyway gets the unknown-tool result. Per level chosen, a HaisosOS test:
  a narrowed Lua script finds the global nil, a narrowed agent is not offered
  the tool, and D4's inheritance holds.
- Documents: the ToolFactory `CLAUDE.md` ("Merges two tool sets", its key
  classes); the LLMService `CLAUDE.md` and `ILLMService`'s comments
  (`CreateAgent`'s selection; `GetToolFactory`'s "shared by every agent", when
  each agent has its own); the HaisosOS `CLAUDE.md`; the Agent `CLAUDE.md`'s
  subagent sentence if A2 changes it; the root `CLAUDE.md`'s Tools section;
  `os_start_process`'s `CLAUDE.md` (the "process-starting disallowed" error,
  parent pid 0); and, for what A4 adds, the haisosfile section and the
  `haisos --init` template.
- Any request whose tool list changes gets a new hash in the cache proxy; with
  only `.gitkeep` committed, nothing is re-recorded.
**Ignored:**
- In the agent tools' library, where the join is today -- HaisosOS would link
  every agent tool to filter its own set.
- Header-only, in libheaders -- implementations of interfaces live in
  components.

## Checked

- The root `CLAUDE.md` (Security, Creating things, Tools, the haisosfile DSL,
  Builtin Commands); the `CLAUDE.md` of ToolFactory, LLMService, HaisosOS,
  Agent, LLMCommunicator, libheaders, ServicesCreator and Factory; the
  `CLAUDE.md` of every tool under `src/tools/`.
- `interfaces/`: `ILLMService.h` (`IAgent`, `ITool`, `IToolFactory`,
  `IAgentConsole`, `ILLMService`), `IHaisosOS.h` (`StartProcessOptions`,
  `StartProcess`, `CreateSubOS`), `IProcess.h`, `IFactory.h`,
  `IServicesCreator.h`, `IEnvironment.h`.
- ToolFactory component (the agent registry, the join, CMake links);
  LLMService (`CreateAgent`'s composition, the per-agent registry,
  `GetToolFactory`, shutdown); HaisosOS (the OS tool set, the agent, Lua and
  builtin start paths, the interactive system prompt, `CreateSubOS`);
  `LuaProcess` (global binding, `CreateTool` per call); `Agent` (cached
  descriptions, tool-call handling); `LLMCommunicator` (the `tools` array);
  every tool's description and `Call` (which use the caller);
  `agent_tools_common`, `tools_common`'s argument readers (the optional
  overload); `CurrentProcessHandle`; `src/haisos/` (`RUN` parsing and start; no
  tool directive); the CMake links of Agent, HaisosOS, the tools and Factory.
- Tests: `ToolFactory.unittests` (no join test), the six `IToolFactory`
  doubles, the `CreateAgent` callers and the agent_start tests' service double;
  `tests/tools/llm_cache_proxy/CLAUDE.md`.
- Git: `db90664` (`SubOSPermissions` and `allowStartProcess` removed; message
  quoted), `a2ae91c` (where they were introduced), `39b17ff` (the join and the
  per-process OS tool set, as merged), `c54efcf` (the tools' argument readers,
  `CreateAgent` refusing at shutdown), `git log -S CompositeToolFactory`.
- Every file in `notes/`, including `note-tool-caller-twice.md`,
  `explore-valid-shared-ptr.md` and `explore-llm-communicator-place.md`, added
  while this was written;
  `note-lua-json-crash.md` and `plan-critical-review-fixes.md` were read before
  they left the working tree.
- External: Anthropic API docs (define tools: name pattern, prefixing advice;
  prompt caching: prefix order and invalidation); anthropics/claude-code
  #10668 ("tools: Tool names must be unique." on subagents) and the Claude Code
  subagent docs (`tools`, `disallowedTools`, `mcp__<server>` patterns, depth
  limit); OpenAI Agents SDK MCP filters, openai-agents-js #1901 (empty
  allowlist), openai-agents-python #464 (duplicate names across MCP servers);
  openclaw #14775 (duplicates through an OpenAI-compatible gateway); factor-q
  #177 (shadowed built-ins); OpenBSD `pledge(2)`; the Dockerfile reference
  (`RUN` flags).
