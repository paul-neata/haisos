# Task builtins--directories: A directory per builtin, manual pages, cat reading stdin, ls into a pipe

- Rock: builtins
- Depends on: streams--console-and-start (and, through it, fd--descriptor-objects, fd--process-table)
- Size: ~500 changed lines in ~14 files (five of them renames)
- Plan checked against: develop @ 0d92271
- PR title: Builtins in directories of their own; cat reads stdin, ls pipes

## Goal

Afterwards:

- Every builtin lives in a directory of its own,
  `src/components/BuiltinCommands/commands/<name>/<Name>.cpp`, ready for
  builtins made of several files (`hsh`, later).
- Every builtin has a manual page, `IBuiltinCommand::ManPage()`, which is by
  default exactly its `--help` text (so the coming `man <name>` prints exactly
  what `<name> --help` prints).
- `cat` with no FILE, or with a FILE of `-`, reads its standard input
  (descriptor 0), as GNU cat does. `echo hi | cat` style pipelines work.
- `ls` prints as GNU ls does when its standard output (descriptor 1) is not a
  terminal (a pipe, a file, a device): one name per line unless a layout is
  asked for, names unquoted and control characters written raw. On a
  terminal (the console) nothing changes, except that names are now quoted
  exactly as GNU's shell-escape quoting does (control characters as
  `$'\001'`, `]` and `{` not quoted) -- see "Shared quoting" below.
- The unit-test fixture lives in a header of its own so the next tasks
  (`builtins--wc`, `builtins--man`) can each add a test file, and it can run a
  builtin with its standard streams connected to in-memory files, giving
  stdout and stderr byte for byte.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security:
`ICurrentProcess` is the only door out of a process", rule 9 of "Automatic
Development Rules"), `src/components/BuiltinCommands/CLAUDE.md`, then all of
`src/components/BuiltinCommands/` and
`tests/unit/components/BuiltinCommands.unittests/`.

What earlier tasks provide (on develop when this task starts; their
interfaces are the truth -- if a name below differs from what is in
`interfaces/`, use the interface and say so in the PR):

- fd--descriptor-objects: `interfaces/IFileDescriptor.h` --
  `class IFileDescriptor` with `ssize_t Read(void* buf, size_t count)`,
  `ssize_t Write(const void* buf, size_t count)`, `bool IsTerminal() const`;
  negative results `kIOError` (-1), `kIOBrokenPipe` (-2), `kIOInterrupted`
  (-3). `IFileSystem::OpenFile` / `IFileIO::OpenFile` return
  `std::shared_ptr<IFileDescriptor>` (null on failure); `ReadFile`,
  `WriteFile`, `CloseFile` are gone (cat already uses the descriptor object).
- fd--process-table: on `IFileIO`, `GetDescriptor(int fd)` (null if the slot is
  empty), and the class constants `IFileIO::kStdIn` (0), `IFileIO::kStdOut` (1),
  `IFileIO::kStdErr` (2).
- streams--console-and-start: `StartProcessOptions` has
  `std::shared_ptr<IFileDescriptor> stdIn, stdOut, stdErr` (null: console
  output / console error, and an empty input whose reads return 0 at once);
  `BuiltinContext::Out` writes to descriptor 1, `Error`, `TryHelp` and the
  not-treated reports to descriptor 2; `BuiltinCommandHost::console` is gone;
  console descriptors answer `IsTerminal()` true, pipes and files false. The
  fixture's `Run(...)` helper captures console output as that task left it.
- Nothing here needs pipes (pipes--pipe-service): the tests connect standard
  streams to in-memory files.

GNU behaviour this task copies was checked against GNU coreutils 9.4
(`src/ls.c`, `decode_switches`): the defaults below are what `ls` does when
`isatty(STDOUT_FILENO)` is false.

## Changes

### Directory layout (renames: use `git mv`, so history follows)

| From | To |
|------|----|
| `src/components/BuiltinCommands/commands/Cat.cpp` | `src/components/BuiltinCommands/commands/cat/Cat.cpp` |
| `.../commands/Echo.cpp` | `.../commands/echo/Echo.cpp` |
| `.../commands/Ls.cpp` | `.../commands/ls/Ls.cpp` |
| `.../commands/Mkdir.cpp` | `.../commands/mkdir/Mkdir.cpp` |
| `.../commands/Pwd.cpp` | `.../commands/pwd/Pwd.cpp` |

