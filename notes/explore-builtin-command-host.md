# Exploration: Doing without BuiltinCommandHost -- a builtin's own process runs the command

## Seed

> `BuiltinCommandHost` -- a struct in `interfaces/IBuiltinCommands.h` -- is
> what the OS hands `IBuiltinCommands::RunCommand` so that it can build a
> builtin's process: the OS the process runs under (held weakly), a fresh pid,
> the parent pid, the path the builtin was started from, and the console its
> output goes to. Why is it needed? The user thinks builtins can do without it.
> Explore what it is for, who fills it in and who reads it, the ways to do
> without it, and what each would change -- given that the other runtimes
> (`.md` agents, `.lua` scripts) get the same information without such a
> struct.

## Base

- Branch: `task/code_review_max`
- Commit: `8515a90` (Add full physical filesystem and Windows drive-letter paths)
- Related notes:
  - `notes/note-process-runs-builtin.md` -- the user's decision, taken while
    this was being explored: a builtin's own process calls `RunCommand`, which
    runs the command synchronously. Recorded here as D1; its "to settle" items
    are A1, A2 and D7.
  - `notes/explore-stdio-exit-codes.md` -- its D1 (streams are descriptors in
    `IFileIO`; no stream objects on `ICurrentProcess`) settles D3 here, and its
    D8 (an exit code on `IProcess`) is A2's second variant. Its D11 had
    `BuiltinCommandHost` carry the process's streams instead of a console; this
    departs from that: with the struct gone, a builtin reaches its streams
    through `process.IO()`, as D1 there already implies.
  - `notes/note-builtin-method-names.md` -- the names on `IBuiltinCommands`:
    the new `RunCommand` takes `commandName` (D2); whether the rest of that
    rename rides along is A4.
  - `notes/note-process-start-stop.md` -- a stop token per process, and KILL
    revoking a process's `ICurrentProcess`: A1's variants 1 and 4 lead there,
    and D1 puts every `ICurrentProcess` implementation in the one component
    that would do the revoking.
  - `notes/note-review-medium-findings.md` (section 2) -- waits in tools need
    "a 'stop requested' query": A1's first variant is one.
  - `notes/note-kill-builtin.md` -- a `kill` builtin stops processes with
    `TriggerStop`, which A1 must carry to a builtin being killed.
  - `notes/note-runtime-thread-destruction.md` -- the rules the moved process
    class keeps (D5).
  - `notes/note-physical-directory-jail.md` -- a future `haisos` builtin would
    need an `IBuiltinCommands` to give its sub-OS (D9).
  - `notes/note-builtin-placement-friend.md` -- unaffected:
    `IBuiltinConfigurator` stays in the BuiltinCommands component (D9).

## The issue

`BuiltinCommandHost` is the parcel the OS fills in when it starts a builtin,
so that `IBuiltinCommands::RunCommand`, in another component, can build the
process the command runs as. The seed asks why it exists and how to do without
it, when agent and Lua processes get the same facts as plain arguments. While
this was being explored, the user settled the main question
(`notes/note-process-runs-builtin.md`): the OS builds a builtin's process
itself, like the other two, and that process's own thread calls `RunCommand`,
which runs the command to completion there and returns its exit status. What
remains is what that direction leaves open or implies: how a running command
learns its process was asked to stop, where the exit status goes, the new
signature (including the one thing the process cannot supply: the console),
what becomes of the process class and the tests, the runtime-thread rules, and
the knock-on changes. Done means: `BuiltinCommandHost` is gone; the
BuiltinCommands component builds no process, allocates no pid and starts no
thread -- it is a library of commands plus the configurator; the builtin's
process class lives in the HaisosOS component, built by the OS like
`AgentProcess` and `LuaProcess`; `RunCommand` runs synchronously and returns a
status; nothing a haisosfile, an agent or a person sees changes (output,
console tags, exit statuses, refusals); the command tests run without threads;
the documentation says so; and the unit tests pass.

## What exists today

