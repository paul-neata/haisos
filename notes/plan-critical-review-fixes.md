# Plan: fix the critical/high findings of the 2026-09-26 code review

Seed: the review's critical/high findings 1-8 (finding 1 in detail in
`notes/note-lua-json-crash.md`). Order of work: 1 -> 4 -> 5 -> 3 -> 2 -> 6 -> 7 -> 8.
After each finding: `./scripts/build_linux_on_linux.sh`, `./scripts/test_linux.sh L U`
(must end "0 failed"), and the matching black-box regression checks (the
reviewer's scratch `run_all.sh`, filtered by `f1`, `f3`, ...). Nothing is
committed; no version bump.

## Finding 1 (critical): the Lua <-> JSON bridge can crash or corrupt the process

Files: `src/components/HaisosOS/LuaProcess.cpp` (`ToJson`, `PushJson`,
`LuaToolTrampoline`, `RegisterBindings`), `src/components/HaisosOS/CLAUDE.md`.

- A named limit `kMaxJsonNestingDepth = 100` (tables inside tables, both ways).
- `ToJson(L, index, depth, ancestors)`:
  - `depth > limit` -> throw a `LuaArgumentsError` ("nested too deeply").
  - cycle detection: `lua_topointer` of every table on the current path kept in
    a small vector; meeting one again -> throw ("contain a cycle"). A table
    reached twice through different branches (a DAG) is still fine.
  - `lua_checkstack(L, 3)` (the non-raising one, never `luaL_checkstack`) before
    each table's pushes; failure -> throw.
  - strings with `lua_tolstring` + explicit length (values and keys); keys no
    longer skipped when empty.
  - The exception is a C++ exception thrown and caught inside our own frames:
    `LuaToolTrampoline` calls `ToJson` directly, no Lua frame in between. The
    trampoline catches it first, logs, `lua_settop(L, 0)` (drops whatever the
    conversion left on the stack), and returns `("<tool>: arguments ...", true)`.
- `PushJson(L, value, depth)` returns `bool`: false when nested deeper than the
  limit or `lua_checkstack` fails. Strings/keys via `lua_pushlstring` +
  `lua_rawset`/`lua_rawseti` (no `lua_setfield`, whose key is a C string).
  The trampoline remembers `lua_gettop` before, and on false resets the top and
  pushes the raw result string instead (as for any non-JSON result).
- Raw results and the `arg` table: `lua_pushlstring` with lengths.
- Tests (`tests/unit/components/HaisosOS.unittests/LuaProcessTest.cpp`, new
  test tools in its `TestToolFactory`: `record_args`, `echo_args`,
  `nested_result`): cyclic table -> error pair, script goes on, tool never
  called; 30-deep table converted whole; 5000-deep -> error pair; a DAG is not a
  cycle; 50-deep JSON result -> table, 300-deep -> raw string; NUL bytes survive
  in values, keys, raw results and `arg`. In `HaisosOSTest.cpp`: a script
  reading a 300-deep JSON file with the real `os_read_file` gets the string.
- Verify: unit tests; regression `f1`; ASan build outside the repo, `f1` there.

## Finding 4 (high): invalid UTF-8 silently kills the agent

Files: `src/components/LLMCommunicator/LLMCommunicator.cpp`,
`src/components/Agent/Agent.{h,cpp}`, `src/tools/agent_query/AgentQueryTool.cpp`,
`src/tools/os_list_processes/OSListProcessesTool.cpp`,
`src/tools/os_start_process/OSStartProcessTool.cpp`, CLAUDE.md of Agent and
LLMCommunicator.

- `BuildRequestJson`: `dump(-1, ' ', false, error_handler_t::replace)` (bad
  bytes reach the LLM as U+FFFD). Same for the three tool dumps.
- `Agent::RunThread` restructured: `Pop` in its own try; each command handled
  by a new `ProcessCommand(command)` inside try/catch. On an exception:
  log (keeping the "Exception in RunThread" wording), `AnswerUnansweredToolCalls`
  (every tool call of the last assistant message without a result gets an error
  result, so the history stays well-formed), `ReportCommandFailure` (error line
  to the console and the message buffer, itself guarded). An interactive agent
  then takes its next command; a non-interactive one finishes, visibly.
- Tests: `LLMCommunicatorTest` (a history with Latin-1 bytes serializes to valid
  JSON carrying U+FFFD; `Call` posts it); `AgentTest` (LLM throwing: interactive
  agent reports and continues; non-interactive finishes with the error visible;
  an exception mid-round -- a console that throws on the "Unknown tool" line --
  leaves the tool call answered in the history the next request carries).
  `tests/mocks/MockLLMCommunicator.h` gains `SetThrowOnCall` and `SetRawToolCall`.
- Verify: regression `f4`.

## Finding 5 (high): a wrongly-typed tool argument kills the agent

Files: new header-only `src/tools/tools_common/ToolArguments.h`; every tool in
`src/tools/`; `src/components/Agent/Agent.cpp` (`ExecuteToolCalls`); tool
CLAUDE.md files; root CLAUDE.md (directory tree, Tools section).

- `ReadRequiredArgument` / `ReadOptionalArgument` for `std::string`, `bool`,
  `uint64_t` (non-negative integer; integral floats accepted) and
  `std::vector<std::string>`: never throw; absent or null optional -> default;
  absent/null required -> "Missing required field: <name>" (existing wording);
  wrong type -> "Invalid field <name>: expected <type>, got <type>".
- Sweep: `os_write_file` (`append`), `os_list_directory` (`path`),
  `os_read_file`, `os_start_process` (`path`, `args` must be an array of
  strings), `agent_start` (`user_prompt`, `system_prompt`, `oneShot`),
  `agent_query` / `agent_wait_to_finish` (`names` array of strings,
  `return_console`, `return_messages`, `timeout_ms`), `agent_list_running`
  (`names`), `get_current_date_time` (`get_gmt`).
- `ExecuteToolCalls`: id/name read type-safely (no `value()`); a tool call that
  is not an object or has no string name gets an error result; parsed arguments
  that are null mean no arguments, any other non-object is an error result;
  `CreateTool` + `Call` in try/catch -> "Error: tool <name> failed: <what>".
- Tests: `AgentTest` (a throwing tool; a numeric tool name; non-object
  arguments), tool unit tests (agent_query, agent_wait_to_finish,
  agent_list_running, agent_start, get_current_date_time), and the os_* tools
  through Lua scripts in `HaisosOSTest.cpp` (`append = "true"` refused and
  nothing written; wrong-typed `path`/`args`).
- Verify: regression `f5`.

## Finding 3 (high): every agent program is mangled by SanitizeUserInput

Files: `src/components/Agent/Agent.cpp`, `src/tools/agent_start/AgentStartTool.cpp`,
delete `src/components/libheaders/SanitizeUserInput.h`, CLAUDE.md of libheaders,
Agent, agent_start; the comment in `src/components/HaisosOS/HaisosOS.cpp`.

- Commands go into the history whole, still between the
  `--- BEGIN USER INPUT ---` / `--- END USER INPUT ---` delimiters; no 64 KB cut.
- `agent_start` passes `user_prompt` / `system_prompt` through untouched.
- Rationale recorded in the Agent CLAUDE.md: none of these texts comes from an
  untrusted third party (program: haisosfile author; typed lines: the operator;
  subagent prompts: a parent at least as powerful); the untrusted content (tool
  results) was never sanitized, only delimited; the denylist is trivially
  evaded and corrupted code, markup and prose.
- Tests: `AgentTest` (phrases + angle brackets verbatim; a 90 KB multi-byte
  command whole); `AgentStartToolTest` (user and system prompt verbatim).
- Verify: regression `f3`.

## Finding 2 (high, Windows only): WinHTTP cannot reach any server

File: `src/components/HTTPClient/windows/WinHTTPClient.cpp` (cannot be compiled
here: checked by hand against the WinHTTP/Win32 signatures).

- `Utf8ToWide` helper: explicit lengths, `MB_ERR_INVALID_CHARS`, empty input ->
  empty, failure reported; no terminating NUL in the result.
- `URL_COMPONENTS`: scheme/host/path/extra-info requested with null pointers and
  `(DWORD)-1` lengths; wide URL passed with its exact length.
- Request path = UrlPath (or "/") + ExtraInfo; components copied through a helper
  that tolerates a null pointer / zero length.
- Header names/values converted without NULs; a header that is not UTF-8 fails
  the request.

## Finding 6 (high): use-after-destruction race at OS shutdown

Files: `src/components/LLMService/LLMService.{h,cpp}`,
`src/tools/agent_start/AgentStartTool.cpp`, `interfaces/ILLMService.h`
(CreateAgent may return null), CLAUDE.md of LLMService and agent_start.

- Members reordered: `m_agentsMutex`, then `m_shuttingDown`, then `m_agents`.
- Explicit `~LLMService`: under the lock set `m_shuttingDown` and move the agents
  out; then `TriggerStop` every one, then wait for each (unbounded, logged every
  5 s like `~Agent`), then release them -- outside the lock. Waiting, not only
  releasing, matters: an agent someone else still holds would otherwise keep
  running with a dangling `ILLMService&` in its tools.
- `CreateAgent`: refuses (logs, returns null) when shutting down -- checked
  before the "creating agent" log line, and again when registering (a creation
  racing the shutdown stops its new agent and returns null). `AddChild` only
  after registering. `CleanupFinishedAgents` unchanged, only ever under the lock.
- `CreateAndStartSubagent` / `agent_start`: a null agent -> error ToolResult.
- Tests: `AgentStartToolTest` (null agent -> error); `ServicesCreatorTest`
  (destroying the service stops an agent the test still holds).
- Verify: regression `f6` (log wording kept).

## Finding 7 (high): writes can escape the jail through a dangling symlink

Files: `src/components/Filesystem/PhysicalFileSystem.{h,cpp}`,
`src/components/Filesystem/CLAUDE.md`.

- `ResolveWithinRoot`: lexically normalize first (so no `..` can hide a
  component from `weakly_canonical`'s walk), canonicalize, check containment,
  then refuse when the result's last component is still a symbolic link (after
  `weakly_canonical` that means dangling).
- POSIX: `O_NOFOLLOW` added to the open flags in `PhysicalFileSystem` (not in the
  unrooted `FileSystem`) as defence in depth; comment on the remaining
  check-then-use race.
- `CreateDirectory` resolved like removal (below): `mkdir` never follows its
  last component either.
- Tests (`PhysicalFileSystemTest.cpp`, POSIX only, unique temp dir): a dangling
  link to outside -> `OpenFile(O_CREAT)` fails and nothing is created outside;
  same for `CreateDirectory`; a dangling link pointing inside is refused too.
- Verify: regression `f7`.

## Finding 8 (high): DELETE follows symlinks to directories

Files: `interfaces/IFileSystemService.h` (`FileStatus::symbolicLink`, Stat /
RemoveFile / RemoveDirectory comments), `PhysicalFileSystem.{h,cpp}`,
`src/haisos/HaisosFileOperations.cpp` (`RemoveTree`), Filesystem CLAUDE.md,
root CLAUDE.md (DELETE line).

- `ResolveEntryWithinRoot`: canonicalize the parent (containment checked, a
  dangling parent refused), append the last name; refuse "", ".", "..", i.e.
  the root itself. Used by `LocalRemoveFile`, `LocalRemoveDirectory`,
  `LocalCreateDirectory`: `unlink` removes a link itself, `rmdir` on a link fails.
- `FileStatus::symbolicLink` (default false): `PhysicalFileSystem::LocalStat`
  sets it from `symlink_status` of the unresolved last component; everything
  else in the status still describes the target. Other filesystems leave it
  false; composing ones pass it on.
- `RemoveTree` stats each path itself: a link is removed with `RemoveFile`
  (falling back to `RemoveDirectory`, which removes a Windows directory link
  itself), never descended into; an entry that cannot be stat'ed (a dangling
  link) is removed with `RemoveFile`.
