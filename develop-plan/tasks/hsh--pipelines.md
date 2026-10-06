# Task hsh--pipelines: hsh pipelines, subshell scopes, background jobs, $(...)

- Rock: hsh
- Depends on: hsh--redirections
- Size: ~1000 changed lines in ~10 files (at the limit; `wait` stays here because the `&` tests need it, and both neighbouring hsh tasks are at ~1000 too)
- Plan checked against: develop @ 8fb8324
- PR title: hsh: pipelines, background jobs, wait and command substitution

## Goal

- **Pipelines** `a | b | c` (and `! a | b`): the stages run at the same time,
  connected by pipes; the status is the last stage's. A stage that runs a
  program is a child process started at once; a stage that must run inside
  the shell (a shell builtin, later a function or compound command) runs in
  an in-process **subshell** -- its variable, directory and descriptor changes
  do not leak (`echo b | read w` leaves `w` unset, as in dash) -- and no
  combination of stages can deadlock.
- **Background lists** `cmd &`: started without waiting, `$!` set, kept for
  `wait`; `wait [pid...]` with dash's statuses and messages. A background
  list that needs the shell itself (a builtin, `&&`/`||`, later `{ ...; } &`,
  `( ... ) &`, `f &`) runs concurrently too, as a child `hsh -c` started
  from its source text.
- **Command substitution** `$(...)` and `` `...` ``: the command runs in an
  in-process subshell with its stdout on a pipe the shell reads to end of
  file; trailing newlines are dropped by the expansion; `$?` is its status.
