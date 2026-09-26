# Code review 2026-09-26: the medium-severity findings

The full code review of 2026-09-26 (every file outside `extern/` and
`.claude/`) ranked seven findings as **medium**. They are recorded here in
detail, each with where it is, what happens, the evidence, and a fix
direction. The critical Lua finding has its own note,
`notes/note-lua-json-crash.md`.

"Reproduced" means it was shown on the release build, driving the real
`haisos` binary with a small scripted fake LLM endpoint (a Node HTTP server
answering `/api/chat` with canned replies and tool calls, some delayed).
"Code reading" means it was found by reading only.

## 1. `agent_wait_to_finish` reports a timeout as success

**Where:** `src/tools/agent_wait_to_finish/AgentWaitToFinishTool.cpp`.

- Line 100 (`bool finished = target->WaitToFinish(waitMs);`) only logs the
  result. Line 108 returns `ToolResult{"", false}` whether the agents finished
  or the wait timed out; the only errors are "not found" and "interactive
  agent without `timeout_ms`" (line 106).
- `return_console` and `return_messages` are in the schema (lines 27-34) and
  in `src/tools/agent_wait_to_finish/CLAUDE.md` (lines 17-18). They are parsed
  (lines 45-53) and then never used.
- The `agent_start` description (`src/tools/agent_start/AgentStartTool.cpp:8`)
  tells the model: "If you need the response, just call agent_wait_to_finish
  with no timeout". The tool never returns the response; the model has to call
  `agent_query` with `return_console` afterwards.
- `LLMCommunicator` never sends `is_error` to the model, so the content is the
  only signal it gets, and here the content is always empty.

**Reproduced:** a parent starts a one-shot child whose LLM reply is delayed
4 s, then calls `agent_wait_to_finish` with `timeout_ms: 100` and
`return_console: true`. The tool message in the parent's next request was
`--- BEGIN TOOL RESULT ---` + empty line + `--- END TOOL RESULT ---`: empty,
not an error, no console output. That is identical to "the child finished".

**Fix direction:** return per-agent JSON (`name`, `finished`, plus
`console_result` / `messages_result` when asked, as `agent_query` does), and
set `isError` or an explicit "timed out" status when any agent is still
running. Dump with `error_handler_t::replace`, since console output and
history may hold invalid UTF-8. Update the tool's CLAUDE.md and description.

## 2. Stopping an agent doesn't stop its subagents, and a waiting agent can't be stopped

**Where:** `src/components/Agent/Agent.cpp:102` (`Agent::TriggerStop`),
`src/components/HaisosOS/AgentProcess.cpp:107` (`AgentProcess::TriggerStop`),
`src/tools/agent_wait_to_finish/AgentWaitToFinishTool.cpp:99-100`,
`src/haisos/main.cpp`.

- `Agent::TriggerStop` closes only the agent's own command queue and sets its
  own stop flag. `AgentProcess::TriggerStop` calls it on the process's
  top-level agent only. Nothing walks `GetChildren`.
- Subagents are kept alive by `LLMService::m_agents`
  (`src/components/LLMService/LLMService.h:53`). Each holds a strong
  `m_parent` (`src/components/Agent/Agent.h:83`), while a parent holds only
  weak references to its children (`Agent.h:89`).
  - A `oneShot=false` subagent therefore keeps its thread and HTTP client
    until the whole OS (its `LLMService`) is destroyed.
  - `src/tools/agent_list_running/CLAUDE.md:9` says it "runs until the agent
    that started it is gone", but the parent can never be gone while the
    child holds it.
  - `oneShot=true` subagents keep making LLM calls after their parent's
    process has finished.
- `agent_wait_to_finish` with `timeout_ms` omitted (the documented use for
  one-shot children) blocks the calling agent's thread for up to
  `MAX_TIMEOUT_MS` = 24 h (line 12). It never checks the caller's own stop
  flag. A stop requested meanwhile takes effect only when the child finishes.
  Such stops include end of input in `AgentInputLoop`
  (`src/components/HaisosOS/AgentInputLoop.cpp:71`), the `~HaisosOS` drain,
  and `self_close`.

