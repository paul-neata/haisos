# Task awk--io: getline, output redirections, pipes, close, fflush, system, ENVIRON

- Rock: awk
- Depends on: awk--printf-math, coreutils--names-env (`BuiltinRunProgram.h`)
- Size: ~950 changed lines in ~11 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add awk getline, redirections, pipes, close, fflush and system

## Goal

The last piece of awk: its input and output beyond the main records, as
gawk 5.2 `--posix` runs them.

- `getline` in every form -- `getline`, `getline var`, `getline [var] <
  file`, `cmd | getline [var]` -- returning 1, 0 at the end, -1 when the
  file cannot be read, with POSIX/gawk's effects on `$0`, `NF`, `NR`, `FNR`;
  an input file or command stays open, read on from where it stopped, until
  `close()`.
- `print`/`printf` with `> file`, `>> file` and `| cmd`; a stream is opened
  once and reused by its name (`print > "out"` twice writes two lines);
  `close()` and its return values (0 for a file, the command's exit status
  for a pipe, -1 when nothing of that name is open); `fflush()`;
  `system(cmd)` returning the command's exit status as plain gawk 5 does
  (`system("exit 3")` is 3; a command killed by a signal gives 256 + the
  signal number) -- the user chose plain gawk's values over `--posix`'s
  wait status (256 x status), which is what awk one-liners expect.
- Commands -- `system`, output pipes, `cmd | getline` -- run in Haisos's
  shell, `hsh -c <cmd>` (found in `PATH`), started through
  `ICurrentProcess::OS()` by `BuiltinRunProgram`; awk's buffered output is
  flushed before any command starts, and every stream is closed (pipes
  waited for) before awk's own standard output is written out at the end,
  so `BEGIN { printf "1"; system("echo 2"); print "3" }` prints `12\n3\n`
  and `{ print $3 | "sort -r" }` sorts as on Linux.
- `ENVIRON` holds the process's environment variables.
- The special names `/dev/stdout`, `/dev/stderr`, `/dev/stdin` and `-`
  are awk's own standard streams.
- Exit codes: `exit` values modulo 256, 2 after a fatal error (streams still
  closed), 141 on a broken standard output, 143 when stopped -- the children
  stopped too.

## Context

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` and the
files it lists (`AwkInterpreter.h/.cpp`, `AwkBuiltins.cpp`, `AwkInput.h`,
`AwkAst.h`); `src/components/BuiltinCommands/BuiltinRunProgram.h/.cpp`;
`commands/hsh/HshShell.cpp` (`CreateStagePipe`: how a pipe is made with
`IO().CreatePipe` and its slots closed; `StartChild`, `WaitForChild`);
`interfaces/IFileIO.h` (`OpenFile`, `CreatePipe`, `GetDescriptor`,
`CloseDescriptor`, `Stat`), `interfaces/IPipeService.h` (a pipe's rules:
`kIOBrokenPipe`, `kIOInterrupted`), `src/components/Filesystem/FilesystemUtils.h`
(`kFileOpenWriteCreateTruncate`, `kFileOpenWriteCreateAppend`,
`kFileCreateMode`); root `CLAUDE.md` ("Security", "Exit codes");
`src/components/BuiltinCommands/CLAUDE.md` ("Output": buffering, broken
pipes).

What earlier tasks provide (on develop; their plans are the authority):
- coreutils--names-env: `BuiltinRunProgram.h` -- `FindProgramInPath(context,
  name, const IEnvironment* = nullptr)` (PATH, or `kBuiltinDefaultSearchPath`
  when there is none -- it holds `/bin`), `struct RunProgramOptions { stdIn,
  stdOut, stdErr; workingDirectory; environment }` (null descriptor: the
  caller's own slot 0/1/2, an empty slot as a closed descriptor; null
  environment: a clone of the caller's), `RunProgramAndWait(context, path,
  args, options, bool* started)` (flushes the caller's stdout, starts through
  `context.Process().OS()->StartProcess` only, waits in 50 ms slices, stops
  the child when the caller is stopped; 127 when not started).
- coreutils--sort: `OpenInputOperand(context, name, failure)` /
  `InputOpenFailure` (`-` is descriptor 0).
- awk--values: `RecordReader(BuiltinContext&, std::shared_ptr<IFileDescriptor>)`,
  `Next(rs, record)` -> `RecordReadResult { Record, End, Error, Stopped }`
  (RS's first byte; `""` paragraph mode since awk--records); `Value`;
  `AwkFatal`.
- awk--interpreter: `Interpreter` -- `Output(const Stmt& print, const
  std::string& text)` (today: refuses a redirection with
  `AwkFatal("output redirection is not implemented yet")`, else
  `m_context.Out`), `EvaluateGetline(const Expr&)` (today
  `AwkFatal("getline is not implemented yet")`), `NextMainRecord()` (the
  operands as ARGV is now, `-` is standard input, FILENAME/NR/FNR, the
  "cannot open file" fatals), `Assign`, `SpecialString(kSlotRS / kSlotFS /
  kSlotCONVFMT)`, `m_fields.SetRecord(record, fs, paragraphMode)`,
  `ThrowIfStopped()`, `Run` (fatal errors written, status 2; 143 when
  stopped), `kSlotENVIRON` (an empty array until now).
- awk--parser: `Stmt::redirect` (`RedirectKind { None, File, Append, Pipe }`)
  and `Stmt::redirectTarget`; `Expr` of kind `Getline` with `getlineForm`
  (`Simple`, `File`, `Command`), `target` (null: `$0`) and `operands[0]`
  (the file or command).
- awk--functions: `CallBuiltin` in `AwkBuiltins.cpp` (`close`, `fflush`,
  `system` still the placeholder ``function `<name>' is not implemented
  yet``), `RuntimeWarning(message)`, the `AwkRunTest` fixture in
  `tests/unit/components/Awk.unittests/AwkRunFixture.h` (`/abc.txt` =
  `a b c\nd e f\n`, `/data.csv` = `x,1,2.5\ny,2,3.25\nz,3,4\n`).