- Acceptance scenarios 1 and 5 work:
  `cat /abc.txt | wc -l; ls /bin | wc -l > /n.txt; cat /n.txt` and
  `/p.lua 2>/e.txt | wc -l; cat /e.txt`.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`
("Running", "Redirections"), `HshShell.h`, `HshRedirection.h`,
`HshDescriptors.h`, `HshBuiltins.h`; the root `CLAUDE.md` ("Security",
"Creating things" -- "Nothing that waits for a runtime thread is destroyed on
one"); `develop-plan/goal.md` (Contracts: pipes, descriptor table);
`src/components/PipeService/CLAUDE.md` and `src/components/libheaders/CLAUDE.md`
(`StopToken`); the dash manual's "Pipelines", "Lists" (asynchronous lists),
"Command Substitution" and the `wait` builtin. Behaviour checked against dash
0.5.12.

What earlier tasks provide (as if on develop; the code wins on names):

- hsh--executor: `Shell` with `ExecuteList`, `ExecuteAndOr`,
  `ExecutePipeline`, `ExecuteCommand`, `ExecuteSimpleCommand`, `IO()`,
  `State()`, `Expansion()`, `Report`, `Fail`, `WriteOut`/`WriteErr` (with
  the `m_subshellDepth` hook for broken pipes), `StartChild`, `WaitForChild`,
  `StopChildren`, `m_liveChildren`, `m_substitutionCount`, `ShellExit`,
  `ShellStopped`, the placeholder helper `NotYet` and its uses for `&`,
  `pipelines` and `command substitution`; `ShellBuiltins()`,
  `FindShellBuiltin`; `HshShellFixture.h` (`HshShellTest`, `Sh`,
  `ReadRootFile`).
- hsh--redirections: `PlaceDescriptor(io, fd, descriptor)`,
  `RedirectionScope`, `ClosedDescriptor`.
- hsh--parser: `Pipeline { negated, commands, line }`, `ListItem { andOr,
  background, sourceText }` (`sourceText`: the and-or list as written,
  without the `&`), `ParseProgram(source, ParserOptions{firstLine, interactive,
  endOfInputName})`; `LiteralText(word)` (hsh--lexer).
- hsh--expansion: `ShellState::lastBackgroundPid` (`$!`); the expander calls
  `IExpansionHost::RunCommandSubstitution(source, line)`, strips the trailing
  newlines and sets `lastExitStatus` from the result.
- pipes--pipe-service: `IFileIO::CreatePipe(capacity = 0)` (64 KiB, bounded,
  blocking); `src/components/libheaders/StopToken.h` -- `StopToken::Current()`,
  `StopCallback(token, onStop)` (registered for its lifetime; runs at once if
  the stop was already requested), installed by `BuiltinProcess` on the
  shell's thread and signalled by its `TriggerStop`; `kIOInterrupted`.
- `kIOBrokenPipe`, `ICurrentProcess::StopForBrokenPipe()`; `IProcess::GetPid()`,
  `ExitCode()`.

## Changes

All in namespace `Haisos::Hsh`, `src/components/BuiltinCommands/commands/hsh/`,
plain portable C++17.

### Rules that bite (restated)

- Pipes come from `IFileIO::CreatePipe` (the OS's pipe service, through
  `ICurrentProcess`); children from `StartChild` (`OS()`); nothing else.
- No thread anywhere: in-process stages run one after another on the shell's
  own thread, which is why the deadlock rule below exists. `UnboundedPipe`
  owns no thread and its ends may be released on any thread.
- New classes implementing `IFileDescriptor` have private constructors and
  static `Create()`.
- hsh's version becomes `0.3.0`; the help notes gain one sentence (below).
- New files in the two `CMakeLists.txt`.

### `HshUnboundedPipe.h` / `.cpp` (new)

```cpp
struct UnboundedPipeEnds {
    std::shared_ptr<IFileDescriptor> readEnd;
    std::shared_ptr<IFileDescriptor> writeEnd;
};
// A pipe without a capacity: Write appends and never blocks; Read blocks
// while it is empty and its write end is held, returns 0 once the write end
// is released and the bytes are drained. Write after the read end is
// released returns kIOBrokenPipe. Used where the reader runs only after the
// writer (two pipeline stages inside the shell; $(...)), where a bounded pipe
// would deadlock.
UnboundedPipeEnds CreateUnboundedPipe();
```

Implement it as `PipeBuffer` (`src/components/PipeService/Pipe.*`) is
implemented, minus the capacity: a shared buffer object (mutex, condition
variable, `std::string` or deque of bytes, two open flags), a read end and a
write end each closing its side in its destructor. A blocked `Read` is
interruptible exactly as `PipeBuffer::Read`: construct a
`StopCallback(StopToken::Current(), wake)` before locking (destroyed after
unlocking), and return `kIOInterrupted` when `token->StopRequested()` while
empty. `Read` on the write end and `Write` on the read end return `kIOError`;
`IsTerminal()` false. Read whatever is there (up to `count`), never waiting
for more once some is there.

### `HshDescriptors.h` / `.cpp`

Add `NullInputDescriptor` (`Create()`; `Read` returns 0 at once, `Write`
returns `count` -- the bytes vanish, as `/dev/null`; not a terminal): the
stdin of a background command.

### `HshSubshell.h` / `.cpp` (new) -- in-process subshells

```cpp
// An in-process subshell: what a subshell may change and must not leak --
// the ShellState (variables, options, positional parameters, $?, $!), the
// working directory (IFileIO::ChangeDirectory back to it), every slot of the
// descriptor table, and the job list -- saved at construction and put back
// at destruction. Also counts the shell's subshell depth.
class SubshellScope {
public:
    explicit SubshellScope(Shell& shell);
    ~SubshellScope();
    SubshellScope(const SubshellScope&) = delete;
    SubshellScope& operator=(const SubshellScope&) = delete;
};
```

- Saving the table: `GetDescriptor(i)` for every `i` in `0 .. IFileIO::kMaxDescriptors - 1`
  into a vector; restoring: `PlaceDescriptor(io, i, saved[i])` for each slot
  whose current descriptor differs (pointer comparison). The working
  directory is restored before the slots. Never throws, never writes.
- hsh--control-flow adds the function table and the loop/function depths to
  what it saves; leave a comment where.

On `Shell` (public):

```cpp
// Runs |body| as a subshell: inside a SubshellScope, with `exit` (ShellExit)
// and fatal errors (ShellError: reported as at the top level, status 2)
// ending only the subshell. ShellStopped passes through. Returns the
// subshell's status, & 0xFF.
int RunSubshell(const std::function<int()>& body);
bool InSubshell() const;  // m_subshellDepth > 0
```

`WriteOut`/`WriteErr` on `kIOBrokenPipe` inside a subshell
(`m_subshellDepth > 0`): drop the rest of the subshell's output and
`throw ShellExit{kExitCodeBrokenPipe}` -- the subshell dies of the broken
pipe, as a forked dash subshell would, and the shell goes on. At the top
level it stays as hsh--executor made it (`StopForBrokenPipe`, 141).

### `HshShell.h` / `.cpp` -- pipelines

New on `Shell`:

```cpp
// Whether |command|, as a pipeline stage, is started as a child at once: a
// SimpleCommand with at least one word whose first word is plain literal
// text (LiteralText) naming neither a shell builtin nor (hsh--control-flow) a
// function. Every other stage runs inside the shell.
bool IsChildStage(const Command& command) const;