**Reproduced:** a `RUN -i` agent starts a one-shot child (LLM reply delayed
15 s) and waits for it with no timeout; stdin closes after 1 s. The log shows
"end of input ... asking it to stop" at +1 s, but haisos exited after
~15.1 s, when the child finished.

**Code reading, same theme:** there is no SIGINT/SIGTERM handling anywhere in
`src/`. Ctrl-C ends the process at once:
- `OUTCOPY` never runs (`src/haisos/main.cpp:366-372`), so results left in a
  `MEM` filesystem are lost for good;
- messages still queued in `Console` are lost; they are flushed by
  `physicalConsole->Stop()` at `main.cpp:374`.

**Fix direction:**
- Make `TriggerStop` reach the subtree: an agent stops its children, or
  `AgentProcess` stops its agent's subtree.
- Have waits in tools poll in slices and give up once the caller has been
  asked to stop. That needs a "stop requested" query on `IAgent`, or a
  cancellation token handed to tools.
- Release subagents when their parent finishes.
- Add a SIGINT/SIGTERM handler that shuts down gracefully: stop every
  process, wait briefly, run `OUTCOPY`, flush the console. A second Ctrl-C
  forces exit.

## 3. CI and the test suite cannot catch HTTP/LLM regressions

- **Windows CI runs no tests.** `.github/scripts/run-tests-windows.bat`
  (lines 10, 34, 37) calls `scripts\run_tests_windows.bat`, which doesn't
  exist. The runner has been `scripts/test_windows.bat` since commit
  `2b142ef`.
- **The integration tests pass with no LLM at all.** Most check only that the
  agent finished (e.g.
  `tests/integration/AgentStart.integrationtest/AgentStartTest.cpp:40`) or
  that the reply is non-empty
  (`tests/integration/LLMCommunicator.2Plus3.integrationtest/LLMCommunicator2Plus3Test.cpp:46`).
  A failed HTTP request makes `LLMCommunicator` return a non-empty
  `"Error: HTTP request failed: ..."` reply, and the agent finishes.
  - **Reproduced:** with `HAISOS_ENDPOINT=http://127.0.0.1:1/api/chat` these
    all exit 0: `LLMCommunicator.2Plus3`, `RunWithSimplePrompt`, `AgentStart`,
    `AgentWaitToFinish`, `AgentListRunning`, `Agent.PostAndProcess`,
    `Call.get_current_date_time`.
  - This is how the broken Windows HTTP client went unnoticed: the Windows
    `LLMCommunicator.2Plus3` test printed SUCCEEDED while every request
    failed with "Failed to connect".
  - The `HTTPClient.GetRequest`/`PostRequest` tests call a public URL
    (`jsonplaceholder.typicode.com`), so they depend on the network.
- **Linux CI never runs integration or haisos tests.**
  `.github/scripts/run-tests-linux.sh:12-16` runs them only when
  `tests/tool/llm_cache_proxy_database/` holds `*.response.json` files, and
  only `.gitkeep` is committed.
- **The cache proxy can't record with the documented setup.**
  `tests/tools/llm_cache_proxy/src/index.js:120` forwards to
  `HAISOS_ENDPOINT` + request path. But it tells clients (`index.js:157`) to
  use the proxy URL plus the upstream's own path, so the upstream receives
  `/api/chat/api/chat`. Reproduced with an echo upstream.
  - Replay keys on the SHA-256 of the exact request body. That can't repeat
    for any conversation containing a random subagent name (`GenerateAgentName`,
    `src/tools/agent_tools_common/AgentToolsCommon.h:19`, has 4 random
    characters) or a `get_current_date_time` result.
  - The `llm-cache` skill refers to `./scripts/run_tests_linux.sh`, which
    doesn't exist.
- **The test runner's filter matches executable names only.**
  `scripts/internal/test.js:221/237` keeps executables whose file name
  contains the filter, and only then passes `--gtest_filter=*filter*`
  (line 342).
  - The root CLAUDE.md example `./scripts/test_linux.sh L U --debug LuaProcess`
    (CLAUDE.md:314) matches no executable, because the Lua tests live in
    `HaisosOS.unittests`.
  - It prints "No tests found." and exits 0 (`test.js:314-316`). Reproduced.
