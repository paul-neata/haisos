# Haisos - C++ Platform for Running Agents

Haisos is a C++ platform that boots a small OS-like environment (`IHaisosOS`) from a `haisosfile` manifest and runs processes in it: LLM agents (`.md` files, sent to a local LLM like Ollama with tool-calling), embedded Lua scripts (`.lua` files), and builtin commands (`echo`, `cat`, `ls`, `wc`, ... compiled into Haisos and placed on a filesystem at a path, such as `/bin/ls`).

## Project Overview

A `haisosfile` (Dockerfile-style; see "The `haisosfile` DSL" below) selects a filesystem root to mount and one or more initial processes to run. Haisos starts each one under a rooted `IHaisosOS`, and exits once they've all finished. Agent-backed processes get both the LLM's agent-management tools (`agent_start`, ...) and the OS's own tools (`os_read_file`, `os_start_process`, ...); Lua processes get the OS's tools directly as global functions, and run against a restricted subset of the Lua standard library, so filesystem/process access is only available through the OS's `os_*` tools.

Platform support:
- **Linux**: Uses libcurl for HTTP
- **Windows**: Uses WinHTTP API
- **WASM**: Uses emscripten_fetch

## Clean-room rule: all code is written from scratch

**This rule comes before every other one.** No code is copied from any other
program or project -- whatever its licence: the same as Haisos's, a compatible
one, or an incompatible one. Every line of Haisos is written from scratch.

- What another program *does* may be matched: its documented and observable
  behaviour -- man pages, POSIX and other specifications, published papers and
  algorithms (e.g. Myers' diff), and the output of running the real program.
  That is how the builtins match GNU (see "Builtin Commands").
- What another program *is* may not be used: its source code is never read in
  order to reproduce it -- not copied, translated, ported, paraphrased, or
  rewritten line by line or function by function. That includes code recalled
  from memory, snippets found online, and libraries' internals (coreutils,
  gnulib, diffutils, findutils, sed, gawk, ripgrep, jq, tar, procps, ...).
- Plans and reviews describe behaviour, never another program's code
  structure (its functions, how they are split, their order); comments name
  the behaviour matched, not another program's functions.
- The libraries in `extern/` are used as dependencies, never copied into
  `src/`.