// Runs |command| with stdin/stdout replaced -- in a SubshellScope, slots 0
// and 1 set with PlaceDescriptor -- and either waits (returns its status) or,
// when |started| is given and the command turns out to be a program, starts
// it without waiting: *started is the child, the status 0.
int RunStage(const Command& command, std::shared_ptr<IFileDescriptor> in,
             std::shared_ptr<IFileDescriptor> out, std::shared_ptr<IProcess>* started);
```

`ExecuteSimpleCommand` gains the no-wait mode `RunStage` needs: a private
member `std::shared_ptr<IProcess>* m_startInsteadOfWait` (null normally);
when set and the command is a child, it stores the child there and returns 0
instead of waiting. A command that turns out to run in the shell (a glob that
expanded to a builtin's name, say) runs and is waited for as usual.

`ExecutePipeline` with `n > 1` commands, replacing the placeholder:

1. **Pipes.** For `i` in `0 .. n-2` the pipe between stage `i` and `i+1` is
   - an **unbounded** pipe (`CreateUnboundedPipe`) when stage `i+1` runs in
     the shell (`!IsChildStage`) and some stage `<= i` also runs in the shell;
   - otherwise a real pipe: `IO().CreatePipe()`; take both descriptors out of
     their slots and close the slots (the shell keeps them as `shared_ptr`s
     only). `nullopt` -> `Report("Pipe call failed")`, status 2, nothing
     started.
   Why this is deadlock-free (write it as a comment): stages inside the shell
   run one at a time, in order, after every child stage has been started; a
   bounded pipe is used only where its reader is either a running child or
   the first in-shell stage (which runs at once), so every bounded pipe has a
   reader that is running or about to run, and the writers that would wait
   for a later in-shell stage write into unbounded pipes, which never block.
2. **Start the child stages** (pass 1), in order: `RunStage(stage, in_i, out_i, &child)`
   with `in_0` = the shell's current slot 0, `out_{n-1}` = its slot 1, the
   pipe ends otherwise; keep each child. Right after starting stage `i`,
   release the shell's references to the ends it was given (read end of pipe
   `i-1`, write end of pipe `i`), so a reader sees end of file when its
   writers are gone.
3. **Run the in-shell stages** (pass 2), in order: `RunStage(stage, in_i, out_i, nullptr)`
   (a subshell); release its ends right after.
4. **Wait** for every child stage in order (`WaitForChild`); the pipeline's
   status is the last stage's (its child's exit code, or the in-shell status;
   a child stage that could not start has the 127/126 of its report). `!`
   negates it. Set `lastExitStatus`.
   A `ShellStopped` thrown anywhere unwinds; `Run` stops every live child.

### Background lists (`&`)

In `ExecuteList`, replacing the placeholder, an item with `background`:

- When its and-or list is one pipeline whose stages are all child stages
  (`IsChildStage`): start every stage as in pass 1 above (no pass 2), with the
  first stage's stdin a `NullInputDescriptor` (dash: an asynchronous list's
  stdin is `/dev/null` when there is no job control, before its own
  redirections). Do not wait. Record a job; `$!` (`lastBackgroundPid`) = the
  last stage's pid. The started children stay in `m_liveChildren` (so a stop
  of the shell stops them too) until `wait` waits for them; a job dropped by
  the bound below leaves `m_liveChildren` as well. Status 0.
- Otherwise (it needs the shell: a builtin, `&&`/`||`, and from
  hsh--control-flow compound commands and functions): start a **child hsh**
  running the item's text, without waiting, exactly like a background
  program -- `$!` is its pid, it is a job, `wait` waits for it:

  ```cpp
  // The script a background child hsh runs for |item|: BackgroundPrelude(),
  // then item.sourceText, then "\n".
  std::string BackgroundScript(const ListItem& item) const;
  // What the child must know of this shell beyond its environment: a line
  // "set -<letters>\n" with the letters of the options that are on among
  // e u f x C a (in OptionLetters' order; no line when none is on).
  // hsh--control-flow appends the sourceText of every function defined.
  std::string BackgroundPrelude() const;
  ```

  Start it with `StartChild(Process().Path(), {"-c", BackgroundScript(item), State().arg0, positional...}, {})`
  -- the path the running shell was started from (`IProcess::Path()` of its
  own process; no `PATH` lookup), `$0` and the positional parameters passed
  on, the environment from the exported variables, the shell's working
  directory, stdout and stderr from the shell's table -- with slot 0 replaced
  for the start by a `NullInputDescriptor` (POSIX, an asynchronous list
  without job control: stdin is empty unless the list redirects it, which its
  own text then does inside the child). Do the replacement with
  `PlaceDescriptor` and put slot 0 back right after `StartChild`. Null from
  `StartChild` -> `Report("fork: Resource temporarily unavailable")` (dash's
  message when it cannot fork), status 2. Otherwise record a job of that one
  process, `$!` = its pid, status 0.

  The child sees only the **exported** variables (they reach it through its
  environment); unexported variables do not reach it, where a forked dash
  subshell would see them. A documented exception -- the help notes' second
  line gains: `A background (&) builtin, function or compound command runs
  in a child hsh, which sees only exported variables.` Errors in the child
  are reported by it with its own line numbers (`<arg0>: 1: ...`).

```cpp
struct Job {
    std::vector<std::shared_ptr<IProcess>> processes;  // every stage started
    uint64_t pid = 0;                                  // the last stage's: what $! shows
};
// m_jobs: std::vector<Job>, oldest first. When it holds more than 1024 jobs,
// finished ones (every process WaitToFinish(0)) are dropped, oldest first.
```

### `HshBuiltinWait.cpp` (new) -- `wait` (regular builtin)

- `wait` alone: wait for every process of every job (`WaitForChild`), clear
  the list, status 0 (POSIX: always 0).
- `wait pid...`: each operand must be digits only, else `Report("wait: Illegal number: <x>")`,
  status 2; `%...` -> `Report("wait: No such job: <x>")`, status 2. A pid that
  is no job's `pid` and no process of a job -> status 127, no message. Else
  wait for that job's processes, remove the job, its status is its last
  process's exit code. The builtin's status is the last operand's.
- Interruptible: `WaitForChild` throws `ShellStopped` when the shell is asked
  to stop.

### `RunCommandSubstitution`

Replacing the placeholder:

1. `++m_substitutionCount`.
2. `ParseProgram(source, {line, false, "end of file"})`. An error (the
   parser already checked the text, so this is rare) -> `Report` it (its own
   line), result `{"", 2}`.
3. `auto pipe = CreateUnboundedPipe();`
   `int status = RunSubshell([&] { PlaceDescriptor(IO(), IFileIO::kStdOut, pipe.writeEnd); pipe.writeEnd.reset(); return ExecuteList(parsed.commands); });`
   -- the scope puts slot 1 back when the subshell ends, which releases the
   write end unless a background child still holds it (then the read below
   waits for it, as dash waits for end of file).
4. Read `pipe.readEnd` to end of file (4096-byte reads; `kIOInterrupted` ->
   `throw ShellStopped`; another negative result ends the read).
5. Return `{output, status}`.

So `x=$(cd /; pwd)` leaves the shell's directory alone, `$(exit 5)` gives
`$?` 5, and `$(nosuch)` reports `hsh: 1: nosuch: not found` and gives 127.

### `HshBuiltins.h` / `.cpp`, `Hsh.cpp`

The `wait` row (regular); remove the `&`, `pipelines` and `command
substitution` uses of `NotYet`; version `0.3.0`; the notes sentence above.

## Tests

`tests/unit/components/Hsh.unittests/HshPipelineTest.cpp` (new; add to
`add_executable`), `TEST_F(HshShellTest, ...)` tables through `Sh`, byte for
byte. Where a test writes `/abc.txt` it holds `one\ntwo\nthree\n`.

- `PipelinesOfChildren`: `echo hi | wc -c` -> `3\n`; `cat /notes.txt | cat | wc -l`
  -> `5\n`; `ls /docs | wc -l` -> `2\n`; `echo a | cat > /p.txt; cat /p.txt`.
- `PipelineStatus`: `false | true; echo $?` -> `0\n`; `true | false; echo $?`
  -> `1\n`; `! true | false; echo $?` -> `0\n`; `nosuch | wc -c; echo $?` ->
  out `0\n0\n`, err `hsh: 1: nosuch: not found\n`; `/e.lua | cat; echo $?`
  with `/e.lua` = `exit(3)` -> `0\n`.
- `InShellStagesAreSubshells`: `x=1; true | x=2; echo $x` -> `1\n`;
  `exit 3 | cat; echo $?` -> `0\n`; `true | exit 4; echo $?` -> `4\n`;
  `echo b | x=2; echo "[$x]"` -> `[]\n`.
- `NoDeadlockBetweenInShellStages`: with a 200000-byte file `/big.txt`:
  `cat /big.txt | : | wc -c` -> `0\n` (an in-shell stage that reads nothing;
  the writer gets a broken pipe and stops); `cat /big.txt | cat | wc -c` ->
  `200000\n`. (More in-shell stages come with hsh--control-flow, which adds
  `while read ... | ...` tests.)
- `BackgroundAndWait`: `/bin/echo bg > /bg.txt & wait $!; echo $?; cat /bg.txt`
  -> `0\nbg\n`; `/e.lua & wait $!; echo $?` -> `3\n`; `/e.lua & wait; echo $?`
  -> `0\n`; `echo $!` with nothing started -> `\n`; `wait 99999; echo $?` ->
  `127\n`; `wait abc; echo $?` -> err `hsh: 1: wait: Illegal number: abc\n`,
  out `2\n`; `wait %1; echo $?` -> err `hsh: 1: wait: No such job: %1\n`,
  out `2\n`; `true & echo $?` -> `0\n`.
- `BackgroundStdinIsEmpty`: input `typed\n` to hsh, `cat & wait` -> out empty.
- `BackgroundShellCommands`: `true && /bin/echo bg > /b.txt & wait $!; echo $?; cat /b.txt`
  -> `0\nbg\n`; `exit 3 & wait $!; echo $?` -> `3\n`;
  `x=1; export y=2; true && echo "[$x][$y]" & wait` -> `[][2]\n` (the words
  are expanded in the child, which sees only exported variables);
  `set -- a b; true && echo $0 $1 $# & wait` -> `hsh a 2\n`;
  `cd /docs; true && pwd & wait` -> `/docs\n`;
  `set -u; true && echo ${nope} & wait` -> err contains
  `nope: parameter not set` (the prelude passed `-u` on).