- **`ctest --output-on-failure` (CLAUDE.md:321) finds no tests.** Nothing
  calls `add_test` or `gtest_discover_tests`; `CMakeLists.txt:94` only has
  `enable_testing()`. Reproduced.
- **Smaller test issues:**
  - `tests/haisos/hello_world.haisostest` only prints a line.
  - `builtins.haisostest.js` hardcodes `output/linux/haisos`, so it ignores
    `--debug` and can't run on Windows.
  - `agent_start.haisostest.js` asks for a `wait_to_finish` argument that
    doesn't exist, and passes as long as the output has no "Error:".
  - Unit tests use fixed `/tmp/...` paths (`HaisosOSTest.cpp:20`,
    `StatTest.cpp:23`, `BuiltinFileTest.cpp:165`, `BuiltinCommandsTest.cpp:512`,
    `AgentTrafficLogTest.cpp:294`), which collide between concurrent runs.

**Fix direction:**
- Fix the script name, and make the Windows scripts propagate failures.
- Make the integration tests assert real behaviour: the answer contains "5",
  the tool was actually called, the output has no "Error:". Fail when the log
  capture saw an Error.
- Fix the proxy's URL join to use the upstream origin plus the request path.
- Make recordings deterministic (seedable agent names, an injectable clock),
  and commit recordings so CI exercises them.
- Have `test.js` fail when nothing is selected, and filter tests inside
  executables (`--gtest_list_tests`).
- Either register the tests with CTest or drop the ctest docs.

## 4. The WASM target isn't buildable as documented

- `scripts/build_wasm_on_linux.sh:43` runs plain `cmake -B build/temp_wasm...`,
  with no `emcmake` and no Emscripten toolchain file.
  - That configures a host Linux build. Its binaries land in `output/linux`,
    because `CMAKE_RUNTIME_OUTPUT_DIRECTORY` is `output/${PLATFORM_NAME}`.
  - The `output/wasm/haisos.js` it prints as the run command is never
    produced.
- Even with `emcmake`, `CMakeLists.txt` tests `elseif(UNIX AND NOT APPLE)`
  (line 23) before `elseif(EMSCRIPTEN)` (line 29).
  - Emscripten's toolchain sets `UNIX`, so the platform would become `linux`
    and `find_package(CURL REQUIRED)` (line 90) would run.
  - Not verified here: emsdk isn't installed.
- WASM isn't built in CI. `.github/workflows/ci.yml` has only `build-linux`
  and `build-windows`; `.github/scripts/build-wasm.sh` is unused.
- `src/components/HTTPClient/wasm/FetchHTTPClient.cpp`:
  - The fetch is given `attr.userData = this` (line 63) and pointers into
    locals: `requestHeaders.data()` (line 75) and `body.c_str()` (line 79).
  - When the 120 s wait gives up, the fetch started at line 83 is neither
    aborted nor closed. Its later `onsuccess`/`onerror` callback then writes
    into a `FetchHTTPClient` that may be destroyed.
  - That response (line 92 on) has status 0 and an empty `error`, with no
    "timed out" message.
  - Whether `emscripten_fetch_waitUntilCompleted` can block for a fetch
    started with only `EMSCRIPTEN_FETCH_LOAD_TO_MEMORY` should be checked
    against the Emscripten version in use; if it can't, the loop spins
    through its 120 iterations without waiting.
- 64 KB stack buffers (`src/components/Filesystem/FilesystemUtils.h:96` in
  `ReadWholeFile`, `src/components/BuiltinCommands/commands/Cat.cpp:197`)
  overflow Emscripten's stacks, which are 64 KB by default in current
  releases, both main and pthread.

**Fix direction:**
- Use `emcmake cmake` in the script, test `EMSCRIPTEN` first in
  `CMakeLists.txt`, and add a CI job.
- Rework `FetchHTTPClient`: keep the request data alive until completion,
  abort and close the fetch on timeout, and report a timeout error.
- Heap-allocate the 64 KB buffers.

## 5. Quadratic-time hot spots

