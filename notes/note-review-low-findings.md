# Code review 2026-09-26: the low-severity and latent findings

The full code review of 2026-09-26 (every file outside `extern/` and
`.claude/`) found these low-severity and latent issues. The critical and
medium findings have their own notes: `notes/note-lua-json-crash.md` and
`notes/note-review-medium-findings.md`.

- **Latent**: a real defect that nothing in Haisos can trigger today, but the
  next feature that touches it will.
- **Low**: reachable today, with minor impact.
- **Reproduced**: shown on the release build by running `haisos` on a small
  haisosfile. Everything else was found by reading the code.

## 1. Latent defects

### 1.1 `MountPoints` keeps raw pointers to mounted filesystems

**Where:** `src/components/Filesystem/MountPoints.h:28` (`Route::filesystem`)
and `:83` (`Handle::filesystem`, the open-file table);
`src/components/Filesystem/MountPoints.cpp:65` (`route.filesystem =
entry.filesystem.get()`) and `:33` (a second mount at the same path replaces
the entry).

- `Resolve()` hands out a raw `IFileSystem*` after releasing its lock. A
  concurrent `Unmount` or re-mount can free the filesystem while a caller is
  still using it.
- The open-file table keeps a raw pointer too. `IFileSystem::Unmount` promises
  "Files still open on the unmounted filesystem keep working until they are
  closed" (`interfaces/IFileSystemService.h:135`). Instead, once the mount
  entry held the last reference, the next `ReadFile`/`WriteFile`/`CloseFile`
  on such a descriptor is a use-after-free
  (`src/components/Filesystem/MountableFileSystem.cpp:85/109/126`).
- **Why latent:** nothing in `src/` calls `Unmount`, and in-place `Mount` is
  only used by `ComposedFileSystem`'s constructor.
- **Fix:** store `std::shared_ptr<IFileSystem>` in both `Route` and `Handle`.

### 1.2 File descriptors are not scoped to the process that opened them

**Where:** `IFileIO`/`IFileSystem` descriptor operations take any `int`.

- `ProcessFileIO` passes descriptors straight through
  (`src/components/HaisosOS/ProcessFileIO.cpp:70` and around).
- So do the composed filesystems: `ReadOnlyFileSystem.cpp:29`,
  `SubFileSystem.cpp:35`, `ComposedFileSystem`.
- At the bottom they reach the host's `::close`, `::read` and `::write`
  (`src/components/Filesystem/linux/PosixFilesystem.cpp:35/39/43`).

Anyone holding an `IFileIO` could therefore read, write or close any
descriptor of the haisos process:
- stdin, stdout and stderr;
- the `-l`/`-L` log files;
- the curl sockets;
- files other processes opened.

That bypasses every path check (jail, sub-path, read-only). Closing someone
else's descriptor also lets the number be reused, so the owner writes into the
wrong file.

- **Why latent:** no runtime lets untrusted code name a descriptor. The `os_*`
  tools and the Lua globals take paths, and builtins use only descriptors they
  opened themselves.
- **Fix:** give each process its own descriptor table in `ProcessFileIO`,
  mapping process-local numbers to (filesystem, descriptor) and rejecting
  unknown ones. Do this before any fd-level API (stdio, a Lua `io`
  replacement) is exposed.

### 1.3 `InMemoryFileSystem` open handles follow the path, not the file

**Where:** `src/components/Filesystem/InMemoryFileSystem.cpp:75`: a handle
stores the normalized path, and every read or write looks the path up again.

- After `RemoveFile` plus re-creating the same path, a still-open old handle
  reads and writes the new file. The comment at line 170 claims `unlink()`
  semantics, where the old file would live on until closed.
- The access mode isn't recorded: a handle opened read-only can write, and a
  write-only one can read.
- `O_APPEND` positions at the end only once, at open (line 74), not before
  every write.
- **Why latent:** processes can't delete files (no delete tool or builtin;
  the `DELETE` directive runs before any process), and the tools open with
  the right flags.