Code that looks copied is a critical finding in a review.

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
│   │   ├── BuiltinCommands/ - The builtin commands ([, basename, cat, chmod, cmp, cp, cut, date, diff, dirname, du, echo, egrep, env, false, fgrep, find, grep, head, hsh, ls, man, mkdir, mv, nl, patch, printf, pwd, realpath, rg, rm, rmdir, sed, seq, sleep, sort, stat, tail, tee, test, touch, tr, true, uniq, wc, which, xargs; each in commands/<name>/) and what places them on filesystems
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
│   │   ├── PipeService/
│   │   ├── Regex/           - The regex engine: GNU BRE/ERE and a Perl subset
│   │   ├── ServicesCreator/
│   │   ├── ToolFactory/
│   │   └── Unicode/
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
├── interfaces/             - Service-based interfaces (IFactory.h [IPhysicalConsole], IBuiltinCommands.h [IBuiltinConfigurator, BuiltinCommandHost], IServicesCreator.h, IHaisosOS.h, IProcess.h, IEnvironment.h [LLMIdentifier], ILLMService.h [IAgent, ITool, IToolFactory, IAgentConsole], INetworkService.h [IHTTPClient], IFileSystemService.h [IFileSystem], IProcess.h [ICurrentProcess], ILLMCommunicator.h, IFileDescriptor.h [IFileDescriptor, IOResult kIO*], IPipeService.h [PipeEnds])
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
│   └── develop/           - The develop workflow's host scripts and task container: Dockerfile, entrypoint, security gate, task pipeline (see "Develop workflow")
├── extern/                - External dependencies (nlohmann_json, googletest, lua)
├── notes/                 - Markdown notes, one per file, named by kind: note-*.md (/note), explore-*.md (/explore), todo-*.md (/todo), plan-*.md (/implement) (.gitkeep'd; see "Planning skills")
├── develop-plan/          - The plan of the develop in progress: goal, rocks, task plans, playbook, reviews (on `develop` only; deleted by /develop-close)
├── subrepo/               - The develop containers' workspace (git-ignored; created by the first task or /claude-docker run): a haisos working tree whose git metadata lives in ~/.haisos-develop/subrepos/
├── .claude/               - Claude Code configuration
│   ├── develop/           - WORKFLOW.md, the develop workflow's reference
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

## Exit codes

Every process reports an exit code, `IProcess::ExitCode()`: empty while it
runs, then a shell-style 0-255 that never changes once set. A program's own
code is taken modulo 256 (`exit(256)` is 0, `exit(-1)` is 255); a process
stopped through `TriggerStop()` reports 143 (128 + SIGTERM, as a shell would
for a killed program); and 141 (128 + SIGPIPE) when a builtin, a Lua script or
an agent wrote to a pipe nobody reads any more -- its runtime stops it quietly,
as SIGPIPE stops a Linux program, and 141 wins over a stop asked for
afterwards. A `RUN` whose process never starts has no code -- as with a shell's 127,
reporting it is the launcher's business, not the process's.

haisos's own exit status is the exit code of the first `RUN` (in file order)
that did not exit 0 -- 127 when one or no process could be started, else 0 --
with haisos's own errors (a bad haisosfile, an unreadable `OUTCOPY` target)
exiting 1. What a runtime counts as its code: a builtin, the command's own
status; a Lua script, `exit([code])`'s argument (0 by default, `true` 0 and
`false` 1, as `os.exit` -- the stock `os.exit` stays unopened), or 1 on a load
or runtime error, which also writes `lua: <message>` to the script's stderr
just as the standalone interpreter does; an agent, 1 when its last command
failed (an LLM, HTTP or parse failure, the LLM-round cap, an exception), else
0. Each runtime decides in one place, `ExitCodeFor` in
`src/components/libheaders/ExitCodes.h`.

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
ENV PATH=/bin                  # where hsh, the shell, finds commands by name
CREATE /notes/a.txt 'hello'    # write a file (replacing it); content is 'quoted' or "quoted"
APPEND /notes/a.txt text: more # append (creating if missing); text: takes the rest of the line as-is
CREATE /notes/b.md multiline END
# a heading -- content, not a comment
END
COPY ./input.txt /work/in.txt  # copy a host file into the OS
DELETE /work/stale             # remove a file, or a directory and everything in it (a link to a directory is followed: its target is emptied, the link removed)
OUTCOPY /work/out.txt ./out.txt  # copy a file out to the host, once every RUN process has finished

RUN /agent.md                  # start an initial process (.md agent, .lua script or builtin) at '/'; may repeat
RUN /bin/ls -l /               # a builtin, placed by BUILTIN above
RUN /tools/setup.lua ${greeting}
RUN -i /chat.md                # an interactive agent, fed each line typed on the console (any program may be run with -i)
RUN -i /bin/hsh                # an interactive shell on the console
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
rooted there (a `..` cannot climb above it), and symbolic links inside it are
followed wherever they lead, as the host follows them -- whoever declares the
filesystem vouches for the links in it. `COPY`/`OUTCOPY` host paths, and the haisosfile path on the command line,
follow the same rules (`src/components/Filesystem/PhysicalPath.h`).

Any token -- a path, a `RUN` argument -- may be quoted, `'...'` or `"..."`, to
hold spaces or a `#`: it runs to the next quote of the same kind and is taken
without the quotes, with nothing inside special (no escapes: `"C:\Program
Files\x"` is taken as written). A quote elsewhere in a token is part of it.

`RUN` takes an absolute program path, and the process starts in `/`. `RUN -i`
(only in front of the path, in front of any program) sets `interactive` -- see
`StartProcessOptions::interactive` in `interfaces/IHaisosOS.h`: the program's
stdin is the console's input (instead of an input that ends at once), and a
`.md` agent run so is interactive: after its program, every line read from its
stdin -- the console's input here, though another descriptor can feed it when
the process is started through `StartProcessOptions` -- is posted to it, until
it closes itself with the `self_close` tool (noticed when the next line
arrives) or input ends. `RUN -i /bin/hsh` runs the shell interactively, its
prompt on stderr.

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

A physical filesystem is only as narrow as the links inside it: symbolic links
(and junctions) on the disk are followed wherever they lead, out of its
directory included. Nothing in Haisos creates a link, and no builtin, tool or
directive may create one on a physical filesystem unless a confinement of
links comes back with it.

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
in the caller's directory, the way a shell would. `IFileIO` also holds the
process's descriptor table -- its open files by number (0 stdin, 1 stdout, 2
stderr, then 3 and up) -- so whatever a process has open, it holds there,
behind the same door, and every one is released when its program ends.

`IFileIO` deliberately omits `Mount`/`Unmount`, which `IFileSystem` has:
composing filesystems is how an OS is assembled, not something a program running
inside one may do to the ground it stands on. A process that could mount could
widen its own reach, which is what this whole arrangement exists to prevent.