- **The struct.** `BuiltinCommandHost` (`interfaces/IBuiltinCommands.h`) holds
  the OS (weak: an OS owns its processes), a pid, a parent pid, the program
  path (what `IProcess::Path()` reports, as opposed to the builtin's name) and
  an `IAgentConsole` ("there is no stdout yet").
  `IBuiltinCommands::RunCommand(host, environment, builtinName, args, workingDirectory, options)`
  takes it with the parameters of `IHaisosOS::StartProcess`, starts the command
  as a process on a thread of its own, and returns that process as an
  `IProcess` at once. It returns null for an unknown name, a null environment
  or a host whose OS is gone, and ignores `interactiveAgent` (logged).
- **Who fills it in.** Only the OS. `IHaisosOS::StartProcess` asks its root
  filesystem `IsBuiltinCommand(path)` before looking at the extension; its
  builtin path (currently `StartBuiltinProcess` in `HaisosOS.cpp`) refuses when
  the OS has no `IBuiltinCommands`, then fills the struct: a pid from the
  program-wide allocator (currently `NextGloballyUniquePID()` in
  `src/components/libheaders/GloballyUniquePID.h`, the one behind
  `IFactory::GetNextGloballyUniquePID()`), itself as parent
  (`GetOSProcessID()`), `weak_from_this()`, the path as given, and a console
  tagged `<builtin name>_<pid>` -- the builtin's name, so `/docs/say.md` placed
  as `echo` prints as `[echo_<pid>]`, where agent and Lua consoles take the
  path's stem. It tracks what `RunCommand` returns like any other process.
- **Who reads it.** `RunCommand` (currently in `BuiltinCommands.cpp`) checks
  the OS, then builds the process class (currently `BuiltinProcess`, in
  `src/components/BuiltinCommands/`), which copies pid, parent and path,
  builds its file I/O from the weak OS (currently
  `ProcessFileIO::Create(os, workingDirectory)`) and keeps the console for the
  command's output. The only other user is one test in
  `tests/unit/components/BuiltinCommands.unittests/` (an unknown name, a null
  environment and a default-constructed host are refused).
- **Why it exists.** The builtin's process is built outside the component that
  knows those facts. By CMake: the HaisosOS component links Logger, Console,
  Agent, the `ProcessFileIO` library, the `os_*` tools and Lua -- not
  BuiltinCommands, which it reaches only through `IBuiltinCommands`; the
  BuiltinCommands component links Logger, Filesystem and `ProcessFileIO` --
  not HaisosOS, not Console; Factory links both. Of the five fields, the
  component could derive two (the pid from the header-only allocator, the
  parent from `GetOSProcessID()`) and the path is a plain argument, but it
  cannot make the console (`IHaisosOS` exposes no physical console, and the
  adapter that tags one lives in the Console component), and the OS a process
  runs under is the OS's to hand over. The struct is how "what only the OS
  knows" crosses the component boundary. Commit `90e094f`, which introduced
  it, records no other reason.
- **The other runtimes.** The OS builds `AgentProcess` and `LuaProcess` itself,
  in the HaisosOS component, passing pid, parent pid, environment, path,
  working directory, weak OS, a `CurrentProcessHandle` (their tools need the
  process before it exists), the program and the console as `Create()`
  arguments. Lua's `print` console and an agent's console are handed over at
  creation: `ICurrentProcess` has no console accessor.
- **The builtin's process.** Shaped like `LuaProcess`: private constructor,
  `Create()` with the `DestroyOffRuntimeThreads` deleter, the thread started
  once the object is whole, a `RuntimeThreadScope` named
  `builtin <path> pid=<n>` (a name the log documentation lists), a destructor
  that joins, and an error logged when it waits for itself on its own thread.
  Its thread builds the command's context (currently `BuiltinContext`, in
  `BuiltinCommand.h`: the process, the command, the arguments, the console and
  the stop flag), runs `IBuiltinCommand::Run`, turns an exception into
  `<name>: internal error: <what>` and status 1, stores the status and marks
  itself finished. Its thread plumbing -- start, finished flag and condition
  variable, join lock, own-thread check, timed wait -- is `LuaProcess`'s almost
  line for line.