- `BackgroundShellCommandsRunConcurrently`: start
  `/bin/hsh -c 'true && /spin.lua & echo started; wait'` with
  `os->StartProcess` (`/spin.lua` = `while true do end`, stdout a
  `MockFileDescriptor`): `started\n` is written while the spin runs (poll
  up to 5 s); then `TriggerStop()` the shell: it finishes with 143 and no
  `/spin.lua` is left running (poll up to 5 s).
- `BackgroundPrelude`: `set -ex; true && echo $- & wait` -> out `xe\n` (the
  prelude was `set -xe\n`, `OptionLetters`' order restricted to e u f x C a;
  stderr holds the trace, not asserted); `true && echo "[$-]" & wait` ->
  `[]\n` (no prelude line).
- `CommandSubstitution`: `echo $(echo a)` -> `a\n`;
  `x=$(echo a; echo b); echo "[$x]"` -> `[a\nb]\n`;
  `` echo `echo b` `` -> `b\n`; `x=$(exit 5); echo $? "[$x]"` -> `5 []\n`;
  `echo $(nosuch); echo $?` -> err `hsh: 1: nosuch: not found\n`, out `\n0\n`;
  `x=$(cat /abc.txt | wc -l); echo $x` -> `3\n`; `echo $(echo $(echo deep))`
  -> `deep\n`; `x=out; y=$(x=inner; echo $x); echo $x $y` -> `out inner\n`;
  `echo "$(cat /notes.txt)"` -> `one\ntwo\n\n\n\tthree\n` (inner empty lines
  kept, only the trailing newlines dropped, then echo's own).
- `CommandSubstitutionBigOutput`: `x=$(cat /big.txt); echo ${#x}` ->
  `200000\n` (the unbounded pipe: no deadlock with the 64 KiB limit).
- `AcceptanceScenarioOne`: `/abc.txt` written; `cat /abc.txt | wc -l; ls /bin | wc -l > /n.txt; cat /n.txt`
  -> `3\n<N>\n` where `<N>` is the number of builtins in `/bin`
  (`builtins->GetCommands().size()`; the haisos test checks the exact `6`).
- `AcceptanceScenarioFive`: `/p.lua` = `print("to stdout")` / `error("boom")`;
  `/p.lua 2>/e.txt | wc -l; cat /e.txt` -> out `1\nlua: /p.lua:2: boom\n`,
  err empty, status 0.
- `StopEndsEveryStage`: start `/bin/hsh -c '/spin.lua | /spin.lua'` with
  `os->StartProcess` (`/spin.lua` = `while true do end`); after 200 ms
  `TriggerStop()`; finished within 10 s with 143; no `/spin.lua` left
  running (poll up to 5 s).

(A broken pipe in an in-shell stage is tested by hsh--shell-builtins, which
brings the first builtin that writes to stdout, `set`.)

`tests/unit/components/Hsh.unittests/HshUnboundedPipeTest.cpp` (new; plain
`TEST(HshUnboundedPipeTest, ...)`, no OS):

- `WritesNeverBlock`: 1 MiB written in 4 KiB writes before any read; then all
  read back in order; after releasing the write end, `Read` returns 0.
- `ReadBlocksUntilDataOrEndOfFile`: a thread reads an empty pipe; after
  100 ms it has not returned; a write wakes it with the bytes; a second
  thread blocked in `Read` returns 0 when the write end is released.
- `WriteAfterTheReaderIsGoneIsABrokenPipe`.
- `ABlockedReadIsInterruptedByItsStopToken`: as `PipeServiceTest`'s
  (`StopTokenScope` on the reading thread, `RequestStop` after 100 ms ->
  `kIOInterrupted`).
- `EndsWorkOneWay`: `Read` on the write end and `Write` on the read end
  return `kIOError`; `IsTerminal()` false.

Every "it blocks" check is "has not returned after ~100 ms" followed by
unblocking it; every thread is joined.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a section
  "Pipelines, subshells, jobs" -- child stages versus in-shell stages, the
  unbounded-pipe rule and why it cannot deadlock, `SubshellScope` and what it
  saves, `RunSubshell` and which exceptions stop at it, background jobs (a
  child stage started directly, or a child `hsh -c` running the list's
  `sourceText` after `BackgroundPrelude()`) and the documented exception
  (only exported variables reach it), `wait`, command substitution through an
  unbounded pipe; the new files; drop the removed placeholders.
- `src/components/BuiltinCommands/CLAUDE.md`: `hsh` row -- 0.3.0, pipelines,
  `&`/`wait`, `$(...)`; the exception: a background shell command runs in a
  child hsh that sees only exported variables.
- Root `CLAUDE.md`: the `hsh` row mentions pipelines, `&`, `$(...)`.

## Acceptance

- [ ] Child stages start at once and run concurrently; in-shell stages run
      in subshells (no leaked variables, directory or descriptors).
- [ ] The pipe rule is implemented as written; the 200000-byte tests pass.
- [ ] The shell releases each pipe end right after the stage that uses it is
      started or has run.
- [ ] `$!`, `wait` and their messages match the tests; jobs are bounded.
- [ ] A background list needing the shell runs concurrently as a child
      `hsh -c` started from `Process().Path()` with the prelude, `$0` and the
      positional parameters, empty stdin, the shell's stdout/stderr and directory.
- [ ] `$(...)` runs in a subshell, reads to end of file, gives its status.
- [ ] A broken pipe in a subshell ends only the subshell (141).
- [ ] No thread is created; scenarios 1 and 5 pass as unit tests; all unit
      and haisos tests pass.

## Out of scope

- Compound commands and functions as stages or in `$(...)` (they work as
  soon as hsh--control-flow implements them, through `ExecuteCommand`);
  `while read` loops (hsh--control-flow).
- Job control: `jobs`, `fg`, `bg`, `%job` specs, notifications.
- Passing unexported variables to a background child hsh.
