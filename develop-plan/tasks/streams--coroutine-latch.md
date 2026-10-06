# Task streams--coroutine-latch: Lua exit/broken pipe stop coroutines' resumers at once; round-cap test

- Rock: streams
- Depends on: streams--exit-codes, pipes--broken-pipe
- Size: ~200 changed lines in ~4 files
- Plan checked against: develop @ 7e857e9
- PR title: Lua: latch exit and broken pipe across coroutines; test the LLM round cap

## Goal

A Lua script that calls `exit(n)`, or whose `print` hits a pipe nobody reads,
from inside a coroutine stops **before the next instruction** of the thread
that resumed the coroutine, exactly as it does outside one. It never runs
another instruction or another tool call. It exits `n`, or 141 for the
broken pipe. Today the latched kill hook is re-armed only on the coroutine's
own thread. `coroutine.resume` catches the "exit" error and returns `false`,
and the resuming thread runs on until its next count hook, up to ~1000
instructions, tool calls included.

The agent's LLM round cap (20 rounds per command) also gets the test it lacks.

## Context

Read first: the root `CLAUDE.md` (Security: `ICurrentProcess`), and
`src/components/HaisosOS/CLAUDE.md`.

- `src/components/HaisosOS/LuaProcess.cpp`:
  - `KillHookTrampoline` (~line 256): the latched hook. When
    `IsKillRequested() || IsExitRequested()` it re-arms itself on `L` with
    `LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1` and raises
    `luaL_error(L, "process killed")`. `StopForBrokenPipe()` sets
    `m_brokenPipe` and calls `Kill()`, so a broken pipe counts as a kill.
  - `LuaPrintTrampoline` (~line 593): on `m_brokenPipe` it re-arms the hook on
    `L` only. It must never raise, because it has C++ locals.
  - `LuaExitTrampoline` (~line 631): sets the exit flags, re-arms the hook on
    `L` only, and raises `luaL_error(L, "exit")`.
  - `m_luaState` is the main thread. `OpenSafeLuaLibs` (~line 212) opens
    `coroutine`. `lua_sethook` acts on one `lua_State` (thread) only. A
    coroutine is a separate thread. It inherits the hook settings of the
    thread that creates it, but later `lua_sethook` calls on one thread do
    not reach the others.
- `src/components/Agent/Agent.cpp` `ProcessCommand` (~line 515):
  `MAX_LLM_ROUNDS` = 20. Past it, `m_lastCommandFailed = true` and
  `WriteError("Error: the command reached the maximum of 20 LLM rounds")`.
  No test covers it.
- Tests that already exist and that you will copy: `LuaProcessTest.ExitCannotBeCaughtByPcall`
  (`tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp`, the
  `RunScript` helper), `HaisosOSPipeTest.ALuaScriptPrintingIntoAPipeWithNoReaderExits141`
  (`HaisosOSPipeTest.cpp`), and `AgentTest.LastCommandFailedFollowsTheLastCommand`
  (`tests/unit/components/Agent.unittests/AgentTest.cpp`, `MockLLMCommunicator`,
  `MockAgentConsole`).

## Changes

### `src/components/HaisosOS/LuaProcess.cpp` (and `.h` if a member is needed)

1. Add one file-local helper, used by all three places that re-arm the
   latched hook today (`KillHookTrampoline`, `LuaPrintTrampoline`,
   `LuaExitTrampoline`), instead of each calling `lua_sethook` itself:
   ```cpp
   // Arms the latched kill hook on L and on the script's main thread.
   void ArmLatchedKillHook(lua_State* L);
   ```
   It calls `lua_sethook(..., &KillHookTrampoline, LUA_MASKCOUNT |
   LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE, 1)` on `L` and on the main
   thread. Get the main thread from the registry
   (`lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD)` then
   `lua_tothread`, popping it afterwards) or from `SelfFromState(L)->m_luaState`.
   Both are fine. It must not allocate C++ objects, because it runs inside
   trampolines that must not throw.
2. **Nested coroutines** (main → co1 → co2): arming `L` and the main thread
   leaves co1, the thread between them, on its plain count hook. Close this
   gap by **wrapping `coroutine.resume` and `coroutine.wrap`**, in
   `OpenSafeLuaLibs` or `RegisterBindings`, after the coroutine library is
   opened:
   - `resume`: a C function that calls the original `coroutine.resume` (kept
     as an upvalue) with all its arguments (`lua_call`, `LUA_MULTRET`). When
     the call returns, it calls `ArmLatchedKillHook(L)` on the **calling**
     thread if `IsKillRequested() || IsExitRequested()`. Then it returns all
     results unchanged. The hook raises on the caller's next instruction, so
     the caller never acts on the `false, "exit"` it got back.
   - `wrap`: `coroutine.wrap(f)` returns a function that raises the
     coroutine's error in the caller. Keep the original's behaviour by
     wrapping the function it returns: a C closure over that function. Call
     it with `lua_pcall`, arm the caller if the latch is set, then return its
     results, or re-raise its error with `lua_error`. `lua_error` is allowed
     here because this closure holds no C++ locals.
   - Neither wrapper may hold C++ objects with destructors across `lua_call`,
     `lua_pcall` or `lua_error` (see the comment on `LuaToolTrampoline`).