- **Fix:** keep a node identity in the handle (e.g. `shared_ptr<Node>`),
  store the open flags and enforce them, and seek to the end on each append
  write.

### 1.4 `IAgent::Send` can block forever

**Where:** `src/components/Agent/Agent.cpp:98` →
`src/components/libheaders/SynchronizedQueueEx.h:39-47`.

`Send` waits until the agent's thread pops the command (`future.wait()`,
line 46). The thread may already be gone:
- a one-shot agent's thread exits after its first command without closing its
  queue (`Agent.cpp`, `if (!m_interactive) break;`);
- a stopped agent's queue is closed, but `SynchronizedQueue::Post`
  (`SynchronizedQueue.h:11`) still accepts items that nobody will ever pop.

Either way `Send` never returns, and `Post` silently loses the command.
- **Why latent:** nothing calls `Send`.
- **Fix:** have `Post` report failure once the queue is closed or the agent
  has finished, make `Send` return false in that case, or remove `Send`.

## 2. Lua bridge (`src/components/HaisosOS/LuaProcess.cpp`)

### 2.1 Strings are cut at the first NUL byte

- Lua → JSON: values use `std::string(lua_tostring(L, index))` (line 72) and
  keys `lua_tostring(L, -2)` (line 84).
- JSON → Lua: `lua_pushstring(L, value.get<std::string>().c_str())` (line 34)
  and `lua_setfield(L, -2, it.key().c_str())` (line 49).
- Tool results use `lua_pushstring(L, result.content.c_str())` (line 379), and
  script arguments `lua_pushstring(L, m_args[i].c_str())` (line 436).

A binary file read with `os_read_file` reaches the script cut at its first
zero byte, and `os_write_file` content containing `\0` is written short. Both
are silent data corruption.

**Fix:** use `lua_tolstring` / `lua_pushlstring` with explicit lengths.
(Listed as a "while there" item in `notes/note-lua-json-crash.md`.)

### 2.2 Lua errors can jump over C++ destructors

Lua is built as C, so `lua_error` unwinds with `longjmp`, which skips C++
destructors; per the standard this is undefined behaviour.
- `LuaPrintTrampoline` (line 394) calls `luaL_tolstring` (line 407), which
  runs a `__tostring` metamethod unprotected. If it raises, or returns a
  non-string, Lua jumps out over the live `std::string line`.
- In `LuaToolTrampoline` (line 341), any `lua_push*` that raises a memory
  error jumps over `ToolResult result` (line 368), `nlohmann::json parsed` and
  the `shared_ptr<ITool>`.
- The `try`/`catch` in both trampolines doesn't help: a `longjmp` is not an
  exception. They only guard the other direction, C++ exceptions into Lua
  frames.
- Any script reaches it:
  `print(setmetatable({}, {__tostring = function() error("x") end}))`.

In practice this is a leak, formally undefined behaviour.

**Fix:** compile Lua as C++ so errors are exceptions (`LUAI_THROW`), or do
every raising Lua call, e.g. `tostring` via `lua_pcall`, before creating
non-trivial C++ locals.

## 3. Logging, threads and test helpers

### 3.1 The `--log-json-in-temp` receiver can dangle

**Where:** `src/haisos/main.cpp:221` registers
`[&tempJsonLog](const LogMessage& msg)`. It captures a local of `main()` by
reference (declared at `:197`, closed at `:377`) and is never unregistered.

After `main` returns, objects handed to the `DestructionThread` are destroyed
during static destruction. An agent still finishing an HTTP call there logs
`[JSON_RESPONSE]`, and the receiver dereferences a dead stack slot.

**Fix:** capture a `shared_ptr`, as the other receivers in `main.cpp` do, and
unregister it before returning.

### 3.2 The `--log-json-in-temp` file: location and permissions

**Where:** `src/haisos/main.cpp:204` builds `"/tmp/haisos_<ms>_<random>.jsonlog"`.

- Windows has no `/tmp`, so the open fails with only a warning.
- The file is created with default permissions (typically 0644) in a shared
  directory. It holds every prompt and every file an agent read, so other
  local users can read it.
