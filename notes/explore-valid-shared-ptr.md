# Exploration: valid_shared_ptr -- a shared_ptr that is never null

## Seed

> Introduce `valid_shared_ptr<T>`: a smart pointer that inherits from `std::shared_ptr<T>` and is guaranteed never to be null -- it cannot be constructed, assigned, reset or moved into a null state -- and use it instead of `std::shared_ptr` wherever a pointer must not be null (the `Create()` factories, the factory methods of the interfaces in `interfaces/`, members and parameters), so that "can this be null?" is answered by the type rather than by comments and null checks.

## Base

- Branch: `task/code_review_max`
- Commit: `8515a90` (Add full physical filesystem and Windows drive-letter paths)
- Related notes:
  - `notes/explore-builtin-command-host.md` -- deletes `BuiltinCommandHost` and
    has `IBuiltinCommands::RunCommand` run a command and return its status:
    done first, it takes away a struct that would need a constructor here (D7)
    and one nullable return (A2); its new console parameter is a valid pointer
    by D3. Its reference parameter for the process fits D1.
  - `notes/note-process-runs-builtin.md` -- the decision behind that
    exploration.
  - `notes/note-process-start-stop.md` -- `IHaisosOS::StartProcess` returns null
    for every failure and should say why: A2's third variant.
  - `notes/note-builtin-placement-friend.md` -- reworks how `IAgent::AddChild`
    is protected; the same declaration changes its parameter here (D9).
  - `notes/plan-critical-review-fixes.md` -- its finding 6 made
    `ILLMService::CreateAgent` return null while the service shuts down: a null
    that stays (D3).
  - `notes/note-review-low-findings.md` -- the null agent `agent_start` used to
    dereference, and the drift in the back-reference sentence of "Creating
    things", which D9 rewrites.
  - `notes/note-lua-json-crash.md` -- why a process-wide crash is ranked
    critical, which rules out `std::terminate` (D4).
  - `notes/note-runtime-thread-destruction.md` -- the `DestroyOffRuntimeThreads`
    deleter the thread-owning `Create()`s pass, which the new type must carry
    (D8).
  - `notes/explore-stdio-exit-codes.md` -- its D14 dropped an error *code* from
    `StartProcess` (a shell works out 127/126 itself); a reason for people to
    read, which A2's third variant would carry, is the later note's different
    need.
  - `notes/note-review-medium-findings.md` -- section 4: the WASM build is not
    buildable today (relevant to exceptions, D4).

## The issue

Every pointer Haisos hands around is a `std::shared_ptr`, and whether one may be
null is said by comments ("Never null", "Returns null when ...") and defended at
run time -- refused with a log line, skipped, tolerated with a branch, or not
defended at all. The seed asks for `valid_shared_ptr<T>`, a `std::shared_ptr`
that is never null, wherever null is not an option, so that the compiler answers
the question. This exploration checks which pointers those are, what the type
must forbid and still allow in C++17 (moves, containers, custom deleters,
`shared_from_this`, casts, googletest), what happens when a null reaches it,
where it lives, what it is called, and how the change is made. One finding
shapes the rest: public inheritance cannot stop a move *into a plain
`std::shared_ptr`* from emptying it, so "inherits `std::shared_ptr`" and "cannot
be moved into a null state" do not both hold completely (A1). Done means: the
type exists with its tests; every never-null pointer -- the `Create()`
factories, the interfaces' factories and accessors, members, parameters -- is
one, so passing null there does not compile and a nullable pointer becomes one
only through an explicit, checked conversion; the runtime checks and "never
null" comments on those pointers are gone; `std::shared_ptr` is left only where
null means something, its comment saying what; the unit tests pass; and
"Creating things" in the root `CLAUDE.md` states the new contract.

## What exists today

- **The rule.** "Creating things" in the root `CLAUDE.md`: every class
  implementing an interface has private constructors and a static `Create()`
  returning a `shared_ptr`, and "every factory method across the interfaces
  returns a `shared_ptr` too, so there is one ownership story end to end and a
  created object can safely hand itself out". Back-references to an owner stay
  raw references so that no ownership cycle closes (`AgentStartTool` holds an
  `ILLMService&`; the tool factory actually keeps a nullable pointer, and the
  sentence still names `IHaisosOS&` for `OSToolFactory` and the `os_*` tools,
  which hold a `CurrentProcessHandle`). `HaisosOS`, `AgentProcess`,
  `LuaProcess`, `BuiltinProcess` and `Agent` build their `shared_ptr` with the
  `DestroyOffRuntimeThreads` deleter (libheaders); `Agent` and `HaisosOS` derive
  from `std::enable_shared_from_this`.