- **`ls` column layout.** `ListInColumns`
  (`src/components/BuiltinCommands/commands/Ls.cpp:746-786`) tries every
  column count from `count` down to 1 (line 751). Each try allocates a vector
  and scans every entry, so the whole thing is O(n²), with no `StopRequested`
  check inside.
  - Measured on a physical directory, release build:

    | Entries | `ls` (columns) | `ls -1` |
    |---------|----------------|---------|
    | 10 000  | 0.75 s         | 0.12 s  |
    | 20 000  | 2.75 s         | 0.23 s  |
    | 40 000  | 10.7 s         | 0.43 s  |

  - Fix: start at `min(count, width / (1 + kColumnGap) + 1)`; GNU ls never
    tries more than width / 3 columns.
- **Agent traffic log** (`--log-agent-to-file`, xdiff mode, the default).
  `WrapText` (`src/haisos/AgentTrafficLog.cpp:109-142`) wraps a long line by
  recounting the whole remainder (`CharCount`, line 113) and erasing the
  consumed prefix (line 118) for every 80-character output line: O(n²) per
  long line.
  - It runs on the agent's thread inside `OnSend`/`OnReceive`, under the
    log's one `m_mutex` (lines 544, 566), so every agent's traffic waits
    behind it.
  - Measured: an agent reading a single-line file of 250 KB / 500 KB / 1 MB
    spent an extra 0.09 / 0.37 / 1.45 s with `-L`. That extrapolates to
    about 2.5 minutes at `os_read_file`'s 10 MB cap.
  - Fix: walk the text with an index instead of erasing and recounting.
- **The same log's bookkeeping.** Each send re-parses the previous request,
  stored as a string (`m_lastSent`, line 556), as well as the current one.
  `m_lastSent` keeps the last request of every agent that ever ran and is
  never pruned.
- **Agent history is copied every LLM round.** `Agent::RunThread` copies the
  whole `m_history` under the lock (`src/components/Agent/Agent.cpp:360`).
  `LLMCommunicator::BuildRequestJson` then builds a JSON copy and dumps it to
  a string. That is three full copies per round, of a history that by design
  is never trimmed and can hold 10 MB tool results.
- **`AgentMessageBuffer`** (`src/components/Agent/AgentMessageBuffer.cpp:11-28`,
  constants in `AgentMessageBuffer.h:20-22`) doubles its capacity only up to
  1 MB, then grows in 512 KB steps (line 21). Each step's `reserve` copies the
  whole buffer, so large console output costs O(n²) copying.
  `std::string`'s own geometric growth would do better: just `append`.
- **`ReopeningLogFile::EnsureOpenLocked`** stats the path on every write
  (`src/haisos/ReopeningLogFile.cpp:34`): one extra syscall per log line.

## 6. Security design gaps (missing guard rails rather than exploits)

- **Raw LLM output reaches the terminal.** `Agent::RunThread` writes the
  model's reply straight to the console (`src/components/Agent/Agent.cpp:367`
  → `src/components/Console/AgentConsoleAdapter.cpp:18` →
  `src/components/Console/Console.cpp:50`, `std::cout`).
  - A model steered by prompt injection, e.g. through a file it read, can
    emit ANSI/OSC escape sequences. They can rewrite or hide what is on
    screen, set the window title, and on terminals that honour OSC 52 write
    the user's clipboard.
  - Fix: strip or escape C0/C1 control characters (keeping `\n` and `\t`)
    before agent output reaches the physical console.
- **The default root is writable by agents.** The `haisos --init` template
  declares `FS rootfs PHYSICAL .` (`src/haisos/HaisosFileParser.cpp:565`).
  With no `FS` at all, the root defaults to the haisosfile's own directory.
  Agents get `os_write_file` on it.
  - They can overwrite the haisosfile itself and their own `.md` program.
  - They can plant files the host later acts on: `.git/config` if the
    directory is a repository (`core.fsmonitor`, aliases, `core.hooksPath`),
    or build and IDE configuration (`Makefile`, `package.json` scripts,
    `.vscode/tasks.json`).
  - New files are created 0600 (`src/components/Filesystem/FilesystemUtils.h:33`),
    so a planted hook isn't executable, but configuration-driven execution
    doesn't need the execute bit.
  - Fix: have the template make a dedicated work directory (`SUB`) writable
    and the rest read-only, and document the risk.