- **Fix:** use `std::filesystem::temp_directory_path()`, and create the file
  exclusively with mode 0600.

### 3.3 `IntegrationTestLogCapture` leaves its receivers registered

**Where:** `tests/integration/helpers/IntegrationTestLogCapture.h`.

- The constructor calls `LogClearMessageReceivers()` (line 41), removing
  anyone else's receiver.
- It registers two receivers that capture `this` (lines 42, 53).
- The destructor (line 68) never unregisters them. Anything logged after the
  object is gone, e.g. by agents destroyed later, touches a destroyed object.
- **Fix:** keep the tokens and unregister them in the destructor; don't clear
  other receivers.

### 3.4 The agent-name generator is shared across threads

**Where:** `src/tools/agent_tools_common/AgentToolsCommon.h:21`.
`GenerateAgentName` uses a function-local `static std::mt19937` with no lock.
Every agent thread calling `agent_start` uses it, so two concurrent calls are
a data race (undefined behaviour).

**Fix:** a `thread_local` generator, or a mutex.

### 3.5 Logger details (`src/components/Logger/`)

- **Receivers can still run after unregistering.** `LogImpl` copies the
  receiver list under the lock and calls it outside (`Logger.cpp:96-103`).
  After `LogUnregisterMessageReceiver` returns, another thread may still be
  running the old receiver, so what it captures can't be destroyed
  immediately.
  - Copying a vector of `std::function` per log line also costs time.
  - Fix: document it, or use shared ownership plus a generation check.
- **Format strings aren't checked by the compiler.** `FormatLogMessage`
  (`Logger.h:32`) is printf-style but lacks
  `__attribute__((format(printf, 1, 2)))`.
  - The review compiled every source with that attribute added: all current
    calls are correct. Adding it keeps them that way (GCC/Clang only).
- **Encoding errors throw.** If `vsnprintf` returns a negative value,
  `std::string msg(len, '\0')` (`Logger.h:39`) is built with a huge size and
  throws.
- **A missing include.** `Logger.cpp:141` uses `std::remove_if` without
  including `<algorithm>`.

## 4. HTTP client (`src/components/HTTPClient/linux/CurlHTTPClient.cpp`)

### 4.1 `curl_global_init` is never called

`curl_easy_init` (line 14) initialises libcurl implicitly on first use. Before
libcurl 7.84 that isn't thread-safe, and the first HTTP clients are created
concurrently on agent threads (subagents).

**Fix:** call `curl_global_init(CURL_GLOBAL_DEFAULT)` once in `main`, and
`curl_global_cleanup()` at exit.

### 4.2 Redirects are followed on the LLM POST

`CURLOPT_FOLLOWLOCATION` (line 59) is on for every request, including POSTs
that carry `Authorization: Bearer <key>`. A chat endpoint has no reason to
redirect.
- libcurl 7.58 and later drop a custom `Authorization` header on a redirect to
  another host, but older versions leak it.
- A 301/302 also turns the POST into a GET.
- **Fix:** disable redirects for LLM requests.

### 4.3 120 s is the total time allowed for a non-streamed generation

`CURLOPT_TIMEOUT` is 120 s (line 63), and requests are sent with
`"stream": false` (`src/components/LLMCommunicator/LLMCommunicator.cpp:45`).
A long answer from a slow local model, or a thinking model, can exceed two
minutes. The request then fails, and the agent's round ends with an error.

WinHTTP instead sets a 120 s timeout per receive
(`windows/WinHTTPClient.cpp:96`), so the platforms differ.

**Fix:** make the timeout configurable (e.g. from the environment), or stream
with an idle timeout.

### 4.4 Smaller curl items

- `CURLOPT_PROTOCOLS` / `CURLOPT_REDIR_PROTOCOLS` (lines 60-61) are deprecated
  since libcurl 7.85; they are the only warnings in the build. Use the `_STR`
  variants when available.
- `WriteCallback` (lines 23-27) has no response-size cap: a misbehaving
  endpoint can stream gigabytes into memory.