- **History.** Commit `a83562c` turned the `unique_ptr` factories into
  `shared_ptr` `Create()`s, and `IServicesCreator::CreateLLMService`'s
  `INetworkService&` into a `std::shared_ptr<INetworkService>`: shared
  ownership gained, the reference's "never null" lost -- the LLM service has
  checked for a missing network service since. Nothing in the history mentions
  a non-null pointer type.
- **`interfaces/`** holds 66 pointer uses: 65 `shared_ptr`, 1 `weak_ptr`
  (`BuiltinCommandHost`'s OS).
  - **Never null in practice: 50.** Every factory of `IFactory`,
    `IServicesCreator`, `IFileSystemService` and `INetworkService`,
    `IEnvironment::Clone`, `ILLMService::GetToolFactory`/`CreateAgentConsole`,
    and `IFactory::CreateHaisosOS`/`IHaisosOS::CreateSubOS` (null today only
    without an environment); the accessors `IHaisosOS::GetRootFileSystem`/
    `GetServicesCreator`/`GetOsEnvironment`, `IProcess::GetEnvironment`,
    `ICurrentProcess::IO`; the elements of `IHaisosOS::GetRunningProcesses` and
    `IAgent::GetChildren`; and the parameters every implementation needs
    (environments, root filesystem, services creator, physical console, network
    service, the filesystems given to `IFileSystem::Mount`,
    `IFileSystemService` and `IBuiltinConfigurator`, `IAgent::AddChild`'s child).
  - **Nullable by contract: 13.** The returns of `ILLMService::CreateAgent`,
    `IHaisosOS::StartProcess`, `IBuiltinCommands::RunCommand`,
    `IToolFactory::CreateTool`, `IAgent::GetParent`, `ICurrentProcess::AsAgent`
    and `ICurrentProcess::OS`; the parameters `CreateAgent`'s `parent` and
    `additionalTools`, the calling agent of `ITool::Call` and
    `IToolFactory::CreateTool`, and the `builtinCommands` of `CreateHaisosOS`
    and `CreateSubOS`.
  - **Null only because the implementation tolerates it: 2.** `CreateAgent`'s
    console and `BuiltinCommandHost`'s console: production always passes one,
    tests pass none.
- **Comments carry it.** `ICurrentProcess::IO` "Never null"; `AsAgent` "or
  null ..."; `OS` "Returns null once the OS is gone" (the reference is weak by
  design: an OS owns its processes); `CreateAgent` "Returns null when ...
  Callers must check"; `RunCommand` "Returns null for an unknown builtin, a
  null environment, or a host with no OS"; `CreateHaisosOS` "null gives an OS
  that runs none"; in `src/`, "Neither may be null" (the input loop) and
  "Returns nullptr if agent or environment is null" (the agent process).
- **Runtime defences, about 35 in `src/`.**
  - Refused with a log line and a null return: the OS created, or asked to
    start a process, without an environment; the agent process without agent
    or environment; the input loop without agent or console; `RunCommand`
    without an environment; the builtin configurator without a filesystem.
  - Skipped: mounting a null filesystem does nothing (currently in
    `MountPoints`).
  - Tolerated with branches: the agent's console and tool factory, the
    builtin's output console (over a third of all the checks are on consoles),
    `AgentConsoleAdapter`'s physical console, `CompositeToolFactory`'s two
    factories, the Lua and builtin processes' environment, the LLM service's
    network service, the `os_*` tools' process handle.
  - Not defended: `HaisosOS::Create` calls its services creator, and the OS
    reads through its root filesystem, unchecked.
  - The three `Create()`s that can return null (`HaisosOS`, `AgentProcess`,
    `AgentInputLoop`) do so only for a null argument.
- **Legitimate nulls.** A weak reference whose target is gone
  (`ICurrentProcess::OS`, `CurrentProcessHandle::Get`, the process file I/O
  finding its OS's root); a dynamic cast that fails (the OS casting
  `CreateAgent`'s `IAgent` to the concrete `Agent` it owns; 14 casts in tests);
  a lookup that finds nothing (`IToolFactory::CreateTool` for an unknown name,
  or for an agent tool with no calling agent); a refusal (`CreateAgent` while
  the service shuts down, `StartProcess`'s many failures, `RunCommand` for an
  unknown name); absence (a top-level agent's parent; the calling agent when a
  Lua script calls a tool, which passes none).
- **`src/` at large.** About 45 static `Create()`s, all returning
  `std::shared_ptr`; 44 `shared_ptr` members and about 60 by-value
  `shared_ptr` parameters in headers. The idiom is to take a `shared_ptr` by
  value and `std::move` it into a member or an onward call as its last use
  (about 100 such moves). One function takes a non-const `std::shared_ptr&`:
  the Logger's callback-slot setter, for a nullable slot.
- **Containers and structs.** The OS's process list and the LLM service's agent
  list are pruned with `erase`/`remove_if`, mount entries sorted and pruned,
  the builtin registry is a `std::map` read with `find`; the haisosfile's
  filesystem builder fills its name-to-filesystem map with `operator[]`
  (currently in `HaisosFileSystemBuilder.cpp`). `BuiltinCommandHost` is filled
  in field by field (by the OS; by a test with no console at all), and so is
  the `os_*` tools' context struct, whose "invalid" state is its null members
  (currently `OSToolContext`, whose `IsValid()` is the one place Haisos says
  "valid" -- meaning "no pointer null").
- **Tests.** 26 hand-written test doubles implement interfaces (gmock is
  included by four tool tests but not used); the mocks have public
  constructors and are made with `std::make_shared` (about 190 calls; the
  builtin commands are made the same way in `src/`). About 90 assertions
  compare a pointer with `nullptr`: 22 on processes (`StartProcess` results,
  nullable), most of the rest on results that cannot be null (12 on
  filesystems). Tests pass null where production never does (a console to
  `CreateAgent`, a process handle to `LuaProcess::Create`, an environment and
  an empty host to `RunCommand`), and one pins the OS refusing a null
  environment (currently `CreateHaisosOSWithoutAnEnvironmentReturnsNull`).
- **Build.** C++17 with extensions off (`CMakeLists.txt`): no designated
  initializers, no concepts, no `std::expected`. GCC/Clang on Linux, MSVC,
  Emscripten. `extern/` holds nlohmann_json, googletest and lua -- no GSL --
  and the build runs no clang-tidy. The code already relies on catching
  exceptions: a tool's exception becomes its call's error result and a
  command's a reported failure (`Agent`), and the Lua bridge catches around
  every tool call. Emscripten disables catching by default, and the WASM build
  is not buildable as documented anyway.
- **Naming.** Every Haisos type and function is PascalCase
  (`CurrentProcessHandle`, `SynchronizedQueue`, `NextGloballyUniquePID`); the
  only lowercase type names are aliases of outside ones (`json`, `ssize_t`).

## Open aspects

### A1. Inherit std::shared_ptr, as the seed says, or wrap one?
Public inheritance cannot keep the seed's "cannot be ... moved into a null
state": `std::shared_ptr`'s own move constructor takes a `valid_shared_ptr` by
its base, so `std::move(p)` into any plain `std::shared_ptr` -- a by-value
parameter, a member -- empties `p`, and so does `reset()` called through a
`std::shared_ptr&` (both compiled and run). Everything done through the type
itself is closed either way (D6, D7). Private or protected inheritance is no
middle way: a conversion function to a base class is never used, so the
implicit conversion to `std::shared_ptr` would be lost.
Combinable: no

1. **Public inheritance** -- `valid_shared_ptr<T> : public std::shared_ptr<T>`,
   as the seed says. For: it *is* a `std::shared_ptr` -- every
   `std::shared_ptr` parameter, `std::dynamic_pointer_cast` (15 uses, 14 in
   tests), `weak_ptr` construction (a parent's list of children,
   `CurrentProcessHandle`) and googletest comparisons take it unchanged
   (checked with googletest 1.14); the smallest header. Against: the hole
   above. Here it is reached only by using a pointer after moving it into a
   nullable slot -- once parameters are valid, a move into one copies (D6) --
   or through a `std::shared_ptr&` that resets its argument (the one such
   function holds no valid pointer). "Never null" then holds as long as nothing
   is used after being moved from, the rule every C++ object follows already;
   but nothing in the build flags a use after move.
2. **Composition** -- holds a `std::shared_ptr` and converts to one
   implicitly: by const reference from an lvalue, by copy from an rvalue, so
   not even `std::move` into a `std::shared_ptr` empties it (compiled and run);
   `gsl::not_null` and Dropbox's `nn` also wrap rather than inherit. For: the
   guarantee is complete. Against: not what the seed says; templates do not see
   through it -- `std::dynamic_pointer_cast` fails to deduce (checked) and needs
   `.shared()` or a cast of its own, `weak_ptr` construction a conversion,
   googletest comparisons operators of its own; a larger header. Going from 1
   to 2 later breaks only the spots leaning on the base, all at compile time.

### A2. What do the pointers that may be null become?
Six returns stay nullable for good (D3) -- `ILLMService::CreateAgent`,
`IHaisosOS::StartProcess`, `IToolFactory::CreateTool`, `IAgent::GetParent`,
`ICurrentProcess::AsAgent`, `ICurrentProcess::OS` -- plus
`IBuiltinCommands::RunCommand` until `notes/explore-builtin-command-host.md`
makes it return a status, and a few parameters. Once every never-null pointer
is a `valid_shared_ptr`, a plain `std::shared_ptr` reads "may be null" by
itself; the question is whether to say more, and whether the refusals say why.
Combinable: yes (3 goes with any of the others, which differ in how the rest
are spelled)

1. **Plain std::shared_ptr** -- as today, its comment saying when it is null.
   For: nothing new; every call site keeps its check; a checked pointer reaches
   a valid parameter through D5's conversion. Against: while the migration
   runs, a `std::shared_ptr` may also be one not converted yet; the reason for
   a refusal stays in the log.
2. **Nullable alias** -- `nullable_shared_ptr<T>` (a `using` of
   `std::shared_ptr<T>`, spelled per A3) on every pointer that is nullable on
   purpose. For: the choice is visible and greppable on both sides; "nullable
   by design" is told apart from "not converted yet". Against: an alias checks
   nothing; two spellings of one type.
3. **Result for refusals** -- `StartProcess` and `CreateAgent` return a small
   result holding either a `valid_shared_ptr` or why nothing was made ("the OS
   is shutting down", "process limit reached", "no such program", "the LLM
   service is shutting down") -- what `notes/note-process-start-stop.md` asks
   for, and what the haisosfile's root-filesystem builder already does with a
   null plus an error string. For: success is typed non-null; `os_start_process`,
   `agent_start` and haisos's `RUN` could say why. Against: C++17 has no
   `std::expected`, so a type of Haisos's own (or `std::variant`); a design of
   its own -- the reasons, their wording in the tools -- which widens this work.
4. **Optional of valid** -- `std::optional<valid_shared_ptr<T>>`. For: once
   checked, the value is already a valid pointer. Against: two layers to unwrap
   (`(*agent)->Post(...)`); `std::shared_ptr` has an empty state already;
   unusual.

### A3. What is it called?
It will be on most pointer declarations. The seed says `valid_shared_ptr`,
while every Haisos type and function is PascalCase. Its companions follow the
choice -- the make function, the exception, the non-throwing check, A2's alias
if chosen -- and so does the header's name (D2).
Combinable: no

1. **valid_shared_ptr** -- as the seed, with `make_valid_shared`. For: the
   user's name; reads as the std smart pointer it extends, as `gsl::not_null`
   and `nn` read; "valid" already means "no pointer null" in the `os_*` tools'
   context check. Against: the first lowercase Haisos type and function;
   "valid" can be read as "works" (a `PhysicalFileSystem` with an unusable root
   is not null, yet refuses every path).
2. **ValidSharedPtr** -- with `MakeValidShared`. For: the codebase's style
   (`CurrentProcessHandle`, `SynchronizedQueue`). Against: departs from the
   seed; reads less like a `std::shared_ptr`.
3. **A "not null" name** -- `not_null_shared_ptr` or `NotNullSharedPtr`. For:
   says exactly what is guaranteed. Against: longer; departs from the seed.

### A4. Where null means "none", does an empty object take its place?
Some pointers are null only to say "nothing here": `ILLMService::CreateAgent`'s
console (the agent then writes nowhere; production always passes one, tests
pass none) and `additionalTools` (documented), the `builtinCommands` of
`IFactory::CreateHaisosOS` and `IHaisosOS::CreateSubOS` (documented: an OS that
runs none), and `BuiltinCommandHost`'s console until that struct goes. Each
keeps a branch alive: the agent's console checks, the LLM service's choice
whether to compose tool factories, the OS's check for builtins.
Combinable: yes (per pointer)

1. **Keep them nullable** -- a `std::shared_ptr` (or A2's spelling) whose null
   means none. For: the smallest change; the documented meanings stand.
   Against: their branches stay.
2. **Empty objects** -- valid parameters, with an object standing for none: an
   in-memory console (`ILLMService::CreateAgentConsole` already makes one) or a
   discarding one, an empty `IToolFactory`, an empty `IBuiltinCommands`. For:
   every branch on them goes; there is always something to call. Against: one
   or two small new classes; "no builtins" becomes an empty set, whose refusal
   is logged as an unknown name rather than "created without builtin
   commands"; tests pass objects where they passed null.
3. **Overloads without them** -- a method without the parameter means none.
   For: no null and no new class. Against: more virtual methods on the
   interfaces for one idea.

## Decided aspects

### D1. A type of Haisos's own -- not a library, an annotation, references or comments
**Chosen:** a small header-only class template written for Haisos. It is a
shared pointer, so the one ownership story of "Creating things" stays whole (a
`Create()` still hands out shared ownership, and a created object can still
hand itself out); nothing is vendored; and it can throw (D4) and convert (D8)
the way this code needs.
**Ignored:**
- `gsl::not_null<std::shared_ptr<T>>` (Microsoft GSL; checked against its
  current headers and compiled) -- a null calls `std::terminate`, with no
  throwing mode left, so one bug in one tool call would end every OS and agent
  in the process; a `not_null<shared_ptr<Agent>>` does not convert to a
  `std::shared_ptr<IAgent>` (that takes two user-defined conversions), where
  Haisos passes concrete objects as their interfaces everywhere; and it would
  be a fourth dependency in `extern/` (clone step, CI).
- Dropbox's `nn` -- a header of another project's conventions to vendor;
  moving one into its pointer type leaves it null; its `NN_CHECK_ASSERT` is an
  assert. Its good ideas -- no `operator bool`, an explicit checked conversion,
  make and cast helpers -- are taken over (D5, D7, D8).
- `absl::Nonnull`, clang's `_Nonnull` -- annotations for a static analyser,
  enforced by neither GCC nor MSVC, and the build runs no analyser.
- References for the never-null accessors (`IO()`, `GetRootFileSystem()`, ...)
  -- a reference cannot be kept beyond the call, factories must hand out
  ownership anyway, and commit `a83562c` moved `CreateLLMService` from a
  reference to a `shared_ptr` for exactly that. (A reference *parameter* for a
  synchronous call that keeps nothing -- the builtin context's process today,
  `RunCommand`'s in `notes/explore-builtin-command-host.md` D2 -- is another
  matter, and fine.)
- Comments and runtime checks -- what exists, and what the seed wants replaced.

### D2. It lives in interfaces/, in a header of its own
**Chosen:** a new header in `interfaces/`, named per A3 -- the first there that
holds no interface. The interfaces' signatures use it, and `interfaces/`
includes nothing but the standard library, nlohmann/json and its own headers;
the vocabulary types the interfaces use are defined there too (`ToolResult`,
`FileStatus`, `LLMIdentifier`, `StartProcessOptions`). Its unit tests can join
`tests/unit/components/libheaders.unittests/`, the binary for header-only
utilities, or get a binary of their own -- the plan's call.
**Ignored:**
- `src/components/libheaders/` -- the home of header-only utilities, but
  `interfaces/` would include from `src/` for the first time, and libheaders'
  headers log through the Logger, which the interfaces must not depend on.
- Inside an existing interface header -- every interface needs it, and there is
  no common root header they all include.

### D3. Which pointers become valid
**Chosen:** the rule "valid, unless null means something a caller must branch
on", applied everywhere:
- `interfaces/`: the 50 never-null uses listed under "What exists today" --
  including the result of `IFactory::CreateHaisosOS` and
  `IHaisosOS::CreateSubOS`, null today only without an environment, and
  `IServicesCreator::CreateLLMService`'s network service, which removes
  `CreateAgent`'s "no network service" refusal (its "shutting down" one stays).
- The 13 nullable by contract stay nullable (A2 says how they are spelled); the
  "none" pointers are A4's.
- `src/`: every static `Create()` -- the three that can return null today do
  so only for a null argument, which a valid parameter rules out; members and
  parameters by the same rule, e.g. the agent's tool factory, the Lua process's
  console and tool factory, the process-handle parameter of the process
  `Create()`s (null only in tests), the `os_*` tools' process handle, the
  adapter's physical console, the composite tool factory's two factories (only
  ever created with both). Stay nullable: the agent's parent, the input loop of
  a process that is not interactive (and its input-console parameter), the
  agent-traffic log's file-or-stream member, the Logger's callback slots, and
  the OS's builtin commands (A4).
- Unchanged: weak references (a process's OS, a parent's children,
  `CurrentProcessHandle`) stay `std::weak_ptr`, whose `lock()` rightly gives a
  nullable pointer; back-references stay raw references ("Creating things": a
  `valid_shared_ptr` back would still close an ownership cycle);
  `std::unique_ptr` members stay.
- Kept as they behave: `IFactory::CreatePhysicalFileSystem` hands back, for an
  unusable root, a filesystem on which every call fails (logged), never null;
  the haisosfile's root-filesystem builder keeps returning null with its error
  (currently `BuildRootFileSystem` in `src/haisos/`).
**Ignored:**
- Every pointer, nullable ones too -- "no parent" or "no calling agent" would
  take a fake agent answering `Name()` and `GetParent()`, which is worse than a
  null.
- Only `interfaces/` -- the members and parameters behind them would keep their
  runtime checks, and the seed names them.

### D4. A null reaching one: an exception
**Chosen:** the checked conversion (D5) throws an exception type of its own,
derived from `std::logic_error` (`std::invalid_argument`, say; named per A3),
whose message says that a null reached a valid pointer. A null there is a bug,
and the code already contains exceptions where it matters: a tool's becomes its
call's error result and a command's a reported failure ("a tool can fail its
call but never the agent", root `CLAUDE.md`), and the Lua bridge catches around
every tool call. Elsewhere -- the setup in `main` -- an uncaught one ends haisos
with its message (libstdc++ prints it; the plan may add a catch in `main` that
prints it on every platform). The header, in `interfaces/`, cannot log; the
message travels in `what()`. Emscripten's default of not catching adds nothing
new: the code relies on catching already.
**Ignored:**
- `std::terminate` (GSL's choice) -- one bug in one tool call would end every
  OS, agent and script in the process, which the review ranks critical
  (`notes/note-lua-json-crash.md`).
- `assert` -- compiled out of release builds, the default; the null would crash
  later, unexplained.
- No check -- undefined behaviour at the first `->`.

### D5. From nullable to valid: explicit and checked
**Chosen:** an explicit constructor from `std::shared_ptr` that checks and
throws (D4), and a non-throwing check returning `std::optional` of a valid
pointer (named per A3), for where a null is expected ("if it is there, go on
with a valid one"). The conversion is the one spot where "may be null" becomes
"is not": explicit, every such spot is visible and greppable, and the compiler
rejects a nullable value flowing into a valid slot -- an
`IHaisosOS::StartProcess` result copy-initializing a valid variable, returned
where a valid one is due, or assigned to one (all checked). GSL's
`strict_not_null` and `nn`'s `NN_CHECK_*` are explicit for the same reason.
**Ignored:**
- Implicit, checked at run time -- the compiler would stop answering "can this
  be null?"; the mistake would show only when it happens.
- An unchecked "I checked, trust me" constructor (`nn`'s
  `i_promise_i_checked_for_null`) -- the check is one comparison; not worth a
  way around it.

### D6. A move copies
**Chosen:** only the copy operations are declared, so a move from one
`valid_shared_ptr` to another copies and the source stays non-null, upcasts
included (compiled and run). That is the seed's "cannot be moved into a null
state" for everything done through the type: a moved-from member, local or
container element (`erase`/`remove_if` tails, `sort`, `swap` -- all run) is
never null. The cost is one atomic count increment per move, nothing next to an
LLM round trip or a thread start; the pointer stays the size of a
`std::shared_ptr` (16 bytes, checked). `gsl::not_null` behaves so too
(compiled). A by-value parameter moved into a member costs one increment more;
the plan may take such parameters by const reference. Moves into a plain
`std::shared_ptr` are A1's.
**Ignored:**
- Moves that empty the source ("don't use it after a move") -- what C++26's
  `std::indirect` and `std::polymorphic` (`valueless_after_move`) and
  `nn<unique_ptr>` accept, because they cannot copy cheaply; a shared pointer
  can.

### D7. No null state, and no asking
**Chosen:** no default constructor; construction and assignment from `nullptr`
deleted; `reset()` (every overload), `swap` with a `std::shared_ptr` and
assignment from one removed; `operator bool` and `==`/`!=` with `nullptr`
deleted; the constructors are its own -- never `using std::shared_ptr<T>::shared_ptr`,
which would bring the null ones back. Each is a way to make one null, or a
question whose answer is always the same; deleted, the compiler lists every
dead check and every assertion on a never-null result as the migration goes
(`nn` deletes `operator bool` too: "that would be silly"). Checked with g++: 18
misuses fail to compile, and so do `EXPECT_NE(p, nullptr)` and
`ASSERT_TRUE(p)` with googletest 1.14, while `EXPECT_EQ` between two pointers
compiles and passes; each property can be pinned with a `static_assert`
(checked). The fallout, all compile errors at the spot:
- a map's `operator[]` (the haisosfile's filesystem builder's, currently)
  becomes `emplace` or `insert_or_assign`; `vector::resize` becomes
  `push_back`;
- a struct filled in field by field needs a constructor, C++17 having no
  designated initializers: `BuiltinCommandHost`, unless
  `notes/explore-builtin-command-host.md` deletes it first (its D6), and the
  `os_*` tools' context struct, whose "invalid" state is its null members (an
  optional of a struct of valid pointers, or nullable members kept: the plan's
  call).
**Ignored:**
- Keeping `operator bool` and the null comparisons -- always true, always
  false, and the dead checks would go on compiling.
- A default constructor pointing at a placeholder -- there is no sensible
  empty `IFileSystem` or `IAgent` to point at.

### D8. Making one, and converting
**Chosen:**
- `Create()`s adopt their new object -- with or without a deleter -- through
  constructors templated on the object's own type, as `std::shared_ptr`'s are:
  a plain `T*` parameter, handed a derived object typed as its base, silently
  breaks `enable_shared_from_this`, which `Agent` and `HaisosOS` use (checked).
  The `DestroyOffRuntimeThreads` deleter passes through unchanged (type-erased
  in the control block; compiled and run).
- A make function (named per A3) for types with public constructors: the test
  mocks and the builtin commands, made with `std::make_shared` today.
- Implicit conversions: valid-of-derived to valid-of-base, with a converting
  assignment too (without one, assigning one to the other is ambiguous --
  checked), and valid to `std::shared_ptr`, which is always safe.
- `shared_from_this()` and `weak_ptr::lock()` results become valid through D5
  where a valid one is needed.
- Casts: `std::dynamic_pointer_cast` stays and rightly returns a nullable
  pointer; a validity-keeping counterpart of `std::static_pointer_cast` only
  when a use appears (none today).
How much of this is free depends on A1: with inheritance the standard functions
(`dynamic_pointer_cast`, `weak_ptr`, comparisons) take a `valid_shared_ptr` as
it is; with composition each needs a conversion or a wrapper.
**Ignored:**
- Adopting constructors taking `T*` only -- see `enable_shared_from_this`
  above.

### D9. The migration, the tests and the documents
**Chosen:** one task branch, in layers that each build and pass the unit tests:
1. the type and its tests -- `static_assert`s for every compile-time property,
   run-time tests for the throw, the copying move and containers;
2. the concrete `Create()`s return valid pointers -- their callers keep
   compiling, since a valid pointer converts to the `std::shared_ptr` they
   expect, but for the null checks the compiler then reports, which go;
3. the never-null interface returns, each method changed together with every
   implementation and every test double (a different return type is not
   covariant, and a changed parameter no longer overrides -- both checked);
4. parameters and members (D3), deleting the runtime checks and "never null"
   comments the compiler and D3 point at;
5. the tests: mocks made with the make function, null arguments to now-valid
   parameters replaced with objects, the tests pinning a refusal of a null
   argument (currently `CreateHaisosOSWithoutAnEnvironmentReturnsNull` and the
   null-environment case of the `RunCommand` refusal test) deleted in favour of
   the type's `static_assert`s, and null assertions kept only on nullable
   results;
6. the documents: "Creating things" in the root `CLAUDE.md` (a `Create()`
   returns a `valid_shared_ptr`, as does every interface factory that cannot
   fail; a `std::shared_ptr` only where a method may return null, its comment
   saying when; the back-reference sentence corrected while there, per
   `notes/note-review-low-findings.md` section 9), the `interfaces/` line of
   the directory tree, and the `CLAUDE.md` of the components whose contracts
   change (HaisosOS, LLMService, Agent, BuiltinCommands, ToolFactory, and
   libheaders if the tests land there).

Overlaps: `notes/explore-builtin-command-host.md` (the shape of `RunCommand`,
`BuiltinCommandHost` deleted, the builtin's process moved into HaisosOS) --
done first, it spares D7 a constructor and A2 a nullable return, and its new
console parameter and the moved process's hold on the builtin set are valid
pointers by D3; `notes/note-builtin-placement-friend.md` reworks how
`IAgent::AddChild` is protected, whose parameter changes here -- either order.
**Ignored:**
- One diff for everything -- the order falls out of the conversions anyway, and
  a layer at a time is reviewable and bisectable.
- Stopping half-way on master -- a `std::shared_ptr` would then mean "nullable,
  or not converted yet".

## Checked

- The root `CLAUDE.md` (Creating things, Security, Tools, Build); the
  `CLAUDE.md` of Agent, LLMService, HaisosOS, ToolFactory, Factory,
  BuiltinCommands, Console and libheaders.
- `interfaces/`: all twelve headers, every `shared_ptr` and `weak_ptr` in them
  classified; their includes (nothing from `src/`).
- `src/`: every static `Create()`; the three that return null; members and
  by-value parameters in headers; every runtime null check; the OS (start
  paths, the cast to `Agent`, sub-OS), the LLM service (`CreateAgent`'s
  refusals, shutdown), the agent (console and tool-factory checks,
  `shared_from_this` handed to tools), `ToolFactory` and `CompositeToolFactory`,
  `OSToolFactory` and the `os_*` tools' context, the process classes, the
  builtin set and configurator, `MountPoints`, the haisosfile's filesystem
  builder, `main.cpp`, the Logger's callback slot, `PhysicalFileSystem` with an
  unusable root.
- Tests: `tests/mocks/` (hand-written, public constructors); every test double
  implementing an interface; the null assertions, dynamic casts and null
  arguments in the unit tests; gmock includes (unused).
- Git: `a83562c` (the `shared_ptr` `Create()` factories;
  `CreateLLMService`'s `INetworkService&` to `shared_ptr`), `39b17ff` (where
  the "Creating things" rule came in), a search of the history for null and
  not_null.
- Prototypes, compiled with g++ 13.3 (`-std=c++17 -Wall -Wextra -pedantic`)
  in a scratch directory, not in the repo:
  - an inheriting `valid_shared_ptr`: moves between valid ones keep the source;
    `std::move` into a `std::shared_ptr` -- a by-value parameter included --
    and `reset()` through a `std::shared_ptr&` empty it; containers; a custom
    deleter; the explicit conversion throwing; `enable_shared_from_this` broken
    by a plain `T*` constructor; 16 bytes;
  - a compile-fail matrix of 18 misuses: default construction, `nullptr`,
    copy-initialization from `std::shared_ptr`, `reset` (two forms), assignment
    from `nullptr` or a `std::shared_ptr`, `swap`, `if (p)`, `!p`,
    `== nullptr`, `!= nullptr`, a map's `operator[]`, `vector::resize`, a
    default-initialized struct, returning a `std::shared_ptr` where a valid one
    is due, an override changing a return type, one changing a parameter type;
    `std::optional` of one compiles;
  - googletest 1.14 from `extern/`: `EXPECT_EQ` between valid and plain
    pointers compiles and passes; `EXPECT_NE(p, nullptr)` and `ASSERT_TRUE(p)`
    do not compile; the converting assignment is needed; `static_assert`s for
    each compile-time property; forward-declared pointee types in interface
    declarations;
  - a composition-based wrapper: moves into a `std::shared_ptr` keep the
    source; `std::dynamic_pointer_cast` cannot deduce;
  - `gsl::not_null<std::shared_ptr>` from GSL's main branch: moves keep the
    source, a null calls `std::terminate`, no implicit conversion to a
    `std::shared_ptr` of a base.
  Not compiled with MSVC or Emscripten here; the prototype uses only standard
  C++17.
- External: GSL `include/gsl/pointers` and `include/gsl/assert` (`not_null`,
  `strict_not_null`, `Expects` calling `std::terminate`); Dropbox `nn.hpp`
  (wraps; `operator bool` deleted; moving into the pointer type empties it;
  `NN_CHECK_ASSERT`/`NN_CHECK_THROW`, `nn_make_shared`,
  `nn_static_pointer_cast`/`nn_dynamic_pointer_cast`); P3019 (`std::indirect`
  and `std::polymorphic`, valueless after move); the Emscripten documentation
  (exception catching disabled by default from `-O1`).
- Every file in `notes/`, including those added while this was written
  (`note-build-arm-x64.md`, `note-builtin-method-names.md`,
  `note-builtin-placement-friend.md`, `note-kill-builtin.md`,
  `note-process-runs-builtin.md`, `note-process-start-stop.md`,
  `note-repeated-json-lookups.md`, `note-shell-escaping.md`,
  `explore-builtin-command-host.md`).
