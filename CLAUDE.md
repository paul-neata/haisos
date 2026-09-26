# Haisos - C++ Platform for Running Agents

Haisos is a C++ platform that boots a small OS-like environment (`IHaisosOS`) from a `haisosfile` manifest and runs processes in it: LLM agents (`.md` files, sent to a local LLM like Ollama with tool-calling), embedded Lua scripts (`.lua` files), and builtin commands (`echo`, `cat`, `ls`, ... compiled into Haisos and placed on a filesystem at a path, such as `/bin/ls`).

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
│   │   ├── BuiltinCommands/ - The builtin commands (echo, cat, ls, mkdir, pwd) and what places them on filesystems
│   │   ├── Console/
│   │   ├── Environment/
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
│   │   ├── agent_query/
│   │   ├── agent_wait_to_finish/
│   │   ├── agent_list_running/
│   │   ├── self_close/
│   │   ├── agent_tools_common/
│   │   ├── os_tools_common/
│   │   ├── tools_common/      - ToolArguments.h: how every tool reads its arguments, never throwing
│   │   ├── os_read_file/
│   │   ├── os_write_file/
│   │   ├── os_list_directory/
│   │   ├── os_start_process/
│   │   └── os_list_processes/
│   └── haisos/            - Entry point, CLI parser, haisosfile parser, root-filesystem builder, file-directive executor, agent traffic log (--log-agent-to-file / -L), and the re-creatable log file behind both file logs
├── interfaces/             - Service-based interfaces (IFactory.h [IPhysicalConsole], IBuiltinCommands.h [IBuiltinConfigurator, BuiltinCommandHost], IServicesCreator.h, IHaisosOS.h, IProcess.h, IEnvironment.h [LLMIdentifier], ILLMService.h [IAgent, ITool, IToolFactory, IAgentConsole], INetworkService.h [IHTTPClient], IFileSystemService.h [IFileSystem], IProcess.h [ICurrentProcess], ILLMCommunicator.h)
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
├── notes/                 - Markdown notes, one per file, named by kind: note-*.md (/note), explore-*.md (/explore), todo-*.md (/todo), plan-*.md (/implement) (.gitkeep'd; see "Planning skills")
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

These are read from the **OS's own environment** (`IEnvironment`), not the
host's, and have no built-in default: whatever the haisosfile's `ENV` directives
say is what agents talk to. Set them outright with `ENV HAISOS_MODEL=llama3`, or
take the host's value with the bare `ENV HAISOS_MODEL` form. `haisos --init`
writes the defaults above into the generated haisosfile. Because they are
variables in the environment, a sub-OS or process handed a `Clone()` of it gets
the LLM configuration along with everything else -- but nothing is inherited
implicitly: an `IEnvironment` is passed explicitly when an OS, a process or a
sub-OS is created.

## Command-Line Arguments

Usage: `haisos [haisosfile] [options] [-- key=value ...]` (Docker-like: `haisosfile`
defaults to `haisosfile` in the current directory when omitted, and everything
after a literal `--` is parsed as `key=value` pairs fed to the haisosfile as
`ARG` overrides).

| Argument | Description |
|----------|-------------|
| `<haisosfile>` | Path to the haisosfile to run (positional; defaults to `./haisosfile`), relative to the current directory or absolute, written as `FS ... PHYSICAL` directories are (on Windows `c:\x\haisosfile` or `/c/x/haisosfile`) |
| `-- key=value ...` | `ARG` overrides passed to the haisosfile |
| `--init` | Write a commented starter haisosfile to `./haisosfile` and exit (refuses to overwrite an existing one) |
| `--version` | Show version information |
| `-h, --help` | Show help message |
| `--log-to-console` | Enable logging to console |
| `-l`, `--log-to-file <path>` | Enable logging to file (re-created if it is deleted while Haisos runs; see `src/haisos/ReopeningLogFile.h`) |
| `--log-level <level>` | Set log level (verbose_debug, debug, trace, info, warning, error) |
| `--log-json-in-temp` | Log input/output JSON to a temporary file |
| `-L`, `--log-agent-to-file <path>` | Write every agent's LLM traffic to `<path>`: each request sent (`SEND`) and response received (`RECEIVE`), headed by the agent's path (its ancestors' names then its own, joined by `>`, e.g. `main_2>aaaaer6o`) and the time. Each entry is indented by the agent's depth in its agent tree: two tabs and a `\|` per level, none for a top-level agent. Re-created if deleted while Haisos runs, starting afresh (requests in full again) |
| `--log-agent-to-file-type <type>` | `xdiff` (default): like `diff`, JSON-like, but shorter -- unchanged fields are left out, `messages` is written `m` and shows only its new entries, `tools` lists only each tool's name with its description, a tool call is written as `m[1].tool_calls[0].function.name = "..."` lines, text is wrapped to 80 characters (line breaks kept) in `"""` blocks, and empty strings plus response timings (`model`, `created_at`, `*_duration`) are dropped; `diff`: each request as its JSON difference from the same agent's previous one, responses in full; `full`: everything in full JSON. Requires `--log-agent-to-file` |

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
FS rootfs PHYSICAL .                 # a real disk directory (relative to this file, or absolute)
FS data PHYSICAL "C:\My Data"        # on Windows also /c/My Data or c:/My Data; quoted, as it holds a space
FS scratch MEM                       # an empty, in-memory read/write filesystem
FS devfs DEV                         # device files, as Linux's /dev: null and zero
FS readonly RO rootfs                # a read-only wrapper over another declared filesystem
FS inner SUB rootfs tools            # confined to a sub-path of another filesystem

# MOUNT overlays one filesystem inside another at a path, in place, overriding
# anything already there: MOUNT <main_fs> <path> <fs_to_mount>
MOUNT rootfs /scratch scratch
MOUNT rootfs /dev devfs              # /dev/null and /dev/zero for every process

# FS ... COMPOSED does the same without touching either operand, declaring the
# result under a new name: FS <name> COMPOSED <main_fs> <path> <fs_to_mount>
FS combined COMPOSED rootfs /scratch scratch

ROOT rootfs                    # which declared filesystem (by name) becomes the OS's root

# File directives act on the root filesystem; paths inside the OS are absolute,
# host paths are relative to this file (or absolute).
CREATE_DIR /bin                # create a directory and any missing parents (fine if it exists)
BUILTIN rootfs ls /bin/ls        # place a builtin on a declared FS, at each path given
CREATE /notes/a.txt 'hello'    # write a file (replacing it); content is 'quoted' or "quoted"
APPEND /notes/a.txt text: more # append (creating if missing); text: takes the rest of the line as-is
CREATE /notes/b.md multiline END
# a heading -- content, not a comment
END
COPY ./input.txt /work/in.txt  # copy a host file into the OS
DELETE /work/stale             # remove a file, or a directory and everything in it (as rm -rf: links are removed, not followed)
OUTCOPY /work/out.txt ./out.txt  # copy a file out to the host, once every RUN process has finished

RUN /agent.md                  # start an initial process (.md agent, .lua script or builtin) at '/'; may repeat
RUN /bin/ls -l /               # a builtin, placed by BUILTIN above
RUN /tools/setup.lua ${greeting}
RUN -i /chat.md                # an interactive agent, fed each line typed on the console
```

`FS <name> DEV` is a device filesystem, meant to be mounted at `/dev`: it holds
the character devices `null` (writes discarded, reads return end-of-file at
once) and `zero` (writes discarded, reads return endless 0 bytes), as on Linux,
and nothing can be created in it or deleted from it -- not even a `BUILTIN`.
`haisos --init` writes the two lines that mount it, commented out.

`ROOT`'s value is looked up by name against the declared `FS`s; if omitted, the
last `FS` declared is used, and naming none of them is an error. If a
haisosfile declares no `FS` at all, `ROOT` falls back to the original shorthand
-- a plain directory path (or the haisosfile's own directory, if `ROOT` is also
omitted) -- so simple haisosfiles never need `FS`.

A `PHYSICAL` directory (and a plain-path `ROOT`) is a directory of the host's
full physical filesystem (`IFactory::CreateFullPhysicalFileSystem`): the disk
from its root on Linux; on Windows a directory per drive, named by its
lowercase letter, as Cygwin shows them. It is relative to the haisosfile's
directory, or absolute, and must be there -- a missing one is an error, not a
process failing to start later. On Linux it is a host path as the host takes
it, `\` included. On Windows `\` and `/` both separate, in any mix: `/c/x`,
`\c\x`, `c:\x` and `c:/x` are all `C:\x`, `\\server\share\x` (or
`//server/share/x`) is a UNC path, and `/` alone is the full filesystem, every
drive in it; a drive-relative `c:x` and a `/tmp` that names no drive are
refused. Each directory is taken with `IFactory::CreatePhysicalFileSystem`,
jailed there -- not as a `SubFileSystem` of the full filesystem, which confines
paths only as written, so a symbolic link inside would lead anywhere on the
disk. `COPY`/`OUTCOPY` host paths, and the haisosfile path on the command line,
follow the same rules (`src/components/Filesystem/PhysicalPath.h`).

Any token -- a path, a `RUN` argument -- may be quoted, `'...'` or `"..."`, to
hold spaces or a `#`: it runs to the next quote of the same kind and is taken
without the quotes, with nothing inside special (no escapes: `"C:\Program
Files\x"` is taken as written). A quote elsewhere in a token is part of it.

`RUN` takes an absolute program path, and the process starts in `/`. `RUN -i`
(only in front of the path) runs a `.md` agent interactively -- see
`StartProcessOptions::interactiveAgent` in `interfaces/IHaisosOS.h`: after its
program, every line typed on the console is posted to it, until it closes itself
with the `self_close` tool (noticed when the next line arrives) or input ends.

`CREATE`/`APPEND` content is one of `'text'` / `"text"` (up to the next quote of
the same kind; a comment may follow), `text:<rest of line>` (taken exactly as
written, `#` included), or `multiline <marker>` (the following lines, up to one
that is only `<marker>` after trimming; the newline before the marker is not
part of the text, so an empty line before it keeps a trailing newline). Content
is never `${}`-substituted; the path is. Missing parent directories are created.
`COPY`/`OUTCOPY` copy files, not directories; `OUTCOPY` creates missing host
directories. The file directives run in `src/haisos/HaisosFileOperations.cpp`.

`BUILTIN <fs_name> <builtin_name> <path>...` places a builtin command on the
declared filesystem named (not necessarily the root), once per path; each path
is absolute, its directory must already exist (hence `CREATE_DIR`), and nothing
may be there yet. An unknown builtin name or filesystem name is an error. The
root filesystem is conventionally named `rootfs`, as `haisos --init` does. A
haisosfile declaring no `FS` has no names for `BUILTIN` to use.

Ordering is enforced: once a file directive (`CREATE`/`APPEND`/`CREATE_DIR`/
`COPY`/`DELETE`/`BUILTIN`/`OUTCOPY`) has appeared, no `ROOT`, `FS` or `MOUNT`
may follow -- the filesystems are fixed once files are written to them -- and
all but `OUTCOPY` must come before the first `RUN`, since they are applied
before any process starts. `OUTCOPY` may appear anywhere after that, and always
runs last.

Mistakes are reported rather than silently absorbed: a `${name}` that resolves
to nothing declared, a duplicate `FS` name, a `ROOT` naming no declared `FS`, a
`PHYSICAL` directory that is not there, an unterminated quote, a `-- key=value`
override naming an argument the file never declares, a `RUN` left empty by
substitution, a relative path inside the OS, and a file directive that fails
when applied are all errors. `ARG name` without `=` declares an argument with no default, which
must then be supplied via `-- name=value`. A `VAR`/`ARG` right-hand side may only
reference names declared above it. Each `RUN` starts a top-most process (its
parent is the OS); Haisos exits once all of them have finished. Composed/temporary filesystems from
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

### Security: `ICurrentProcess` is the only door out of a process

**Everything a running program reaches beyond its own memory -- the filesystem,
other processes, the services -- it reaches through `ICurrentProcess`: files via
`IO()`, everything else via `OS()`.** That holds for every runtime alike: the
tools an agent calls, the agent itself, and the globals a Lua script gets.
Nothing is handed an `IHaisosOS` or an `IFileSystem` directly, and nothing keeps
a private path to one.

Two things follow, and they are the whole reason for the rule:

1. **Every runtime has the same reach.** A `.lua` script can do neither more nor
   less than a `.md` agent, because both go through the same one door.
2. **Narrowing a process is a matter of handing it a narrower OS**, with no
   runtime needing to know. `ICurrentProcess::OS()` need not return the
   OS that started the process: it may be a narrowed clone of it, confined by
   the root filesystem it was built with. When user support arrives, the first
   process of a user will be given an OS whose root is writable only under that
   user's home and a temp directory -- and nothing inside the process changes.

A security policy can then be enforced in one place rather than in every tool.
**That policy is not implemented yet; it lands in a later PR.** The plumbing it
will need is in place: when adding a tool, a runtime, or anything else a process
can call, route it through `ICurrentProcess` rather than giving it its own
handle on the OS.

How it is wired: `OSToolFactory` and every `os_*` tool are built **per process**,
around a `CurrentProcessHandle` (`src/components/libheaders/`) rather than
around an `IHaisosOS`. The handle exists before the process does -- an agent
needs its tools before the process wrapping it can be built -- and whoever
builds the process fills it in before the process goes live, so no tool can
observe it empty. At call time a tool asks the handle for its process, the
process for its OS, and gets exactly the OS that process was given.

All file access goes through `ICurrentProcess::IO()`, an `IFileIO`: the
`IFileSystem` operations bound to the root filesystem of the OS that process was
given, plus the working directory they resolve against. That is what makes a
bare name or a relative path mean anything at all -- an `IFileSystem`
understands absolute paths alone, so `IFileIO` is the one place a path and the
process's position are brought together. `os_start_process` also starts a child
in the caller's directory, the way a shell would.

`IFileIO` deliberately omits `Mount`/`Unmount`, which `IFileSystem` has:
composing filesystems is how an OS is assembled, not something a program running
inside one may do to the ground it stands on. A process that could mount could
widen its own reach, which is what this whole arrangement exists to prevent.

### Creating things: private constructors and `Create()`

Every class implementing an interface from `interfaces/` has **private
constructors** and a public static `Create(...)` returning a `shared_ptr`; these
classes are only ever held by `shared_ptr`, never by value, by `unique_ptr`, or
on the stack. Every factory method across the interfaces returns a `shared_ptr`
too, so there is one ownership story end to end and a created object can safely
hand itself out (`Agent`, for instance, starts its thread in `Create()` rather
than in its constructor, because the thread passes `shared_from_this()` to every
tool it calls).

The exception is a back-reference to the owner: `ToolFactory` holds an
`ILLMService&`, `OSToolFactory` and the `os_*` tools hold an `IHaisosOS&`, and
`AgentStartTool` holds an `ILLMService&`. These stay raw references on purpose
-- the referent owns the holder, so a `shared_ptr` back would close an ownership
cycle and nothing would ever be freed.

**Nothing that waits for a runtime thread is destroyed on one.** A runtime
thread -- an agent's conversation, a Lua script, a builtin command, an
interactive agent's input loop -- can hold the last reference to the very
object that owns it: an `os_*` tool holds its process and its OS for the length
of a call, and an agent hands `shared_from_this()` to every tool it calls.
Destroyed there, `~Agent` would wait for itself forever, `~LuaProcess` and
`~BuiltinProcess` would join their own thread (`std::terminate`), and
`~HaisosOS` would wait out its 5 s stop timeout on the process it is running
on. So `HaisosOS`, the process classes and `Agent` pass the
`DestroyOffRuntimeThreads` deleter to the `shared_ptr` their `Create()` builds,
and every runtime thread's function starts with a `RuntimeThreadScope` (both in
`src/components/libheaders/DestroyOffRuntimeThreads.h`): released on a runtime
thread, such an object is destroyed on the one `DestructionThread` instead. A
new class that owns a thread, or waits for one in its destructor, does the
same. The whole story, and what to look for in the log (whose lines name their
thread), is written up in `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`
under "Objects released last on their own threads".

| Component | Path | Description |
|-----------|------|-------------|
| **Agent** | `src/components/Agent/` | Manages LLM conversations with parent/child agent relationships; supports subagents via agent tools |
| **LLMCommunicator** | `src/components/LLMCommunicator/` | Handles LLM API communication, request/response formatting, and tool call parsing (HTTP is handled by HTTPClient) |
| **ToolFactory** | `src/components/ToolFactory/` | Creates tool instances by name, including context-aware tools like `agent_start` |
| **Environment** | `src/components/Environment/` | An OS's or a process's environment: variables, secrets (nameable but not readable), and LLM identifiers; `Clone()`d rather than shared |
| **Console** | `src/components/Console/` | Async physical console output and line input, plus adapters giving agents a view onto it (or onto memory only) |
| **Logger** | `src/components/Logger/` | Thread-safe logging with configurable receivers |
| **HTTPClient** | `src/components/HTTPClient/` | Platform-specific HTTP implementation (Curl/WinHTTP/Fetch) |
| **Factory** | `src/components/Factory/` | Creates the root concepts: physical console, disk-backed filesystems (a directory, or the host's whole disk), the services layer, and the OS itself |
| **Filesystem** | `src/components/Filesystem/` | Composable `IFileSystem` implementations: an unrooted passthrough, a `PhysicalFileSystem` jailed to a real disk path, the Windows-only `WindowsFullPhysicalFileSystem` (every drive under `/`, as `/c/...`), plus in-memory, read-only, sub-path and mounted/overlay ones |
| **ServicesCreator** | `src/components/ServicesCreator/` | Factory-of-services built on `IFactory`; creates `IFileSystemService`/`INetworkService`/`ILLMService`, passing each the services it depends on |
| **NetworkService** | `src/components/NetworkService/` | Service-layer wrapper over network access (creates `IHTTPClient`) |
| **FileSystemService** | `src/components/FileSystemService/` | Stateless factory that composes filesystems (read-only / in-memory / sub / mount); holds no filesystem of its own |
| **LLMService** | `src/components/LLMService/` | Service-layer entry point for creating LLM-backed agents; exposes the shared agent-management tool set |
| **HaisosOS** | `src/components/HaisosOS/` | An OS instance: owns a rooted filesystem, physical console, and services; starts processes (`.md` agents, `.lua` scripts, builtins) and sub-OS instances |
| **BuiltinCommands** | `src/components/BuiltinCommands/` | The builtin commands (`IBuiltinCommands`), each run as a process on its own thread, and the `IBuiltinConfigurator` that places them on filesystems |

## Tools

| Tool | Path | Description |
|------|------|-------------|
| `get_current_date_time` | `src/tools/get_current_date_time/` | Returns the current date and time |
| `agent_start` | `src/tools/agent_start/` | Starts a subagent with a given prompt |
| `agent_query` | `src/tools/agent_query/` | Queries a subagent's status and output |
| `agent_wait_to_finish` | `src/tools/agent_wait_to_finish/` | Waits for a subagent to finish |
| `agent_list_running` | `src/tools/agent_list_running/` | Lists all running subagents |
| `self_close` | `src/tools/self_close/` | Closes the calling agent; how an interactive agent ends its session |
| `os_read_file` | `src/tools/os_read_file/` | Reads a file from the OS's filesystem |
| `os_write_file` | `src/tools/os_write_file/` | Writes a file on the OS's filesystem |
| `os_list_directory` | `src/tools/os_list_directory/` | Lists a directory on the OS's filesystem |
| `os_start_process` | `src/tools/os_start_process/` | Starts a new OS process (`.md`/`.lua`) as a child of the calling process |
| `os_list_processes` | `src/tools/os_list_processes/` | Lists the OS's currently running processes |

Every tool reads its arguments through `src/tools/tools_common/ToolArguments.h`,
which never throws: an optional argument that is absent or null takes its
default, and one of the wrong type (`"append": "true"` for a boolean, say) is
an error result naming it and the type expected -- never silently the default.
An exception out of a tool anyway becomes that call's error result
(`Agent::ExecuteToolCalls`), so a tool can fail its call but never the agent.

The `agent_*` tools and `self_close` above are agent-management tools, returned by `ILLMService`
and available to every agent. The `os_*` tools are the OS's own tool set,
returned by `IHaisosOS`'s `OSToolFactory` and merged with an agent's tools
(via `CompositeToolFactory`) only for processes started by an `IHaisosOS`.

## Builtin Commands

Commands compiled into Haisos, implemented in `src/components/BuiltinCommands/`
(see its `CLAUDE.md`). A builtin is not a file on disk: it is placed on a
filesystem at a path (`IBuiltinConfigurator`, or the haisosfile's `BUILTIN`),
where it lists as a file, reads as a note naming it, cannot be written, and
keeps its directory from being deleted. `IHaisosOS::StartProcess` runs any path
its root filesystem says is a builtin, from the `IBuiltinCommands` the OS was
created with -- so `RUN /bin/ls` and `os_start_process` both work.

**Rules for every builtin** -- they exist so that an agent (or a person) can
use a builtin with what it already knows about the real command:

- **Same output format as the real command.** What a builtin prints --
  layout, column order and alignment, date formats, quoting, error messages
  and their wording, exit statuses -- is what the GNU/Linux command prints, so
  output can be parsed with built-in knowledge of that command. An exception
  is allowed only when HaisosOS cannot supply the data, and it must be
  documented: in the command's `--help` notes, and in the table in
  `src/components/BuiltinCommands/CLAUDE.md` (e.g. `ls -l` shows owner and
  group `haisos` and permissions `rwxrwxrwx`, since there are no users or
  permissions yet; the columns themselves are kept).
- **Accept every argument the real command accepts.** Every option of the
  real command is listed in the builtin's option table (`IBuiltinCommand::Options()`),
  treated or not, so none is ever rejected as unknown. One the builtin does
  not act on -- because HaisosOS lacks what it needs, or it is not needed --
  is parsed (its argument consumed), ignored, and reported on a line of its
  own: `Parameter --xyz is not treated by HaisosOS <command> v. <version>`
  (`BuiltinContext::ReportNotTreated`/`NotTreated`, which `BeginBuiltin` calls
  for you). A value of a treated option that is not handled is reported the
  same way (`Parameter --sort=version is not treated ...`).
- **`--help` has one shape** (`BuiltinHelpText`, generated from the option
  table, never hand-written):
  ```
  HaisosOS <command> version <version> - <what it does, in a few words>
  Based on Linux <command>: https://man7.org/linux/man-pages/man1/<command>.1.html

  Usage: <command> ...

    <each treated option, described in a few words>

  <notes: documented exceptions, in a line or two>

  Not treated arguments: <every untreated option, on this one last line, no descriptions>
  ```
  Only what the builtin handles is described. The reference is always the
  Linux man-pages project's page for the command (`BuiltinReferenceUrl`).
  `--version` prints `<command> (HaisosOS builtin) <version>`; bump the
  version whenever a builtin's behaviour changes.

| Builtin | Description |
|---------|-------------|
| `cat` | Concatenates files (`-A -b -e -E -n -s -t -T -u -v`); no stdin |
| `echo` | Prints its arguments (`-n -e -E`) |
| `ls` | Lists directories as GNU ls prints them to a terminal: columns, `-l` with `total`/links/owner/group/size/time, sorting, time styles, quoting |
| `mkdir` | Creates directories (`-p -v`) |
| `pwd` | Prints the working directory (`-L -P`) |

## Planning skills

Four skills in `.claude/skills/` take a piece of work from an idea to commits,
each leaving a Markdown file in `notes/` that the next one can start from:

| Skill | Writes | What it does |
|-------|--------|--------------|
| `/note` | `notes/note-*.md` | Records a finding, a decision or an idea |
| `/explore` | `notes/explore-*.md` | Explores the variants and aspects of an issue against the code; records clear winners with why the rest were dropped, and lists undecided aspects most probable first |
| `/todo` | `notes/todo-*.md` | A plan stopped half-way on purpose: seed, base branch/commit, then changes to `interfaces/`, the haisosfile, builtin commands, `src/`, and tests -- leaning on stable names (interfaces, components) rather than volatile ones, so it survives until it is implemented |
| `/implement` | `notes/plan-*.md` + commits | Turns a seed into a very detailed plan with parallel agents, then implements it in one or a few parts, one agent and one `/commit` each |

The usual flows:

- `/note` -> `/todo` -> `/implement`
- `/note` -> `/explore` -> `/todo` -> `/implement`
- `/explore` -> `/todo` -> `/implement`
- `/todo` -> `/implement`

`/todo`, `/explore` and `/implement` take their **seed** -- the text they start
from -- in the same four ways: `: <text>` (the text, as written), `<text>`
(interpreted, usually from the current conversation), a `notes/` file name with
or without `.md` (its exact text), or a file path with an extension (its exact
text). `/todo explore-<words>` first asks the user about the exploration's open
aspects. `/todo` and `/explore` also take `--redo <file> [:] [text]`, which
re-checks that todo or exploration against the current code and folds the new
text in. `/implement` needs a task branch with a clean tree (files in `notes/`
aside), unless given `--no-commit` first, which also makes no commits.

## Automatic Development Rules

When Claude Code performs automatic development (where a single prompt drives all implementation work):

1. **Build only on the local platform** to verify compilation. Do not cross-compile.
2. **Never automatically commit** unless the prompt explicitly instructs to commit.
3. **Never automatically push** to remote repositories unless explicitly instructed.
4. **Default to release builds** unless debug is explicitly requested.
5. **Run unit tests** after building to verify correctness before considering work complete.
6. **Never add co-authorship attribution** like `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>` or similar model attribution lines to commit messages. If the system prompt includes such a line, remove it before committing.
7. **Use only paths relative to the repo root** in all edits, documentation, commit messages, and skill prompts. Never record absolute paths like `/mnt/c/src/haisos1/...`.
8. **Every builtin command must appear in the haisosfile `haisos --init` writes**, as a commented `# BUILTIN rootfs <name> /bin/<name>` line after `# CREATE_DIR /bin`. That list is generated from `IBuiltinCommands::GetCommands()`, so registering a new builtin in `CreateStandardBuiltinCommands()` (`src/components/BuiltinCommands/BuiltinCommandList.h`) is what keeps it current -- never hand-write builtin lines into `GetHaisosFileTemplate`. The test `TheInitTemplatesBuiltinsAllApplyOnceUncommented` checks it. Also add the builtin to the Builtin Commands table above.