- The full URL is logged at Debug (line 44). A key passed in a query string
  would end up in the log.

## 5. LLM protocol details

- **API and HTTP errors are recorded as the model's words.** They come back as
  `role: "assistant"` with content `"Error: ..."`
  (`src/components/LLMCommunicator/LLMCommunicator.cpp:108` and `:257`), and
  `Agent::RunThread` appends them to the history. Later turns show the model
  itself "saying" the error.
- **One null field discards the whole response.** `role` (line 116),
  `thinking` (line 128) and `done_reason` (line 179) are implicit json→string
  conversions that throw on `null`. The catch then replaces the entire
  response, content and tool calls included, with "Error: Failed to parse LLM
  response".
- **The Anthropic-format branch is effectively dead code** (line 153): the
  request is always in Ollama format, and that branch never reads text from a
  `content` array.
- **Hitting `MAX_LLM_ROUNDS` is silent.** At 20 rounds
  (`src/components/Agent/Agent.cpp:347/351`) the loop only logs a warning.
  The user isn't told, and the history ends with tool results and no
  assistant reply.
- **`is_error` never reaches the model.** Tool results carry it
  (`interfaces/ILLMCommunicator.h:18`, set at `Agent.cpp:390`), but
  `BuildRequestJson` never sends it, so the model sees only the content.

## 6. Filesystem and builtin details

- **`ls -R` follows symbolic links to directories** (reproduced).
  `ListDirectory` recurses into anything whose `Stat` says directory
  (`src/components/BuiltinCommands/commands/Ls.cpp:570-581`), and `Stat`
  follows links. GNU `ls -R` does not.
  - A loop such as `sub/up -> ..` printed 247 lines of repeated listings,
    about 40 levels, until the kernel's symlink limit (`ELOOP`) stopped it.
  - Fix: report links as links, and don't descend into them.
- **`ReadWholeFile` silently truncates at 10 MB**, plus up to 64 KB
  (`src/components/Filesystem/FilesystemUtils.h:87-111`, cap at `:93`), and
  still reports success.
  - `os_read_file` documents the cap.
  - Program loading doesn't: a `.md` or `.lua` program over 10 MB
    (`src/components/HaisosOS/HaisosOS.cpp:179/308`) is cut, so a Lua script
    fails with a syntax error.
  - `main.cpp` guards the haisosfile itself by rejecting it at the cap.
  - Fix: report truncation instead of succeeding.
- **A `PhysicalFileSystem` can remove its own root directory** (reproduced).
  `RemoveDirectory("/")` resolves to the root and calls `rmdir` on it.
  `InMemoryFileSystem` refuses that (`InMemoryFileSystem.cpp:149`).
  - Reached through a mount point: `FS data PHYSICAL ./data`,
    `MOUNT rootfs /data data`, `DELETE /data` emptied the host directory and
    then removed `./data` itself.
  - Fix: refuse to remove `"/"` in `PhysicalFileSystem`.
- **`mkdir` messages differ from GNU mkdir.** Every `CreateDirectory` failure
  is reported as "Permission denied"
  (`src/components/BuiltinCommands/commands/Mkdir.cpp:95`), whatever the
  cause.
  - `mkdir -p a/b` where `a` is a file says "cannot create directory 'a':
    File exists" (line 81). GNU says "cannot create directory 'a/b': Not a
    directory".
  - This breaks the builtins' same-output-as-the-real-command rule.
- **A builtin's output never reaches the agent that started it.**
  `os_start_process` returns only a pid
  (`src/tools/os_start_process/OSStartProcessTool.cpp:8` advertises builtins
  such as `/bin/ls`).
  - The output goes to the physical console, each line tagged
    `[<name>_<pid>] ` (`HaisosOS.cpp:283`).
  - Either capture the output for the caller, or say in the tool description
    that it isn't returned.