- Tests: `HaisosFileOperationsTest.cpp` (physical root in a unique temp dir,
  `work/link -> ../important`, `DELETE /work` -> keep.txt kept, work/ gone; a
  dangling link inside is removed too); `PhysicalFileSystemTest.cpp`
  (RemoveFile on a link removes the link, RemoveDirectory on one fails, Stat
  reports `symbolicLink`, the root cannot be removed).
- Verify: regression `f8`, then the whole suite on release, `f1` on ASan.

## As implemented: notes and deviations

- **Finding 1** as planned. The arguments-error message is
  `"<tool>: arguments contain a cycle (a table that contains itself)"` /
  `"<tool>: arguments nested too deeply (more than 100 levels of tables)"`.
  String keys are now kept even when empty (they used to be dropped); keys of
  other types are still dropped. Verified on the ASan build too (all of `f1`,
  and the touched unit-test executables, with no reports).
- **Finding 4** as planned. The failure is still logged with the words
  "Exception in RunThread", so existing log greps keep working. The error line
  goes to the message buffer before the console (the console may be what
  failed).
- **Finding 5**: `ReadOptionalArgument` has a `std::optional<T>` overload for
  `timeout_ms`, whose absence means "wait as long as allowed". Changed
  messages: "Invalid timeout_ms: must be a non-negative integer" became the
  common `Invalid field timeout_ms: expected a non-negative integer, got ...`;
  "Error: Tool name is empty" became "Error: the tool call names no tool (its
  name must be a non-empty string)". Behaviour tightened where a wrong type
  used to be silently skipped: `names` entries that are not strings,
  `os_start_process` `args` entries, `agent_list_running`'s filter, and the
  boolean flags of `agent_query` / `agent_wait_to_finish` /
  `get_current_date_time` / `agent_start` (`oneShot`) are now errors.
  `oneShot` stays optional in the code (absent = false), as before.