Pipes go through the same door: a process makes a pipe only with
`IFileIO::CreatePipe`, which reaches the OS's pipe service through
`ICurrentProcess::OS()` -- nothing in a process holds an `IPipeService` or an
`IHaisosOS` of its own, and a pipe's ends land in the process's descriptor
table like any other descriptor.

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
| **Unicode** | `src/components/Unicode/` | UTF-8 decoding, character classes and display widths -- a compact, locale-free stand-in for glibc's, for builtins (`wc`) |
| **Environment** | `src/components/Environment/` | An OS's or a process's environment: variables, secrets (nameable but not readable), and LLM identifiers; `Clone()`d rather than shared |
| **Console** | `src/components/Console/` | Async physical console output and line input, plus adapters giving agents a view onto it (or onto memory only) |
| **Logger** | `src/components/Logger/` | Thread-safe logging with configurable receivers |
| **HTTPClient** | `src/components/HTTPClient/` | Platform-specific HTTP implementation (Curl/WinHTTP/Fetch) |
| **Factory** | `src/components/Factory/` | Creates the root concepts: physical console, disk-backed filesystems (a directory, or the host's whole disk), the services layer, and the OS itself |
| **Filesystem** | `src/components/Filesystem/` | Composable `IFileSystem` implementations: an unrooted passthrough, a `PhysicalFileSystem` rooted at a real disk path, the Windows-only `WindowsFullPhysicalFileSystem` (every drive under `/`, as `/c/...`), plus in-memory, read-only, sub-path and mounted/overlay ones, with rename and set-times throughout |
| **ServicesCreator** | `src/components/ServicesCreator/` | Factory-of-services built on `IFactory`; creates `IFileSystemService`/`IPipeService`/`INetworkService`/`ILLMService`, passing each the services it depends on |
| **NetworkService** | `src/components/NetworkService/` | Service-layer wrapper over network access (creates `IHTTPClient`) |
| **PipeService** | `src/components/PipeService/` | `IPipeService`: unnamed pipes -- bounded, blocking, one-way, both ends descriptors; no threads |
| **Regex** | `src/components/Regex/` | Haisos's own regex engine, shared by grep, sed, find, awk, rg and jq: GNU BRE and ERE (leftmost-longest) and a Perl subset (leftmost-first), on bytes |
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

Commands compiled into Haisos (`[`, `basename`, `cat`, `chmod`, `cmp`, `cp`,
`cut`, `date`, `diff`, `dirname`, `du`, `echo`, `egrep`, `env`, `false`, `fgrep`,
`find`, `grep`, `head`, `hsh`, `ls`, `man`, `mkdir`, `mv`, `nl`, `patch`, `printf`, `pwd`,
`realpath`,
`rg`, `rm`, `rmdir`, `sed`, `seq`, `sleep`, `sort`, `stat`, `tail`, `tee`, `test`, `touch`, `tr`,
`true`, `uniq`, `wc`, `which`, `xargs`), implemented in
`src/components/BuiltinCommands/`
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
  Only what the builtin handles is described. The reference is the Linux
  man-pages project's page for the command (`BuiltinReferenceUrl`) -- unless
  it has none, when the builtin names where it is documented itself
  (`BuiltinHelp::referenceUrl`: rg links ripgrep's guide). A builtin copying a
  command of another name says so (`BuiltinHelp::basedOn`: hsh is based on
  dash, and the `Based on Linux <command>:` line names dash and links its
  page).
  `--version` prints `<command> (HaisosOS builtin) <version>`; bump the
  version whenever a builtin's behaviour changes.

| Builtin | Description |
|---------|-------------|
| `[` | The `[ EXPRESSION ]` program, one with `test`: its last argument must be exactly `]`, `--help`/`--version` only as its sole argument; the expression is `test`'s |
| `basename` | Strips directories and a suffix from names (`-a -s SUFFIX -z`), GNU's trailing-slash rules |
| `cat` | Concatenates files and standard input (`-A -b -e -E -n -s -t -T -u -v`) |
| `chmod` | Changes file mode bits (`-c -f -v --no-preserve-root --preserve-root --reference=RFILE -R`), GNU 9.4's mode grammar; Haisos has no permissions: the mode is validated and nothing changes, every mode taken as 0777, the umask as 0 |
| `cmp` | Compares two files byte by byte (`-b -c -i -l -n -s -v`, SKIP1/SKIP2 operands), diffutils 3.10's `differ` and EOF messages, `-l`'s difference list, exit 0/1/2 |
| `cp` | Copies files and directories (`-a -b --backup[=CONTROL] -d -f -H -i -L -l -s -n -P -p --preserve[=LIST] --no-preserve=LIST --parents -R -r --remove-destination --strip-trailing-slashes -S -t -T -u --update[=UPDATE] -v`), GNU's messages, prompts, backups and the `--parents` walk |
| `cut` | Selects bytes or fields of each line (`-b -c -f -d -s -n, --complement, --output-delimiter, -z`), a LIST of `N`, `N-`, `N-M`, `-M` items; a line with no field delimiter is printed whole unless `-s` drops it |
| `date` | Prints the date and time (`-d -f -I[FMT] -r -R -s -u --resolution --rfc-3339=FMT`, `+FORMAT`); the date sources `-d`/`-f`/`-r`/`--resolution` mutually exclusive, `+FORMAT` through `BuiltinDate`'s `FormatDateTime` as GNU date's own; `-u` in UTC; setting the time (or the `MMDDhhmm[[CC]YY][.ss]` operand) is refused: the parsed time printed, then `Operation not permitted` |
| `diff` | Compares files line by line (`-a --text -b --ignore-space-change -B --ignore-blank-lines -c -C NUM --context[=NUM] -d --minimal -e --ed -E --ignore-tab-expansion -i --ignore-case -L --label --normal -q --brief -s --report-identical-files -t --expand-tabs -T --initial-tab -u -U NUM --unified[=NUM] -w --ignore-all-space -Z --ignore-trailing-space --strip-trailing-cr --tabsize=NUM --suppress-blank-empty --horizon-lines=NUM -r --recursive -N --new-file -P --unidirectional-new-file -x --exclude=PAT -X --exclude-from=FILE -S --starting-file=FILE --from-file --to-file`), the output formats normal, unified, context and ed script, diffutils 3.10's byte for byte: change ranges, marks, `\ No newline at end of file`, the headers with names quoted GNU's way and the files' own times (`-L` labels in their place, the current time for `-`), `-q`'s and `-s`'s one-line reports, `Binary files ... differ` for a NUL within the first 4096 bytes unless `-a` (then `-q` drops the `Binary`), the whitespace options strongest-wins (`-w` over `-b` over `-Z` over `-E`), `-B` dropping the hunks whose changes are all blank-line runs, `-t`'s tab expansion, `-T`'s and `--suppress-blank-empty`'s marks, the ed script last-change-first with its `No newline at end of file` warnings on stderr (exit 2) unless `-q`, and directories compared as GNU compares them: `-r` descending into common ones, entries in byte order, `Only in DIR: NAME`, `Common subdirectories: A and B`, `File A is a T1 while file B is a T2`, and a `diff OPTIONS A B` line (the options as typed, shell-quoted) before each differing file pair's output; a directory and a file compared through the directory's same-named entry, `-N`/`-P` a missing file as empty (the epoch time in its header), `-x`/`-X` excluding names from the walk, `-S` starting the top level, `--from-file`/`--to-file` comparing one file with every operand; exit 0 same / 1 different / 2 trouble, and GNU's usage errors with the `diff: Try 'diff --help'` line | the change list is always minimal, as GNU's `-d` (Myers' algorithm, `commands/diff/DiffEngine.*`): on inputs where GNU's default heuristics pick a different script, the hunks may be placed differently, and GNU's `--speed-large-files` shortcuts are not taken; the side-by-side and if-then-else families (`-y -W --left-column --suppress-common-lines -p -F -I -D --line-format` and the group formats), `-f`/`-n`, `-H`, `-l`, `--color` and the name-case options are not treated; `--no-dereference` changes nothing (no links) |
| `dirname` | Strips the last component from names (`-z`), GNU's dir_len: empty `.`, slashes-only `/` |
| `du` | Estimates file space usage (`-a -s -c -d -h -k -m -b -0 -x -S -L -l -P -H -D -B -t --apparent-size --si --inodes --exclude -X --files0-from --time --time-style`), sizes from `FileStatus.blocks`, GNU 9.4's messages |
| `echo` | Prints its arguments (`-n -e -E`) |
| `egrep` | `grep -E`: grep with the extended matcher preselected (see `grep`) |
| `env` | Runs a program in a modified environment (`-i -0 -u NAME -C DIR -S STRING`), the child looked up in the new PATH; with no COMMAND, prints the variables sorted by name |
| `false` | Does nothing, exits 1 (`--help`/`--version` only as the sole argument, and 1 even after them) |
| `fgrep` | `grep -F`: grep with fixed strings preselected (see `grep`) |
| `find` | Searches a directory tree (findutils 4.9.0): the leading options `-H -L -P -O -D --`, the expression operators `( ) ! -not -a -and -o -or ,` with implicit `-a`, the global options (`-depth -d -maxdepth -mindepth -mount -xdev -noleaf -ignore_readdir_race -noignore_readdir_race -files0-from -warn -nowarn`), the positional (`-daystart -follow -regextype`), every test (`-amin` ... `-xtype`, `-name`/`-path` through `FnMatch`, `-regex` through the `Regex` engine, the time windows GNU's, `-newerXY`, `-perm`, `-size`, the ownership and type ones) and the actions `-delete -exec -execdir -fls -fprint -fprint0 -fprintf -ls -ok -okdir -print -print0 -printf -prune -quit` (programs run only through `RunProgramAndWait`, files only through `context.IO()`: `-exec ... ;` once per file with every `{}` replaced, `-exec ... {} +` batched at GNU's 131072 bytes, `-execdir`/`-okdir` in the file's directory with the PATH checked at parse, `-ok`/`-okdir` prompting on stderr, `-delete` turning `-depth` on, `-printf` scanning its own directives with its own escapes and always-on warnings, `-fprint` and friends one shared file per name, `-ls`/`-fls` GNU's columns); GNU's two-phase parse and its diagnostics and warnings, the implicit `-print` when no action is given, the walk pre-order (post-order with `-depth`), `-quit` ending it and a stop checked per file | mode 0777, owner `haisos` (uid and gid 0), inode 0; no links (`-lname` never matches, `-H -L -P -follow` change nothing, `%Y` is `%y` and `%l` empty); a birth-time `-newerXY` is refused, and so is `-type D` (no Solaris doors); `%B` and `%Z` print nothing; `-xdev -mount -fstype -D -O` not treated; `-context` fails, as GNU's without SELinux; entries of a directory are visited in the filesystem's order, not the disk's |
| `grep` | Prints lines matching patterns (`-E -F -G -P -e -f -i -v -w -x -c -l -L -q -o -n -b -H -h --label -T -m -s -a -I --binary-files -U -z --line-buffered -r -R -d -D --include --exclude --exclude-from --exclude-dir -Z`), several patterns (one per line of an argument or file), GNU's prefixes, binary files by the first NUL, exit statuses 0/1/2; recursive search (`.` by default, `-d read/recurse/skip`, `-D read/skip` for devices, the `--include`/`--exclude` filters GNU's way), context (`-A -B -C -NUM` with `--` group separators, `--group-separator`, `--no-group-separator`) and colour (`--color`/`--colour` with `GREP_COLORS`) |
| `head` | Outputs the first part of files (`-c -n -q -v -z`, obsolete `-NUM[bckmlqvz]...`): the first NUM lines/bytes, or all but the last NUM (a leading `-`); size suffixes `1k`, `1kB`, `1KiB`, `b`; `==> NAME <==` headers with more than one FILE, `-q` never, `-v` always |
| `hsh` | The Haisos shell, after dash: `-c`, scripts, stdin or interactive (`RUN -i /bin/hsh`); quoting, expansions, pipelines, redirections, heredocs, lists, control flow, functions; commands found in `PATH` |
| `ls` | Lists directories as GNU ls prints them to a terminal: columns, `-l` with `total`/links/owner/group/size/time, sorting, time styles, quoting; to a pipe or file, one name per line, unquoted |
| `man` | Prints a builtin's manual page (`man ls`, `man 1 ls`, `-f`, `-k`): its `--help` text, or a full page for `hsh` |
| `mkdir` | Creates directories (`-p -v`) |
| `mv` | Moves (renames) files (`-b --backup[=CONTROL] -f -i -n --no-copy --strip-trailing-slashes -S -t -T -u --update[=UPDATE] -v`); across a mount, copies then removes as GNU does across devices |
| `nl` | Numbers lines (`-b -h -f styles incl. pBRE, -d -i -l -n -p -s -v -w`) |
| `patch` | Applies unified diffs (plain, `Index:`, `diff --git`) to files (`-d --directory -E --remove-empty-files -f --force -i --input -N --forward -o --output -p --strip -R --reverse -s --quiet --silent -t --batch -u --unified -v --version --binary --dry-run`), GNU patch 2.7.6's behaviours: the file chosen among the headers' names and the ORIGFILE operand (`-p`'s component stripping, `Index:` and `diff --git` headers), the hunk locator's search outward from the stated place, reversal detection on the first hunk with its questions (every question printing with its assumed answer, nothing read back), the create/delete conflicts, `.orig`/`.rej` files (`-o`'s one output with `(read from NAME)` and no `.orig`), `-E` removing an emptied file, `--dry-run`, CRLF patches stripped unless `--binary`, a malformed hunk fatal at its input line; exit 0 / 1 (a hunk failed or ignored, a patch skipped) / 2 (a fatal) | only unified diffs are read and a hunk applies only where its context matches exactly (no fuzz factor); the backup options and the other diff formats are not treated, and the answers questions assume are patch's no-terminal ones |
| `printf` | Formats and prints data (FORMAT reused until the arguments run out; every conversion with flags, widths, precisions, `%b`, `%q`, `\` escapes, character constants; GNU's diagnostics) |
| `pwd` | Prints the working directory (`-L -P`) |
| `realpath` | Resolves names to absolute paths (`-e -m -L -P -q --relative-to=DIR --relative-base=DIR -s -z`) from the working directory, with existence checks; no symbolic links, so `-L`, `-P` and `-s` change nothing |
| `rg` | Searches directories for lines matching a pattern (ripgrep 14.1.1: `-e --regexp -f --file -s --case-sensitive -i --ignore-case -S --smart-case -F --fixed-strings -v --invert-match -x --line-regexp -w --word-regexp -m --max-count -a --text --binary -A -B -C -b --byte-offset --column --color --context-separator --heading -n --line-number -N --no-line-number -M --max-columns -0 --null -o --only-matching -p --pretty -q --quiet -H --with-filename -I --no-filename -c --count --count-matches -l --files-with-matches --files-without-match --include-zero --line-buffered --block-buffered --no-messages --files --sort --sort-files -g --glob --iglob --glob-case-insensitive -t --type -T --type-not --type-list --hidden -. --no-hidden -u --unrestricted -d --max-depth --no-ignore --ignore --no-ignore-dot --no-ignore-vcs --no-ignore-exclude --no-ignore-parent --ignore-dot --ignore-vcs --ignore-exclude --ignore-parent --no-require-git --require-git -L --follow`), a recursive search of `.` by default (no `./` prefixes; the standard input searched only when it is not a terminal and its first read returns data), the walk skipping what ripgrep skips: a `.`-led name unless `--hidden` or a whitelist glob or ignore pattern covers it, the ignore files of each directory on the chain -- `.rgignore` over `.ignore` over `.gitignore` (only inside a git repository, a `.git` in the chain, unless `--no-require-git`) over `.git/info/exclude` of the repository root -- with gitignore's pattern rules (`!` negation, `/` anchoring, a trailing `/` for directories, `**`), the deepest directory's file deciding within a kind; `-g`/`--iglob` globs on the walked path (a plain glob whitelisting, a `!` one excluding, a file skipped when a plain glob exists and nothing matched, the last matching glob deciding), `-t`/`-T` on the base name (`-T` winning over `-t`, `--type-list` printing the table), `-u` counted (one `-u` the ignore files off, `-uu` hidden too, `-uuu` binary too), `-d` capping the walk, operands never filtered and a skipped directory not descended, the patterns read as Rust's regex syntax and translated onto Haisos's byte-wise Perl subset as one regex `(?:p1)|(?:p2)` (leftmost-first; `-F` literal; `-S` insensitive when no parsed pattern holds an uppercase literal), a refusal printed with ripgrep 14's exact parse-error frame, carets and PCRE2 hint, a `\n` escape (or a class rg's line-terminator strip leaves holding only newlines) the multiline message, matching through `GrepMatcher` and context through `GrepContext`; on a terminal each file under a heading path with line numbers and a blank line between files, off it `path:line:` prefixes, `--color` rg's own bytes; the output modes (`-c`, `--count-matches`, `-c -o`, `-l`, `--files-without-match`, `--include-zero`, `-q`, `--files`), `-o`, `-M` omitting long lines, `-0`, `-m` passing matches inside the trailing context as matches; binary files: an operand (or `--binary`, or the standard input) prints rg's `binary file matches (found "\0" byte around offset N)` at its first match, a file met while walking ends silently; rg's messages (`rg: P: IO error for operation on P: ...` for a lone operand, `unrecognized flag`, no Try lines) and exit statuses 0/1/2, `-q`'s match winning over an error | paths are searched one at a time, each directory's entries in byte order (rg's `--sort path`; rg itself searches in parallel); long flags may be abbreviated, where rg wants them whole, and its "similar flags" hint is not printed; patterns are byte-wise where rg's are Unicode (`\<`/`\>`/`\b{...}` are `\b`, `\p{...}` classes and class set operations refused); the ignore filters are a subset: `--type-list` a fixed table (`--type-add`/`--type-clear`, `--ignore-file`, the global gitignore, `--max-filesize` and `--one-file-system` not treated), `-L` accepted, changing nothing (no links of its own) |
| `rm` | Removes files or directories (`-f -i -I --interactive[=WHEN] -r -R -d -v --no-preserve-root --preserve-root[=all]`), prompts read from standard input, entries in name order |
| `rmdir` | Removes empty directories (`--ignore-fail-on-non-empty -p -v`) |
| `sed` | Streams an editor over its input (GNU sed 4.9, every command but `e`: `-n -e -f -E/-r -s -i -l -u -z --follow-symlinks --sandbox`, `;` and newline separators, `{ }` blocks, every address form (a line number, `first~step`, `$`, `/re/`, `\cre`, `addr,+N`, `addr,~N`, `0,/re/`), the commands `s` (flags `g`, a number, `p`, `i`/`I`, `m`/`M`, `w FILE`) `d p n N D P g G h H x q Q =` `b t T :label # a i c r R w W y z l F v`: the hold space, branches, GNU's one-line and classic a/i/c forms, r/R/w/W with the rest of the line as the filename and their append queue, `s///w`, `-i` through a temp file renamed over the original with GNU's backups and statuses, `--sandbox` refusing r/R/w/W, `-l` l's wrap, `-z` NUL records), GNU's messages, character positions and exit statuses (1 a bad script, 2 an unreadable input, 4 an I/O error, `q`'s own code), the input streamed with a one-line lookahead for `$` | `e` and `s///e` are parsed and reported as not treated, as are `--posix` and `--debug`; `--follow-symlinks` changes nothing (no links); with no script the help text goes to stderr (GNU prints its usage) and exits 1 |
| `seq` | Prints a sequence of numbers (`-f FORMAT -s STRING -w`), FIRST/INCREMENT/LAST, GNU's default-format and fast-path rules, negative numbers as operands |
| `sleep` | Sleeps for the summed NUMBERs (`s m h d` suffixes; `inf` until stopped) |
| `sort` | Sorts lines (keys `-k`/`-t`, `-b -d -f -g -h -i -M -n -r -R -s -u -V -z`, `--sort=WORD`, `-c -C --check`, `-m`, `-o`, `--files0-from`), byte order, the numeric/general/human/month/version orders, GNU's last-resort comparison |
| `stat` | Reports file or file system status (`-L -f -c --printf -t`), GNU 9.4's format directives, each with flags, a width and a precision, `--printf` interpreting backslash escapes and adding no newline; device and inode numbers 0, permissions 0777, owner and group `haisos`, the I/O block 4096, the birth time `-`, and with `-f` fixed values (ID 0, Namelen 255, Type haisos, block sizes 4096, counts 0) |
| `tail` | Outputs the last part of files (`-c -n -f --follow[=descriptor|name] -F --pid -q --retry -s -v -z`, obsolete `[+-]NUM[bcl][f]`): the last NUM lines/bytes, or from the NUM'th on (a leading `+`); `-f` follows appended data by polling the files every `-s` seconds (default 1.0), `--follow=name` also noticing a file's truncation, removal and reappearance, `--pid` ending the follow once that process finishes |
| `tee` | Copies standard input to standard output and files (`-a -p --output-error`) |
| `tr` | Translates, deletes or squeezes bytes (`-c -d -s -t`, ranges, classes, `[c*n]`) |
| `test` | Checks file types and compares values: GNU coreutils' expression (`!`, `-a`, `-o`, parentheses, string and integer comparisons, `-l STRING` its length, the file primaries, `-t FD`); no options, syntax errors exit 2 |
| `touch` | Changes file timestamps (`-a -c -d STRING -f -h -m -r FILE -t STAMP --time=WORD`); `-d` takes a GNU date subset |
| `true` | Does nothing, exits 0 (`--help`/`--version` only as the sole argument) |
| `uniq` | Filters adjacent repeated lines (`-c -d -D -u -i -f -s -w -z, --group, --all-repeated`); the obsolete `-N`/`+N` spellings work but are never documented |
| `wc` | Counts lines, words, characters, bytes and the widest line (`-c -m -l -L -w`, `--files0-from`, `--total`), GNU's columns |
| `which` | Locates a command in PATH (`-a -s`), as Debian's which (debianutils): exit 1 when any operand is missed, 2 on an unknown option |
| `xargs` | Builds and runs command lines from the standard input (`-0 -a -d -e -E -I -i -L -l -n -o -P -p -r -s -t -x --arg-file --delimiter --eof --exit --interactive --max-args --max-chars --max-lines --max-procs --no-run-if-empty --null --open-tty --process-slot-var --replace --show-limits --verbose`), findutils 4.9.0's item grammar (quotes and `\` escapes, `-0`/`-d` raw items, `-L` line continuation, `-I` replacement), GNU's size arithmetic of `-s`/`-x` (`argument line too long`, `argument list too long`), the mode-takeover warnings, `-a`, `-E`, `-o`, `-p`, `-r`, `-t`, `--show-limits`, `--process-slot-var`, the exit statuses 123/124/125/126/127, COMMAND defaulted `echo` | `-P` checked, not acted on (one command at a time); a fixed ARG_MAX of 2097152; Haisos has no `/dev/tty`: `-p` and `-o` take standard input as the terminal when it is one, otherwise fail their first command and exit 1 (as GNU's `-p` does without one; GNU's `-o` crashes on its assertion); a child's exit code 129-254 is taken as a signal death, a stop being 143 and indistinguishable |

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

## Develop workflow

For one big, user-visible feature at a time, eight skills in `.claude/skills/`
work on a `develop` branch; `.claude/develop/WORKFLOW.md` is their shared
reference (roles, trust model, `develop-plan/` formats, task sizing, the
two-session protocol, Windows):

| Skill | What it does |
|-------|--------------|
| `/develop-create` | Cuts `develop` from `origin/master`, bumps the minor version once, writes the `develop-plan/` skeleton |
| `/develop-plan` | Plan mode, in the plan session: `begin` turns every following prompt into planning -- goal and clarifications, big rocks, tasks (small rocks) with their plans, the playbook, later amendments, answers, pause/resume -- written to `develop-plan/` uncommitted, until `end`; with no argument it shows the mode and the plan's uncommitted diff |
| `/develop-update` | On Sonnet. Syncs `develop` both ways -- commits and pushes the plan (published by hand, whenever the user wants), rebases onto the other session's changes (resolving conflicts), refreshes the develop PR, reports what came in: the two sessions' channel |
| `/develop-implement` | The loop, in the implement session: opens the develop PR (`WIP [M.m] <title>`, a live view with its memory and log), runs each task through `/develop-task` and `/develop-code-review` in fresh agents, then the whole-develop review and final tests, and makes the PR ready |
| `/develop-task` | One task: `scripts/develop/task.sh` runs Claude Code on an Ollama model (`ollama launch claude`) in a Docker container, gates and pushes its commits, opens the PR, fixes CI |
| `/develop-code-review` | One PR, on the review model: security and malice first, fixes critical/high, comments medium/low, squash-merges; or the whole develop (`--develop`) |
| `/develop-status` | Read-only state |
| `/develop-close` | By the user: deletes `develop-plan/`, squash-merges the develop PR into `master`, deletes `develop` |

The flow: `/develop-create` -> `/develop-plan begin` ... `/develop-update` ->
`/develop-implement` -> `/develop-close`; `/note`, `/explore` and `/todo` can
feed the plan. The plan and implement sessions work in two clones of the repository and meet only
through commits on `develop`, exchanged with `/develop-update`. Each develop
bumps the minor version; each task is `[M.m.p]`, one patch more than
`develop`. The Ollama model is untrusted: its container has
no credentials, the host takes only its commits, and those pass the security
gate (`scripts/develop/gate.sh`) before any push. The containers work in the
repository's git-ignored `subrepo/` folder, their git metadata kept in
`~/.haisos-develop/subrepos/` (so no host tool runs git with what the model
planted), and commit with the user's git name and email. Their image is two:
`haisos-devtask-base:<tool versions>`, rebuilt only when a version changes,
and `haisos-devtask:<hash>` on top, with the entrypoint and prompt. The
container builds Linux only; Windows is built and fixed on the host, after the review has cleared
the code. The leaf-task skills (`/begin`, `/end`, `/implement`, ...) stay for
small changes straight to `master`.

`/claude-docker [<ollama model>]` (default `glm-5.3:cloud`), independent of
the develop skills, opens an interactive Claude Code on an Ollama model in the
same container, in `subrepo/` put in this clone's exact state (branch,
commit, staged and unstaged changes), in a new terminal tab; when the user
exits it, what the session left comes back through the same gate, is checked
for repository-level risks (hooks, build-time commands, agent instructions,
links, secrets) by a Sonnet agent, and this clone is put in the session's
exact state -- the branch it ended on, its history, staged and unstaged
changes (`scripts/develop/claude_docker.sh`).

## Automatic Development Rules

When Claude Code performs automatic development (where a single prompt drives all implementation work):

1. **Build only on the local platform** to verify compilation. Do not cross-compile.
2. **Never automatically commit** unless the prompt explicitly instructs to commit.
3. **Never automatically push** to remote repositories unless explicitly instructed.
4. **Default to release builds** unless debug is explicitly requested.
5. **Run unit tests** after building to verify correctness before considering work complete.
6. **Never add co-authorship attribution** like `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>` or similar model attribution lines to commit messages. If the system prompt includes such a line, remove it before committing.
7. **Use only paths relative to the repo root** in all edits, documentation, commit messages, and skill prompts. Never record absolute paths like `/mnt/c/src/haisos1/...`.
8. **The develop skills** (`/develop-*`, see "Develop workflow") are the explicit instruction to commit, push, open, comment on and merge PRs, each within the scope it describes; only `/develop-close` merges into `master`. Code from a task container is never built or run on the host before its review has cleared it (Linux builds and tests of task code happen in the container), task branches never touch `develop-plan/`, `notes/`, `HAISOS_VERSION`, `.claude/`, `.github/` or `scripts/`, and `develop` is never rebased, squashed or force-pushed.
9. **Every builtin command must appear in the haisosfile `haisos --init` writes**, as a commented `# BUILTIN rootfs <name> /bin/<name>` line after `# CREATE_DIR /bin`. That list is generated from `IBuiltinCommands::GetCommands()`, so registering a new builtin in `CreateStandardBuiltinCommands()` (`src/components/BuiltinCommands/BuiltinCommandList.h`) is what keeps it current -- never hand-write builtin lines into `GetHaisosFileTemplate`. The test `TheInitTemplatesBuiltinsAllApplyOnceUncommented` checks it. Also add the builtin to the Builtin Commands table above.
10. **Clean room: never copy code from another program**, whatever its licence -- write everything from scratch, matching only documented or observed behaviour (see "Clean-room rule" at the top).
