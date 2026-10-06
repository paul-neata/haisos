# Task hsh--executor: hsh as a builtin -- invocation, simple commands, lists

- Rock: hsh
- Depends on: hsh--expansion, pipes--broken-pipe (and through them hsh--parser, hsh--arith-glob, streams--exit-codes, builtins--directories)
- Size: ~1000 changed lines in ~14 files (split: redirections, pipelines, the shell builtins, control flow and the interactive mode are the next five hsh tasks)
- Plan checked against: develop @ 8fb8324
- PR title: hsh: register the shell, run simple commands and lists

(The second half of the hsh rock came out at ~5000 lines, so it is six tasks:
hsh--executor, hsh--redirections, hsh--pipelines, hsh--shell-builtins,
hsh--control-flow, hsh--interactive. This one makes `hsh` a builtin that
runs simple commands and `;` `&&` `||` lists; the others add the rest.)

## Goal

`hsh`, the Haisos shell (dash reimplemented as a builtin), becomes a builtin
command placed like any other (`BUILTIN rootfs hsh /bin/hsh`) and listed by
`haisos --init`. Afterwards:

- `hsh -c 'string' [name [args]]`, `hsh script [args]`, `hsh -s [args]` and
  `hsh` with commands on its standard input run shell programs, with dash's
  invocation options (`-aCefinsux`, `+...`, `-o name`, `+o name`) -- treated or
  reported as not treated -- and dash's invocation errors.
