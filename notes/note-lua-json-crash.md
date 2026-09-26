# CRITICAL: Lua scripts can crash or corrupt the whole haisos process (Lua <-> JSON bridge)

**Severity: critical.** Found in the full code review of 2026-09-26 (finding 1),
reproduced on the release build and confirmed with an AddressSanitizer build.

## Where

`src/components/HaisosOS/LuaProcess.cpp`, the two converters behind every
`os_*` tool call made from a `.lua` process (`LuaProcess::LuaToolTrampoline`):

- `ToJson` (line 59) turns the Lua table a script passes to a tool into JSON
  (called at line 354, `args = ToJson(L, 1)`).
- `PushJson` (line 18) turns a JSON tool result back into Lua tables (called
  at line 377, `PushJson(L, parsed)`), e.g. whatever `os_read_file` read, when
  it parses as JSON.

Both recurse once per nesting level and:

- never call `luaL_checkstack`: each level pushes onto the Lua stack (2 slots
  per level in `ToJson`: the `lua_next` key/value at lines 79/81/86; 1 in
  `PushJson`: the table under construction at lines 41/49), past the
  `LUA_MINSTACK` (20) slots a C function is guaranteed. Lua does not check
  this in release builds, so it writes past the end of the Lua stack's heap
  buffer;
- have no depth limit, so deep enough nesting also overflows the C++ stack;
- do not detect cycles, so a self-referencing table recurses forever.

## Reproduced

- `local t = {}; t.self = t; os_list_directory(t)` -> segfault (exit 139).
- A table nested 25 deep (`for i = 1, 25 do t = {t} end`) passed to any tool
  -> segfault; at 200 deep -> `malloc(): unaligned tcache chunk detected`.
- Nested only 20 deep looks fine in release, but ASan reports
  `heap-buffer-overflow` in `luaH_next` <- `lua_next` <- `ToJson`
  (LuaProcess.cpp:79/81): silent heap corruption.
- The other direction: a script doing `os_read_file({path="/nested.json"})` on
  a file holding `[[[...]]]` 300 deep -> segfault inside `PushJson`.

## Why it is critical

The crash takes down the entire haisos process -- every OS, agent and
process in it -- not just the script. And a `.lua` program is untrusted
input: an agent can write one with `os_write_file` and start it with
`os_start_process`, and any script that merely reads a file someone else
wrote (an agent, another process) can be crashed by that file's content. The
Lua sandbox (`OpenSafeLuaLibs`) is sound; this bypasses it through the
host-side bridge.

## Fix direction

- `luaL_checkstack(L, 3, "...")` at every level of both functions, turning an
  overflow into a normal Lua error.
- A depth limit (e.g. 100-200 levels) in both, reported as a tool error.
- Cycle detection in `ToJson` (a set of visited table pointers,
  `lua_topointer`), reported as a tool error.
- While there: strings are converted with `lua_tostring`/`lua_pushstring`
  (lines 72, 379), which cut at the first NUL byte; `lua_tolstring`/
  `lua_pushlstring` keep binary data intact.
- Add unit tests in `tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp`
  for a cyclic table, deep nesting both ways, and a NUL-containing string.
