# Exploration: Where ILLMCommunicator belongs, and what it is for

## Seed

> `ILLMCommunicator` (`interfaces/ILLMCommunicator.h`) talks to an LLM endpoint: it
> builds a request from an agent's history and tools, sends it through an
> `IHTTPClient`, and parses the response and its tool calls. Today it is a
> top-level interface of its own, although only the LLM service creates one
> (`LLMService::CreateAgent` makes one per agent and hands it to the `Agent`), and
> `interfaces/IFactory.h` includes its header. What is its place? Shouldn't it be
> under `ILLMService` -- created, owned and hidden by the LLM service -- and if it
> stays a public interface, what is it for?

## Base

- Branch: `task/code_review_max`
- Commit: `8515a90` (Add full physical filesystem and Windows drive-letter paths)
- Related notes:
  - `notes/note-builtin-placement-friend.md` -- the other leak of the LLM
    service's insides: HaisosOS casts to the concrete `Agent` and its agent
    process holds one, while `IAgent::AddChild` is meant for `LLMService` alone.
    D4 closes that leak from this side; its choice of pattern for friends of
    concrete classes (friend, passkey, narrower interface) comes back if a
    communicator ever reads a secret (A4).
  - `notes/explore-builtin-command-host.md` -- the precedent D1 follows: it
    keeps `IBuiltinCommand`, an interface internal to the BuiltinCommands
    component, out of `interfaces/`, where it would serve nothing (its D1).
  - `notes/explore-valid-shared-ptr.md` -- counts HaisosOS's downcast to
    `Agent` among the pointers that may be null (a dynamic cast that fails); D4
    removes it. Its D2 (the vocabulary types the interfaces use live in
    `interfaces/`) agrees with `LLMMessage`/`LLMResponse` leaving with the only
    interface that uses them; its D3 makes `CreateLLMService`'s network
    service, the source of every communicator's HTTP client, never null.
  - `notes/explore-stdio-exit-codes.md` -- its D13 sends an agent's LLM, HTTP
    and parse failures to stderr with exit code 1, which needs the communicator
    to report a failure apart from the model's words (A3).
  - `notes/note-review-low-findings.md` -- section 5 (errors recorded as the
    model's words, `is_error` never sent, the Anthropic branch dead) feeds A2
    and A3; section 8 (`<tuple>` missing from `ILLMCommunicator.h`) is fixed
    when the header moves (D5).
  - `notes/note-review-medium-findings.md` -- section 3: the integration tests
    that drive the communicator directly pass with no LLM at all; they keep the
    concrete class (D6). Section 5: the history copied into every `Call`,
    unaffected.
  - `notes/note-repeated-json-lookups.md` -- the response parsing inside the
    implementation; it moves with it, unchanged.
  - `notes/note-agent-program-role.md` -- a second system message, which
    protocols take differently (Anthropic has no system role); part of the
    message model A2's third variant would define.
  - `notes/note-runtime-thread-destruction.md` -- the deleter an agent is
    created with, which D4's `IAgent` pointers keep.
  - An exploration of how tool parameters are encoded for several LLM
    protocols (OpenAI chat completions, Anthropic, Gemini), written at the same
    time and not yet in `notes/` when this was finished, overlaps A2.

## The issue

`ILLMCommunicator` is the one piece of the LLM stack still standing alone in
`interfaces/`: only the LLM service creates one (per agent), only the agent
calls one, and no other interface names it. The seed asks where it belongs --
under `ILLMService`, created, owned and hidden by the LLM service -- and, if it
stays public, what for. This exploration looks at who creates, holds, calls and
fakes it; how it came to be public; what `interfaces/` is for here and how the
code already keeps an interface inside a component; what "hidden" takes beyond
moving one header (the concrete `Agent`, which HaisosOS reaches, carries its
types, and `IFactory.h` spreads them); and what the interface is for once
hidden -- the boundary a second LLM protocol would plug into, and the agent's
test seam -- with what that means for its types, its failures and its
configuration. Done means: no header in `interfaces/` declares or includes the
communicator or its message types; only the LLM service's own components and
their tests see them; HaisosOS holds its agents as `IAgent`; the build links
follow the code; the documents and skills name the new place; nothing a
haisosfile, an agent, a person or an LLM endpoint sees changes (the request
bytes stay identical); and the unit tests pass -- plus whatever the open
aspects add.

## What exists today

- **The interface.** `interfaces/ILLMCommunicator.h` declares `ILLMCommunicator`
  with one method, `Call(messages, availableTools)`, returning an
  `LLMResponse`, and the types it speaks: `LLMMessage` (role, content, name,
  thinking, tool_call_id, the tool calls as raw JSON objects -- `toolCallsJson`
  -- and is_error) and `LLMResponse` (the message, `done`, `done_reason`).
  `availableTools` has the type `IToolFactory::GetAvailableToolDescriptions()`
  returns (name, description, JSON schema). The header also puts a `json` alias
  into the whole `Haisos` namespace, and lacks `<tuple>`. The last change to it
  was commit `39b17ff`, which only removed a callbacks parameter.
- **Nothing public uses it.** No declaration in `interfaces/` mentions
  `ILLMCommunicator`, `LLMMessage` or `LLMResponse`; the one include of the
  header is `IFactory.h`'s, which uses nothing from it -- while `IFactory`'s own
  comment says it "deliberately knows nothing about agents, LLM communication,
  or tool factories". Every other interface there is a factory or a service, or
  is handed out or taken by a method of another interface (`IHTTPClient` by
  `INetworkService::CreateHTTPClient`; `IAgent`, `IToolFactory`, `IAgentConsole`
  by `ILLMService`; `IFileSystem` by `IFactory` and `IFileSystemService`;
  `IFileIO` by `ICurrentProcess::IO()`; ...), or is `ICurrentProcess` itself,
  the door every runtime, tool and builtin shares. The root `CLAUDE.md`'s `interfaces/`
  line brackets each header's companions (`INetworkService.h [IHTTPClient]`,
  `ILLMService.h [IAgent, ITool, IToolFactory, IAgentConsole]`) and lists
  `ILLMCommunicator.h` last, under no service.