- Simple commands run: assignments (`x=1`), expansions (everything
  hsh--expansion does except `$(...)`, which hsh--pipelines adds), commands
  found in `PATH` (dash's default when the environment has none) or given by a
  path with a `/`, started as Haisos processes (builtins, `.md` agents, `.lua`
  scripts) through `ICurrentProcess::OS()`, with an environment of the
  exported variables plus the command's prefix assignments, the shell's
  working directory and the shell's descriptors 0/1/2 as stdin/stdout/stderr.
  `$?` is the child's `ExitCode()`.
- dash's messages: `hsh: 1: nosuch: not found` (127), `hsh: 1: /docs: Permission denied` (126),
  syntax errors (`hsh: 2: Syntax error: "fi" unexpected`, status 2), expansion
  errors (fatal, 2).
- Lists `;`, `&&`, `||` and `!` on a single command; the builtins `:`,
  `true`, `false`, `exit`.
- A shell asked to stop (`TriggerStop`) stops its running child and itself
  (exit code 143); a write of its own that finds a broken pipe ends it quietly
  with 141.
- Not yet: redirections, multi-command pipelines, `&`, `$(...)`, compound
  commands and functions. Each reports `hsh: <line>: <what> is not supported
  yet` with status 2 until its task lands (see "Placeholders").

## Context

Read first:

- the root `CLAUDE.md`: "Security: `ICurrentProcess` is the only door out of a
  process", "Creating things" (especially "Nothing that waits for a runtime
  thread is destroyed on one"), "Builtin Commands", rule 9 of "Automatic
  Development Rules";
- `src/components/BuiltinCommands/CLAUDE.md` and
  `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`;
- `develop-plan/goal.md`: the hsh, exit-code and `RUN` clarifications and
  "## Contracts";
- the dash manual (https://man7.org/linux/man-pages/man1/dash.1.html):
  "Invocation", "Argument List Processing", "Simple Commands", "Search and
  Execution", "Lists", "Exit Status", "Builtins" (`:`, `true`, `false`,
  `exit`). dash is installed on most Linux machines; every message below was
  checked against dash 0.5.12, check more with `dash -c '...'`.

What earlier tasks provide, as if already on develop (their plans are in
`develop-plan/tasks/`; if the code names anything differently, the code
wins -- use its name and keep this plan's behaviour):

- hsh--lexer, hsh--parser, hsh--arith-glob, hsh--expansion (namespace
  `Haisos::Hsh`, in `src/components/BuiltinCommands/commands/hsh/`):
  `ShellError(message, line = 0, incomplete = false)` with `Line()`,
  `Incomplete()`, and `FormatShellError(shellName, line, message)` ->
  `"<shellName>: <line>: <message>\n"` (`HshError.h`); `Word`, `LiteralText`,
  `IsValidShellName` (`HshWord.h`); the AST (`HshAst.h`): `CommandList { items }`,
  `ListItem { andOr, background }`, `AndOrList { pipelines, operators }`,
  `AndOrOperator { And, Or }`, `Pipeline { negated, commands, line }`,
  `Command { kind, line, redirections }`, `CommandKind`, `SimpleCommand {
  assignments, words }`, `Assignment { name, value }`, `Redirection`;
  `Parser(source, ParserOptions{firstLine, interactive, endOfInputName})`,
  `ParseResult Parser::ParseNext()` with `Status { Command, EndOfInput, Error }`,
  `commands`, `errorMessage`, `errorLine`, `incomplete`; `ParseProgram(source, options)`
  (`HshParser.h`); `IPathnameSource { ReadDirectory(path), Exists(path) }`
  (`HshGlob.h`); `ShellVariables` (`ImportFrom`, `Get`, `IsSet`, `Set`,
  `Unset`, `Export`, `IsExported`, `MakeReadonly`, `IsReadonly`, `Names`,
  `ExportedVariables`), `ShellOptions { errexit, noglob, interactive, noexec,
  stdinInput, xtrace, verbose, noclobber, allexport, nounset }`,
  `OptionLetters(options)`, `ShellState { variables, options, arg0, positional,
  lastExitStatus, shellPid, lastBackgroundPid }` (`HshVariables.h`);
  `CommandSubstitutionResult { output, exitStatus }`, `IExpansionHost :
  IPathnameSource { RunCommandSubstitution(source, line) }`, `Expander(ShellState&,
  IExpansionHost&)` with `ExpandWords`, `ExpandWord`, `ExpandAssignmentValue`,
  `ExpandToString`, `ExpandPattern`, `ExpandHereDocument` (`HshExpansion.h`).
  The `Hsh.unittests` executable (`tests/unit/components/Hsh.unittests/`,
  filter `Hsh`, every suite name starts with `Hsh`).
- builtins--directories: builtins live in `commands/<name>/`;
  `IBuiltinCommand::ManPage()` (default: the `--help` text);
  `BuiltinContext::ErrorText`, `OutIsTerminal`; the test fixture header
  `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h`
  (fixture class `BuiltinCommandsTest` with `root`, `os`, `builtins`,
  `factory`, `WriteFile`, `Run`, and `RunCaptured(command, args, input,
  workingDirectory) -> Captured { out, err, status }`, which runs `/bin/<command>`
  with stdout/stderr on files and stdin on a file holding `input`).
- fd--process-table: the `IFileIO` descriptor table -- `GetDescriptor(int)`,
  `AddDescriptor`, `Dup`, `Dup2`, `CloseDescriptor`, `IFileIO::kStdIn` /
  `kStdOut` / `kStdErr` / `kMaxDescriptors`; `IFileIO::OpenFile` returns a
  `std::shared_ptr<IFileDescriptor>` (null on failure); `ReadWholeDescriptor(IFileDescriptor&, std::string&)`
  in `src/components/Filesystem/FilesystemUtils.h`.
- streams--console-and-start: `StartProcessOptions { stdIn, stdOut, stdErr,
  interactive }` (null streams mean the console defaults); `BuiltinContext`
  writes `Out` to slot 1 and `Error`/`NotTreated` to slot 2, and has
  `Process()` (the `ICurrentProcess&`), `IO()`, `Name()`, `Version()`,
  `Args()`, `StopRequested()`.
- streams--exit-codes: `IProcess::ExitCode() -> std::optional<int>`;
  `src/components/libheaders/ExitCodes.h` (`kExitCodeStopped` 143,
  `kExitCodeBrokenPipe` 141, `kExitCodeNotStarted` 127).
- pipes--pipe-service / pipes--broken-pipe: `IFileDescriptor::Write` returns
  `kIOBrokenPipe` when nobody reads, `kIOInterrupted` when the caller's process
  was asked to stop while blocked; `ICurrentProcess::StopForBrokenPipe()`;
  `IHaisosOS::GetPipeService()` (tests only here).

The code this task touches today: `BuiltinCommand.h/.cpp` (`BuiltinHelp`,
`BuiltinHelpText`), `BuiltinCommandList.h`, `BuiltinCommands/CMakeLists.txt`,
`tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`
(`ListsEveryBuiltinSortedWithAVersion`, `EveryBuiltinsHelpHasTheSameShape`).

## Changes

Everything new is in namespace `Haisos::Hsh` (except `CreateHshCommand`, in
`Haisos`), in `src/components/BuiltinCommands/commands/hsh/`. Plain portable
C++17: no POSIX headers (it is built for Windows/MSVC and WASM too).

### Root rules that bite (restated)

- **`ICurrentProcess` is the only door.** The shell reaches files only through
  `context.IO()` (the process's `IFileIO`: open, stat, read directories, its
  descriptor table) and other processes only through `context.Process().OS()`
  -- asked for each time it is needed and released right after the call,
  never stored in a member (as the `os_*` tools do). No `IFileSystem`, no
  `IHaisosOS` member, no host filesystem.
- **Builtins:** every dash invocation option is in the option table, treated
  or reported (`Parameter -m is not treated by HaisosOS hsh v. 0.1.0`);
  `--help` is generated by `BuiltinHelpText`, never hand-written; `--version`
  is `BuiltinVersionText`. Version `0.1.0` (each later hsh task bumps the
  minor version; hsh--interactive makes it `1.0.0`).
- **Rule 9:** `hsh` is registered in `CreateStandardBuiltinCommands()`, which
  is what puts `# BUILTIN rootfs hsh /bin/hsh` into the `haisos --init`
  template. Never hand-write that line.
- **Threads:** the shell creates no thread and no object that owns one. It
  holds `std::shared_ptr<IProcess>` children and releases them on its own
  runtime thread, which is safe because every process class is created with
  the `DestroyOffRuntimeThreads` deleter -- do not change that, and do not keep
  an `IHaisosOS` past a call.
- **CMake:** every new `.cpp` is listed in
  `src/components/BuiltinCommands/CMakeLists.txt`; test files in
  `tests/unit/components/Hsh.unittests/CMakeLists.txt`.

### `src/components/BuiltinCommands/BuiltinCommand.h` / `.cpp` -- the reference exception

`hsh` copies dash, so its help must point at dash's page. Add to
`struct BuiltinHelp`, after `notes`:

```cpp
// The real command this builtin copies, when its name differs from the
// builtin's own: "dash" for hsh. Empty: the builtin's own name. The "Based
// on Linux <command>: <url>" line of --help names it and links its page.
std::string basedOn;
```

`BuiltinHelpText`: the second line becomes `Based on Linux <real>: ` +
`BuiltinReferenceUrl(<real>)`, `<real>` being `help.basedOn` when it is not
empty, else the builtin's name. Nothing else changes (every other builtin's
help is byte for byte the same).

### `commands/hsh/Hsh.cpp` (new) -- the builtin

`class HshCommand : public IBuiltinCommand` in an anonymous namespace, and
`std::shared_ptr<IBuiltinCommand> CreateHshCommand()` in namespace `Haisos`.

- `Name()` `"hsh"`, `Version()` `"0.1.0"`. `ManPage()` not overridden yet
  (hsh--interactive does).
- `Help()`: summary `command interpreter (shell)`; `basedOn` `"dash"`; usage
  (three forms, as dash's synopsis):
  ```
  hsh [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name] [+o option_name] [command_file [argument ...]]
  hsh -c [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name] [+o option_name] command_string [command_name [argument ...]]
  hsh -s [-aCefnuvxIimVEbp] [+aCefnuvxIimVEbp] [-o option_name] [+o option_name] [argument ...]
  ```
  notes (two lines; later tasks may add a sentence, never a third paragraph):
  ```
  Commands not built into hsh are looked up in PATH and started as Haisos processes (builtins, .md agents, .lua scripts).
  --help and --version only as the first argument; $0 is "hsh" unless a script or -c command_name names it; +X turns option X off.
  ```
- `Options()`: one row per dash option letter, short names only (dash has no
  `--long` options; `-o NAME` is the long form):

  | short | argument | treated | description |
  |-------|----------|---------|-------------|
  | `a` | | T | `allexport: export every variable assigned` |
  | `b` | | - | |
  | `c` | | T | `read commands from the command_string operand` |
  | `C` | | T | `noclobber: > does not overwrite an existing file` |
  | `e` | | T | `errexit: exit when an untested command fails` |
  | `E` | | - | |
  | `f` | | T | `noglob: no pathname expansion` |
  | `i` | | T | `interactive: prompt for commands` |
  | `I` | | - | |
  | `l` | | - | |
  | `m` | | - | |
  | `n` | | T | `noexec: read commands without running them` |
  | `o` | Required `NAME` | T | `turn on the option NAME (+o: off)` |
  | `p` | | - | |
  | `s` | | T | `stdin: read commands from standard input` |
  | `u` | | T | `nounset: expanding an unset variable is an error` |
  | `v` | | - | |
  | `V` | | - | |
  | `x` | | T | `xtrace: show each command before running it` |

  Treated rows get distinct ids (any positive numbers); the table is used
  only for `--help` and by the generic builtin tests -- `Run` parses the
  arguments itself (below), because dash's syntax (`+x`, `-o name`, options
  ending at the first operand) is not `getopt_long`'s.
- `Run(context)`:
  1. If the first argument is exactly `--help`: `context.Out(BuiltinHelpText(*this))`,
     return 0; exactly `--version`: `context.Out(BuiltinVersionText(*this))`,
     return 0. Anywhere else they are ordinary arguments, as for dash.
  2. `Invocation invocation = ParseInvocation(context.Args())`.
  3. Each spelling in `invocation.notTreated`, in order:
     `context.NotTreated(spelling)`.
  4. `invocation.error` not empty: `context.ErrorText(FormatShellError("hsh", 0, invocation.error))`,
     return 2.
  5. `Shell shell(context, std::move(invocation)); return shell.Run();`

### `commands/hsh/HshInvocation.h` / `.cpp` (new)

```cpp
// One of dash's options: its letter (0 when it has only a name), its -o name,
// and the ShellOptions field it sets -- null when hsh does not act on it
// (it is then reported as not treated, and not stored).
struct ShellOptionInfo {
    char letter;
    const char* name;
    bool ShellOptions::* field;
};
// dash's options, in the order `set -o` lists them: errexit e, noglob f,
// ignoreeof I, interactive i, monitor m, noexec n, stdin s, xtrace x,
// verbose v, vi V, emacs E, noclobber C, allexport a, notify b, nounset u,
// privileged p, nolog (no letter), debug (no letter). Treated (field set):
// e f i n s x C a u. Not treated: I m v V E b p nolog debug.
const std::vector<ShellOptionInfo>& ShellOptionTable();
const ShellOptionInfo* FindShellOption(char letter);               // null if none
const ShellOptionInfo* FindShellOption(const std::string& name);   // null if none

// What `hsh` was asked to run.
struct Invocation {
    enum class Source { CommandString, ScriptFile, StandardInput };
    Source source = Source::StandardInput;
    std::string commandString;            // -c
    std::string scriptPath;               // the command_file operand
    std::string arg0 = "hsh";             // $0, and what messages start with
    std::vector<std::string> positional;  // $1 ...
    ShellOptions options;
    std::vector<std::string> notTreated;  // "-m", "+V", "-o monitor", "-l": to report
    std::string error;                    // dash's message after "hsh: 0: ", e.g. "Illegal option -y"
};
Invocation ParseInvocation(const std::vector<std::string>& args);
```

`ParseInvocation`, dash's `procargs` (the arguments after the command name):

1. Walk the arguments while one starts with `-` or `+`:
   - `--` or a lone `-`: consumed; the options end.
   - A lone `+`: not an option; the options end (it is an operand).
   - Starts with `--` and is longer: error `Illegal option --`.
   - Otherwise each letter after the sign, in order, `on` = sign is `-`:
     - `c`: the command-string flag (`+c` sets it too, as dash does);
     - `s`: sets `options.stdinInput` (on/off);
     - `o`: takes the **next argument** as an option name (missing: error
       `-o requires an argument`); `FindShellOption(name)` null: error
       `Illegal option -o <name>` (with `+` for `+o`); not treated: append
       `-o <name>` / `+o <name>` to `notTreated`; else set its field to `on`;
     - `l`: not treated (append `-l`);
     - any other letter: `FindShellOption(letter)`; null: error
       `Illegal option -<letter>` (`+<letter>` with `+`); not treated: append
       `-<letter>` / `+<letter>`; else set the field to `on`.
   Stop at the first error.
2. After the options:
   - command-string flag: no argument left -> error `-c requires an argument`;
     else `commandString` = the next argument; if one follows, it is `arg0`;
     the rest are `positional`; `source` = CommandString.
   - else `stdinInput` set, or no argument left: `source` = StandardInput,
     every remaining argument is positional, `options.stdinInput` = true.
   - else `scriptPath` = the next argument, `arg0` = it too, the rest
     positional; `source` = ScriptFile.

Examples (dash): `-c 'echo $0 $1' nm a` -> arg0 `nm`, positional `{a}`;
`-ec 'echo $-'` -> errexit on, command string; `- -c x` -> script `-c`;
`-c x -- a` -> arg0 `--`, positional `{a}`; `+o errexit -e` -> errexit on;
`-y` -> `Illegal option -y`; `-o nosuch` -> `Illegal option -o nosuch`;
`--help` (not first) or `--frob` -> `Illegal option --`.

### `commands/hsh/HshShell.h` / `.cpp` (new) -- the executor

```cpp
// `exit`, `return` at the top level, and the end of a subshell: unwinds to
// Shell::Run (or to the subshell boundary, hsh--pipelines) with the status.
struct ShellExit { int status; };
// The shell was asked to stop (TriggerStop), or broke a pipe with its own
// output: unwinds to Shell::Run past everything, subshell boundaries included.
struct ShellStopped {};

// Why IFileIO::OpenFile failed, in dash's words, worked out with Stat:
// reading: "No such file" (nothing there), "Is a directory", else
// "Permission denied"; creating: "Is a directory", "Directory nonexistent"
// (the parent of io.ResolvePath(path) is missing or not a directory), else
// "Permission denied".
std::string OpenFailureReason(IFileIO& io, const std::string& path, bool creating);

class Shell : public IExpansionHost {
public:
    Shell(BuiltinContext& context, Invocation invocation);
    ~Shell() override;
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    // Runs the -c string, the script file or standard input to its end and
    // returns the exit status (0-255).
    int Run();

    // --- Running commands (later tasks fill in the kinds marked "not yet") ---
    int ExecuteList(const CommandList& list);
    int ExecuteAndOr(const AndOrList& andOr);
    int ExecutePipeline(const Pipeline& pipeline);
    int ExecuteCommand(const Command& command);
    int ExecuteSimpleCommand(const SimpleCommand& command);

    // --- What builtins and later tasks use ---
    BuiltinContext& Context();
    ICurrentProcess& Process();
    IFileIO& IO();
    ShellState& State();
    Expander& Expansion();
    int CurrentLine() const;
    // The shell's own output: written to whatever its slot 1 / slot 2 holds
    // now (after redirections), one Write per call, partial writes looped.
    void WriteOut(const std::string& bytes);
    void WriteErr(const std::string& bytes);
    // A diagnostic: FormatShellError(State().arg0, CurrentLine(), message) to WriteErr.
    void Report(const std::string& message);
    // A fatal error: throws ShellError(message, CurrentLine()).
    [[noreturn]] void Fail(const std::string& message);
    // Throws ShellStopped once the shell has been asked to stop.
    void ThrowIfStopRequested();

    // --- Children ---
    struct CommandLookup {
        enum class Result { Found, NotFound, NotRunnable };
        Result result = Result::NotFound;
        std::string path;  // Found: the absolute path to start
    };
    CommandLookup LookUpCommand(const std::string& name);
    // A clone of the process's environment (secrets and LLM identifiers kept)
    // with every variable removed, then every exported shell variable set,
    // then |assignments| set.
    std::shared_ptr<IEnvironment> ChildEnvironment(
        const std::vector<std::pair<std::string, std::string>>& assignments);
    // Starts |path| (absolute) with |args| (argv[1] on) as a child: the
    // environment above, the shell's working directory, and slots 0/1/2 as
    // its stdin/stdout/stderr. Null when there is no OS or it refused.
    std::shared_ptr<IProcess> StartChild(const std::string& path, const std::vector<std::string>& args,
        const std::vector<std::pair<std::string, std::string>>& assignments);
    // Waits for |child| and returns its exit code (kExitCodeStopped if it has
    // none). Passes a stop of the shell on to every live child; throws
    // ShellStopped after |child| has finished if the shell was asked to stop.
    int WaitForChild(const std::shared_ptr<IProcess>& child);
    // TriggerStop on every live child, then waits for each up to kStopGraceMs.
    void StopChildren();

    // --- IPathnameSource / IExpansionHost ---
    std::vector<DirectoryEntry> ReadDirectory(const std::string& path) override;  // IO().ReadDirectory
    bool Exists(const std::string& path) override;                                 // IO().Stat(...) == 0
    CommandSubstitutionResult RunCommandSubstitution(const std::string& source, int line) override;

private:
    // ... the implementer's choice, at least:
    // BuiltinContext& m_context; Invocation m_invocation; ShellState m_state;
    // Expander m_expander{m_state, *this}; int m_currentLine = 0;
    // uint64_t m_substitutionCount = 0;          // RunCommandSubstitution calls, for bare assignments
    // std::vector<std::shared_ptr<IProcess>> m_liveChildren;   // started, not yet waited for
    // int m_subshellDepth = 0;                   // hsh--pipelines; 0 here
    // bool m_brokenPipe = false;
};
```

Constants in `HshShell.cpp`: `kWaitSliceMs` = 50 (how long one
`WaitToFinish` call waits before the stop flag is looked at again),
`kStopGraceMs` = 5000. The default `PATH`:
`/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin` (dash's).

#### Startup (constructor)

1. `m_state.variables.ImportFrom(*Process().GetEnvironment())`.
2. As dash does: `IFS` = space, tab, newline (always, overriding the
   environment, not exported); `OPTIND` = `1`; `PPID` = `Process().GetParentPid()`;
   `PS1` = `$ `, `PS2` = `> `, `PS4` = `+ ` -- each only if not imported;
   `PATH` = the default if not imported (set, not exported); `PWD` =
   `IO().GetCurrentDirectory()`, exported.
3. `arg0`, `positional`, `options` from the invocation; `shellPid` =
   `Process().GetPid()`.

#### Run

1. The source: CommandString -> the string. ScriptFile ->
   `IO().OpenFile(path, kFileOpenReadOnly)` and `ReadWholeDescriptor`; on
   failure write `FormatShellError("hsh", 0, "cannot open <path>: " + OpenFailureReason(io, path, false))`
   to stderr and return 2 (dash: `dash: 0: cannot open /nonexist: No such file`).
   StandardInput -> read slot 0 in 4096-byte reads until 0; `kIOInterrupted`
   means the shell was stopped (return); any other negative result ends the
   input. (hsh--interactive reads an interactive stdin line by line; this task
   reads it whole, which is what dash's block reads amount to for commands
   reading the same stdin: they see nothing of the script.)
2. `Parser parser(source, {1, false, "end of file"})`; loop on `ParseNext()`:
   - `EndOfInput`: stop.
   - `Error`: `WriteErr(FormatShellError(arg0, errorLine, errorMessage))`,
     status 2, stop.
   - `Command`: `ExecuteList(commands)` inside a `try`:
     - `ShellExit e`: status `e.status & 0xFF`, stop;
     - `ShellError e`: `WriteErr(FormatShellError(arg0, e.Line() ? e.Line() : m_currentLine, e.what()))`,
       status 2, stop (a non-interactive shell exits on a fatal error; the
       interactive one, hsh--interactive, goes on);
     - `ShellStopped`: `StopChildren()`, return 143 (the process records 143,
       or 141 after `StopForBrokenPipe`, whatever `Run` returns).
   Stop requested between commands: the same as `ShellStopped`.
3. Return the last status (`m_state.lastExitStatus` unless a stop above set it).

#### Lists

- `ExecuteList`: each item in order; `item.background` -> placeholder `&`
  (hsh--pipelines); else `ExecuteAndOr`. Returns the last status.
- `ExecuteAndOr`: run `pipelines[0]`; for each operator, `And` runs the next
  pipeline only if the status is 0, `Or` only if it is not. Every pipeline run
  sets `m_state.lastExitStatus` before the next one is looked at.
- `ExecutePipeline`: more than one command -> placeholder `pipelines`
  (hsh--pipelines); else `ExecuteCommand(*commands[0])`; `negated` turns 0
  into 1 and anything else into 0. Sets `lastExitStatus`.
- `ExecuteCommand`: `Simple` -> `ExecuteSimpleCommand`; every other kind ->
  placeholder named after the kind: `{ }`, `( )`, `if`, `while`, `until`,
  `for`, `case`, `function definitions` (hsh--control-flow). Calls
  `ThrowIfStopRequested()` first.

#### Simple commands (`ExecuteSimpleCommand`)

1. `m_currentLine = command.line`.
2. Redirections present -> placeholder `redirections` (hsh--redirections).
3. Note `m_substitutionCount`; `fields = m_expander.ExpandWords(command.words)`.
4. **No fields** (`x=1`, `x=$(...)`): each assignment in order,
   `value = ExpandAssignmentValue(a.value)`, `variables.Set(name, value)`;
   a false result is fatal: `Fail("<name>: is read only")` (dash exits:
   `x: is read only`). Status: `lastExitStatus` if `m_substitutionCount`
   changed since step 3 (the last command substitution's status), else 0.
5. **A shell builtin** (`FindShellBuiltin(fields[0])`, below):
   - special: the assignments are applied to the shell as in step 4 (they
     stay, as POSIX says for special builtins: `x=1 :; echo $x` prints 1);
   - regular: they are applied for the call only -- remember each variable's
     old value (or that it was unset), set, run, put back (`x=1 true; echo "[$x]"`
     prints `[]`); a read-only one is fatal as in step 4.
   Then `return builtin->run(*this, fields)`.
6. **Anything else** is a child process:
   - expand the assignments into `(name, value)` pairs (a read-only name is
     fatal: `Fail("<name>: is read only")`, as dash for `x=2 /bin/true`);
   - `LookUpCommand(fields[0])`: `NotFound` -> `Report("<name>: not found")`,
     127; `NotRunnable` -> `Report("<name>: Permission denied")`, 126;
   - `StartChild(path, fields[1..], pairs)`; null -> `Report("<name>: Permission denied")`,
     126 (the OS refused it: not a program it can run, e.g. `/notes.txt`, or
     the process limit -- log which with `LogDebug`);
   - `return WaitForChild(child)`.
   `<name>` is `fields[0]` as given, never the resolved path.

#### Looking a command up (`LookUpCommand`)

- A name holding a `/`: `IO().Stat(name)` fails -> `NotFound`; a directory ->
  `NotRunnable`; else `Found` with `IO().ResolvePath(name)`.
- Otherwise `PATH` unset -> `NotFound` (dash: after `unset PATH` nothing is
  found). Split `PATH` at `:`; an empty entry means the working directory;
  the candidate is `<entry>/<name>` (no doubled `/` after `/`), or `<name>`
  for an empty entry. The first candidate whose `Stat` succeeds and is not a
  directory is `Found` (`ResolvePath` of it). None: `NotFound`.
- Builtins placed on the filesystem are files (`Stat` succeeds), so
  `/bin/ls` is found like any program; which runtime runs a path is the OS's
  business (`StartProcess`), not the shell's.

#### Children, waiting, stopping

- `StartChild`: `auto os = Process().OS()`; null -> null. Options: `stdIn` =
  `IO().GetDescriptor(0)`, `stdOut` = `(1)`, `stdErr` = `(2)`, `interactive`
  false. `os->StartProcess(ChildEnvironment(assignments), path, args,
  IO().GetCurrentDirectory(), options)`; release `os`. A started child is
  appended to `m_liveChildren`.
- `WaitForChild`: loop `child->WaitToFinish(kWaitSliceMs)` until true; on each
  pass, if `m_context.StopRequested()` and the stop was not yet passed on, call
  `TriggerStop()` on every live child once; after `kStopGraceMs` of waiting
  since the stop, give up waiting. Remove `child` from `m_liveChildren`.
  Then: stop requested -> throw `ShellStopped`. Return
  `child->ExitCode().value_or(kExitCodeStopped)`.
- `StopChildren`: `TriggerStop()` on each live child, then `WaitToFinish` up
  to `kStopGraceMs` each; clear the list.

#### The shell's own output, and broken pipes

`WriteOut`/`WriteErr` get the descriptor from the table at each call (null:
drop the bytes) and loop over partial writes. A negative result:
`kIOBrokenPipe` -> the shell ends like a program killed by SIGPIPE: at the
top level (`m_subshellDepth == 0`) `Process().StopForBrokenPipe()`, set
`m_brokenPipe` (every later write is dropped), throw `ShellStopped` (the exit
code becomes 141); hsh--pipelines adds the subshell case. `kIOInterrupted`
-> throw `ShellStopped`. Anything else -> drop. Never call them from a
destructor (they may throw).

#### `RunCommandSubstitution`

Placeholder in this task: `Fail("command substitution is not supported yet")`
(hsh--pipelines implements it). Increment `m_substitutionCount` first.

#### Placeholders

One private helper, `int NotYet(const std::string& what)`:
`Report(what + " is not supported yet")`, return 2. Used as described above
for `&`, `pipelines`, `redirections` and the compound kinds. Each later task
removes its uses; hsh--control-flow removes the helper. They exist only on
`develop` between tasks.

### `commands/hsh/HshBuiltins.h` / `.cpp` (new) -- the shell's own builtins

```cpp
// A builtin of the shell itself, run inside the shell. args[0] is its name.
using ShellBuiltinFunction = int (*)(Shell& shell, const std::vector<std::string>& args);
struct ShellBuiltin {
    const char* name;
    // POSIX special builtin: its errors are fatal (Shell::Fail) and the
    // prefix assignments of its command stay. Regular: errors are reported
    // (Shell::Report) with status 2, prefix assignments are temporary.
    bool special;
    ShellBuiltinFunction run;
};
// Every builtin hsh has, sorted by name (byte order). Later tasks add rows.
const std::vector<ShellBuiltin>& ShellBuiltins();
const ShellBuiltin* FindShellBuiltin(const std::string& name);  // null if none
```

Rows in this task:

| name | special | behaviour |
|------|---------|-----------|
| `:` | yes | status 0, arguments ignored |
| `exit` | yes | `exit [n]`: n absent -> `lastExitStatus`; n must be one or more decimal digits fitting an `int` (else `Fail("exit: Illegal number: <n>")` -- dash: `exit -1`, `exit abc`); extra arguments ignored; throws `ShellExit{n & 0xFF}` |
| `false` | no | status 1 |
| `true` | no | status 0 |

Each builtin's function is declared in `HshBuiltins.h`
(`int BuiltinColon(Shell&, const std::vector<std::string>&)`, `BuiltinExit`,
`BuiltinFalse`, `BuiltinTrue`) so later tasks add theirs the same way, in
files of their own.

### `src/components/BuiltinCommands/BuiltinCommandList.h`, `CMakeLists.txt`

- Declare `std::shared_ptr<IBuiltinCommand> CreateHshCommand();` and add it to
  `CreateStandardBuiltinCommands()` in name order (after `echo`).
- Add `commands/hsh/Hsh.cpp`, `HshInvocation.cpp`, `HshShell.cpp`,
  `HshBuiltins.cpp` to the `BuiltinCommands` sources.

## Tests

### `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`

- `ListsEveryBuiltinSortedWithAVersion`: the expected list gains `hsh` in
  name order.
- `EveryBuiltinsHelpHasTheSameShape`: the expected second line uses
  `command->Help().basedOn` when not empty (`Based on Linux dash:
  https://man7.org/linux/man-pages/man1/dash.1.html` for hsh).
- `EveryUntreatedOptionIsAcceptedAndReported` and `EveryBuiltinHasAVersion`
  must pass unchanged for hsh (`hsh -b /docs` reports `-b`, then fails to
  read `/docs` as a script -- the test only looks for the report).

### `tests/unit/components/Hsh.unittests/`

`CMakeLists.txt`: add `HshInvocationTest.cpp` and `HshShellTest.cpp` to
`add_executable(Hsh.unittests ...)`; link `Factory` too; add the include
directories `${CMAKE_SOURCE_DIR}/src/components/Factory` and
`${CMAKE_SOURCE_DIR}/tests/unit/components/BuiltinCommands.unittests` (for
`BuiltinCommandsFixture.h`).

`HshShellFixture.h` (new, `#pragma once`, shared by every later hsh test
file): `class HshShellTest : public BuiltinCommandsTest` with

```cpp
// hsh -c <script> [args...], stdout and stderr captured byte for byte.
Captured Sh(const std::string& script, const std::vector<std::string>& args = {},
            const std::optional<std::string>& input = std::nullopt,
            const std::string& workingDirectory = "/");
// A file of the root filesystem, whole ("" if it is not there).
std::string ReadRootFile(const std::string& path);
// "Parameter <spelling> is not treated by HaisosOS hsh v. <version>\n",
// with the version of CreateHshCommand(): tests never spell the version.
static std::string NotTreatedLine(const std::string& spelling);
```

`Sh` is `RunCaptured("hsh", {"-c", script, args...}, input, workingDirectory)`.
Every hsh test using the OS is `TEST_F(HshShellTest, ...)`; tests never
write hsh's version number out (later tasks bump it).

`HshInvocationTest.cpp` -- `ParseInvocation` alone, a table of {args,
expected source / command string / script / arg0 / positional / `$-` letters
(`OptionLetters`) / notTreated / error}:

- `HshInvocationTest.Sources`: `{"-c", "x"}`, `{"-c", "x", "nm", "a", "b"}`,
  `{"s.sh", "a"}`, `{}`, `{"-s", "a"}`, `{"--", "s.sh"}`, `{"-", "-c", "x"}`
  (script `-c`), `{"-c", "x", "--", "a"}` (arg0 `--`), `{"-ec", "x"}`.
- `HshInvocationTest.Options`: `{"-eu", "-c", "x"}` -> `ue`;
  `{"-o", "errexit", "-c", "x"}` -> `e`; `{"+o", "errexit", "-e", "-c", "x"}`
  -> `e`; `{"-eu", "+e", "-c", "x"}` -> `u`; `{"-aCfnx", "-c", "x"}`;
  `{"-m", "+V", "-o", "monitor", "-l", "-c", "x"}` -> notTreated
  `{"-m", "+V", "-o monitor", "-l"}`, no letters.
- `HshInvocationTest.Errors`: `{"-y"}` `Illegal option -y`; `{"+y"}`
  `Illegal option +y`; `{"-c"}` `-c requires an argument`; `{"-o", "nosuch"}`
  `Illegal option -o nosuch`; `{"-o"}` `-o requires an argument`; `{"--frob"}`
  `Illegal option --`.
- `HshInvocationTest.OptionTable`: 18 rows in dash's order; `FindShellOption('e')->name`
  is `errexit`; `FindShellOption("nolog")` has letter 0 and no field.

`HshShellTest.cpp` (`#include "HshShellFixture.h"`). Most tests are tables of {script, expected out, expected err, expected
status}, checked byte for byte (expected values from dash):

- `HshShellTest.SimpleCommands`: `echo hi`; `echo a  b "c  d"` -> `a b c  d\n`;
  `/bin/echo x`; `x=1; echo $x`; `x=a y=b; echo $x$y`; `:`; `true`; `false`
  (1); `false; echo $?` -> `1\n`; `` (empty) -> 0; `echo /docs/*` ->
  `/docs/a.md /docs/sub\n`.
- `HshShellTest.NotFoundAndNotRunnable`: `nosuch` -> err `hsh: 1: nosuch: not found\n`,
  127; `/nope/x` 127; `/docs` -> `hsh: 1: /docs: Permission denied\n`, 126;
  `/notes.txt` 126; `echo a; nosuch; echo $?` -> out `a\n127\n`; a two-line
  script `echo a` / `nosuch` -> err `hsh: 2: nosuch: not found\n`.
- `HshShellTest.PathLookup`: `echo $PATH` -> the default PATH (the fixture's
  environment has none); `PATH=/docs; echo x` -> `hsh: 1: echo: not found`;
  `PATH=; echo x` -> not found; `PATH=/nowhere:/bin; echo y` -> `y\n`; with
  working directory `/docs`: `../bin/echo z` -> `z\n` and `echo *` -> `a.md sub\n`.
- `HshShellTest.Lists`: `true && echo a`; `false && echo a`; `false || echo b`;
  `true || echo b`; `echo 1 && false || echo 2` -> `1\n2\n`; `! true; echo $?`
  -> `1\n`; `! false; echo $?` -> `0\n`.
- `HshShellTest.EnvironmentOfChildren`: set `GREETING=hi` on
  `os->GetOsEnvironment()` first; `echo $GREETING; hsh -c 'echo $GREETING'`
  -> `hi\nhi\n`; `y=2; hsh -c 'echo "[$y]"'` -> `[]\n`;
  `x=1 hsh -c 'echo $x'; echo "[$x]"` -> `1\n[]\n`; working directory `/docs`:
  `hsh -c 'echo $PWD'` -> `/docs\n`.
- `HshShellTest.BuiltinAssignments`: `x=1 :; echo $x` -> `1\n`;
  `x=1 true; echo "[$x]"` -> `[]\n`.
- `HshShellTest.StartupVariables`: `echo "$PS1|$PS2|$PS4|$OPTIND"` ->
  `$ |> |+ |1\n`; `echo "[$IFS]"` -> `[ \t\n]\n`; `echo $0 $#` -> `hsh 0\n`.
- `HshShellTest.Exit`: `exit 3` (3); `false; exit` (1); `exit 256` (0);
  `exit 1 2` (1); `echo a; exit 4; echo b` -> out `a\n`, 4; `exit abc` -> err
  `hsh: 1: exit: Illegal number: abc\n`, 2; `exit -1` likewise.
- `HshShellTest.FatalErrors`: `echo ${x?}; echo after` -> err
  `hsh: 1: x: parameter not set\n`, 2, no `after`; `echo ${x;}` ->
  `hsh: 1: Bad substitution\n`; `echo a; fi` -> out empty, err
  `hsh: 1: Syntax error: "fi" unexpected\n`, 2; the script `echo a` / `fi` /
  `echo b` -> out `a\n`, err line 2, 2.
- `HshShellTest.Invocation` (through `RunCaptured("hsh", args, ...)`):
  `-c 'echo $0 $1 $#' nm a b` -> `nm a 2\n`; `-c 'echo $0' --help` ->
  `--help\n`; `-y` -> err `hsh: 0: Illegal option -y\n`, 2; `-m -c 'echo ok'`
  -> out `ok\n`, err `NotTreatedLine("-m")`;
  `--version` -> `BuiltinVersionText` of the hsh command; `--help` -> out equals
  `BuiltinHelpText` of the hsh command; a script `/s.sh` holding `echo $0 $1`
  / `nosuch` run as `/s.sh p` -> out `/s.sh p\n`, err
  `/s.sh: 2: nosuch: not found\n`, 127; `/nonexist.sh` -> err
  `hsh: 0: cannot open /nonexist.sh: No such file\n`, 2; no arguments with
  input `echo in $#\n` -> `in 0\n`; `-s a1` with input `echo $1\n` -> `a1\n`.
- `HshShellTest.LuaChildren`: `/e.lua` = `exit(3)`: `/e.lua; echo $?` ->
  `3\n`; `/p.lua` = `print("to stdout")` / `error("boom")`: `/p.lua; echo $?`
  -> out `to stdout\n1\n`, err `lua: /p.lua:2: boom\n`.
- `HshShellTest.StopEndsTheShellAndItsChild`: `/spin.lua` = `while true do end`;
  start `/bin/hsh -c /spin.lua` with `os->StartProcess` (default options);
  after 200 ms `TriggerStop()`; `WaitToFinish(10000)` true; `ExitCode()` 143;
  no process with path `/spin.lua` left in `os->GetRunningProcesses()` once
  it has finished (poll up to 5 s).
- `HshShellTest.BrokenPipeOnItsOwnStderr`: a pipe from
  `os->GetPipeService()->CreatePipe()`, its read end released; start
  `/bin/hsh -c 'nosuch; /bin/echo after'` with `stdErr` = the write end and
  `stdOut` = a `MockFileDescriptor` (`tests/mocks/MockFileDescriptor.h`);
  release the test's write end; wait: `ExitCode()` 141, the mock got nothing.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/haisos.unittests --gtest_filter='*Template*'
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```

(The filter must be a substring of the test executable's name,
case-insensitive, and is also passed as `--gtest_filter=*<filter>*`: `Hsh`
selects every hsh test, `BuiltinCommands` the builtin tests. The template
tests run from their executable directly, since `haisos` would also select
`HaisosOS.unittests`.)

## Docs

- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a section
  "Running" -- `Hsh.cpp` (the builtin, its own argument parsing), `HshInvocation`,
  `Shell` (startup variables, the `Run` loop and how each error kind ends a
  non-interactive shell: syntax and fatal errors exit 2, others set `$?`),
  simple commands (the order: words, assignments, builtin or child), lookup,
  how a child is started (environment, working directory, slots 0/1/2, all
  through `ICurrentProcess`), waiting and stopping (slices, 143/141),
  `HshBuiltins` (special versus regular), the placeholders and which task
  removes each; one line per new file in the file list.
- `src/components/BuiltinCommands/CLAUDE.md`: the intro lists `hsh`; table
  row `hsh` | 0.1.0 | dash's invocation (`-c`, script, stdin, `-aCefinsux`,
  `-o`), simple commands and lists, `:` `true` `false` `exit` | documented
  exceptions: `--help`/`--version` only first; `$0` defaults to `hsh`; the
  reference is dash's page (`BuiltinHelp::basedOn`). Mention `basedOn` where
  the help shape is described.
- Root `CLAUDE.md`: "Builtin Commands" table row `hsh` -- "The Haisos shell,
  after dash: `-c`, scripts, stdin; simple commands and lists (more to come
  in this develop)"; in the `--help` shape paragraph, one sentence: a builtin
  copying a command of another name says so (`BuiltinHelp::basedOn`: hsh is
  based on dash); the directory-tree line of `BuiltinCommands/` lists `hsh`.

## Acceptance

- [ ] `hsh` is registered; `haisos --init` lists `# BUILTIN rootfs hsh /bin/hsh` (generated, not hand-written); `TheInitTemplatesBuiltinsAllApplyOnceUncommented` passes.
- [ ] `--help` has the one shape, its second line naming dash's page through `BuiltinHelp::basedOn`; every other builtin's help is unchanged.
- [ ] Every dash option letter is in the table; untreated ones are reported, not applied; invocation errors read as dash's (`hsh: 0: ...`, status 2).
- [ ] The shell reaches files only through `context.IO()` and processes only through `context.Process().OS()`, held for one call at a time.
- [ ] Children get exported variables plus prefix assignments, the working directory, slots 0/1/2; `$?` is their `ExitCode()`.
- [ ] 127/126 and their messages are dash's; special-builtin prefix assignments stay, regular-builtin ones do not.
- [ ] A stop ends the child and the shell (143); a broken pipe on its own output gives 141 quietly.
- [ ] Every test above passes; all unit and haisos tests pass.

## Out of scope

- Redirections and heredocs (hsh--redirections); pipelines, `&`, `wait`,
  `$(...)`, subshell scopes (hsh--pipelines); `cd`, `export`, `unset`,
  `readonly`, `set`, `shift`, `test`, and the `-x`/`-a`/`-n` behaviours
  (hsh--shell-builtins); compound commands, functions, `break`/`continue`/`return`,
  `.`, `eval`, `read`, `-e` (hsh--control-flow); the interactive mode, prompts,
  the man page, `ENV PATH=/bin` in the template, the haisos tests
  (hsh--interactive).
- Job control, traps, signals, `#!` lines, line editing.
