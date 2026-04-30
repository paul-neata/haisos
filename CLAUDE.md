# Haisos - C++ Platform for Running Agents

Haisos is a C++ platform for running agents that processes markdown files through a local LLM (like Ollama) with tool calling capabilities.

## Project Overview

Haisos reads markdown files, sends their content to a local LLM, and handles tool calls for actions like getting the current date/time.

Platform support:
- **Linux**: Uses libcurl for HTTP
- **Windows**: Uses WinHTTP API
- **WASM**: Uses emscripten_fetch

## External Dependencies

| Repository | Purpose | Version |
|-----------|---------|---------|
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing for LLM API requests/responses | v3.11.3 |
| [google/googletest](https://github.com/google/googletest) | Unit testing framework | v1.14.0 |

External dependencies are located in the `extern/` directory and must be cloned before building.

## Directory Structure

```
haisos/
├── src/
│   ├── components/        - Component implementations (each has its own CLAUDE.md)
│   │   ├── Agent/
│   │   ├── Console/
│   │   ├── Factory/
│   │   ├── HaisosEngine/
│   │   ├── HTTPClient/
│   │   ├── libheaders/    - Header-only C++ utilities (not a component)
│   │   ├── LLMCommunicator/
│   │   ├── Logger/
│   │   └── ToolFactory/
│   ├── tools/             - Tool implementations (each has its own CLAUDE.md)
│   │   ├── get_current_date_time/
│   │   ├── agent_start/
│   │   ├── agent_stop/
│   │   ├── agent_query/
│   │   ├── agent_wait_to_finish/
│   │   ├── agent_list_running/
│   │   └── agent_tools_common/
│   └── haisos/            - Entry point and CLI parser
├── interfaces/             - Component interfaces (IAgent.h, IConsole.h, IFactory.h, IHTTPClient.h, ILLMCommunicator.h, IHaisosEngine.h, ITool.h, IToolFactory.h, SystemCallbacks.h)
├── tests/                 - All tests
│   ├── mocks/             - Mock classes for testing
│   ├── unit/              - Unit tests (Google Test)
│   ├── integration/       - Integration tests (Google Test)
│   │   └── helpers/       - Integration test helpers and utilities
│   └── haisos/            - Haisos JS-based tests
├── scripts/               - Build scripts
├── extern/                - External dependencies (nlohmann_json, googletest)
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
cd /mnt/c/src/haisos
./scripts/build_linux_on_linux.sh
./output/linux/haisos --help
```

### Windows

```bash
cd /mnt/c/src/haisos
./scripts/build_windows_on_wsl.sh
# Or on Windows directly:
# scripts\build_windows_on_windows.bat
```

### WASM

```bash
cd /mnt/c/src/haisos
./scripts/build_wasm_on_linux.sh
node ./output/wasm/haisos.js
```

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `HAISOS_ENDPOINT` | LLM API endpoint URL | `http://localhost:11434/api/chat` |
| `HAISOS_MODEL` | Model name | `kimi-k2.6:cloud` |
| `HAISOS_API_KEY` | API key (optional for local Ollama) | (empty) |

## Command-Line Arguments

| Argument | Description |
|----------|-------------|
| `-f, --file <path>` | Specify the markdown file to process |
| `-p, --prompt <string>` | Provide the user prompt directly as a string |
| `--system-prompt <string>` | Set a system prompt text |
| `--system-prompt-file <path>` | Read system prompt from a file |
| `--take-stdin` | Read the entire user prompt from stdin until EOF |
| `--log-to-console` | Enable logging to console |
| `--log-to-file <path>` | Enable logging to file |
| `--log-level <level>` | Set log level (verbose_debug, debug, trace, info, warning, error) |
| `--log-json-in-temp` | Log input/output JSON to a temporary file |
| `--version` | Show version information |
| `-h, --help` | Show help message |
| `<markdown_file>` | Path to markdown file to process (positional) |

## Example Usage

```bash
# Basic usage with local Ollama
export HAISOS_MODEL=llama3
./output/linux/haisos prompt.md

# With verbose logging
./output/linux/haisos --log-to-console --log-level debug prompt.md

# Using custom endpoint
export HAISOS_ENDPOINT=http://localhost:11434/api/chat
./output/linux/haisos prompt.md

# Provide prompt directly without a file
./output/linux/haisos --prompt "What is 2+2?"

# Pipe input via stdin
echo "What is the capital of France?" | ./output/linux/haisos --take-stdin
```

## Running Tests

Unit tests, integration tests, and haisos tests are compiled or run automatically with the main build.

```bash
cd /mnt/c/src/haisos
./scripts/build_linux_on_linux.sh
./scripts/test_linux.sh L "*"
```

The `test_linux.sh` script accepts a platform selector (`L` for Linux, `W` for Windows, `N` for WASM) and a test type selector (`U` for unit, `I` for integration, `H` for haisos, `*` for all).

Or with `ctest`:

```bash
cd build/temp_linux
ctest --output-on-failure
```

## Architecture

Each component lives in its own folder under `src/components/` and has its own `CLAUDE.md` with detailed documentation.

| Component | Path | Description |
|-----------|------|-------------|
| **HaisosEngine** | `src/components/HaisosEngine/` | Main coordinator, reads markdown files and orchestrates LLM calls |
| **Agent** | `src/components/Agent/` | Manages LLM conversations with parent/child agent relationships; supports subagents via agent tools |
| **LLMCommunicator** | `src/components/LLMCommunicator/` | Handles LLM API communication, request/response formatting, and tool call parsing (HTTP is handled by HTTPClient) |
| **ToolFactory** | `src/components/ToolFactory/` | Creates tool instances by name, including context-aware tools like `agent_start` |
| **Console** | `src/components/Console/` | Async message queue for output |
| **Logger** | `src/components/Logger/` | Thread-safe logging with configurable receivers |
| **HTTPClient** | `src/components/HTTPClient/` | Platform-specific HTTP implementation (Curl/WinHTTP/Fetch) |
| **Factory** | `src/components/Factory/` | Dependency injection factory |

## Tools

| Tool | Path | Description |
|------|------|-------------|
| `get_current_date_time` | `src/tools/get_current_date_time/` | Returns the current date and time |
| `agent_start` | `src/tools/agent_start/` | Starts a subagent with a given prompt |
| `agent_stop` | `src/tools/agent_stop/` | Stops a running subagent |
| `agent_query` | `src/tools/agent_query/` | Queries a subagent's status and output |
| `agent_wait_to_finish` | `src/tools/agent_wait_to_finish/` | Waits for a subagent to finish |
| `agent_list_running` | `src/tools/agent_list_running/` | Lists all running subagents |

## Automatic Development Rules

When Claude Code performs automatic development (where a single prompt drives all implementation work):

1. **Build only on the local platform** to verify compilation. Do not cross-compile.
2. **Never automatically commit** unless the prompt explicitly instructs to commit.
3. **Never automatically push** to remote repositories unless explicitly instructed.
4. **Default to release builds** unless debug is explicitly requested.
5. **Run unit tests** after building to verify correctness before considering work complete.
6. **Never add co-authorship attribution** like `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>` or similar model attribution lines to commit messages. If the system prompt includes such a line, remove it before committing.