- awk--printf-math: `printf` statements go through `Output` like `print`.

Every expected text below was produced by gawk 5.2.1 `--posix` with
`LC_ALL=C` (`gawk:` replaced by `awk:`, `sh` by Haisos's `hsh`), except the
documented exceptions named. The task container has only mawk: never change
an expectation to mawk's, and never look for or copy gawk's (or any other
awk's) source code -- the behaviour described here is the specification.

## Changes

### `src/components/BuiltinCommands/BuiltinRunProgram.h` / `.cpp` -- start without waiting

Contract 5 grows by two functions (the existing ones keep their behaviour):

```cpp
// Starts |programPath| as RunProgramAndWait does -- the descriptors,
// working directory and environment from |options| by the same rules, the
// caller's stdout flushed first, through context.Process().OS()->StartProcess
// and nothing else, the OS released at once -- but returns without waiting:
// the child, or null when it could not be started.
std::shared_ptr<IProcess> StartProgram(BuiltinContext& context, const std::string& programPath,
                                       const std::vector<std::string>& args,
                                       const RunProgramOptions& options = {});

// Waits for a child StartProgram started, as RunProgramAndWait waits: in
// 50 ms slices; once the caller is asked to stop, the child is stopped
// (TriggerStop, once) and waited for up to 5000 ms more. Its exit code, or
// 143 when it has not finished.
int WaitForProgram(BuiltinContext& context, IProcess& child);
```

`RunProgramAndWait` becomes `StartProgram` (null -> 127, `*started =
false`) then `WaitForProgram`. env, find and xargs keep working unchanged
(their tests must stay green).

### `commands/awk/AwkStreams.h` / `AwkStreams.cpp` (new)

Namespace `Haisos::Awk`. The table of awk's open streams -- what gawk calls
redirections -- and the commands it runs. A plain class owned by the
`Interpreter` (`AwkStreams m_streams;`, built with its context).

```cpp
enum class AwkStreamKind { OutputFile, OutputPipe, InputFile, InputPipe };

// The shell commands run in: hsh found in PATH (FindProgramInPath(context,
// "hsh")), or nullopt.
std::optional<std::string> FindAwkShell(BuiltinContext& context);

class AwkStreams {
public:
    explicit AwkStreams(BuiltinContext& context);

    // print/printf with a redirection (File ">", Append ">>", Pipe "|"):
    // |bytes| to the stream |name|, opened first when it is not open.
    // |statement| is "print" or "printf", for the error text. Throws AwkFatal.
    void Write(RedirectKind redirect, const std::string& name, const std::string& bytes,
               const std::string& statement);

    // getline < name (InputFile) or name | getline (InputPipe): 1 with
    // |record| set, 0 at the end (and when stopped), -1 when it cannot be
    // opened or read. |rs| is RS now.
    int ReadRecord(AwkStreamKind kind, const std::string& name, const std::string& rs,
                   std::string& record);

    // close(name): -1 when nothing of that name is open (or its output could
    // not be written out); for a file 0; for a pipe (output or input) the
    // command's status mapped as RunSystem maps it (`close("cat; exit 3")`
    // is 3).
    int Close(const std::string& name);
    // fflush(name) (null: everything): 0, or -1 when |name| is no open output.
    int Flush(const std::string* name);
    // Awk's stdout and every output stream written out: before any command
    // starts, and by fflush().
    void FlushAll();
    // system(command): the status as plain gawk 5 returns it (below).
    int RunSystem(const std::string& command);
    // Every stream closed, in the order opened (pipes waited for): at the
    // end of every run -- normal, exit, fatal error, stop. Never throws.
    void CloseAll();
};
```

A stream: its name, kind, the descriptor (output or input), an output buffer,
a `RecordReader` (input), the child `IProcess` (pipes), whether it reached its
end, whether a write failed. Streams are kept in opening order and found by
**name and kind** (`>` and `>>` are the same kind): `print "x" | "cat"; print
"y" > "cat"` opens a pipe and a file both named `cat`.

**Write**:
1. Find the stream, or open it:
   - `>`/`>>` to `/dev/stdout` or `-`: awk's standard output (written with
     `context.Out`, so it shares awk's buffer and order); to `/dev/stderr`:
     `context.ErrorText` (unbuffered). They are registered under that name
     like any stream, so `close()` and `fflush()` find them. (gawk `--posix`
     opens the real devices, which on Linux truncates a redirected standard
     output and loses lines: a documented exception.)
   - `>`: `context.IO().OpenFile(name, kFileOpenWriteCreateTruncate,
     kFileCreateMode)`; `>>`: `kFileOpenWriteCreateAppend` (a stream opened
     by `>` and written again by `>>` is not reopened). On failure
     `AwkFatal("cannot redirect to `<name>': <reason>")`, the reason found
     with `Stat`: a directory there -> `Is a directory`; something else there
     -> `Permission denied`; the parent is not an existing directory -> `No
     such file or directory`; otherwise `Permission denied`.
   - `|`: `FlushAll()`; `context.IO().CreatePipe()` (nullopt ->
     `AwkFatal("cannot open pipe `<name>': Too many open files")`), its two
     descriptors taken with `GetDescriptor` and both slots closed with
     `CloseDescriptor` (as hsh's `CreateStagePipe`); `StartProgram(context,
     shell, {"-c", name}, options)` with `options.stdIn` = the read end (stdout
     and stderr: awk's own); then **release awk's copy of the read end** (else
     the child never sees the end of its input). No shell or no child ->
     ``AwkFatal("cannot open pipe `<name>': No such file or directory")``.
2. Standard streams are written at once; a file or pipe stream appends to its
   buffer, written out when it reaches `kBuiltinOutBufferSize` (and on
   flush/close). Writing loops until every byte is out: `kIOBrokenPipe` ->
   `AwkFatal("<statement> to \"<name>\" failed: Broken pipe")` (gawk
   `--posix` ignores SIGPIPE for redirections: status 2, not 141); any other
   negative result but `kIOInterrupted` -> the same with `Input/output
   error`; `kIOInterrupted` (stopped) -> return quietly. A stream whose write
   failed is marked and never written again.

**ReadRecord**:
1. Find the stream, or open it:
   - InputFile `-` or `/dev/stdin`: descriptor 0 (`context.IO().GetDescriptor(0)`;
     empty -> -1). Each name is a stream of its own with its own reader (a
     reader reads ahead, so mixing them with the main input from standard
     input is unspecified -- as in gawk, which reads `-` and `/dev/stdin`
     through separate buffers).
   - Other InputFile: `OpenInputOperand(context, name, failure)`; any failure
     (missing, directory, ...) -> -1, nothing registered.
   - InputPipe: `FlushAll()`; a pipe as above; `StartProgram` with
     `options.stdOut` = the write end (stdin and stderr: awk's own); **release
     awk's copy of the write end** (else the read never ends). No shell, no
     pipe or no child -> -1.
   - A `RecordReader` over the descriptor.
2. A stream at its end returns 0 again (it stays open until `close`). Else
   `Next(rs, record)`: Record -> 1; End -> 0 (marked); Error -> -1; Stopped
   -> 0 (the interpreter's `ThrowIfStopped` unwinds right after).

**Close(name)**: every stream of that name, any kind, in opening order:
output streams first written out (`/dev/stdout`: `context.Flush()`); a file
released; an output pipe's write end released, then `WaitForProgram`; an
input pipe's reader and read end released, then `WaitForProgram`; removed
from the table. Returns 0 -- for a pipe too, whatever the command's exit
status (gawk `--posix`) -- or -1 when none was open or an output could not be
written out.

**Flush(name)**: null or (from the interpreter) `""` -> `FlushAll()`, 0.
`/dev/stdout` -> `context.Flush()`, 0; `/dev/stderr` -> 0 -- both even when
not opened (gawk). Otherwise the output streams of that name written out, 0
(-1 on a failed write); none -> -1 (the interpreter writes the warning).
`-` is not special here: not opened, it is -1 with the warning.

**FlushAll()**: `context.Flush()`, then every output stream's buffer
(failures only mark the stream).

**RunSystem(command)**: `FlushAll()`; no shell -> 127; else
`RunProgramAndWait(context, shell, {"-c", command})` (the child gets awk's
own standard streams and environment, and awk's working directory), its code
c mapped as plain gawk 5 reports it (not `--posix`): 143 (stopped: Haisos's
SIGTERM) -> 271 (256 + 15), 141 (broken pipe: SIGPIPE) -> 269 (256 + 13),
any other c -> c (`exit 3` -> 3, not found -> 127). No shell -> 127. `system("")` runs `hsh -c ''` (0) and
flushes everything, as gawk.

**CloseAll()**: `Close` of every stream, in opening order. When the process
is being stopped, `WaitForProgram` stops each child and waits at most its
grace time.

### `commands/awk/AwkInterpreter.h` / `.cpp`

- `AwkStreams m_streams;` (constructed with `m_context`).
- **Output(stmt, text)**: no redirection -> `m_context.Out(text)` as before.
  Otherwise the target's value `ToString(CONVFMT)`; an empty name ->
  ``AwkFatal("expression for `>' redirection has null string value")`` (`>>`,
  `|` alike); then `m_streams.Write(stmt.redirect, name, text, stmt.kind ==
  StmtKind::Printf ? "printf" : "print")`, then `ThrowIfStopped()`.
- **EvaluateGetline(expr)** (paragraph mode = RS is `""`):
  - `Simple`: the next record of the main input -- split `NextMainRecord`
    into `bool NextMainRecordText(std::string& record)` (the operands as ARGV
    is now, FILENAME, NR + 1, FNR + 1, its fatals) and the `SetRecord` around
    it. No target: `$0` and NF set; with a target: `Assign(target,
    Value::FromInput(text))` ($0 untouched). 1, or 0 when the main input is
    used up (in END too). Works in BEGIN (it opens the first operand, and the
    main loop goes on from the next record).
  - `File`: the name (`ToString(CONVFMT)` of `operands[0]`; empty ->
    ``AwkFatal("expression for `<' redirection has null string value")``);
    `ReadRecord(InputFile, ...)`; on 1: a target gets `FromInput(text)`, else
    `m_fields.SetRecord(text, FS, paragraphMode)` -- NR and FNR unchanged.
  - `Command`: the same with InputPipe (empty name: the `|` message); NR and
    FNR unchanged in both forms (gawk).
  - `ThrowIfStopped()` after reading; the result is a Number (1, 0, -1).
- **CallBuiltin** (`AwkBuiltins.cpp`), replacing the placeholders:
  `close(x)` -> `Close(ToString(CONVFMT))`; `fflush()` -> `Flush(nullptr)`;
  `fflush(x)` -> `""` means everything, else `Flush(&name)`, and on -1
  `RuntimeWarning("fflush: `<name>' is not an open file, pipe or co-process")`;
  `system(x)` -> `RunSystem(...)` then `ThrowIfStopped()`. All return Numbers.
- **Prepare**: `ENVIRON[name] = Value::FromInput(value)` for every variable
  of `m_context.Process().GetEnvironment()` (`GetVariableNames`,
  `GetVariable`; secrets and LLM identifiers are not variables). Changing
  `ENVIRON` changes nothing for commands (gawk: they get awk's environment).
- **NextMainRecord(Text)**: an operand `/dev/stdin` is standard input, like
  `-` (FILENAME stays as written).
- **Run**: `m_streams.CloseAll()` on every way out -- after END, after
  `exit`, after a fatal error has been written (gawk writes the message,
  then its pipes finish), when stopped or on a broken stdout -- and before
  returning, so the children's output reaches the shared descriptors before
  awk's own buffered stdout is written by `~BuiltinContext`.

### `commands/awk/Awk.cpp`

`Help().notes`: drop the last "not available yet" line; add `commands run in
hsh (-c), found in PATH`; the documented exceptions `/dev/stdout,
/dev/stderr, /dev/stdin and - are awk's own streams (gawk --posix opens the
devices)`, `system() and close() of a pipe return the exit status as plain
gawk does (256 + the signal for a stopped command or a broken pipe), not
gawk --posix's wait status`.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkStreams.cpp`.

### Rules that apply (root `CLAUDE.md`)

- `ICurrentProcess` is the only door out of a process: files through
  `context.IO()` (`OpenFile`, `OpenInputOperand`, `Stat`), pipes only through
  `context.IO().CreatePipe()`, commands only through `StartProgram` /
  `RunProgramAndWait` (which use `context.Process().OS()->StartProcess`, the
  OS released at once). Nothing holds an `IFileSystem`, `IHaisosOS` or
  `IPipeService`.
- A pipe end awk does not use is released at once: the read end of an
  output pipe and the write end of an input pipe.
- Stops promptly: a blocked pipe read or write returns `kIOInterrupted` on
  `TriggerStop()`; `WaitForProgram` stops the children; status 143. A broken
  standard output stays 141 (`context.Out`); a broken redirected pipe is
  gawk's fatal error (status 2).
- Output byte for byte as gawk `--posix`, the exceptions documented in
  `--help` notes (from `BuiltinHelpText`) and the CLAUDE.md table.
- Portable C++17: no POSIX headers, no `<regex>`.

## Tests

New `tests/unit/components/Awk.unittests/AwkIoTest.cpp` (in that
directory's `CMakeLists.txt`): `class AwkIoTest : public AwkRunTest {};`,
`TEST_F`s on `RunCaptured("awk", ...)` with exact `out`, `err`, `status`
(0 and empty err unless given); files the program writes are read back with
`ReadWholeFile(*root, path, text)`. Programs are the awk text (C++ raw
strings); expected texts use C escapes. Commands (`echo`, `cat`, `sort`,
`true`, `false`, `sleep`, `hsh`) are the fixture's `/bin` builtins, found
through the default search path (the fixture's environment has no PATH).

- `GetlineMainInput`:
  input `l1\nl2 x\nl3\nl4\n`, `{ print "rec", NR, FNR, NF, $0; r = getline; print "got", r, NR, FNR, NF, $0 }`
  -> `rec 1 1 1 l1\ngot 1 2 2 2 l2 x\nrec 3 3 1 l3\ngot 1 4 4 1 l4\n`;
  input `l1\nl2 x\nl3\n`, `{ r = getline v; print r, NR, FNR, NF, $0, "v=" v }`
  -> `1 2 2 1 l1 v=l2 x\n0 3 3 1 l3 v=l2 x\n`;
  `BEGIN { getline; print "B", NR, $0 } { print "M", NR, $0 }` `/abc.txt` -> `B 1 a b c\nM 2 d e f\n`;
  `BEGIN { while ((getline line) > 0) n++; print n, NR, FNR, FILENAME }` `/abc.txt /data.csv` -> `5 5 3 /data.csv\n`;
  `END { r = getline; print r, $0 }` `/abc.txt` -> `0 d e f\n`;
  input `a\n`, `BEGIN { getline; getline x; print NR, $0, "[" x "]" }` -> `1 a []\n`.
- `GetlineFromFiles`:
  input `l1\n`, `NR == 1 { while ((r = (getline line < "/abc.txt")) > 0) print r, NR, FNR, NF, line; print "end", r; r = getline line < "/abc.txt"; print "again", r }`
  -> `1 1 1 1 a b c\n1 1 1 1 d e f\nend 0\nagain 0\n`;
  input `l1\n`, `{ while ((getline < "/abc.txt") > 0) print NR, FNR, NF, $2 }` -> `1 1 3 b\n1 1 3 e\n`;
  `BEGIN { r = getline line < "/nope.txt"; print r; r = getline < "/docs"; print r }` -> `-1\n-1\n`;
  `{ getline x < FILENAME; print $0 "|" x }` `/abc.txt` -> `a b c|a b c\nd e f|d e f\n`;
  `BEGIN { getline a < "/abc.txt"; close("/abc.txt"); getline b < "/abc.txt"; print a "|" b }` -> `a b c|a b c\n`;
  `BEGIN { RS = "e"; while ((getline x < "/abc.txt") > 0) print "[" x "]" }` -> `[a b c\nd ]\n[ f\n]\n`.
- `GetlineFromCommands`:
  `BEGIN { while (("echo a b; echo c" | getline) > 0) print NR, NF, $0; print close("echo a b; echo c") }` -> `0 2 a b\n0 1 c\n0\n`;
  `BEGIN { while (("echo a b; echo c" | getline v) > 0) print NR, NF, v }` -> `0 0 a b\n0 0 c\n`;
  `BEGIN { while (("echo 1; echo 2" | getline) > 0) n++; print n, NR; print ("echo 1; echo 2" | getline), close("echo 1; echo 2"), ("echo 1; echo 2" | getline), $0 }` -> `2 0\n0 0 1 1\n`;
  `BEGIN { "echo a" | getline; "echo b" | getline; print $0; print close("echo a"), close("echo b"), close("echo a") }` -> `b\n0 0 -1\n`;
  input `in1\n`, `BEGIN { "cat" | getline x; print "[" x "]" }` -> `[in1]\n` (the command reads awk's stdin);
  `BEGIN { r = ("nocmdx" | getline); print r }` -> out `0\n`, err hsh's not-found line for `nocmdx` (assert it contains `nocmdx: not found`).
- `OutputRedirections`:
  `BEGIN { print "a" > "/out1.txt"; print "b" > "/out1.txt"; close("/out1.txt"); print "c" >> "/out1.txt"; print close("/out1.txt"), close("/out1.txt"), close("never"); while ((getline l < "/out1.txt") > 0) print "read", l }`
  -> `0 -1 -1\nread a\nread b\nread c\n`, and `/out1.txt` is `a\nb\nc\n`;
  `BEGIN { print "x" > "/out2.txt"; print "y" >> "/out2.txt"; close("/out2.txt"); while ((getline l < "/out2.txt") > 0) print "read", l }` -> `read x\nread y\n`;
  `BEGIN { printf "%s-%d\n", "n", 1 > "/out" "9.txt"; close("/out9.txt"); getline l < "/out9.txt"; print l }` -> `n-1\n`;
  `BEGIN { print "x" > "/out4.txt"; getline l < "/out4.txt"; print "[" l "]" }` -> `[]\n` (not written out before close);
  `BEGIN { print "x" > "/out10.txt"; print "y" > "/out10.txt"; print close("/out10.txt"); getline l < "/out10.txt"; print l }` -> `0\nx\n`;
  `BEGIN { print "x" | "cat"; print "y" > "cat"; close("cat") }` -> out `x\n`, and `/cat` is `y\n`.
- `RedirectionErrors` (status 2, out empty, err exactly):
  `BEGIN { print "x" > "/nonexistent/dir/f"; print "after" }` -> ``awk: cmd. line:1: fatal: cannot redirect to `/nonexistent/dir/f': No such file or directory\n``;
  `BEGIN { print "x" > "/docs" }` -> ``awk: cmd. line:1: fatal: cannot redirect to `/docs': Is a directory\n``;
  `BEGIN { printf "x" >> "/nonexistent/f" }` -> ``awk: cmd. line:1: fatal: cannot redirect to `/nonexistent/f': No such file or directory\n``;
  `BEGIN { print "x" > "" }`, `BEGIN { print "x" | "" }`, `BEGIN { "" | getline }`, `BEGIN { getline x < "" }`
  -> ``awk: cmd. line:1: fatal: expression for `>' redirection has null string value\n`` (with `>`, `|`, `|`, `<` respectively);
  `BEGIN { for (i = 0; i < 100000; i++) print i | "true"; print "end" }` -> `awk: cmd. line:1: fatal: print to "true" failed: Broken pipe\n`;
  the same with `printf "%d\n", i | "true"` -> `awk: cmd. line:1: fatal: printf to "true" failed: Broken pipe\n`.
- `PipesAndOrdering` (stdout a file, as `RunCaptured` gives):
  `BEGIN { print "z" | "cat"; print "w" | "cat"; r = close("cat"); print "closed", r }` -> `z\nw\nclosed 0\n`;
  `BEGIN { print "z" | "cat; exit 3"; print "closed", close("cat; exit 3") }` -> `z\nclosed 3\n`;
  `BEGIN { printf "1"; system("echo 2"); print "3" }` -> `12\n3\n`;
  `BEGIN { printf "1"; print "2" | "cat"; print "3" }` -> `12\n3\n`;
  `BEGIN { printf "1"; print "2" | "cat"; close("cat"); print "3" }` -> `12\n3\n`;
  `BEGIN { print "to-cat" | "cat"; print "direct" }` -> `to-cat\ndirect\n`;
  `BEGIN { print "a" > "/o6.txt"; system("cat /o6.txt"); print "b" }` -> `a\nb\n`;
  `{ print $3 | "sort -r" } END { close("sort -r"); print "done" }` `/abc.txt` -> `f\nc\ndone\n`.
- `SystemStatus`:
  `BEGIN { r = system("exit 3"); print r; print system("true"); print system(""); print system("false") }` -> `3\n0\n0\n1\n`;
  `BEGIN { print system("nocmdx") }` -> out `127\n`, err contains `nocmdx: not found`;
  input `in1\nin2\n`, `BEGIN { system("cat") }` -> `in1\nin2\n`.
- `Fflush`:
  `BEGIN { print fflush(), fflush(""), fflush("nope"), fflush("/dev/stdout") }` -> out `0 0 -1 0\n`,
  err ``awk: cmd. line:1: warning: fflush: `nope' is not an open file, pipe or co-process\n``;
  `BEGIN { print "a" > "/out3.txt"; print fflush("/out3.txt"); while ((getline l < "/out3.txt") > 0) print "read", l }` -> `0\nread a\n`;
  `BEGIN { print fflush("-"), fflush("/dev/stderr") }` -> out `-1 0\n`,
  err ``awk: cmd. line:1: warning: fflush: `-' is not an open file, pipe or co-process\n``.
- `SpecialFiles` (Haisos's behaviour; gawk `--posix` loses lines here, the documented exception):
  `BEGIN { print "a" > "/dev/stderr"; print "b" > "/dev/stdout"; print "c" > "-"; print "d" | "cat 1>&2"; print "e" }`
  -> out `b\nc\ne\n`, err `a\nd\n`;
  `BEGIN { print "x" > "/dev/stdout"; print close("/dev/stdout"); print "y" }` -> `x\n0\ny\n`;
  `BEGIN { print close("/dev/stderr") }` -> `-1\n`;
  input `z\n`, `BEGIN { getline v < "-"; print v; getline w < "/dev/stdin"; print "[" w "]" }` -> `z\n[]\n`;
  input `q\n`, `{ print FILENAME, $0 }` `/dev/stdin` -> `/dev/stdin q\n`.
- `Environ`: an environment (`factory->CreateEnvironment()`) with `HOME_X=hx`
  passed to `RunCaptured`: `BEGIN { print ENVIRON["HOME_X"], length(ENVIRON["NOPE"]); ENVIRON["HOME_X"] = "changed"; system("echo $HOME_X") }`
  -> `hx 0\nhx\n`; `BEGIN { for (k in ENVIRON) n++; print (n > 0) }` -> `1\n`.
- `ExitCodes`: `BEGIN { exit -1 }` -> 255; `BEGIN { exit 256 }` -> 0;
  `BEGIN { exit "3x" }` -> 3; `BEGIN { exit 2.9 }` -> 2 (out and err empty);
  `BEGIN { print "x" | "cat"; exit 3 }` -> out `x\n`, status 3;
  `BEGIN { print "x" | "cat"; print "f" > "/o7.txt"; z = 0; y = 1 / z }` -> out `x\n`,
  err `awk: cmd. line:1: fatal: division by zero attempted\n`, status 2, `/o7.txt` is `f\n`.
- `StopsPromptly`: start `/bin/awk` with `os->StartProcess` (stdout and
  stderr in-memory files as `RunCaptured` makes them), ~100 ms, `TriggerStop()`,
  then `WaitToFinish(3000)` true and exit code 143, for each of
  `BEGIN { system("sleep 100") }`, `BEGIN { while (("sleep 100" | getline) > 0) ; }`
  and `BEGIN { print "x" | "sleep 100" }` (the last stops while waiting for
  the pipe's command at the end); afterwards `os->GetRunningProcesses()`
  holds no `sleep` (the children were stopped).

Update earlier tests: awk--interpreter's and awk--printf-math's
`NotYetAvailable` placeholders for getline, redirections and `close` (they
now run) -- remove them.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
./output/linux/Awk.unittests --gtest_filter='AwkIoTest.*'
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Env*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/awk/CLAUDE.md`: an "Input and output" section -- `AwkStreams`
  (streams by name and kind, buffering, the special names, open/close/flush
  rules and return values, error texts), getline's forms and their effects
  on `$0 NF NR FNR`, the shell and how commands are started (pipes made with
  `IO().CreatePipe`, the unused end released, `StartProgram` /
  `WaitForProgram`, flush before a command, `CloseAll` before the final
  stdout), `system()`'s status mapping, `ENVIRON`; `AwkStreams.h/.cpp` in the
  file list; the exceptions under "Documented exceptions".
- `src/components/BuiltinCommands/CLAUDE.md`: under "Key Classes",
  `BuiltinRunProgram` gains `StartProgram` and `WaitForProgram`; the awk row:
  the whole of POSIX awk now, with its exceptions.
- Root `CLAUDE.md`: the awk row (POSIX awk as gawk `--posix`: patterns,
  fields, arrays, functions, printf, getline, redirections, pipes and
  `system` through hsh).

## Acceptance

- [ ] `StartProgram` and `WaitForProgram` declared exactly;
  `RunProgramAndWait` built on them; env/find/xargs tests still green.
- [ ] Programs start only through `StartProgram`/`RunProgramAndWait`; pipes
  only through `context.IO().CreatePipe()`; files only through
  `context.IO()`; the unused pipe end released at once.
- [ ] getline's forms, return values and NR/FNR/NF effects exactly as
  specified; streams reused by name and kind; `close`/`fflush`/`system`
  results exactly as specified.
- [ ] Output is flushed before any command starts; all streams are closed
  before awk's stdout is written at the end -- on exit, fatal errors and
  stops too.
- [ ] Stops within a second or so of `TriggerStop()` while blocked on a pipe
  or waiting for a command; its children are stopped.
- [ ] Every test above passes byte for byte; no expectation changed to
  mawk's; all unit tests green; the three CLAUDE.md files updated.

## Out of scope

- gawk extensions: `|&` coprocesses, `close(cmd, "to")`, `/dev/fd/N`,
  `/inet/...`, `PROCINFO`, `RT`, `BEGINFILE`/`ENDFILE`, `ERRNO`.
- Running commands in `sh` (Haisos has `hsh`) or signals beyond the stop
  and broken-pipe mapping.