- **Finding 3** as planned (the user confirmed deleting the sanitizer).
- **Finding 2**: beyond the brief, a URL whose path is empty requests "/", a
  URL with no host fails as a parse error, and a fragment ("#...") is cut from
  the extra info before it is appended. The conversion uses
  `MB_ERR_INVALID_CHARS`, so a URL or header that is not UTF-8 fails the
  request (the error names the header, never its value). Not compiled here.
- **Finding 6**: the destructor waits for every agent (not only releases them),
  so no agent can outlive the service; `CreateAgent` registers the agent as its
  parent's child only after it is in the list; `HaisosOS::StartAgentProcess`
  logs a null agent as such instead of "an agent this OS cannot own". New log
  lines ("LLMService: shutting down, stopping N agent(s)", "refusing to create
  agent") do not match the regression script's patterns.
- **Findings 7/8** share one resolver: `ResolveWithinRoot(path,
  LastComponent::{Follow,Keep})`, with the joining of a virtual path to the root
  isolated in `JoinUnderRoot` (where the reviewer's separate Windows fix for
  "/foo" not being `is_absolute()` belongs). Added beyond the brief: lexical
  normalization before `weakly_canonical`, since a `..` after a missing
  component made it return the rest unresolved (`missing/../link` opened a link
  to outside the root). A dangling link is refused even when it points inside
  the root (where it points is unknowable without following it). Side effect:
  the root of a `PhysicalFileSystem` can no longer be removed (review's low
  finding "A PhysicalFileSystem can remove its own root directory"), so `DELETE`
  of a mount point backed by a physical filesystem now empties it and then
  fails, as it already did for an in-memory mount. `RemoveTree` falls back to
  `RemoveDirectory` for a link `RemoveFile` could not remove (a directory link
  on Windows).
