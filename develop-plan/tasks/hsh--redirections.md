# Task hsh--redirections: hsh redirections, heredocs and exec

- Rock: hsh
- Depends on: hsh--executor, builtins--wc (the tests use `wc`)
- Size: ~700 changed lines in ~10 files
- Plan checked against: develop @ 8fb8324
- PR title: hsh: every redirection, heredocs, here-strings and exec

## Goal

Every redirection the parser knows works in `hsh`, as in dash (plus bash's
`&>` and `<<<`), on every simple command -- external commands, shell
builtins and bare assignments alike:

- `<`, `>`, `>|`, `>>`, `<>`, `n>&m`, `n<&m`, `n>&-`, `n<&-`, `&>`, `<<`,
  `<<-`, `<<<`, with an optional IO number; noclobber (`-C`) refusing `>` onto
  an existing file, `>|` overriding it.
- They change the shell's own descriptor table (the process's `IFileIO`
  slots) for the length of the command and are undone afterwards; a child
  started by the command gets slots 0/1/2 as they are then.
- A heredoc or here-string is a pipe sized exactly to its text, filled and
  closed before the reader starts, so writing it never blocks.
- dash's messages: `hsh: 1: cannot open /nonexist: No such file`,
  `cannot create /nonexist/x: Directory nonexistent`, `cannot create /n.txt:
  File exists`, `5: Bad file descriptor` (status 2; fatal for a special
  builtin), `Syntax error: Bad fd number` (fatal).
- The `exec` builtin: `exec 3>file` keeps its redirections; `exec cmd args`
  runs the command in the shell's place -- the shell ends with its status.
- Acceptance scenario 2's first haisosfile works:
  `ls /nope 2>/err.txt || echo "failed: $?"; cat /err.txt; cat < /abc.txt >> /out.txt 2>&1 && wc -c /out.txt`.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (the
"Running" section of hsh--executor), `HshShell.h`, `HshBuiltins.h`,
`HshAst.h` (`Redirection`, `RedirectionKind`), `HshExpansion.h`; the root
`CLAUDE.md` ("Security: `ICurrentProcess` is the only door out of a
process"); `develop-plan/goal.md` (Contracts: the descriptor table,
`CreatePipe`); the dash manual's "Redirections" and "Here Documents"
sections and the `exec` builtin. Every message below was checked against
dash 0.5.12.

What earlier tasks provide (as if on develop; the code wins on names):

- hsh--executor: `Shell` (`HshShell.h`) with `ExecuteSimpleCommand`,
  `IO()`, `State()`, `Expansion()`, `Report`, `Fail`, `CurrentLine`,
  `LookUpCommand` (`CommandLookup { Result { Found, NotFound, NotRunnable }, path }`),
  `StartChild(path, args, assignments)`, `WaitForChild`, `WriteOut`/`WriteErr`,
  `ShellExit { status }`, `ShellStopped`, `OpenFailureReason(io, path, creating)`;
  the placeholder helper `NotYet` and its `redirections` use in
  `ExecuteSimpleCommand`; `ShellBuiltin { name, special, run }`,
  `ShellBuiltins()`, `FindShellBuiltin` (`HshBuiltins.h`); `Invocation`,
  `ShellOptions::noclobber` set by `-C`; the test fixture
  `tests/unit/components/Hsh.unittests/HshShellFixture.h` (`HshShellTest`,
  `Sh`, `ReadRootFile`, `NotTreatedLine`).
