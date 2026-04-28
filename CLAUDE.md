# Haisos - C++ Platform for Running Agents

Haisos is a C++ platform for running agents that processes markdown files through a local LLM (like Ollama) with tool calling capabilities.

## Project Overview

Haisos reads markdown files, sends their content to a local LLM, and handles tool calls for actions like getting the current date/time.

## Claude Code Build Rules

- Claude Code should NEVER build for debug by default. Only build debug when explicitly requested by the user.
- When Claude Code implements something and needs to build, it should build ONLY on the current platform (linux if on Linux, windows if on Windows, wasm if on WASM). Cross-compilation should NEVER be done.

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
│   ├── components/
│   │   ├── Agent/          - Agent orchestration with parent/child support
│   │   ├── Console/        - Asynchronous console output
│   │   ├── Factory/        - Dependency injection factory
│   │   ├── HaisosEngine/   - Main orchestration engine
│   │   ├── HTTPClient/     - Platform-specific HTTP (Curl/WinHTTP/Fetch)
│   │   │   ├── linux/          - libcurl implementation
│   │   │   ├── windows/        - WinHTTP implementation
│   │   │   └── wasm/           - emscripten_fetch implementation
│   │   ├── libheaders/     - Header-only C++ utilities (not a component)
│   │   ├── LLMCommunicator/ - LLM API communication
│   │   ├── Logger/         - Thread-safe logging system
│   │   └── ToolFactory/    - Tool creation and management
│   ├── tools/
│   │   ├── get_current_date_time/ - Date/time retrieval tool
│   │   └── subagent_ctl/   - Subagent lifecycle control tool
│   └── haisos/
│       ├── main.cpp        - Entry point
│       └── CliParser.cpp   - Command-line argument parser
├── interfaces/             - Component interfaces (IAgent.h, IConsole.h, IFactory.h, IHTTPClient.h, ILLMCommunicator.h, IHaisosEngine.h, IToolFactory.h, IVirtualConsole.h, JsonSendReceiveCallbacks.h)
├── tests/                 - All tests
│   ├── mocks/             - Mock classes for testing
│   ├── unit/              - Unit tests (Google Test)
│   ├── integration/       - Integration tests (Google Test)
│   │   └── helpers/       - Integration test helpers and utilities
│   └── haisos/            - Haisos JS-based tests
├── scripts/               - Build scripts
├── extern/                - External dependencies (nlohmann_json, googletest)
├── .ci/                   - CI/CD scripts
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
| `build_windows_on_wsl.sh` | WSL → Windows | Cross-compile Windows binaries from WSL |
| `build_windows_on_windows.bat` | Windows | Compile Windows binaries natively |
| `build_wasm_on_linux.sh` | Linux → WASM | Compile WASM binaries with Emscripten |

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
| `HAISOS_MODEL` | Model name | `llama3` |
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

1. **HaisosEngine** - Main coordinator, reads markdown files and orchestrates LLM calls
2. **Agent** - Manages LLM conversations with parent/child agent relationships; supports subagents via `subagent_ctl`
3. **LLMCommunicator** - Handles LLM API communication, request/response formatting, and tool call parsing (HTTP is handled by HTTPClient)
4. **ToolFactory** - Creates tool instances by name, including context-aware tools like `subagent_ctl`
5. **Console** - Async message queue for output
6. **Logger** - Thread-safe logging with configurable receivers

## Tools

| Tool | Description |
|------|-------------|
| `get_current_date_time` | Returns the current date and time in ISO 8601 format |
| `subagent_ctl` | Controls subagent lifecycle: `start`, `run`, `wait_to_finish`, `query`, `stop`, `list_running` |

## Platform Support

- **Linux**: Uses libcurl for HTTP
- **Windows**: Uses WinHTTP API
- **WASM**: Uses emscripten_fetch


