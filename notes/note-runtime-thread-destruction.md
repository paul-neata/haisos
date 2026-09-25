# Fix: objects released last on their own threads

The bug is fixed. All unit tests (20 binaries), integration tests (14) and haisos tests (6) pass on the Linux release build. Nothing is committed, and I didn't build for Windows or WASM.

**What was wrong.** A thread running a program (an agent, a Lua script or a builtin) can hold the last reference to its own process and to its OS. An `os_*` tool holds both for the length of a call, and an agent hands itself to every tool it calls. When everyone else lets go during the call, the object is destroyed on that thread, and its destructor ends up waiting for the thread it is running on. There were three results:
- **The OS** wasted 5 s waiting for the process it was running on.
- **Lua scripts and builtins** crashed the program (`std::terminate`).
- **Agents** hung forever. A new test confirmed this on the unfixed code: `~Agent` logged "has not stopped … still waiting" every 5 s and never finished.

**The fix** is a new header, `src/components/libheaders/DestroyOffRuntimeThreads.h`:
- Each runtime thread (agent, Lua, builtin, and the interactive agent's input loop) now marks itself on start.
- `HaisosOS`, `LuaProcess`, `BuiltinProcess`, `AgentProcess` and `Agent` get a custom deleter. If the last reference is released on a marked thread, the object is handed to a single dedicated destruction thread, where waiting is safe. Anywhere else, it is destroyed immediately, as before.
- The destruction thread starts on first use, and at exit it finishes everything it has been handed before the program ends.

**New log lines:**
- Every line now names its thread: `[timestamp][LEVEL][thread][file:line] message`, also in `--log-to-file`. Names are `main`, `agent <name>`, `lua <path> pid=<n>`, `builtin <path> pid=<n>`, `input <agent>`, `destruction`, or `t<N>` for unnamed threads. Nothing in the repo parses the old format.
- An INFO line when an object is handed off; DEBUG lines from the destruction thread with each destruction's duration; DEBUG "destroying" from each destructor; per-process stop times during OS shutdown.
- ERROR lines if the problem ever comes back: a process or agent waiting for itself, a builtin destroyed on its own thread, or the OS destroyed on a runtime thread.

The fixed scenario now reads like this:
```
[INFO][lua read_held.lua pid=2] HaisosOS pid=1: its last reference was released on a runtime thread, so it is destroyed on the destruction thread
[DEBUG][destruction] DestructionThread: destroying HaisosOS pid=1
[INFO][destruction] HaisosOS pid=1: shutting down, draining 1 running process(es)
[DEBUG][destruction] HaisosOS: process pid=2 'read_held.lua' stopped in 0ms
```

**Documentation:**
- **Full write-up:** in `HaisosOSTest.cpp`, under "Objects released last on their own threads": the problem, each symptom and how it showed, the fix, what to look for in the log, and how the tests reproduce it.
- **Agent case:** written up next to its test in `AgentTest.cpp`.
- **The rule:** added to "Creating things" in the root `CLAUDE.md`, so new thread-owning classes follow it. The Logger, libheaders, HaisosOS, Agent and BuiltinCommands docs are updated too.
- **Code comments:** at each change, saying why it's there.

**Tests:**
- The two HaisosOS tests now pass in 8 ms and 20 ms.
- The new agent test failed before the fix and passes now.
- There are 4 new tests for the header itself, including the problem in miniature.

One behaviour change: an object released on a runtime thread is now destroyed shortly afterwards on the destruction thread, rather than immediately. The unreachable extra passes in `~HaisosOS`'s drain loop are unchanged, as you haven't said whether you want them removed.