- hsh--parser / hsh--lexer: `Redirection { kind, fd, target, hereDoc, line }`,
  `RedirectionKind { Input, Output, OutputClobber, Append, ReadWrite,
  DupInput, DupOutput, HereDoc, HereString, OutputAndError }` (`fd` already
  holds the kind's default when no IO number was given); `HereDocument`
  (`delimiter`, `quoted`, `stripTabs`, `rawBody`, `body`). The parser has
  already rejected a dup target without expansions that is not one digit or
  `-`.
- hsh--expansion: `Expander::ExpandToString(word)`, `ExpandHereDocument(hereDoc)`.
- fd--process-table: `IFileIO::GetDescriptor`, `AddDescriptor`, `Dup2`,
  `CloseDescriptor`, `OpenFile` (returns the descriptor without placing it).
- pipes--pipe-service: `IFileIO::CreatePipe(size_t capacity = 0) ->
  std::optional<std::pair<int, int>>` (read slot, write slot; the atomic
  write size is capped at the capacity, so a write of `capacity` bytes into
  an empty pipe never blocks).
- `src/components/Filesystem/FilesystemUtils.h`: `kFileOpenReadOnly`,
  `kFileOpenWriteCreateTruncate`, `kFileOpenWriteCreateAppend`,
  `kFileCreateMode`, `kFileReadWriteBit`, `kFileCreateBit`.

## Changes

All in namespace `Haisos::Hsh`, `src/components/BuiltinCommands/commands/hsh/`,
plain portable C++17.

### Rules that bite (restated)

- The shell's table is the process's own (`context.IO()`): redirections go
  through `IFileIO` -- `OpenFile` for files (paths resolved against the
  shell's working directory), `CreatePipe` for heredocs, the table operations
  for the rest. Nothing reaches an `IFileSystem` or the host.
- The heredoc pipe needs no thread: it is sized to the text, so the shell
  writes it whole before anyone reads.
- New classes implementing an interface (`ClosedDescriptor`) have a private
  constructor and a static `Create()` returning `shared_ptr`.
- New `.cpp` files go in `src/components/BuiltinCommands/CMakeLists.txt`,
  new test files in `tests/unit/components/Hsh.unittests/CMakeLists.txt`.
- hsh's version becomes `0.2.0` (`Hsh.cpp`).

### `src/components/Filesystem/FilesystemUtils.h`

Add `kFileOpenReadWriteCreate` beside the other open constants, on both
platforms: `_O_RDWR | _O_CREAT | _O_BINARY` on Windows, `O_RDWR | O_CREAT`
elsewhere (what dash opens `<>` with).

### `HshRedirection.h` / `.cpp` (new)

```cpp
// Makes slot |fd| of |io| hold |descriptor|; a null one empties the slot.
// Uses AddDescriptor then Dup2 and CloseDescriptor of the temporary slot.
// False when the table is full.
bool PlaceDescriptor(IFileIO& io, int fd, std::shared_ptr<IFileDescriptor> descriptor);

// Applies a command's redirections to the shell's own descriptor table and
// undoes them when it ends, unless Keep() was called (exec without a command).
class RedirectionScope {
public:
    explicit RedirectionScope(Shell& shell);
    ~RedirectionScope();  // Restore()
    RedirectionScope(const RedirectionScope&) = delete;
    RedirectionScope& operator=(const RedirectionScope&) = delete;

    // Applies |redirections| in order. On the first failure, undoes what this
    // call applied and returns the message to report (without the
    // "hsh: <line>: " prefix): "cannot open /x: No such file". Throws
    // ShellError (through Shell::Fail) for "Syntax error: Bad fd number" and
    // for expansion errors, which are fatal in dash.
    std::optional<std::string> Apply(const std::vector<Redirection>& redirections);
    // Puts every slot this scope changed back as it was, latest change undone
    // first. Never writes anything and never throws.
    void Restore();
    // Forgets the saved slots: the redirections stay in effect.
    void Keep();

private:
    // Shell& m_shell; std::vector<std::pair<int, std::shared_ptr<IFileDescriptor>>> m_saved;
};
```

`Apply`, per redirection (`n` = `redirection.fd`):

1. **Save first.** Before the first change to slot `n` in this scope, push
   `{n, io.GetDescriptor(n)}` (possibly null) onto `m_saved`. Every step below
   that changes a slot saves it first -- including slot 2 for `&>`, and
   before `CreatePipe` (which may take free slots, slot `n` among them).
2. By kind (the target expanded with `Expansion().ExpandToString(target)`):
   - `Input` `<`: `OpenFile(t, kFileOpenReadOnly)`; null -> `cannot open <t>: <OpenFailureReason(io, t, false)>`.
   - `Output` `>` and `OutputAndError` `&>`: when `State().options.noclobber`
     and `io.Stat(t)` succeeds with type `DirectoryEntryType::File` ->
     `cannot create <t>: File exists` (dash's noclobber spares an existing
     device such as `/dev/null`). Else `OpenFile(t, kFileOpenWriteCreateTruncate, kFileCreateMode)`;
     null -> `cannot create <t>: <OpenFailureReason(io, t, true)>`.
   - `OutputClobber` `>|`: as `>` without the noclobber check.
   - `Append` `>>`: `kFileOpenWriteCreateAppend`, `kFileCreateMode`; `cannot create` on failure.
   - `ReadWrite` `<>`: `kFileOpenReadWriteCreate`, `kFileCreateMode`; `cannot create` on failure (dash).
   - `DupInput` `<&`, `DupOutput` `>&` (the same handling, as dash): `t` is
     `-` -> `CloseDescriptor(n)` (closing an empty slot is fine: `exec 9>&-`
     succeeds); exactly one digit `m` -> `io.GetDescriptor(m)` null ->
     `<m>: Bad file descriptor`; `m == n` -> nothing; else `Dup2(m, n)`.
     Anything else -> `Fail("Syntax error: Bad fd number")` (an expanded
     target such as `>&$x` with `x=a`; dash exits).
   - `HereDoc` `<<`, `<<-`: `text = Expansion().ExpandHereDocument(*redirection.hereDoc)`.
   - `HereString` `<<<`: `text = ExpandToString(target) + "\n"`.
   - For both: `auto slots = io.CreatePipe(std::max<size_t>(text.size(), 1))`;
     nullopt -> `Pipe call failed` (dash's wording). Take the two descriptors
     out of their slots (`GetDescriptor`, then `CloseDescriptor` of both),
     write the whole text to the write end (loop over partial writes; a
     negative result just stops -- `kIOBrokenPipe` cannot happen, nobody has
     the read end yet), release the write end, then place the read end in
     slot `n`. The reader sees the text, then end of file.
   - `OutputAndError` after its file is in slot 1: `Dup2(1, 2)`.
   - Placing an opened descriptor in slot `n`: `PlaceDescriptor(io, n, d)`;
     false -> `<n>: Too many open files`.
3. On a failure: `Restore()` the slots changed by this call, return the message.

`Restore()`: for each saved pair, last first, `PlaceDescriptor(io, fd, saved)`;
ignore failures; clear `m_saved`. The descriptors opened only for the command
are released with the slots.

### `HshDescriptors.h` / `.cpp` (new)

```cpp
// What a child gets in place of an empty (closed) slot: StartProcessOptions
// takes a null stream as "use the console", which a closed descriptor must
// not become. Read and Write fail (kIOError); not a terminal.
class ClosedDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ClosedDescriptor> Create();
    ssize_t Read(void* buf, size_t count) override;         // kIOError
    ssize_t Write(const void* buf, size_t count) override;  // kIOError
    bool IsTerminal() const override;                        // false
private:
    ClosedDescriptor() = default;
};
```

(If `IFileDescriptor` has gained more pure virtual methods by then, implement
them as a descriptor that is not a file would.)

### `HshShell.h` / `.cpp`

- `StartChild`: a null slot 0, 1 or 2 is passed as `ClosedDescriptor::Create()`,
  never as null.
- New public `void KeepRedirections();` -- called by `exec` with no command:
  the simple command running it keeps its redirections (sets a flag the
  executor reads after the builtin returns).
- `ExecuteSimpleCommand`, replacing the `redirections` placeholder, in dash's
  order:
  1. expand the words (as now);
  2. `RedirectionScope scope(*this)`; `auto error = scope.Apply(command.redirections)`;
     on an error: if the command is a special builtin (`FindShellBuiltin(fields[0])`
     with `special`) -> `Fail(*error)` (dash exits: `: < /nonexist`);
     otherwise `Report(*error)` and return 2 -- the command does not run and
     its prefix assignments are not made;
  3. assignments and the command as before (a bare assignment's redirections
     are made and undone too: `> /f` creates `/f`);
  4. after a builtin returns, if `KeepRedirections()` was called: `scope.Keep()`
     and clear the flag;
  5. the scope restores the table when the function returns (after the wait).
- Remove the `redirections` use of `NotYet`.

### `HshBuiltinExec.cpp` (new) and `HshBuiltins.h` / `.cpp`

Add the row `exec` (special) and declare `int BuiltinExec(Shell&, const std::vector<std::string>&)`.

- `exec` alone (a leading `--` is skipped): `shell.KeepRedirections()`, status 0.
- `exec name args...`: `LookUpCommand(name)` -- PATH and paths only, never a
  shell builtin (dash: `exec :` is `exec: :: not found`). `NotFound` ->
  `Report("exec: <name>: not found")` then `throw ShellExit{127}`;
  `NotRunnable` -> `exec: <name>: Permission denied`, `ShellExit{126}`;
  `StartChild(path, args, {})` null -> the same as `NotRunnable`; else
  `throw ShellExit{WaitForChild(child)}`: the shell (or, later, the subshell)
  ends with the command's status, as dash's exec replaces it.

### `Hsh.cpp`

Version `0.2.0`.

## Tests

`tests/unit/components/Hsh.unittests/HshRedirectionTest.cpp` (new; add to
`add_executable`). `#include "HshShellFixture.h"`, `TEST_F(HshShellTest, ...)`,
tables of {script, out, err, status} checked byte for byte through `Sh`,
plus `ReadRootFile` where a file is the result. The fixture's `/notes.txt`
holds `one\ntwo\n\n\n\tthree\n`.

- `RedirectionsToAndFromFiles`: `echo a > /o.txt; cat /o.txt` -> `a\n`;
  `echo a > /o.txt; echo b >> /o.txt; cat /o.txt` -> `a\nb\n`; `cat < /notes.txt`;
  `cat 0</notes.txt`; `echo x 1>/o2.txt` then `ReadRootFile("/o2.txt")` is
  `x\n`; `echo z 1<>/new.txt; cat /new.txt` -> `z\n`; `cat 0<>/notes.txt`;
  `> /bare.txt; echo $?` -> `0\n` and `/bare.txt` exists, empty.
- `RedirectionsAreUndone`: `echo a > /u.txt; echo b; cat /u.txt` -> `b\na\n`;
  `: > /c.txt; echo after` -> `after\n` on stdout.
- `StderrAndDuplicates`: `ls /nope 2>/e.txt; cat /e.txt` -> out the `ls:` line,
  err empty; `ls /nope >/e2.txt 2>&1; cat /e2.txt` -> the `ls:` line;
  `echo hi 1>&2` -> out empty, err `hi\n`; `echo x 3>&-; echo $?` -> `x\n0\n`;
  `echo x >&5; echo $?` -> err `hsh: 1: 5: Bad file descriptor\n`, out `2\n`;
  `ls /docs /nope &>/both.txt; cat /both.txt` -> out contains the `ls:` line
  and `a.md` (order not asserted).
- `BadFdNumberIsFatal`: `x=a; echo hi >&$x; echo after` -> err
  `hsh: 1: Syntax error: Bad fd number\n`, status 2, out empty.
- `HereDocuments`: `HOME=/h; cat <<E` / `x $HOME` / `E` -> `x /h\n`;
  `cat <<'E'` / `$x` / `E` -> `$x\n`; `cat <<-E` / `<TAB>a` / `<TAB>E` -> `a\n`;
  `cat <<E` / `E` -> empty; `cat 0<<E` / `y` / `E` -> `y\n`;
  `wc -l <<E` / `x` / `y` / `E` -> `2\n`; two heredocs on one line,
  `cat <<A; cat <<B` / `a` / `A` / `b` / `B` -> `a\nb\n`; a heredoc of
  100000 bytes (one line of `x`, built in the test) through `wc -c` ->
  `100001\n` (the pipe is sized to the text, nothing blocks).
- `HereStrings`: `cat <<<"one two"` -> `one two\n`; `x=v; cat <<<$x` -> `v\n`;
  `wc -c <<<""` -> `1\n`.
- `Noclobber` (through `RunCaptured("hsh", {"-C", "-c", script})`, with
  `/n.txt` written first): `echo b > /n.txt; echo $?; echo c >| /n.txt; cat /n.txt`
  -> err `hsh: 1: cannot create /n.txt: File exists\n`, out `2\nc\n`;
  `echo d >> /n.txt` works; `echo e &> /n.txt` refused the same way.
- `RedirectionErrors`: `cat < /nonexist; echo $?` -> err
  `hsh: 1: cannot open /nonexist: No such file\n`, out `2\n`;
  `echo hi > /nonexist/x; echo $?` -> `cannot create /nonexist/x: Directory nonexistent`;
  `echo hi > /docs; echo $?` -> `cannot create /docs: Is a directory`;
  `echo hi > /bin/ls; echo $?` -> `cannot create /bin/ls: Permission denied`;
  `: < /nonexist; echo after` -> err the `cannot open` line, out empty,
  status 2 (a special builtin's redirection error is fatal).
- `Exec`: `exec /bin/echo hi; echo no` -> `hi\n`, 0; `exec /e.lua; echo no`
  with `/e.lua` = `exit(3)` -> status 3, out empty; `exec nosuch; echo no` ->
  err `hsh: 1: exec: nosuch: not found\n`, 127; `exec 3>/f.txt; echo z >&3;
  exec 3>&-; cat /f.txt` -> `z\n`; `exec >/all.txt; echo a; echo b; cat /all.txt >&2`
  -> out empty, err `a\nb\n`; `exec 9>&-; echo $?` -> `0\n`.
- `ClosedSlotsReachChildrenClosed`: `cat 0<&-; echo $?` -> err
  `cat: -: Input/output error\n`, out `1\n` (a child sees the closed slot as
  a descriptor whose reads fail, never as the console).
- `AcceptanceScenarioTwo`: `WriteFile("/abc.txt", "one\ntwo\nthree\n")`;
  the script of scenario 2 -> out exactly
  `failed: 2\nls: cannot access '/nope': No such file or directory\n14 /out.txt\n`,
  err empty, status 0.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a section
  "Redirections" -- the scope (save before change, restore last first, `Keep`
  for `exec`), the open flags per kind, noclobber, the dash messages, the
  heredoc pipe sized to its text (why it never blocks and needs no thread),
  `ClosedDescriptor` for closed slots; `exec` in the builtin list; the new
  files in the file list; drop `redirections` from the placeholder list.
- `src/components/BuiltinCommands/CLAUDE.md`: the `hsh` row -- version 0.2.0,
  redirections and heredocs, `exec`.
- Root `CLAUDE.md`: the `hsh` row of "Builtin Commands" mentions
  redirections and heredocs.

## Acceptance

- [ ] Every `RedirectionKind` is handled; each changed slot is saved before
      its first change and restored after the command (except after `exec`
      without a command).
- [ ] Files are opened only through `IFileIO::OpenFile`; heredocs use
      `IFileIO::CreatePipe` sized to the text and are written whole before the
      reader starts.
- [ ] Messages and statuses match the tests byte for byte; a special
      builtin's redirection error and `Bad fd number` are fatal.
- [ ] A child never receives a null stream for a closed slot.
- [ ] Scenario 2's first haisosfile works (the unit test); all unit and haisos tests pass.

## Out of scope

- Redirections on compound commands and functions (hsh--control-flow, using
  `RedirectionScope`); pipelines, `&`, `$(...)` (hsh--pipelines); `set -C`
  (hsh--shell-builtins; `-C` at invocation works now).
- `&>>`, `>&word` as bash's "file" form, `{fd}>` named descriptors,
  `/dev/fd/N`, process substitution -- none are dash's.