- **How it came to be public.** In the first design (commit `9f67ec6`),
  `IFactory` made HTTP clients, LLM communicators
  (`CreateLLMCommunicator`), tool factories and agents, so the communicator had
  to be public for the wiring. The services refactor (`39b17ff`) moved each
  interface under the service that hands it out -- `IHTTPClient.h` became
  `INetworkService.h`, `IAgent`, `ITool` and `IToolFactory` joined
  `ILLMService.h`, `IFileSystem` joined `IFileSystemService.h` -- and had
  `LLMService` build agents and communicators itself. `ILLMCommunicator.h`
  alone stayed, and `IFactory.h` kept including it.
- **Who creates it.** Only `LLMService`, inside `ILLMService::CreateAgent`: one
  per agent, from an `IHTTPClient` of the service's `INetworkService`, the
  service's endpoint, model and API key, and the agent's path (its ancestors'
  names, then its own), under which every request and response is reported
  (`LogAgentSend`/`LogAgentReceive`, which feed `--log-agent-to-file`). It goes
  to `Agent::Create`, and the agent holds it for its life. HTTP clients are not
  for sharing across threads: the Linux one holds one curl handle and no lock.
- **Where its configuration comes from.**
  `IServicesCreator::CreateLLMService(networkService, endpoint, modelName, apiKey)`,
  called by the OS when it is created (currently in `HaisosOS::Create`) with
  `HAISOS_ENDPOINT`, `HAISOS_MODEL` and `HAISOS_API_KEY` read from the OS's own
  environment. So every agent of an OS -- process agents and subagents alike --
  talks to one endpoint and one model; a sub-OS gets a service of its own from
  its own environment; the environment a process is started with is never
  consulted for it -- although the root `CLAUDE.md` says a process handed a
  `Clone()` "gets the LLM configuration along with everything else" (the
  HaisosOS `CLAUDE.md` says so of a sub-OS only). `LLMIdentifier` in
  `IEnvironment` (endpoint, model, an API token or the name of a secret) is used
  by nothing in `src/`; `IEnvironment::ReadSecretValue` is protected, its comment
  naming "whatever authenticates an outgoing request on the environment's
  behalf" as a future caller.