3. Update the comments on the three trampolines to say that the hook is now
   armed on the main thread as well, and that coroutine resumers are armed by
   the wrappers.

The rule that applies: `ICurrentProcess` stays the only door out. Nothing
new reaches outside the process.

### `tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp`

- `ExitInsideACoroutineEndsTheScript`: script
  `coroutine.resume(coroutine.create(function() exit(2) end)) print('after')`.
  `out` is `""`, `err` is `""`, exit code 2.
- `ExitInsideANestedCoroutineEndsTheScript`:
  `coroutine.resume(coroutine.create(function() coroutine.resume(coroutine.create(function() exit(5) end)) print('mid') end)) print('after')`.
  `out` is `""`, exit code 5.
- `ExitInsideAWrappedCoroutineEndsTheScript`:
  `pcall(coroutine.wrap(function() exit(6) end)) print('after')`. `out` is
  `""`, exit code 6.
- `CoroutinesStillWork`: `local co = coroutine.create(function(a) local b = coroutine.yield(a + 1) print(b) end) local _, x = coroutine.resume(co, 1) print(x) coroutine.resume(co, 'z') print(coroutine.status(co)) local g = coroutine.wrap(function() coroutine.yield(7) end) print(g())`.
  `out` is `"2\nz\ndead\n7\n"`, exit code 0. A plain error inside a coroutine
  is still caught: `print(coroutine.resume(coroutine.create(function() error('x', 0) end)))`
  prints `false\tx\n`, exit code 0.

### `tests/unit/components/HaisosOS.unittests/HaisosOSPipeTest.cpp`

- `ALuaScriptPrintingIntoAPipeWithNoReaderInsideACoroutineExits141`. Set it up
  as `ALuaScriptPrintingIntoAPipeWithNoReaderExits141` does, with the script
  `coroutine.resume(coroutine.create(function() print("a") end))\nos_write_file({path = '/after.txt', content = 'x'})\n`.
  Assert exit code `kExitCodeBrokenPipe`, `/after.txt` absent, and the console
  output and error empty.

### `tests/unit/components/Agent.unittests/AgentTest.cpp`

- `ACommandReachingTheRoundCapFails`. A `MockLLMCommunicator` that answers
  every call with a tool call, for a tool the `ToolFactory` knows, such as
  `get_current_date_time`, so the conversation never ends on its own. Check
  `MockLLMCommunicator` for how a tool-call response is set. If it has no
  way to do this, add a small setter there (`tests/mocks/`) and keep it
  minimal. Post one command, then wait as `LastCommandFailedFollowsTheLastCommand`
  does until `LastCommandFailed()` is true. Assert that the call count is 20,
  not 21, and that `console->GetErrors()` contains `"Error: the command reached
  the maximum of 20 LLM rounds"`. Then `TriggerStop` and `WaitToFinish`.

### Commands

```bash
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U LuaProcess
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U Agent
```

## Docs

- `src/components/HaisosOS/CLAUDE.md`: in the Lua section, one sentence that
  `exit()` and a broken pipe stop the script from inside any coroutine too,
  and that `coroutine.resume`/`wrap` are wrapped for this.

## Acceptance

- [ ] `exit()` and a broken-pipe `print` inside a coroutine, nested or not,
      `resume` or `wrap`, stop the script before the resumer's next
      instruction. The four new Lua tests and the pipe test pass.
- [ ] Ordinary coroutines behave exactly as before (`CoroutinesStillWork`).
- [ ] All three trampolines arm through the one helper. No C++ object lives
      across a `lua_error`, `lua_call` or `lua_pcall` in the new wrappers.
- [ ] The round-cap test asserts exactly 20 LLM calls and the error line.
- [ ] All Linux unit tests are green.

## Out of scope

- Any change to the kill hook's count interval (1000) or to how a plain kill
  reaches a running coroutine.
- Lua `io`/`os`, any other Lua library change.
- The Windows CI per-executable timeout (raised in `scripts/` by the plan
  session).