- **Subagent fan-out is unbounded.** `agent_start` caps depth at 5
  (`src/tools/agent_start/AgentStartTool.cpp:83`) but not breadth.
  - One LLM response can carry many tool calls, and each subagent costs a
    thread, an HTTP client and LLM spend.
  - `MAX_CONCURRENT_PROCESSES` (`src/components/HaisosOS/HaisosOS.cpp:40`)
    limits processes, not subagents.
  - Fix: a per-agent child limit and a per-OS agent budget.
- **Lua resources.** `luaL_newstate` (`src/components/HaisosOS/LuaProcess.cpp:453`)
  uses the default allocator with no memory cap. A script can exhaust host
  memory (`string.rep`, table growth) and bring down the whole process.
  - The kill hook runs every 1000 VM instructions (line 459). A long C
    function, such as a pathological `string.find` pattern, can't be
    interrupted, so `TriggerStop` and `~LuaProcess` wait for it.
  - Fix: a custom `lua_Alloc` with a budget; consider an instruction or
    wall-clock budget.
- **Unbounded buffers.** `BuiltinContext::Out` keeps the current line in
  `m_pendingLine` until a `\n` arrives
  (`src/components/BuiltinCommands/BuiltinCommand.cpp:34`). `cat /dev/zero`,
  or a large file with no newlines, grows it without limit.
  `InMemoryFileSystem` has no size quota
  (`src/components/Filesystem/InMemoryFileSystem.cpp:122`).
  - Fix: flush partial lines past a size limit; add a per-filesystem quota.

## 7. Haisosfile filesystem-setup quirks

- **A mistyped `ROOT` is silently accepted when `FS` directives exist.**
  `BuildRootFileSystem` (`src/haisos/HaisosFileSystemBuilder.cpp:111-121`)
  falls back to treating `ROOT` as a directory path when it names no declared
  FS. It only logs a warning, which isn't shown by default: console logging
  is off unless `--log-to-console` is given.
  - Reproduced: `FS rootfs PHYSICAL .` + `ROOT rotfs` + `RUN /a.lua` gives
    "Error: Failed to start process: /a.lua" and "no process could be
    started"; nothing points at `ROOT`.
  - The root CLAUDE.md promises that mistakes are reported (line 270), and
    describes the directory fallback as applying only when no `FS` is
    declared (lines 235-239).
  - Fix: when `fsSteps` is non-empty, an unknown `ROOT` name is an error.
- **A `PHYSICAL` root at `/` rejects every path.**
  `PhysicalFileSystem::ResolveWithinRoot`
  (`src/components/Filesystem/PhysicalFileSystem.cpp:35-38`) requires the
  character right after the root prefix to be a separator. With root `/`,
  that character is the first letter of the path, so everything "escapes".
  - Reproduced: `FS host PHYSICAL /` logs "path escapes root (/)" for every
    file.
  - A Windows drive root (`C:\`) is affected the same way.
  - Fix: handle a root that already ends in a separator, or compare with
    `std::filesystem::path` components (`lexically_relative`).
- **`MOUNT` is documented as in place but rebinds the name.** CLAUDE.md:199
  and `IFileSystem::Mount` describe mounting in place, but
  `src/haisos/HaisosFileSystemBuilder.cpp:103` binds the main FS's name to a
  new `ComposedFileSystem`.
  - Filesystems declared earlier on top of it (`RO`, `SUB`, `COMPOSED`) keep
    seeing the original without the mount. For example `FS ro RO rootfs`,
    then `MOUNT rootfs /dev devfs`, then `ROOT ro` gives an OS without
    `/dev`.
  - `src/haisos/HaisosFileParser.h:70` relies on the rebinding ("a MOUNT can
    retarget a name that a later SUB then builds on").
  - Fix: either call `IFileSystem::Mount` on the original filesystem (truly
    in place) or document `MOUNT` as rebinding.
