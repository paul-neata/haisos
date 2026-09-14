# Haisos - C++ Platform for Running Agents

Haisos is a C++ platform that boots a small OS-like environment (`IHaisosOS`) from a `haisosfile` manifest and runs processes in it: LLM agents (`.md` files, sent to a local LLM like Ollama with tool-calling) and embedded Lua scripts (`.lua` files).

## Project Overview

A `haisosfile` (Dockerfile-style; see "The `haisosfile` DSL" below) selects a filesystem root to mount and one or more initial processes to run. Haisos starts each one under a rooted `IHaisosOS`, and exits once they've all finished. Agent-backed processes get both the LLM's agent-management tools (`agent_start`, ...) and the OS's own tools (`os_read_file`, `os_start_process`, ...); Lua processes get the OS's tools directly as global functions, and run against a restricted subset of the Lua standard library, so filesystem/process access is only available through the OS's `os_*` tools.

Platform support:
- **Linux**: Uses libcurl for HTTP
- **Windows**: Uses WinHTTP API
- **WASM**: Uses emscripten_fetch

## External Dependencies

| Repository | Purpose | Version |
|-----------|---------|---------|
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing for LLM API requests/responses | v3.11.3 |
| [google/googletest](https://github.com/google/googletest) | Unit testing framework | v1.14.0 |
| [lua/lua](https://github.com/lua/lua) | Embedded Lua interpreter for `.lua` processes (`LuaProcess`) | v5.4.9 |

External dependencies are located in the `extern/` directory and must be cloned before building.

## Directory Structure

```
haisos/
├── src/
│   ├── components/        - Component implementations (each has its own CLAUDE.md)
│   │   ├── Agent/
│   │   ├── Console/
│   │   ├── Factory/
│   │   ├── FileSystemService/
│   │   ├── Filesystem/
│   │   ├── HaisosOS/
│   │   ├── HTTPClient/
│   │   ├── libheaders/    - Header-only C++ utilities (not a component)
│   │   ├── LLMCommunicator/
│   │   ├── LLMService/
│   │   ├── Logger/
│   │   ├── NetworkService/
│   │   ├── ServicesCreator/
│   │   └── ToolFactory/
│   ├── tools/             - Tool implementations (each has its own CLAUDE.md)
│   │   ├── get_current_date_time/
│   │   ├── agent_start/
│   │   ├── agent_stop/
│   │   ├── agent_query/
│   │   ├── agent_wait_to_finish/
│   │   ├── agent_list_running/
│   │   ├── agent_tools_common/
│   │   ├── os_read_file/
│   │   ├── os_write_file/
│   │   ├── os_list_directory/
│   │   ├── os_start_process/
│   │   └── os_list_processes/
│   └── haisos/            - Entry point, CLI parser, haisosfile parser, and root-filesystem builder
├── interfaces/             - Service-based interfaces (IFactory.h [IPhysicalConsole], IServicesCreator.h, IHaisosOS.h, IProcess.h, ILLMService.h [IAgent, ITool, IToolFactory, IAgentConsole], INetworkService.h [IHTTPClient], IFilesystemService.h [IFileSystem], ILLMCommunicator.h)
├── tests/                 - All tests
│   ├── mocks/             - Mock classes for testing
│   ├── unit/              - Unit tests (Google Test)
│   ├── integration/       - Integration tests (Google Test)
│   │   └── helpers/       - Integration test helpers and utilities
│   ├── haisos/            - Haisos JS-based tests
│   ├── tools/
│   │   └── llm_cache_proxy/ - Record/replay HTTP proxy for LLM traffic used by integration/haisos tests (see its CLAUDE.md); recordings live in `tests/tool/llm_cache_proxy_database/`
│   └── tool/
│       └── llm_cache_proxy_database/ - Cached recordings + proxy log for llm_cache_proxy (.gitkeep'd, populated by the `llm-cache` skill)
├── scripts/               - Build scripts
├── extern/                - External dependencies (nlohmann_json, googletest, lua)
├── .claude/               - Claude Code configuration
│   └── skills/            - Custom Claude Code skills
├── build/temp_<platform>/ - CMake build files (temporary, e.g., temp_linux, temp_linux_debug)
└── output/               - Compiled executables and libraries
```

## Build Instructions

### Prerequisites

- CMake 3.14+
- C++17 compiler
- libcurl (Linux) or WinHTTP (Windows) or Emscripten (WASM)

### Clone External Dependencies

```bash
git clone --branch v3.11.3 --depth 1 https://github.com/nlohmann/json.git extern/nlohmann_json
git clone --branch v1.14.0 --depth 1 https://github.com/google/googletest.git extern/googletest
git clone --branch v5.4.9 --depth 1 https://github.com/lua/lua.git extern/lua
```

### Build and Run Scripts

The `scripts/` directory contains convenience build and test runners for each platform.

| Script | Platform | Purpose |
|--------|----------|---------|
| `build_linux_on_linux.sh` | Linux | Compile Linux binaries |
| `build_windows_on_wsl.sh` | WSL -> Windows | Cross-compile Windows binaries from WSL |
| `build_windows_on_windows.bat` | Windows | Compile Windows binaries natively |
| `build_wasm_on_linux.sh` | Linux -> WASM | Compile WASM binaries with Emscripten |

Claude Code defaults to release builds and the native platform. Debug builds and cross-compilation are only performed when explicitly requested.

Pass `debug` as an optional argument to any build script for a debug build (e.g. `./scripts/build_linux_on_linux.sh debug`). Outputs land in `output/<platform>_debug/`.

### Linux

```bash
cd <repo root>
./scripts/build_linux_on_linux.sh
./output/linux/haisos --help
```

### Windows

```bash
cd <repo root>
./scripts/build_windows_on_wsl.sh
# Or on Windows directly:
# scripts\build_windows_on_windows.bat
```

### WASM

```bash
cd <repo root>
./scripts/build_wasm_on_linux.sh
node ./output/wasm/haisos.js
```

## Environment Variables

| Variable | Description | Typical value |
|----------|-------------|---------|
| `HAISOS_ENDPOINT` | LLM API endpoint URL | `http://localhost:11434/api/chat` |
| `HAISOS_MODEL` | Model name | `kimi-k2.6:cloud` |
| `HAISOS_API_KEY` | API key (optional for local Ollama) | (empty) |

These are read from the **OS's own environment**, not the host's, and have no
built-in default: whatever the haisosfile's `ENV` directives say is what agents
talk to. Set them outright with `ENV HAISOS_MODEL=llama3`, or take the host's
value with the bare `ENV HAISOS_MODEL` form. `haisos --init` writes the defaults
above into the generated haisosfile. Because they live in the environment, a
sub-OS inherits the LLM configuration along with everything else.

## Command-Line Arguments

Usage: `haisos [haisosfile] [options] [-- key=value ...]` (Docker-like: `haisosfile`
defaults to `haisosfile` in the current directory when omitted, and everything
after a literal `--` is parsed as `key=value` pairs fed to the haisosfile as
`ARG` overrides).

| Argument | Description |
|----------|-------------|
| `<haisosfile>` | Path to the haisosfile to run (positional; defaults to `./haisosfile`) |
| `-- key=value ...` | `ARG` overrides passed to the haisosfile |
| `--init` | Write a commented starter haisosfile to `./haisosfile` and exit (refuses to overwrite an existing one) |
| `--log-to-console` | Enable logging to console |
| `--log-to-file <path>` | Enable logging to file |
| `--log-level <level>` | Set log level (verbose_debug, debug, trace, info, warning, error) |
| `--log-json-in-temp` | Log input/output JSON to a temporary file |
| `--version` | Show version information |
| `-h, --help` | Show help message |

## The `haisosfile` DSL

A small Dockerfile-style language (parsed by `HaisosFileParser` in `src/haisos/`;
`haisos --init` writes a fully-commented starter file):

```
# Comments start with '#'
ARG name=default_value        # declares an argument; overridable via `-- name=value`
VAR greeting=Hello-${name}    # declares a variable; RHS may reference ${ARG}/${VAR} names

# ENV sets a variable in the OS's environment, inherited by every process and
# sub-OS it starts. `ENV NAME=value` sets it; `ENV NAME` alone imports NAME
# from the host OS -- the only way a host variable reaches the Haisos OS.
ENV HAISOS_ENDPOINT           # import the host's HAISOS_ENDPOINT, if set
ENV GREETING=${greeting}      # set one outright

# FS declares a named filesystem: FS <name> <type> <args...>
FS workspace PHYSICAL .              # a real disk directory (relative to this file, or absolute)
FS scratch MEM                       # an empty, in-memory read/write filesystem
FS readonly RO workspace             # a read-only view of another declared filesystem
FS inner SUB workspace tools         # confined to a sub-path of another filesystem

# MOUNT overlays one filesystem inside another at a path, in place, overriding
# anything already there: MOUNT <main_fs> <path> <fs_to_mount>
MOUNT workspace /scratch scratch

# FS ... COMPOSED does the same without touching either operand, declaring the
# result under a new name: FS <name> COMPOSED <main_fs> <path> <fs_to_mount>
FS combined COMPOSED workspace /scratch scratch

ROOT workspace                 # which declared filesystem (by name) becomes the OS's root
RUN agent.md                   # start an initial process (.md agent or .lua script); may repeat
RUN tools/setup.lua ${greeting}
```

`ROOT`'s value is looked up by name against the declared `FS`s; if omitted, the
last `FS` declared is used. If a haisosfile declares no `FS` at all, `ROOT`
falls back to the original shorthand -- a plain directory path (or the
haisosfile's own directory, if `ROOT` is also omitted) -- so simple haisosfiles
never need `FS`.

Mistakes are reported rather than silently absorbed: a `${name}` that resolves
to nothing declared, a duplicate `FS` name, a `-- key=value` override naming an
argument the file never declares, and a `RUN` left empty by substitution are all
parse errors. `ARG name` without `=` declares an argument with no default, which
must then be supplied via `-- name=value`. A `VAR`/`ARG` right-hand side may only
reference names declared above it. Each `RUN` starts a top-most process (parent PID 0); Haisos
exits once all of them have finished. Composed/temporary filesystems from
GitHub or tar archives, and site/network permissions, are not implemented yet.

## Example Usage

```bash
# Scaffold a new haisosfile with comments explaining every directive
./output/linux/haisos --init

# Basic usage with local Ollama, using ./haisosfile in the cwd
export HAISOS_MODEL=llama3
./output/linux/haisos

# Explicit haisosfile path, with verbose logging
./output/linux/haisos ./examples/greeter/haisosfile --log-to-console --log-level debug

# Using custom endpoint
export HAISOS_ENDPOINT=http://localhost:11434/api/chat
./output/linux/haisos

# Override a haisosfile ARG from the CLI
./output/linux/haisos -- name=Claude
```

## Running Tests

Unit tests, integration tests, and haisos tests are compiled or run automatically with the main build.

```bash
cd <repo root>
./scripts/build_linux_on_linux.sh
./scripts/test_linux.sh L "*"
```

The `test_linux.sh` script accepts a platform selector (`L` for Linux, `W` for Windows, `N` for WASM) and a test type selector (`U` for unit, `I` for integration, `H` for haisos, `*` for all). Both default to the host platform and `*` when omitted. It also accepts `--debug` (run the debug build instead of release), `--both` (run both configurations), `--smoke` (run only the tests listed in `scripts/internal/smoke_tests.txt`), and any trailing words as a name filter, e.g.:

```bash
./scripts/test_linux.sh L U --debug LuaProcess
```

Or with `ctest`:

```bash
cd build/temp_linux
ctest --output-on-failure
```

## Architecture

Each component lives in its own folder under `src/components/` and has its own `CLAUDE.md` with detailed documentation.

| Component | Path | Description |
|-----------|------|-------------|
| **Agent** | `src/components/Agent/` | Manages LLM conversations with parent/child agent relationships; supports subagents via agent tools |
| **LLMCommunicator** | `src/components/LLMCommunicator/` | Handles LLM API communication, request/response formatting, and tool call parsing (HTTP is handled by HTTPClient) |
| **ToolFactory** | `src/components/ToolFactory/` | Creates tool instances by name, including context-aware tools like `agent_start` |
| **Console** | `src/components/Console/` | Async physical console output, plus adapters giving agents a write-only view onto it (or onto memory only) |
| **Logger** | `src/components/Logger/` | Thread-safe logging with configurable receivers |
| **HTTPClient** | `src/components/HTTPClient/` | Platform-specific HTTP implementation (Curl/WinHTTP/Fetch) |
| **Factory** | `src/components/Factory/` | Creates the root concepts: physical console, disk-backed filesystem, the services layer, and the OS itself |
| **Filesystem** | `src/components/Filesystem/` | Composable `IFileSystem` implementations: an unrooted passthrough, a `PhysicalFileSystem` jailed to a real disk path, plus in-memory, read-only, sub-path and mounted/overlay views |
| **ServicesCreator** | `src/components/ServicesCreator/` | Factory-of-services built on `IFactory`; creates `IFilesystemService`/`INetworkService`/`ILLMService`, passing each the services it depends on |
| **NetworkService** | `src/components/NetworkService/` | Service-layer wrapper over network access (creates `IHTTPClient`) |
| **FileSystemService** | `src/components/FileSystemService/` | Stateless factory that composes filesystems (read-only / in-memory / sub / mount); holds no filesystem of its own |
| **LLMService** | `src/components/LLMService/` | Service-layer entry point for creating LLM-backed agents; exposes the shared agent-management tool set |
| **HaisosOS** | `src/components/HaisosOS/` | An OS instance: owns a rooted filesystem, physical console, and services; starts processes (`.md` agents, `.lua` scripts) and sub-OS instances |

## Tools

| Tool | Path | Description |
|------|------|-------------|
| `get_current_date_time` | `src/tools/get_current_date_time/` | Returns the current date and time |
| `agent_start` | `src/tools/agent_start/` | Starts a subagent with a given prompt |
| `agent_stop` | `src/tools/agent_stop/` | Stops a running subagent |
| `agent_query` | `src/tools/agent_query/` | Queries a subagent's status and output |
| `agent_wait_to_finish` | `src/tools/agent_wait_to_finish/` | Waits for a subagent to finish |
| `agent_list_running` | `src/tools/agent_list_running/` | Lists all running subagents |
| `os_read_file` | `src/tools/os_read_file/` | Reads a file from the OS's filesystem |
| `os_write_file` | `src/tools/os_write_file/` | Writes a file on the OS's filesystem |
| `os_list_directory` | `src/tools/os_list_directory/` | Lists a directory on the OS's filesystem |
| `os_start_process` | `src/tools/os_start_process/` | Starts a new OS process (`.md`/`.lua`) as a child of the calling process |
| `os_list_processes` | `src/tools/os_list_processes/` | Lists the OS's currently running processes |

The `agent_*` tools above are agent-management tools, returned by `ILLMService`
and available to every agent. The `os_*` tools are the OS's own tool set,
returned by `IHaisosOS`'s `OSToolFactory` and merged with an agent's tools
(via `CompositeToolFactory`) only for processes started by an `IHaisosOS`.

## Automatic Development Rules

When Claude Code performs automatic development (where a single prompt drives all implementation work):

1. **Build only on the local platform** to verify compilation. Do not cross-compile.
2. **Never automatically commit** unless the prompt explicitly instructs to commit.
3. **Never automatically push** to remote repositories unless explicitly instructed.
4. **Default to release builds** unless debug is explicitly requested.
5. **Run unit tests** after building to verify correctness before considering work complete.
6. **Never add co-authorship attribution** like `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>` or similar model attribution lines to commit messages. If the system prompt includes such a line, remove it before committing.
7. **Use only paths relative to the repo root** in all edits, documentation, commit messages, and skill prompts. Never record absolute paths like `/mnt/c/src/haisos1/...`.