- **`ls --time-style=+FORMAT` passes a user-chosen format to `strftime`**
  (`Ls.cpp:182-211`, call at `:209`).
  - With MSVC, an invalid conversion (e.g. `+%Q`, or a trailing `%`) invokes
    the invalid-parameter handler, which by default terminates the whole
    process. An agent can pass such arguments through `os_start_process`, so
    this is a crash on Windows.
  - Output longer than the 256-byte buffer comes back as an empty column.
  - Fix: validate the conversions, and grow the buffer.
- **`Console` can't restart, and drops empty messages.** `Start()` after
  `Stop()` starts a thread that exits at once, because the queue stays closed
  (`src/components/Console/Console.cpp:30-41`). A message that is empty is
  dropped (`:49`), so `Write("")` prints nothing.
- **`CompositeToolFactory` sends duplicate tools.** It concatenates both tool
  lists without removing duplicate names
  (`src/components/ToolFactory/CompositeToolFactory.cpp:39-49`), so a name
  present in both would reach the LLM twice. Its header comment
  (`CompositeToolFactory.h:8`) calls the first factory the OS's tool set
  "shared across every process", but `OSToolFactory` is now built per
  process.
- **`InMemoryFileSystem` scans every node for each directory operation.**
  Directory listing (`InMemoryFileSystem.cpp:178`), `rmdir`, and a
  directory's link count each walk all nodes: O(total nodes) per call, so
  `ls -R` is quadratic. Its `ParentOf`/`LastSegment` (line 12) duplicate
  `VirtualParentOf`/`VirtualLastSegment` in `VirtualPath.h`.
- **Descriptor numbers are never reused.** The synthetic-descriptor counter
  (`std::atomic<int>`, `MountPoints.cpp:74`) and the plain `int m_nextFd` of
  `InMemoryFileSystem` (`.h:68`) and `DeviceFileSystem` only count up. After
  about 2^31 opens they turn negative, which is undefined behaviour for the
  plain `int`s. Theoretical today.

## 7. OS lifecycle, `main` and the haisosfile

- **The process cap can be overshot.** `StartProcess` checks
  `MAX_CONCURRENT_PROCESSES` under the lock
  (`src/components/HaisosOS/HaisosOS.cpp:364`), but the new process is
  inserted later, in a separate critical section, so concurrent starts can
  exceed the cap slightly.
  - Finished processes, and the agent histories they hold, stay in
    `m_processes` until the next `StartProcess` cleans them up.
- **A null agent is dereferenced.** `agent_start` doesn't check what
  `CreateAgent` returns (`src/tools/agent_start/AgentStartTool.cpp:32-41`,
  `agent->Post` at line 41). `LLMService::CreateAgent` returns null without a
  network service, which can't happen through the normal wiring.
- **haisos exits 0 when some `RUN` directives failed to start**, as long as
  one started (`src/haisos/main.cpp:338-346`); failures show only on stderr.
- **A relative haisosfile path leaving the current directory is rejected.**
  `haisos ../x/haisosfile` fails, while the absolute path to the same file
  works. `ReadFileWithinCwd` (`main.cpp:60-87`) jails relative paths to the
  current directory.
- **Output during OS teardown is dropped.** `main` stops the console
  (`main.cpp:374`) before the OS is destroyed at `return`. Anything written
  during teardown is lost, e.g. by a process that outlived the 24 h wait.
- **No quoting in the haisosfile.** Tokens are split on whitespace
  (`SplitWhitespace`, `src/haisos/HaisosFileParser.cpp:80`), so `RUN`
  arguments and host paths can't contain spaces. A Windows path such as
  `C:\Users\John Doe\...` in `COPY`/`OUTCOPY`/`FS PHYSICAL` is impossible.
  - Declaring the same `ARG` twice silently keeps the later one (line 264).
- **File and directory modes are inconsistent.** Directories created by
  haisosfile directives are 0700
  (`src/haisos/HaisosFileOperations.cpp:17`), while the `mkdir` builtin uses
  0777 & umask (`Mkdir.cpp:12`). Files are always 0600
  (`FilesystemUtils.h:33`), including what `OUTCOPY` writes on the host.