The files keep `#include "BuiltinCommand.h"` (the component directory is
already a public include directory). `src/components/BuiltinCommands/CMakeLists.txt`:
the five source paths become `commands/cat/Cat.cpp` etc. Nothing else in the
build changes. Tests do **not** move into per-command directories: they stay
in `tests/unit/components/BuiltinCommands.unittests/`, one file per command
from now on for new commands (see Tests).

`BuiltinCommandList.h`: the comment says each factory is defined in its own
directory, `commands/<name>/`.

### `src/components/BuiltinCommands/BuiltinCommand.h` / `.cpp`

1. `IBuiltinCommand` gains, after `Help()`:
   ```cpp
   // The builtin's manual page, as `man <name>` prints it: plain text, ending
   // in a newline. By default exactly its --help text (BuiltinHelpText), so
   // `man <name>` and `<name> --help` print the same; a builtin with more to
   // say (hsh) overrides it.
   virtual std::string ManPage() const;
   ```
   Defined in `BuiltinCommand.cpp` as returning `BuiltinHelpText(*this)` (it
   cannot be inline in the class: `BuiltinHelpText` is declared after it).
   No command overrides it in this task.

2. `BuiltinContext` gains (only if streams--console-and-start has not already
   added a method doing exactly this -- then use that one and add nothing):
   ```cpp
   // Standard error, text exactly as given: no "<name>: " prefix, no newline
   // added. For the lines GNU tools print without their name ("Valid
   // arguments are:", man-db's "No manual entry for x").
   void ErrorText(const std::string& text);
   // Whether standard output (descriptor 1) is a terminal: false when the
   // slot is empty.
   bool OutIsTerminal() const;
   ```
   streams--console-and-start already keeps `m_outIsTerminal` (taken at
   construction from `GetDescriptor(IFileIO::kStdOut)`): `OutIsTerminal()`
   returns it. `ErrorText` does what `Error` does (`Flush()` the stdout
   buffer first, then one write through the private `WriteAll` to the stderr
   descriptor) without the prefix and the added newline; `TryHelp` may be
   rewritten on top of it.