- **Stopping.** `TriggerStop` sets an atomic flag that the context exposes as
  `StopRequested()`. `cat` (between files and between reads), `ls` (between
  directories, and while recursing) and `mkdir` (between operands) check it and
  return early: 1, or `ls`'s status so far. No test exercises it.
  `ICurrentProcess` has no stop query: `LuaProcess` keeps an internal kill
  flag, the concrete `Agent` a private stop flag (set by `TriggerStop`, which
  `self_close` and an interactive agent's end of input call). No interface in
  `interfaces/` carries a `std::atomic` or a `std::function`, and the project
  is C++17, so there is no `std::stop_token`.
- **Exit status.** `IBuiltinCommand::Run` returns GNU's statuses; `IProcess`
  has none. The process class keeps the status for tests only, which read it
  through a `dynamic_pointer_cast` to the concrete class.
  `notes/explore-stdio-exit-codes.md` decided an exit code on `IProcess` (D8),
  not built yet.
- **Unknown builtin names.** The haisosfile's `BUILTIN` refuses a name that
  `GetCommands()` does not list. `IBuiltinConfigurator` does not check ("a path
  naming a builtin that does not exist simply fails to start"), which holds
  today because `RunCommand` returns null. `GetBuiltinVersion` is documented to
  return an empty string for an unknown name.
- **Other users of `IBuiltinCommands`.** The Factory creates it
  (`IFactory::CreateBuiltinCommands`: "stateless apart from the processes it
  starts"); `haisos --init` and the `BUILTIN` directive call `GetCommands()`
  only; `IHaisosOS::CreateSubOS` takes one, typically the parent's. It is
  private to the OS: no running program is ever handed one.
- **Tests.** `BuiltinCommands.unittests` (linking BuiltinCommands and Factory)
  build an OS over an in-memory root, run every command through `StartProcess`
  and a wait, strip the console tag, and read the status through the cast.
  Three of them test the OS rather than a command: a builtin runs whatever its
  path's extension; a builtin process has the path, parent and empty agent name
  of any process; its `IO()` reads a builtin but cannot write it.
  `HaisosOS.unittests` test builtin dispatch and an OS without builtins, and
  build `LuaProcess` directly, with no OS and a test tool factory.
  `tests/mocks/` has `MockAgentConsole` and no `ICurrentProcess` fake.
  `tests/haisos/builtins.haisostest` checks console content only.
- **`ProcessFileIO`** is a library of its own "so a runtime living outside this
  component -- `BuiltinProcess` -- gives its processes the same I/O without
  linking all of `HaisosOS`" (`src/components/HaisosOS/CLAUDE.md`). Nothing
  else needs it apart.
- **Documentation of the arrangement.** The root `CLAUDE.md` (the interfaces
  line lists `BuiltinCommandHost`; the component table says the builtins are
  "each run as a process on its own thread"; "Creating things" names
  `~BuiltinProcess`), `src/components/BuiltinCommands/CLAUDE.md` ("How a
  builtin gets run", Key Classes), `src/components/HaisosOS/CLAUDE.md` (the
  builtin paragraph, the classes created with the deleter, `ProcessFileIO`),
  and the comment on `IFactory::CreateBuiltinCommands`.

## Open aspects

### A1. How does a running command learn that its process was asked to stop?
Today the flag is the process class's, passed into the context by reference.
Once the process is the OS's and `RunCommand` is handed only the process, the
request has to reach the command some other way. This shapes either
`ICurrentProcess` or `RunCommand`.
Combinable: no

1. **Query on ICurrentProcess** -- e.g. `bool StopRequested() const`, the
   inside half of `IProcess::TriggerStop()`; the context asks the process. Each
   process answers from what it already has: the builtin's flag, the Lua
   process's kill flag, and the agent's own flag (a public query on the
   concrete `Agent`, which `AgentProcess` holds; `IAgent` unchanged). For:
   `RunCommand` stays (process, console, name, args); "was I asked to stop" is
   the process's own state, and the process is the one door; it is the question
   others want answered -- waits in tools (`notes/note-review-medium-findings.md`,
   section 2), an interruptible pipe read (`notes/explore-stdio-exit-codes.md`,
   D7), the per-process stop of `notes/note-process-start-stop.md`, a `kill`
   builtin waiting with `--timeout` for another to end while it may itself be
   stopped; a test process answers it with a plain flag. Against:
   `ICurrentProcess` is the central interface -- three process classes and
   every fake implement one more method, used only by builtins at first; a
   query cannot wake a blocked wait, which a token would have to add later.
2. **Flag parameter** -- `RunCommand` also takes the process's
   `const std::atomic<bool>&`, which the context keeps as today. For: no change
   to `ICurrentProcess`; today's mechanism exactly; the smallest diff.
   Against: the first `std::atomic` in `interfaces/`; a second channel into the
   command beside the process; only builtins ever learn of a stop this way; the
   signature strays further from the note's.
3. **Callback parameter** -- a `std::function<bool()>` asked at each check.
   For: no atomic in an interface; the process decides what "stopped" means.
   Against: as 2, plus a type-erased call at every check.
4. **Stop-token type** -- a small class in `interfaces/` (C++17 has no
   `std::stop_token`), owned by each process, able to wake waits, reachable
   through `ICurrentProcess`. For: the token `notes/note-process-start-stop.md`
   asks for, designed once. Against: a design of its own (waking waits,
   callbacks, children stopped first) that belongs to the stop/kill work; three
   commands that only poll between steps do not need it.

### A2. Where does a builtin's exit status go once RunCommand returns it?
`RunCommand` returns the status to the process class in HaisosOS, and the
command tests read it straight from `RunCommand`; the question is whether it
goes further now. `IProcess` has no exit status.
Combinable: no

1. **Kept in the process** -- the builtin's process class stores and logs it
   (with an accessor for HaisosOS tests, like today's test-only one) until
   `IProcess` gets an exit code with the stdio work. For: no interface change;
   the cast-to-the-concrete-class hook loses its reason in the command tests.
   Against: the status still reaches no agent, script, `os_list_processes` or
   `haisos`'s own exit code.
2. **On IProcess now** -- `notes/explore-stdio-exit-codes.md` D8 brought
   forward: an exit code on `IProcess`, empty until the process has finished; a
   builtin's is `RunCommand`'s result, or 143 when it ended because it was asked
   to stop (the commands return 1 today); Lua scripts and agents get theirs per
   D12/D13 there, or stay empty until then. For: the design is already
   decided, and the status becomes visible (`os_list_processes`, a future
   `wait`, `haisos`'s exit code, stdio A8). Against: pulls part of the stdio
   work into a restructuring; an `IProcess` method that two of three runtimes
   leave empty, or D12/D13 come too.
3. **Logged only** -- written to the log and dropped. For: nothing kept.
   Against: a status computed and thrown away; no process-level test can see
   it.

### A3. Do LuaProcess and the moved builtin process share their thread plumbing?
Moved into HaisosOS, the builtin's process sits beside `LuaProcess`, whose
thread handling it copies. `AgentProcess` has no thread of its own (its agent
does).
Combinable: no

1. **Move as is** -- the builtin process keeps its own copy. For: a pure move,
   reviewable as one. Against: two copies of subtle code (join under a lock,
   the waiting-for-itself check) in one component, kept alike by hand.
2. **Shared helper** -- a small class owning a runtime thread (start once the
   owner is whole, the runtime-thread scope, finished state, timed wait, join,
   own-thread check), held by both process classes. For: one copy; composition
   leaves `AgentProcess` alone. Against: touches `LuaProcess` inside a move; the
   two destructors differ (`LuaProcess` kills and warns after 5 s, the builtin
   joins at once), so the helper must leave that to its owner.
3. **Common base class** -- an abstract threaded process implementing the
   shared `ICurrentProcess` parts (pid, parent, path, environment clone, `IO()`,
   `OS()`) and the thread. For: the most code removed. Against: inheritance for
   code sharing; the thread body is a virtual called from a thread the base
   starts, which must wait until the derived object is whole; `AgentProcess`
   does not fit it.

### A4. Does the rest of note-builtin-method-names.md's rename ride along?
The new `RunCommand` names its parameter `commandName` either way (D2); the
note also renames `GetBuiltinVersion` and settles `IBuiltinCommand`'s getters.
Combinable: no

1. **Separate** -- only the new signature follows the note's spelling; the rest
   stays that note's own to-do. For: a focused change. Against:
   `IBuiltinCommands` is edited twice in a row.
2. **Rides along** -- the whole rename in the same work. For: the interface
   changes once, and the same files are open anyway (the BuiltinCommands
   component, its tests, the Factory tests). Against: a bigger diff mixing a
   restructuring with renames; `IBuiltinCommand`'s getters reach every command
   file.

## Decided aspects

### D1. Who builds a builtin's process, and who calls RunCommand
**Chosen:** the OS builds the process -- a process class in the HaisosOS
component, like `AgentProcess` and `LuaProcess` -- and that process's own
thread calls `IBuiltinCommands::RunCommand` with the process itself, which runs
the command to completion on that thread and returns its exit status, starting
nothing. Decided by the user (`notes/note-process-runs-builtin.md`). The code
bears it out: `BuiltinCommandHost` exists only because the process is built in
a component that cannot know the OS's pid, weak self or console (it may not
link HaisosOS or Console), so building the process where those are known
leaves the struct nothing to carry. It also puts every `ICurrentProcess`
implementation in the component that hands out OSes, so narrowing a process's
OS (root `CLAUDE.md`, Security) or revoking its door
(`notes/note-process-start-stop.md`) is done in one place for all runtimes;
the runtime-thread rules are applied in one component; and a command becomes a
plain function of its arguments and files, testable without a thread.
**Ignored:**
- Keeping it as it is, with the reason documented -- the BuiltinCommands
  component would keep building processes and threads beside the OS, and the
  struct would remain a hand-over of what only the OS knows; the user chose
  otherwise.
- Plain parameters instead of the struct -- the same design behind a
  nine-parameter `RunCommand`; the packaging was never the problem.
- `RunCommand` given an `IHaisosOS` and asking it for a pid and a console --
  `IHaisosOS` would gain process-building methods (mint a pid, make a tagged
  console) that anyone holding an OS could call, running programs included
  through `ICurrentProcess::OS()`, and so forge identities; and numbering its
  processes is the OS's job (`src/components/HaisosOS/CLAUDE.md`).
- Folding the fields into `StartProcessOptions` -- the options are what any
  caller of `StartProcess` may ask for (`os_start_process` included); pid,
  parent, OS and console are what the OS decides, and no caller may set them.
- The BuiltinCommands component deriving what it can (the pid from the
  program-wide allocator, the parent from `GetOSProcessID()`) -- the console
  still cannot be made there, so the struct shrinks but stays, and process
  numbering leaves the OS.
- `IBuiltinCommands` handing out a per-command object for the process to run
  -- `IBuiltinCommand` and its context (GNU parsing, `--help`, not-treated
  reports) would move into `interfaces/`, for nothing `RunCommand` by name does
  not already give.
- Moving the commands themselves into HaisosOS -- the OS would link every
  command and lose `IBuiltinCommands` as a separate dependency (null for an OS
  with no builtins, shared with sub-OSes, listed by `haisos --init`), and the
  command tests would need a whole OS.

### D2. The shape of RunCommand
**Chosen:** `int RunCommand(ICurrentProcess& process, std::shared_ptr<IAgentConsole> console, const std::string& commandName, const std::vector<std::string>& args)`,
plus a stop parameter only if A1 picks one (the order of the parameters is the
plan's). In detail:
- The process by reference: the call is synchronous and keeps nothing, and
  its caller is the process itself, alive for the whole call because its
  destructor joins the thread making it. The context already holds the process
  by reference. A `shared_ptr` would need `shared_from_this` on the process
  class, for nothing.
- No environment, working directory or options: the process has its
  environment (`IProcess::GetEnvironment()`, a clone, enough for reading; no
  command reads one today) and its directory (`IO()->GetCurrentDirectory()`),
  and `interactiveAgent` means nothing to a builtin (the OS logs it as ignored,
  as it does for a `.lua` program).
- The console per D3.
- It returns the command's exit status as `IBuiltinCommand::Run` gives it:
  GNU's values.
- It never throws: a command's exception becomes today's
  `<name>: internal error: <what>` line and status 1 inside `RunCommand`, the
  way a tool's exception becomes its error result (root `CLAUDE.md`, Tools).
  The process's thread keeps only a catch-all backstop, since an exception
  leaving a `std::thread` is `std::terminate`.
- An unknown name -- reachable only by a direct caller (D4) -- returns 127 and
  is logged, printing nothing: 127 is what GNU `env` and `timeout` return when
  "COMMAND cannot be found", and what bash returns for a missing command; it is
  the shell, not the command, that prints "command not found".
- All output is flushed before it returns (the context flushes at its end), so
  a process reports finished only after its last line is written.
- No `RuntimeThreadScope` inside: it runs on its caller's thread, which a
  runtime caller has already marked; a test calling it holds everything it
  passes.
- It gives no program new reach: its caller already holds the process, whose
  `IO()` and `OS()` are all a command uses, and nothing running inside an OS is
  handed an `IBuiltinCommands`.
**Ignored:**
- `std::shared_ptr<ICurrentProcess>` -- see above.
- Keeping environment and working-directory parameters -- they would repeat
  what the process says, and could contradict it.
- Returning a result object (status plus a message) -- the message already
  went to the console; a status is what a program has.
- Letting exceptions out -- every caller would need the same catch.

### D3. Where a builtin's output goes until there is stdout
**Chosen:** a console parameter of `RunCommand`: the tagged console the OS
makes for the process, which the builtin's process holds the way `LuaProcess`
holds its `print` console. This is one parameter more than the note's
(process, name, args), and it lasts only until stdout exists: a builtin then
writes descriptors 1 and 2 through `process.IO()`
(`notes/explore-stdio-exit-codes.md`, D1 and D11), and the parameter goes.
**Ignored:**
- A console on `ICurrentProcess` (a `Console()` accessor) -- exactly the stream
  object on `ICurrentProcess` that `notes/explore-stdio-exit-codes.md` D1
  rejected (two I/O worlds; streams belong in `IO()`'s descriptors). It would
  be added to every process class and fake now, to be removed by stdio, and
  agents and Lua scripts get their console at creation, not through
  `ICurrentProcess`.
- Stdio first -- the stdio work has ten open aspects; this restructuring would
  wait on all of them to save one parameter.
- A console device path (`/dev/tty`, `/dev/console`) resolved per process by
  `IFileIO` -- invents what that exploration's A9 (`/dev/stdout`) leaves open.

### D4. An unknown builtin name
**Chosen:** the OS refuses it before building anything, and `StartProcess`
returns null as it does today. The OS asks its `IBuiltinCommands` whether it
knows the name (`GetBuiltinVersion` returns an empty string for an unknown one;
`GetCommands` lists them), so `IBuiltinConfigurator`'s "a path naming a builtin
that does not exist simply fails to start" stays true, and `os_start_process`
and `RUN` go on reporting a failed start. `RunCommand`'s 127 (D2) is then only
for direct callers.
**Ignored:**
- Starting a process that exits 127 -- a documented behaviour change, a start
  reported as successful where it fails today, and a thread for nothing.

### D5. The builtin's process in the HaisosOS component
**Chosen:** the process class currently `BuiltinProcess` moves into the
HaisosOS component, keeping its name, beside `AgentProcess` and `LuaProcess`.
The OS's builtin start path builds it the way it builds a `LuaProcess`: a
private constructor and a `Create()` taking the pid, the parent pid, the
environment, the path, the working directory, the weak OS, the OS's
`IBuiltinCommands`, the command name, the arguments and the console, returning
a `shared_ptr` with the `DestroyOffRuntimeThreads` deleter and starting the
thread once the object is whole. The thread begins with a `RuntimeThreadScope`
named `builtin <path> pid=<n>`, as now, calls
`RunCommand(*this, console, name, args)`, records the status (A2) and marks the
process finished. The destructor joins; a timed wait on its own thread keeps
its error line. The rest stays as it is: the OS allocates the pid from the
program-wide allocator and names itself as parent, the console keeps its
`<builtin name>_<pid>` tag, and HaisosOS still depends on the BuiltinCommands
component only through `IBuiltinCommands`. The process holds the OS's
`IBuiltinCommands` by `shared_ptr`; the set holds nothing back, so there is no
cycle. It needs no `CurrentProcessHandle`: it has no tools, and `RunCommand`
is handed the process itself.
The runtime-thread rules still apply for the reason they apply today: the
command's thread can hold the last reference to its OS, which its file I/O
takes for a moment on every operation, and releasing the OS releases this
process, whose destructor joins that very thread. `RunCommand` adds no thread
and waits for nothing, so the BuiltinCommands component no longer needs
`DestroyOffRuntimeThreads.h` at all.
**Ignored:**
- A new name -- `BuiltinProcess` is what three `CLAUDE.md` files and the
  runtime-thread write-up in `HaisosOSTest.cpp` already call it.
- `RunCommand` marking the thread itself -- it does not own the thread, and a
  nested scope would rename the thread in the log mid-run.
- A deleter for the `IBuiltinCommands` set -- it owns no thread and waits for
  nothing, so it may be destroyed anywhere.

### D6. BuiltinCommandHost itself
**Chosen:** deleted, with nothing in its place. The OS's weak self, the pid,
the parent pid and the path become arguments of the process class's `Create()`
inside HaisosOS; the console becomes `RunCommand`'s parameter (D3); the
BuiltinCommands component builds no process.
**Ignored:**
- Keeping it as the argument bundle of the new process's `Create()` --
  `LuaProcess` and `AgentProcess` take the same values as plain arguments, and
  one convention is enough.

### D7. The tests
**Chosen:**
- `BuiltinCommands.unittests` run each command synchronously -- e.g.
  `RunCommand(testProcess, console, "ls", args)` -- taking the status from the
  return value and the output from `MockAgentConsole` (`tests/mocks/`), with no
  tag to strip, no thread and no wait. The test process is a small
  `ICurrentProcess` of the test's own (none exists yet; it can go to
  `tests/mocks/` once a second user appears). Its `IO()` is a real
  `ProcessFileIO` over the OS the fixture already builds from the in-memory
  root, so paths, the working directory and builtins-as-files behave as they
  do for a real builtin, and its stop state is a flag the test sets (by A1's
  mechanism). That makes the stop paths of `cat`, `ls` and `mkdir` testable
  for the first time. The `IBuiltinConfigurator` tests stay as they are.
- The tests that are really about the OS move to `HaisosOS.unittests`: a
  builtin runs whatever its path's extension; a builtin process's path, parent
  and agent name; its `IO()` reading a builtin but not writing it; an
  unknown builtin name refused (D4), next to the existing OS-without-builtins
  test. The moved process class is tested there as `LuaProcess` is, built
  directly with a test `IBuiltinCommands` that records the process it is handed
  and can block until stopped: `RunCommand` runs on the process's thread with
  the process itself, the status is kept (A2), and a stop reaches it (A1).
- The `RunCommand` refusal test becomes "an unknown name returns 127 and writes
  nothing"; the null-environment and no-OS cases go with those parameters.
- `tests/haisos/builtins.haisostest` needs no change.
**Ignored:**
- Running the commands through `StartProcess` as now -- a thread and a wait
  for each call of what is a function of arguments and files, and the status
  reachable only through HaisosOS's internal class.
- A test `IFileIO` over the filesystem, with no OS -- a second `IFileIO` to
  keep in step with `ProcessFileIO`, when the fixture has an OS anyway.

### D8. The ProcessFileIO library
**Chosen:** folded back into the HaisosOS library. It was split out only so
that a runtime outside HaisosOS -- the builtin's process -- could have the same
I/O without linking all of HaisosOS. With that process inside HaisosOS, the
BuiltinCommands component drops it (keeping Logger and Filesystem, which the
commands and the configurator use), and the command tests still reach it
through the Factory, which links HaisosOS.
**Ignored:**
- Keeping it separate -- harmless, but the only reason given for it is gone,
  and the CMake comment and the HaisosOS `CLAUDE.md` would need a new one.

### D9. What else follows
**Chosen:** these follow from D1-D8, with no choice left in them:
- `interfaces/`: the comments on `IBuiltinCommands` and `RunCommand` say it
  runs a command on the caller's thread and returns its status, starting
  nothing; its includes of `IHaisosOS.h` and `IEnvironment.h` can go with the
  options and the environment. `IFactory::CreateBuiltinCommands`'s "stateless
  apart from the processes it starts" becomes "stateless".
  `IHaisosOS::StartProcess`'s "run, on a thread of its own" stays true (it is
  the process's thread).
- The root `CLAUDE.md`: `BuiltinCommandHost` leaves the interfaces line; the
  BuiltinCommands row of the component table says the commands are run by the
  process the OS starts for them; "Creating things" still names
  `~BuiltinProcess`, now in HaisosOS.
- `src/components/BuiltinCommands/CLAUDE.md`: "How a builtin gets run"
  rewritten (placed; the OS starts a `BuiltinProcess`; its thread calls
  `RunCommand`); `BuiltinProcess` leaves Key Classes; the context is described
  as built by `RunCommand`.
- `src/components/HaisosOS/CLAUDE.md`: the builtin paragraph without the
  struct; `BuiltinProcess` in Key Classes and among the classes created with
  the deleter; the `ProcessFileIO` sentence (D8); the remark that the tracked
  processes are "all `IBuiltinCommands::RunCommand` hands back" goes (keeping
  them as `IProcess` is still right: stopping, waiting and naming are all the
  OS asks of them).
- HaisosOS: the builtin start path returns its process as the other two do,
  and logs an ignored `interactiveAgent` itself.
- Nothing changes for a haisosfile, `haisos --init`, `os_start_process`,
  `os_list_processes`, the console tags, or any builtin's output or exit
  status, so no builtin's version is bumped (the root `CLAUDE.md` asks for a
  bump when a builtin's behaviour changes).
- Future builtins fit: a `kill` (`notes/note-kill-builtin.md`) finds processes
  through `process.OS()` and stops them with `TriggerStop`, which A1's
  mechanism must deliver to a builtin being killed; a `haisos` builtin
  (`notes/note-physical-directory-jail.md`) would need an `IBuiltinCommands` to
  give its sub-OS, which no running program can reach -- `RunCommand`, being a
  method of the set, could hand the set to such a command through its context
  when that day comes. Neither is needed now.
- `notes/note-builtin-placement-friend.md` is unaffected: the configurator
  stays in the BuiltinCommands component.
**Ignored:**
- Tagging a builtin's console by its path's stem, as agent and Lua consoles
  are -- a behaviour change unrelated to the struct.

## Checked

- The root `CLAUDE.md` (Security, Creating things, Builtin Commands, the
  component table), and the `CLAUDE.md` of BuiltinCommands, HaisosOS, Factory
  and libheaders.
- `interfaces/`: `IBuiltinCommands.h`, `IHaisosOS.h` (`StartProcessOptions`,
  `StartProcess`, `CreateSubOS`), `IProcess.h` (`IProcess`, `ICurrentProcess`),
  `IFileIO.h`, `IFactory.h`, `ILLMService.h` (`IAgent`, `ITool`,
  `IAgentConsole`), `IPhysicalConsole.h`; no `std::atomic` or `std::function`
  in any of them.
- BuiltinCommands component: the set and its `RunCommand`, the process class
  (creation, thread, stop flag, exception handling, test-only status), the
  command context (output, errors, stop query), the command list, the stop
  checks and project includes of `cat`, `ls`, `mkdir`, `pwd` and `echo`, and
  its `CMakeLists.txt`.
- HaisosOS component: the OS (dispatch in `StartProcess`, the builtin, agent
  and Lua start paths, process tracking, the drain), `AgentProcess`,
  `LuaProcess` (creation, thread, waits, kill), `ProcessFileIO`, and its
  `CMakeLists.txt` (the separate `ProcessFileIO` library).
- libheaders (`GloballyUniquePID.h`, `CurrentProcessHandle.h`,
  `DestroyOffRuntimeThreads.h`), the Logger's thread naming, the Agent's stop
  flag and `TriggerStop`, `self_close`, the Factory's wiring and CMake links,
  and `src/haisos/` (`GetCommands()` only).
- Tests: `BuiltinCommands.unittests` (fixture, run helper, the cast for the
  status, the OS-level tests, the refusal test, CMake), `HaisosOS.unittests`
  (builtin tests, the runtime-thread write-up, `LuaProcess` built directly),
  `Factory.unittests`, `tests/mocks/` (`MockAgentConsole`; no process fake),
  `tests/haisos/builtins.haisostest`; no test calls `TriggerStop` on a
  process.
- Git: `git log -S BuiltinCommandHost` (introduced in `90e094f`, whose message
  gives no rationale) and the recent history.
- Every file in `notes/`, including those added while this was written
  (`note-process-runs-builtin.md`, `note-builtin-method-names.md`,
  `note-builtin-placement-friend.md`, `note-kill-builtin.md`,
  `note-repeated-json-lookups.md`, `note-shell-escaping.md`).
- External: the host's GNU coreutils `env --help` and `timeout --help` (126 if
  COMMAND is found but cannot be invoked, 127 if it cannot be found), and bash
  (127 for a missing command).