## 8. CI, scripts and build hygiene

- **Packages can ship without binaries.** `scripts/pack.sh:42-43` copies them
  with `2>/dev/null || true`, so a CI package with no binaries succeeds
  silently.
- **`.github/scripts/run-tests-windows.bat` has broken control flow.**
  - Its `goto :proxy_ready` (line 26) jumps to a label (line 30) inside a
    parenthesised `if` block; cmd.exe doesn't support that, so the rest of
    the block, including the `else` branch, runs wrongly.
  - This only matters once recordings exist.
  - The proxy it starts is never stopped (line 39).
- **`scripts/internal/test.js` never escalates to SIGKILL.** After SIGTERM it
  checks `proc.killed` (line 178), which is true as soon as the signal was
  sent, not when the process died.
  - Its temporary output directories (line 291) are never removed.
  - For `H` tests, killing `node` leaves the `haisos` it spawned running.
- **The cache proxy listens on every interface.**
  `tests/tools/llm_cache_proxy/src/index.js:155` calls `server.listen(port)`,
  which exposes recorded prompts to the network. Bind to `127.0.0.1`.
- **No compiler warnings are enabled.** `CMakeLists.txt` sets no
  `-Wall -Wextra` (or `/W4`). The only warnings in a build are curl's
  deprecations.
- **Missing includes.** `interfaces/ILLMService.h` and
  `interfaces/ILLMCommunicator.h` use `std::tuple` without `<tuple>`;
  `Logger.cpp` uses `std::remove_if` without `<algorithm>`. They build only
  through transitive includes.

## 9. Documentation drift

- **Root `CLAUDE.md:387`** says `OSToolFactory` and the `os_*` tools hold an
  `IHaisosOS&`. They hold a `std::shared_ptr<CurrentProcessHandle>`, which
  the Security section of the same file describes correctly.
- **`--init` writes a different model than documented.** The root `CLAUDE.md`
  gives `kimi-k2.6:cloud` as the typical `HAISOS_MODEL` (line 142) and says
  `--init` writes "the defaults above" (line 149). The template writes
  `ENV HAISOS_MODEL=llama3` (`src/haisos/HaisosFileParser.cpp:558`).
- **`interfaces/IProcess.h:21-23`** says a top-most process has
  `GetParentPid()` 0. `HaisosOS` gives every process it starts the OS's own
  pid as parent, as `src/components/HaisosOS/CLAUDE.md:11` says.
- **The agent-name format is described wrongly.** The `agent_start` and
  `agent_list_running` tool descriptions tell the LLM names "may contain
  underscores and spaces" (`AgentStartTool.cpp:8`,
  `AgentListRunningTool.cpp:7`). `GenerateAgentName` produces 8 characters
  of `[a-z0-9]`.
- **The `llm-cache` skill** (`.claude/skills/llm-cache/SKILL.md:64-65`) uses
  `./scripts/run_tests_linux.sh`, which doesn't exist. The runner is
  `scripts/test_linux.sh`.
- **`tests/haisos/agent_start.haisostest/agent_start.haisostest.js`** asks the
  agent for a `wait_to_finish` argument that doesn't exist (line 9). Its
  comment says the agent "falls back to the defaults" (line 14), but there
  are no defaults any more.
- **"View" is used for composed filesystems.** The project reserves "view"
  for a future copy-on-write filesystem: composed filesystems delegate each
  call rather than project a snapshot. The word still appears here:
  - `interfaces/IFileSystemService.h:192-193` ("Every method returns a new,
    independent view", the very sentence already rejected once) and `:188`;
  - `src/components/Filesystem/MountableFileSystem.h:59`,
    `MountableFileSystem.cpp:197`, `MountPoints.h:42`;
  - `src/components/Filesystem/CLAUDE.md:27/65/68`;
  - the root `CLAUDE.md:196` ("a read-only view of another declared
    filesystem") and `:420` ("mounted/overlay views").

  Say "composes", "wraps" or "routes to" instead. The console and process
  uses of "view" are unrelated and fine.