3. Shared quoting, moved out of `Ls.cpp` (its `NeedsQuoting` and `Quote` go)
   and made exactly GNU's `quotearg` shell-escape styles, which ls (on a
   terminal), and next wc and man, use for names:
   ```cpp
   // A name as GNU tools print it in shell-escape quoting: as it is when no
   // shell would read anything in it specially, else quoted. |always| quotes
   // even a name that needs none (GNU's quoteaf, used for "cannot open 'x'"),
   // otherwise only when needed (GNU's quotef and ls on a terminal).
   std::string ShellEscapeQuoted(const std::string& name, bool always = false);
   ```
   The rules, byte for byte (verified with GNU ls 9.4 `--quoting-style=shell-escape`):
   - A name **needs quoting** when it is empty; or its first byte is `#` or
     `~`; or it is exactly `{` or `}`; or it contains a byte below 0x20, 0x7F,
     or any of `` ' ' ! " $ & ' ( ) * ; < = > ? [ \ ^ ` | `` (space included).
     `]`, `{`, `}`, `#`, `~` elsewhere, and `% + , - . / : @ _` do not need
     quoting. Bytes 0x80 and above are kept as they are (valid UTF-8 is
     printable; an invalid byte, which GNU would write as `\NNN`, is kept
     too -- say so in the function's comment).
   - Needs no quoting and not `always`: the name as is.
   - No control byte and no `'`: `'name'`.
   - A `'`, no control byte, and none of `` ! " $ & ( ) * ; < = > ? [ \ ^ ` | ``:
     `"name"` (`it's` -> `"it's"`, `it's x` -> `"it's x"`).
   - Otherwise: `'` + the name with each `'` written `'\''` and each run of
     control bytes written as: close the quote (`'`), then `$'`, then each
     byte as `\a \b \t \n \v \f \r` for 7..13, else `\` and three octal
     digits (`\001`, `\033`, `\177`), then `'`; after the run, reopen with `'`
     only if more bytes follow; the closing `'` at the end is written only if
     the name does not end in such a run. Examples:
     `nl<LF>y` -> `'nl'$'\n''y'`, `<LF>b` -> `''$'\n''b'`,
     `a<SOH><STX>b` -> `'a'$'\001\002''b'`, `x<DEL>y` -> `'x'$'\177''y'`,
     `tab<TAB>t` -> `'tab'$'\t''t'`, `it's $x` -> `'it'\''s $x'`,
     `a<SOH>` -> `'a'$'\001'`.
   - `always` with a name needing no quoting: `'name'`.
   (A name holding both a `'` and a control byte gets whatever these rules
   give; GNU differs slightly there and it is not tested.)

### `commands/cat/Cat.cpp` -- reads standard input

- No operands means one operand `-`. An operand `-` reads descriptor 0
  (`context.IO().GetDescriptor(IFileIO::kStdIn)`), through the same `CatFormatter`, so
  numbering and squeezing carry across files and stdin as GNU does. A second
  `-` reads on from where the first stopped (usually at end of input: nothing).
- Reading stdin: `Read` > 0 -> format and `Out`; 0 -> done;
  `kIOInterrupted` -> stop at once, quietly, status 1; any other negative ->
  `cat: -: Input/output error`, status 1, go on with the next operand. An
  empty slot 0 -> `cat: -: Bad file descriptor`, status 1.
- The two "reading standard input is not supported" errors go, and so does
  the help note `No standard input: at least one FILE, and not -.`; the notes
  become empty (`""`) unless something else is documented.
- Version `1.1.0` -> `1.2.0`.

### `commands/ls/Ls.cpp` -- terminal or not

`LsSettings` gains `bool formatGiven = false;` and `bool hideControlChars`.
In `ApplyOptions`:

- Every option that sets a layout sets `formatGiven`: `-C`, `-x`, `-m`, `-l`,
  `-g`, `-o`, `-1`, `--format=...`, `--full-time`.
- `-1` sets `OnePerLine` **only if the format is not already `Long`** (GNU:
  "-1 has no effect after -l"); `ls -l1` and `ls -1l` are both long listings.
- After the loop (before the existing `-u`/`-c` sort rule), with
  `terminal = context.OutIsTerminal()`:
  - `!formatGiven && !terminal` -> `format = OnePerLine`.
  - `!terminal` -> `literal = true` (GNU's default quoting is `literal` off a
    terminal; `-N` still forces it on a terminal).
  - `hideControlChars = terminal` (GNU's `-q` default; `-q` and
    `--show-control-chars` stay not treated).
- The width rules do not change (80 unless `-w`; GNU ignores the terminal size
  off a terminal and uses `COLUMNS`/80 -- Haisos has no terminal size).

`ShownNames`: a name is quoted (`ShellEscapeQuoted(name)`) when `!literal`
and it needs quoting; when `literal`, it is written as is, except that with
`hideControlChars` every byte below 0x20 and 0x7F becomes `?`. So:

| stdout | name `ctl<SOH>x` | name `with space` | layout without options |
|--------|------------------|-------------------|------------------------|
| terminal | `'ctl'$'\001''x'` | `'with space'` | columns |
| terminal, `-N` | `ctl?x` | `with space` | columns |
| pipe/file | `ctl<SOH>x` (raw byte) | `with space` | one per line |
| pipe/file, `-C` | raw | `with space` | columns (80 wide), no shift |

The one-character shift of unquoted names (when some are quoted) is
unchanged; off a terminal nothing is quoted, so nothing shifts. `-l` off a
terminal is the same long listing with literal names and no shift. `-R`
off a terminal: one name per line under each `dir:` header, blank line
between directories, as now. The documented exception "columns are padded
with spaces, not tabs" stays (GNU pads columns with tabs, on a pipe too).

Version `1.2.1` -> `1.3.0`. The help notes are unchanged (pipe behaviour is
GNU's own, not an exception).

### Root `CLAUDE.md` rules that bite

- Builtins print what the GNU command prints; every real option stays in the
  table; `--help` is generated (`BuiltinHelpText`), never hand-written; bump
  a builtin's version whenever its behaviour changes (cat, ls here; echo,
  mkdir, pwd keep theirs).
- Rule 9: no builtin is added, so the `--init` template is untouched.
- `ICurrentProcess` is the only door out: stdin and stdout are reached through
  `context.IO()` (the process's descriptor table), nothing else.
- Rule 7: repo-relative paths only, in code comments and docs.

## Tests

### Fixture -> `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h` (new)

Move out of `BuiltinCommandsTest.cpp`, unchanged except as said: the
capturing console, `kWaitMs`, `kDirMode`, the `BuiltinCommandsTest` fixture
class with `SetUp`, `WriteFile`, `Run`, `Contains`, and `Lines`,
`WithoutTimes`. Include guard `#pragma once`; everything stays in an
anonymous-namespace-free form usable from several `.cpp` files (mark free
functions `inline`). `BuiltinCommandsTest.cpp` includes it.

Add to the fixture:

- A member `std::shared_ptr<IFileSystem> streams`, a second empty in-memory
  filesystem created in `SetUp` and mounted nowhere: the files standing in
  for standard streams live there, so they never show in a listing.
- ```cpp
  struct Captured {
      std::string out;
      std::string err;
      int status = -1;
  };
  // Runs /bin/<command> with stdout and stderr connected to files (not
  // terminals) and, if |input| is given, stdin reading it (else the default
  // empty input). Returns both streams byte for byte and the exit status.
  Captured RunCaptured(const std::string& command, const std::vector<std::string>& args,
                       const std::optional<std::string>& input = std::nullopt,
                       const std::string& workingDirectory = "/");
  ```
  It writes `input` to `/in` on `streams` and opens it read-only for
  `stdIn`, opens `/out` and `/err` with `kFileOpenWriteCreateTruncate` for
  `stdOut`/`stdErr`, starts the process with those in `StartProcessOptions`,
  waits (`kWaitMs`), releases its own descriptor pointers, reads `/out` and
  `/err` back with `ReadWholeFile(*streams, ...)`. The status is read the same
  way `Run` reads it (`BuiltinProcess::ExitStatus()`, or
  `IProcess::ExitCode()` if streams--exit-codes has replaced it by then).
- ```cpp
  // Places empty files cat, echo, ls, mkdir, pwd in /five: a directory whose
  // listing does not change as builtins are added to /bin.
  void MakeFiveNames();
  ```

`tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt`: unchanged
(the header needs no entry); the next tasks add `WcTest.cpp` and
`ManTest.cpp` to `add_executable`.

### Existing tests to adjust (`BuiltinCommandsTest.cpp`)

- Listings of `/bin` in `LsListsInColumnsByDefault` and `LsLayouts` (the
  assertions on `"cat  echo  ls  mkdir  pwd"` and the `-m`, `-x`, `-C`, `-1`,
  `-r` variants) call `MakeFiveNames()` and list `/five` instead, with the
  same expected lines -- so `builtins--wc` and `builtins--man` do not have to
  recompute them.
- `CatReportsWhatItCannotRead`: the `Run("cat", {})` part goes (replaced
  below).
- Every expected `Parameter ... is not treated by HaisosOS ls v. 1.2.1` /
  `cat v. 1.1.0` line takes the new version (`grep -n "v. 1.2.1\|cat v. 1.1.0"`).

### New tests (in `BuiltinCommandsTest.cpp`, fixture `BuiltinCommandsTest`)

- `EveryBuiltinsManPageIsItsHelp`: for each of `CreateStandardBuiltinCommands()`,
  `ManPage() == BuiltinHelpText(*command)`, and it ends in `\n`.
- `CatReadsStandardInputWithoutFile`: `RunCaptured("cat", {}, "piped\nlines\n")`
  -> out `"piped\nlines\n"`, err empty, status 0.
- `CatReadsStandardInputForDash`: `RunCaptured("cat", {"-", "/docs/a.md", "-"}, "x\n")`
  -> out `"x\nalpha"` (the second `-` reads nothing); status 0.
- `CatNumbersAcrossStdinAndFiles`: `RunCaptured("cat", {"-n", "-", "/notes.txt"}, "in\n")`
  -> out `"     1\tin\n     2\tone\n     3\ttwo\n     4\t\n     5\t\n     6\t\tthree\n"`.
- `CatWithNoInputPrintsNothing`: `RunCaptured("cat", {})` (no input given)
  -> out empty, err empty, status 0.
- `LsIntoAPipeListsOneNamePerLine`: `MakeFiveNames()`;
  `RunCaptured("ls", {"/five"}).out == "cat\necho\nls\nmkdir\npwd\n"`;
  with `-C`: `"cat  echo  ls  mkdir  pwd\n"`; with `-x -w 16`:
  `"cat    echo  ls\nmkdir  pwd\n"`; with `-m`: `"cat, echo, ls, mkdir, pwd\n"`;
  with `-r`: `"pwd\nmkdir\nls\necho\ncat\n"`.
- `LsIntoAPipeDoesNotQuote`: dir `/q` with `plain`, `with space`, `it's`,
  `ctl\x01x`: `RunCaptured("ls", {"/q"}).out == "ctl\x01x\nit's\nplain\nwith space\n"`;
  `RunCaptured("ls", {"-C", "/q"}).out == "ctl\x01x  it's  plain  with space\n"`
  (no shift); `-l` lines end in the raw names (compare with `WithoutTimes`
  applied to the lines split from `out`).
- `LsOnATerminalQuotesAsGnuShellEscape`: dir `/q2` with `nl\ny`, `a]b`,
  `a{b`, `{`, `a=b`, `#h`, `a#`, `tab\tt`: `Run("ls", {"-1", "/q2"})`
  gives, in name order, `'#h'`, `a#`, `'a=b'`, `a]b`, `a{b`,
  `'nl'$'\n''y'`, `'tab'$'\t''t'`, `'{'` (sort by bytes: `#` 0x23 < `a` <
  `n` < `t` < `{`). With `-N` on the terminal: `nl?y`, `tab?t`.
- `LsOnePerLineAfterLongKeepsLong`: `Run("ls", {"-l1", "/docs"})` equals
  `Run("ls", {"-l", "/docs"})` (times removed).
- `ShellEscapeQuotedFollowsGnu` (plain `TEST(BuiltinArgsTest, ...)` is fine
  too, but then select it by running the binary): the examples of
  "Shared quoting" above, plus `""` -> `''`, `"plain"` with `always` ->
  `'plain'`, `"it's"` with `always` -> `"it's"`.

Existing terminal tests (`LsQuotesNamesAsTheRealOneDoes` and the rest) must
pass unchanged apart from the adjustments above.

### Commands

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```

(`test_linux.sh`'s filter must match the executable name *and* is passed as
`--gtest_filter=*<filter>*`, so `BuiltinCommands` selects every
`BuiltinCommandsTest.*`; run the binary directly for `BuiltinArgsTest.*`.)

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`:
  - "Output": rewrite for streams as streams--console-and-start left it, plus:
    `ErrorText`, `OutIsTerminal`; `ShellEscapeQuoted` as the one quoting of
    names.
  - Key Classes: `IBuiltinCommand::ManPage()` (default: the `--help` text).
  - Table: `cat` 1.2.0, exceptions `--` (stdin is read); `ls` 1.3.0, add to
    its description: off a terminal, one name per line unless `-C`/`-x`/`-m`/`-l`,
    names literal with control characters raw (GNU's defaults when stdout is
    not a terminal); `-1` after `-l` keeps the long listing.
  - The paragraph under the table: ls on a terminal quotes as GNU's
    shell-escape style, control characters as `$'\NNN'`.
  - "Adding a builtin": write `commands/<name>/<Name>.cpp` (more files in that
    directory if it needs them), list each source in `CMakeLists.txt`,
    declare its factory in `BuiltinCommandList.h`, add it to
    `CreateStandardBuiltinCommands()`; override `ManPage()` only if it has
    more to say than `--help`; tests go in
    `tests/unit/components/BuiltinCommands.unittests/<Name>Test.cpp` on the
    `BuiltinCommandsFixture.h` fixture (added to that `CMakeLists.txt`);
    then the table here and the root `CLAUDE.md` table.
- Root `CLAUDE.md`, "Builtin Commands" table: `cat` -- "Concatenates files
  and standard input (`-A -b -e -E -n -s -t -T -u -v`)"; `ls` -- add "to a
  pipe or file, one name per line, unquoted". The Directory Structure line
  for `BuiltinCommands/` says each builtin is in `commands/<name>/`.

## Acceptance

- [ ] `src/components/BuiltinCommands/commands/` holds only directories, one
      per builtin, each with its `<Name>.cpp`; `git log --follow` works on them.
- [ ] `IBuiltinCommand::ManPage()` exists, is virtual, and defaults to `BuiltinHelpText(*this)`.
- [ ] `cat` reads stdin for no FILE and for `-`; no "not supported" text remains (`grep -rn "not supported" src/components/BuiltinCommands` finds nothing).
- [ ] `ls` decides terminal or not only through `context.OutIsTerminal()`; on the console its output is as before except GNU-exact quoting.
- [ ] `ShellEscapeQuoted` is the only quoting code; `NeedsQuoting`/`Quote` are gone from `Ls.cpp`.
- [ ] cat 1.2.0, ls 1.3.0; echo, mkdir, pwd unchanged.
- [ ] The fixture is in `BuiltinCommandsFixture.h`; `RunCaptured` and `MakeFiveNames` exist; no test lists `/bin` expecting exactly five names.
- [ ] Both CLAUDE.md files updated as in Docs.
- [ ] Linux build passes; `BuiltinCommands.unittests`, all unit tests and the haisos tests pass.

## Out of scope

- `wc` (builtins--wc) and `man` (builtins--man); `hsh` and its `ManPage()` override (hsh--*).
- Quoting `ls -R` directory headers and file-operand names differently from
  now; GNU's tab padding of columns; `COLUMNS`/`TABSIZE`; `-q`,
  `--show-control-chars`, `--quoting-style` (still not treated).
- Changing the other builtins' error-message quoting (cat, mkdir) to
  `ShellEscapeQuoted`.
- Pipes; anything in `interfaces/`.