- **Who calls it.** Only the agent, once per LLM round, with a copy of its
  history and the tool descriptions it cached when created. The history *is* a
  vector of `LLMMessage`, so the communicator's vocabulary is the agent's too;
  `IAgent::GetHistory()` turns it into JSON, which `agent_query` can hand to a
  model. The agent reads the tool calls itself, in both shapes it may meet
  (under `function`, as Ollama and OpenAI send them, or flat), with arguments as
  an object or a string. It ignores `done` and `done_reason`: an HTTP, API or
  parse failure arrives as an assistant message `Error: ...`, which it prints
  and keeps in its history.
- **The implementation** (the LLMCommunicator component) speaks Ollama's
  `/api/chat`: `model`, `stream: false`, tools as OpenAI-style function objects,
  and messages whose tool results carry OpenAI's `tool_call_id` and a `name`,
  where Ollama documents `tool_name`. It authenticates with a bearer token. It
  parses Ollama's response shape, a flat `content`, and an Anthropic `tool_use`
  branch converted into the OpenAI shape (dead in practice, since requests are
  always Ollama's); it makes up ids for Ollama's id-less tool calls. Its request
  builder and an accessor for its HTTP client are public for its unit tests.
- **Who sees it.** `Agent.h` includes the interface (constructor, member and
  history need it); `LLMService.h` includes `Agent.h`, and `LLMService.cpp` the
  concrete communicator. Outside the LLM stack: HaisosOS, whose agent process
  (currently `AgentProcess`) holds the concrete `Agent` and so includes
  `Agent.h`, and whose agent start path downcasts the `IAgent` that
  `CreateAgent` returns, refusing any other kind; the process then calls only
  `IAgent` methods (`Post`, `TriggerStop`, `WaitToFinish`, `Name`) and hands the
  agent out as an `IAgent`. And, through `IFactory.h` -- included as an umbrella
  by the Console component, HaisosOS, the haisos executable's sources and the
  `agent_*` tools' headers -- all of those see the communicator's types.
- **The build.** The Agent library links the LLMCommunicator library although
  its code uses only the interface, so `Agent.unittests` link the HTTP client
  and libcurl (the build tree's link line shows both); the LLMCommunicator
  library links HTTPClient although it only takes an `IHTTPClient`, and so do
  its unit tests, which use `MockHTTPClient`; the Factory links HTTPClient
  although `IFactory` makes no HTTP clients any more. All date from the first
  design.
- **Tests.** `Agent.unittests` inject the mock in `tests/mocks/` (currently
  `MockLLMCommunicator`: canned replies, raw tool calls, a throw on the n-th
  call, a hook run on the agent's thread mid-round, the last messages seen) into
  `Agent::Create`. `ServicesCreator.unittests` fake the LLM a level lower, with
  an `INetworkService` whose `IHTTPClient` answers in Ollama's JSON.
  `LLMCommunicator.unittests` drive the concrete class over `MockHTTPClient`.
  Three integration tests (`LLMCommunicator.2Plus3`,
  `LLMCommunicator.HandlesErrorGracefully`, `RunWithSimplePrompt`) build the
  concrete communicator on an HTTP client from the HTTPClient component;
  `Call.get_current_date_time` includes its header without using it; the agent
  integration tests go through `IServicesCreator`. None needs the interface in
  `interfaces/`.
- **Precedents.** `IBuiltinCommand`, the interface of one builtin command,
  lives inside the BuiltinCommands component, implemented once per file under
  `commands/` and made by factory functions, while the component's public face
  is `IBuiltinCommands`. The HaisosOS folder holds several internal parts -- the
  agent and Lua processes, the input loop, the OS tool factory -- and
  `ProcessFileIO` as a CMake library of its own.
- **Rules that bear on it.** "Creating things" in the root `CLAUDE.md`
  (private constructors and `Create()` for every class implementing an
  interface from `interfaces/`); `ICurrentProcess` as the only door out of a
  process; the NetworkService as "the only place HTTP clients come from"; site
  and network permissions "not implemented yet".
- **Documents naming it.** The root `CLAUDE.md` (the `interfaces/` line; the
  LLMCommunicator row of the component table), the `CLAUDE.md` of
  LLMCommunicator, LLMService and Logger, the `code-review-llm-protocol` skill
  and its copy in `code-review/references/llm-protocol.md` (both list
  `interfaces/ILLMCommunicator.h`, and `interfaces/IAgent.h`, gone since
  `39b17ff`), and the `todo` skill's order of layers ("HTTPClient/NetworkService,
  LLMCommunicator, Agent/LLMService/ToolFactory").
- **Recordings.** The llm_cache_proxy keys each recording by the SHA-256 of the
  request body, so anything that changes a request's bytes invalidates
  recordings (none are committed today).

## Open aspects

### A1. Where do the interface and its implementation live, once out of interfaces/?
D1 takes the communicator out of the public API; this picks its folder, and so
which component depends on which and how much is renamed. Further protocol
implementations (A2) would go wherever the first one is.
Combinable: no

1. **In the LLMCommunicator component** -- the interface and its two types in a
   header of their own in `src/components/LLMCommunicator/`, beside the
   implementation; the agent includes it from there, depending on that
   component for a header only (D5); `LLMService` stays the only creator; later
   protocol implementations join the component one per file, as the builtins do
   under `commands/`. For: the smallest move -- one header, a dozen includes,
   the documents; today's dependency direction and the `todo` skill's layer
   order (LLMCommunicator below Agent/LLMService) stand; every stable name in
   skills, notes and test folders keeps working. Against: "under LLMService" in
   ownership only; in the tree it still reads as a component of its own.
2. **Folded into LLMService** -- Agent and LLMCommunicator become parts of the
   LLMService component: one folder and one `CLAUDE.md` with several CMake
   libraries (as the HaisosOS folder holds `ProcessFileIO` beside the OS), the
   interface among them. For: literally under LLMService -- what only the
   service builds and uses lives inside it, and one document describes the
   whole LLM stack. Against: the biggest move -- two components leave the root
   `CLAUDE.md`'s tree and table, `Agent.unittests`, `LLMCommunicator.unittests`,
   the integration tests' CMake, the skills and the `todo` skill's layer list
   change; ToolFactory and the `agent_*` tools, also built only for the service,
   raise the same question; it needs D4 done first, or HaisosOS reaches into
   the service's folder.
3. **In the Agent component** -- the consumer owns the port: the conversation
   types and the interface in `src/components/Agent/`, the LLMCommunicator
   component implementing it and depending on Agent. For: the agent depends on
   nothing protocol-related; the history's vocabulary is the agent's own.
   Against: reverses today's dependency, so the concrete communicator, its tests
   and the three integration tests reach into the Agent's folder; the side that
   grows with every protocol is the one reaching across.

### A2. What do the interface's types carry: one protocol's JSON, or neutral types?
As the protocol boundary (D2), the interface is where a wire format should end,
but today it does not: the agent parses tool calls in the shapes Ollama and
OpenAI send, and the request mixes the two protocols' fields. This decides how
much of a second protocol reaches the agent. The exploration of tool-parameter
encoding across protocols may settle it.
Combinable: no

1. **As today, for now** -- the move carries the types unchanged:
   `toolCallsJson` stays the OpenAI/Ollama-shaped JSON the agent reads, and a
   second protocol converts into it, as the Anthropic branch does. For: a pure
   move -- the request bytes, `IAgent::GetHistory()`'s JSON and the mock stay;
   the types are decided by the protocol work, with a second protocol in hand.
   Against: the agent keeps knowing a wire format (two tool-call shapes,
   arguments as object or string); every new protocol converts to and from
   OpenAI's shape.
2. **Neutral tool calls** -- a tool call becomes a typed triple (id, name,
   arguments object), and a tool result keeps its call's id, the tool's name and
   its error flag; each implementation converts both ways, the ids it makes up
   for Ollama included. For: the agent stops parsing wire JSON, and handling a
   malformed call moves to the one place that sees the wire; `is_error` gets a
   meaning per protocol (Anthropic's `tool_result` has the flag) instead of
   never being sent; the mock gets simpler. Against: touches the agent, the mock
   and `GetHistory()`'s JSON (or that keeps today's shape by conversion); the
   Ollama request must stay byte-identical or recordings change.
3. **A neutral message model** -- beyond tool calls: roles as an enumeration,
   system prompts apart from the messages, tool results as their own kind
   (Anthropic sends them as `tool_result` blocks of a user message, Gemini as
   `functionResponse` parts), and the model's thinking kept as the protocol
   returned it (Anthropic's thinking blocks carry a `signature` and must be
   sent back "complete and unmodified" with tool results; a plain `thinking`
   string cannot hold that). For: what an Anthropic or Gemini implementation
   needs anyway. Against: a design of its own, sized for protocols not
   implemented yet; the largest change to the agent.

### A3. How does Call report a failure?
An HTTP, API or parse failure is an assistant message `Error: ...` that the
agent prints and keeps, so later rounds show the model "saying" it
(`notes/note-review-low-findings.md`, section 5), and
`notes/explore-stdio-exit-codes.md` D13 wants such failures reported as the
agent's diagnostics, with exit code 1. It is part of the interface's contract.
Combinable: no

1. **Typed failure in the response** -- `LLMResponse` says the call failed (a
   kind -- transport, API, unreadable response -- and a text; `done_reason`
   half does it today) and carries no message; the agent reports it (console
   now, stderr later), keeps it out of the history, and ends the command. For:
   explicit in the contract; a fake returns one as easily as a reply; what a
   failure means (report, retry, exit code) is decided by the agent. Against:
   every caller must check it before using the message.
2. **An exception** -- `Call` throws; the agent's per-command catch already
   logs, answers the open tool calls, writes `Error: the command failed: ...`
   and keeps the history valid. For: nothing new on the interface; a path
   already tested (the mock's throw on the n-th call). Against: an exception
   for an expected condition (an endpoint down); the report reads as a failed
   command rather than a failed LLM call unless its text says so.
3. **As today** -- a failure stays an assistant message `Error: ...`, told
   apart only by `done_reason`, which the agent ignores. For: no change.
   Against: the history keeps errors as the model's words, and a failure cannot
   be told from an answer, which D13 needs.

### A4. Where does a communicator's configuration come from?
Today there is one endpoint, model and key per LLM service, i.e. per OS;
choosing a model per agent, naming a secret instead of passing a key, and
picking one of several protocol implementations all need more. It shapes
`IServicesCreator::CreateLLMService` and `ILLMService::CreateAgent`, not the
communicator's place: every variant keeps it inside the service, so it can wait.
Combinable: yes (2 and 3 together; 1 excludes both)

1. **Per service, as today** -- endpoint, model and key given to
   `CreateLLMService`, read from the OS's environment when the OS is created.
   For: nothing changes; one haisosfile `ENV` block is one model. Against: no
   agent can use another model than its OS's; `LLMIdentifier` stays unused;
   the root `CLAUDE.md`'s sentence about a process getting the LLM
   configuration must then be narrowed to a sub-OS.
2. **Per agent, from an LLMIdentifier** -- `CreateLLMService` takes a default
   `LLMIdentifier` in place of three strings, and `CreateAgent` an optional one;
   a subagent inherits its parent's unless `agent_start` names another; the
   service builds the communicator, and picks its protocol, from it. For: uses
   the struct `IEnvironment` already has; a secret named there is read inside
   the service, by what authenticates the request -- the caller
   `ReadSecretValue`'s comment anticipates. Against: new signatures on two
   interfaces; how `agent_start` names an identifier, and who may pick which
   model; reading a secret needs a friend or a passkey on `IEnvironment` (the
   patterns `notes/note-builtin-placement-friend.md` weighs).
3. **Per process, from its environment** -- the environment `StartProcess` is
   given decides its agent's LLM (the `HAISOS_*` variables, or an identifier in
   it), the OS passing it on to `CreateAgent`. For: what the root `CLAUDE.md`
   already says; a process started with a narrowed environment can be given
   another model with no runtime knowing. Against: subagents still need a rule
   (their parent's); the configuration is read at every agent start.

## Decided aspects

### D1. Public or internal
**Chosen:** internal to the LLM service -- the communicator, `LLMMessage` and
`LLMResponse` leave `interfaces/`, and `ILLMService` stays the service's only
face (agents, their tools and consoles). Why: no interface declares, takes or
returns them, and the only include of their header is a leftover (D5); only
`LLMService` creates a communicator and only the agent calls one; the services
refactor put every other interface under the service that hands it out, and
none hands this one out; `IBuiltinCommand` is already an interface kept inside
its component, and `notes/explore-builtin-command-host.md` (D1) keeps it there,
since in `interfaces/` it would serve nothing. Hidden, the protocol work (A2,
A3, a second protocol) changes no public header, and `IFactory.h`, the umbrella
include, stops carrying LLM wire types into Console, HaisosOS, the haisos
executable and the tools. Hiding is about the API, not about security: the
communicator's HTTP client comes from the OS's own `INetworkService` wherever
the communicator lives, and that is where network reach is decided.
**Ignored:**
- Keeping it a top-level interface (today) -- a public type no public API uses,
  left behind by the refactor that gave every other interface a service.
- Declaring it in `ILLMService.h` with nothing handing it out -- `IProcess.h`
  includes `ILLMService.h`, so every process, tool and builtin would see it:
  wider, not hidden, and "under" a service that never mentions it.
- A door on `ILLMService` -- a method handing out a communicator (for an
  `LLMIdentifier`, say) or making a one-shot call, for Lua scripts, a builtin,
  or summarizing a history. Nothing asks for a raw LLM call today: no builtin,
  tool, Lua global or note. The day something does, the interface moves into
  `ILLMService.h` together with that method, as `IHTTPClient` sits in
  `INetworkService.h` beside `CreateHTTPClient`; a process would still reach it
  only through `ICurrentProcess::OS()`, and `IHaisosOS` hands out its services
  creator, not the LLM service it made.
- Under `INetworkService`, as an "LLM client" beside `IHTTPClient` -- the
  protocol is LLM knowledge, not transport, and the network service already
  supplies the transport.

### D2. What it is for
**Chosen:** it stays an interface, under its name, with two jobs. It is the LLM
service's protocol boundary: on one side the conversation -- the agent's
history and tool loop -- and on the other one wire protocol over an
`IHTTPClient`: encoding the request, the authentication headers, parsing the
response and its tool calls, mapping failures (A3), and reporting the traffic
under the agent's path. Its implementations are protocol adapters -- one today
(Ollama's `/api/chat`, with an Anthropic-response branch), more with the
protocol work -- and the service, which makes them, is what will pick one per
agent once there are several, from the configuration (A4). And it is the
agent's test seam: the agent is tested with no HTTP and no wire format. It is
not an extension point for anything outside the service.
**Ignored:**
- No interface -- the agent on the concrete class, its tests faking HTTP as the
  ServicesCreator tests do: every agent test would be written in Ollama's JSON
  and lose the mock's hooks (the throw on the n-th call, the hook on the agent's
  thread), and several protocols need the polymorphism anyway.
- A callable (`std::function`) in its place -- a communicator has state (its
  client, its configuration, its agent path), and the project's objects are
  interfaces made by `Create()`.
- A thin HTTP wrapper, with the protocol in the agent -- the agent would learn
  every protocol.
- Renaming it (`ILLMClient`, `ILLMProtocol`) -- churn across skills, notes and
  tests for nothing; "communicator" still fits several protocols.

### D3. Who creates and owns one
**Chosen:** unchanged -- `LLMService` creates one per agent in `CreateAgent`
and hands it to `Agent::Create`; the agent owns it for its life; `ILLMService`
gains no factory method. The concrete class keeps its private constructor and
`Create()`: the rule in "Creating things" names `interfaces/`, but its reason,
one ownership story, holds inside a component too. Why: each agent reports its
traffic under its own path and needs an HTTP client of its own -- agents run on
threads of their own, and the Linux client holds one curl handle and no lock;
besides its client, a communicator holds only configuration, so sharing one
would save nothing.
**Ignored:**
- One communicator per service -- the agent path would be passed with every
  call, and one client used from every agent's thread would need a lock
  serializing every agent's LLM calls.
- A pool -- nothing to pool.
- The agent making its own -- it would need the network service and the
  configuration, which are the service's.

### D4. The concrete Agent stays inside the LLM service too
**Chosen:** HaisosOS holds the agents it runs as `IAgent`: its agent start path
stops downcasting what `CreateAgent` returns, its agent process holds an
`IAgent`, and HaisosOS neither includes `Agent.h` nor links the Agent library.
Why: the communicator's types reach HaisosOS through `Agent.h`, so without this
the hiding would be nominal; the process calls only `IAgent` methods and hands
the agent out as one; `IAgent`'s destructor is virtual and the
`DestroyOffRuntimeThreads` deleter lives in the control block, so an `IAgent`
pointer owns the agent as fully -- the HaisosOS `CLAUDE.md`'s "holds the
concrete Agent because it owns the agent's lifetime" does not need the concrete
type; the downcast only refuses agents made by another `ILLMService`, and no
test relies on it. `notes/note-builtin-placement-friend.md` points at the same
leak, and `notes/explore-valid-shared-ptr.md` loses one nullable result. Inside
the service nothing has to change: `LLMService` may keep concrete agents, being
their creator (its comment's reason, that stopping is not part of `IAgent`, is
stale since `IAgent::TriggerStop`).
**Ignored:**
- Keeping the concrete Agent in HaisosOS -- HaisosOS would go on seeing the
  conversation types, and under A1's second variant would reach into the
  service's folder.
- Forward-declaring the communicator in `Agent.h` -- the history member needs
  `LLMMessage` complete, so the types would still leak.

### D5. The leftovers of the first design
**Chosen:** they go.
- `IFactory.h` stops including `ILLMCommunicator.h`. Checked: all 103 existing
  translation units of the Linux build compile (syntax only, with the build's
  own flags) against a copy of `interfaces/` without the include; the Windows-
  and WASM-only sources neither include `IFactory.h` nor use the LLM types; the
  header's namespace-wide `json` alias is used by nothing but the header
  itself.
- The build follows the code: the agent needs the interface's header (and
  nlohmann/json), not the LLMCommunicator library with its HTTP client and
  libcurl; the LLMCommunicator library and its unit tests need no HTTPClient (an
  `IHTTPClient` comes from outside, `MockHTTPClient` in the tests); nor does the
  Factory (the network service brings HTTPClient into the program anyway). The
  integration tests that call the HTTP client's creator directly keep their
  link.
- The moved header includes `<tuple>` and drops the namespace-wide `json`
  alias.
**Ignored:**
- Leaving them -- the include contradicts `IFactory`'s own comment, and the
  links put curl into the agent's unit tests.

### D6. The tests
**Chosen:** the seams stay. `Agent.unittests` keep injecting the mock in
`tests/mocks/`, which includes the interface from its new place;
`ServicesCreator.unittests` keep faking at `INetworkService`, the one way into
an LLM service from outside, which also runs the real protocol code;
`LLMCommunicator.unittests` and the three integration tests keep the concrete
class (tests already include component headers, as `Agent.unittests` include
`Agent.h`); `Call.get_current_date_time` drops its unused include, and the
agent integration tests their explicit link to the LLMCommunicator library,
which the services bring anyway.
**Ignored:**
- Moving the mock into `Agent.unittests`, its only user -- `tests/mocks/` is
  where test doubles live, and LLMService or protocol tests may use it next.
- An injection point on `ILLMService` or `IServicesCreator` for a fake
  communicator -- the network-level fake already reaches the service, and a
  public way in would undo D1.

### D7. What else follows
**Chosen:**
- The root `CLAUDE.md`: `ILLMCommunicator.h` leaves the `interfaces/` line; the
  LLMCommunicator row of the component table says it is the LLM service's
  protocol layer, built by the service and used by the agent (as A1 places it);
  the LLM configuration paragraph as A4 decides.
- The `CLAUDE.md` of LLMCommunicator (where the interface lives, what it is for:
  D2), LLMService (the only creator; nothing public), Agent (reaches the LLM
  only through the interface) and HaisosOS (holds its agents as `IAgent`: D4).
- The `code-review-llm-protocol` skill and its copy in
  `code-review/references/llm-protocol.md`: the header's new path, and
  `interfaces/ILLMService.h` in place of the long-gone `interfaces/IAgent.h`;
  the `todo` skill's layer list only under A1's second variant.
- Nothing a haisosfile, an agent, a person or an LLM endpoint sees changes: with
  A2's first variant the request bytes are identical, so recordings and the
  `--log-agent-to-file` output stay as they are.
**Ignored:**
- Reworking the umbrella use of `IFactory.h` (Console, HaisosOS and the
  `agent_*` tools include it for one or two interfaces) -- a matter of its own
  once it no longer carries the communicator.

## Checked

- The root `CLAUDE.md` (directory tree and `interfaces/` line, environment
  variables, Security, Creating things, component table), and the `CLAUDE.md` of
  LLMService, LLMCommunicator, Agent, ServicesCreator, NetworkService,
  HTTPClient, Factory, HaisosOS, Environment, Logger, ToolFactory,
  BuiltinCommands and `tests/tools/llm_cache_proxy`.
- `interfaces/`: all thirteen headers -- what each interface is created, handed
  out or taken by; every include of `ILLMCommunicator.h`; `IEnvironment`'s
  `LLMIdentifier`, `ReadSecretValue` and LLM variable names.
- `src/`: the LLM service (creation per agent, configuration, shutdown), the
  communicator (request, parsing, failures, traffic reports, test-only
  members), the agent (history, rounds, tool-call parsing, failure handling),
  the OS (the LLM service made from its environment, the agent start path and
  its downcast), the agent process and input loop, `ServicesCreator`,
  `NetworkService`, the Linux HTTP client, the Factory, `agent_start`,
  `agent_query`, the configuration log in `main.cpp`; the CMake files of Agent,
  LLMCommunicator, LLMService, Factory, HaisosOS, NetworkService and HTTPClient,
  and the build tree's link line for `Agent.unittests`.
- Tests: `tests/mocks/` (the communicator, HTTP client and agent mocks),
  `Agent.unittests`, `LLMCommunicator.unittests`, `ServicesCreator.unittests`
  (the network-level fake), `HaisosOS.unittests` (no agent process built from a
  concrete agent), the integration tests and their CMake.
- An experiment in a scratch directory: `IFactory.h` without the include, in a
  copy of `interfaces/` put first on the include path, and every C++ source of
  the Linux build syntax-checked with its target's flags from
  `build/temp_linux` -- 103 clean; three failures were `agent_stop` sources
  deleted in `39b17ff` and still listed by the stale build tree; the four
  Windows/WASM-only sources checked by reading.
- Git: the history of `ILLMCommunicator.h` and `IFactory.h` (`9f67ec6` with
  `IFactory::CreateLLMCommunicator`; `39b17ff`, the services refactor, with
  `IHTTPClient.h` renamed to `INetworkService.h`), the CMake links since
  `9f67ec6`, and where the OS's downcast to `Agent` came from (`39b17ff`).
- The `code-review-llm-protocol` skill and its copy under `code-review/`, and
  the `todo` skill's layer order.
- Every file in `notes/`, including those added while this was written
  (`explore-builtin-command-host.md`, `explore-valid-shared-ptr.md`,
  `note-tool-caller-twice.md`); `note-lua-json-crash.md` and
  `plan-critical-review-fixes.md`, read at first, have since been removed from
  the working tree (their fixes are committed in `c54efcf`). The exploration of
  tool-parameter encoding across protocols was not in `notes/` yet.
- External: Ollama's API documentation (`/api/chat` messages: `thinking`,
  `tool_calls` without ids and with object arguments, `tool_name` on tool
  results); Anthropic's tool-use documentation ("Define tools": `input_schema`;
  "Handle tool calls": `tool_use` blocks with `id`, `name` and an `input`
  object, `stop_reason` `tool_use`, results as `tool_result` blocks of a user
  message with `tool_use_id`, `content` and `is_error`; "Thinking": thinking
  blocks carry a `signature` and go back "complete and unmodified" with tool
  results).
