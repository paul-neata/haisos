# BuiltinCommands

The commands compiled into Haisos itself -- `cat`, `echo`, `hsh`, `ls`, `man`,
`mkdir`, `pwd`, `wc` -- and what places them on filesystems. Implements `IBuiltinCommands`
and `IBuiltinConfigurator` (`interfaces/IBuiltinCommands.h`); both are created
through `IFactory` (`CreateBuiltinCommands`, `CreateBuiltinConfigurator`).

## How a builtin gets run

1. `IBuiltinConfigurator::AddBuiltinCommand(fs, path, name)` places the builtin
   on a filesystem, after checking that its directory exists and that nothing
   is at the path yet. The filesystem records it in its own builtin list (see
   the Filesystem component): the path then lists as a file, reads as a note
   (`BuiltinCommandFileContent`), cannot be written or removed, and pins its
   directory in place. The haisosfile's `BUILTIN` directive goes through here.
2. `IHaisosOS::StartProcess` asks its root filesystem `IsBuiltinCommand(path)`
   before looking at the extension. If it names a builtin, the OS fills in a
   `BuiltinCommandHost` -- its own weak self, a fresh pid, itself as parent,
   the program path -- and calls `IBuiltinCommands::RunCommand`, whose other
   parameters are those of `StartProcess` with the builtin's name in place of
   the path, the standard streams resolved already (a null one is refused).
3. `RunCommand` builds a `BuiltinProcess`, which installs the resolved streams
   as slots 0/1/2 of its `IFileIO` table before its thread starts, and returns
   at once; the command runs on the process's own thread.

## Key Classes

- `BuiltinCommands` - the `IBuiltinCommands`: a name -> command map filled once
  from `CreateStandardBuiltinCommands()` (`BuiltinCommandList.h`)
- `BuiltinConfigurator` - the `IBuiltinConfigurator`; stateless
- `BuiltinProcess` - the `ICurrentProcess` a builtin runs as, shaped like
  `LuaProcess`: a thread started by `Create()` once the object is whole,
  `TriggerStop()` sets a flag commands check between steps, the destructor
  joins. So it is never destroyed on its own thread (see "Creating things" in
  the root `CLAUDE.md`): `Create()` passes the `DestroyOffRuntimeThreads`
  deleter, and the command runs inside a `RuntimeThreadScope`. Its `IO()` is a
  `ProcessFileIO` (the separate `ProcessFileIO` library
  in `src/components/HaisosOS/`), so a builtin reaches files exactly as every
  other runtime does -- `ICurrentProcess` is still the only door out.
  `ExitCode()` (`IProcess::ExitCode`) is empty while the command runs, then
  its status modulo 256 -- or 143 when it was stopped, or 141 when a write of
  its output hit a pipe with no reader (`StopForBrokenPipe`, checked before
  the stop; see "Exit codes" in the root `CLAUDE.md`). The process also owns a `StopToken`
  (`src/components/libheaders/StopToken.h`), installed on the command's thread
  by `RunThread` and signalled by `TriggerStop()`, so a pipe `Read`/`Write`
  the command is blocked in returns `kIOInterrupted` at once rather than
  outliving the stop.
- `IBuiltinCommand` / `BuiltinContext` (`BuiltinCommand.h`) - one command, and
  what a run of it is handed: its process, arguments, output, and the stop flag.
  Commands are stateless, so one instance serves every process running it.
  `IBuiltinCommand::ManPage()` is the builtin's manual page, as `man <name>`
  prints it; by default exactly its `--help` text (`BuiltinHelpText`), so the
  two print the same -- a builtin with more to say overrides it.
- `ParseBuiltinArgs` - a GNU `getopt_long`-style parser shared by the commands:
  clustered short options, unambiguous long-option prefixes, options mixed with
  operands, `--`. `--help`/`--version` are recognized for every command.
  `BuiltinArgument::OptionalAttached` is man-db's `-Tutf8` kind: the argument
  is taken only when attached (`-Tutf8`, `--troff-device=utf8`), never from the
  next word (`-T utf8` is `-T`, then the operand utf8).
- `commands/hsh/` - `hsh`, the Haisos shell (dash reimplemented); has its own
  CLAUDE.md.

## Output

A builtin writes to its process's descriptor table, fetched once by
`BuiltinContext` at construction: `Out` to slot 1, `Error`
(`<name>: <message>`), the `Try '... --help'` line and the "not treated"
reports to slot 2. The output rule:

- stdout to a terminal is unbuffered -- each `Out(text)` is one write, so a
  partial line (a prompt) shows at once;
- stdout to anything else (a file, a pipe, a device) is block-buffered --
  written when the buffer reaches `kBuiltinOutBufferSize` (4096 bytes), before
  anything is written to stderr, and when the command ends (`~BuiltinContext`
  flushes it, which is how `echo -n` still shows its text);
- stderr is unbuffered -- every message is one write.

So `ls: cannot access ...` lands on the host's stderr (through the console
error descriptor), and `echo hi` lands raw and untagged on its stdout.
Messages follow the GNU coreutils wording. `ErrorText` writes bytes to stderr
exactly as given -- no `<name>: ` prefix, no newline -- for the lines GNU
tools print without their name; `Error`, `TryHelp` and the not-treated
reports are built on it. `OutIsTerminal()` is whether descriptor 1 is a
terminal (false when the slot is empty): what ls uses to pick its defaults,
as GNU ls picks them from `isatty(STDOUT_FILENO)`. A write that returns
`kIOBrokenPipe` -- the reader of the pipe is gone, on stdout or on stderr --
stops the builtin quietly with exit code 141, as SIGPIPE would
(`BuiltinContext::WriteAll` calls the process's `StopForBrokenPipe()`), and
everything after is dropped, a diagnostic included: the program is dying, so
nothing more may go out.

A name is quoted for printing in one place, `ShellEscapeQuoted`
(`BuiltinCommand.h`): GNU's shell-escape quoting, byte for byte -- the name
as is when no shell would read it specially, `'name'`, `"it's"`, or the
`'...'$'\n''...'` form for control bytes (`\a \b \t \n \v \f \r`, octal
`\NNN` for the rest); `always` quotes even a plain name. ls (on a terminal),
and every other builtin that prints names, uses it.

## The commands

The rules every builtin follows -- the real command's output format, every
real argument accepted (untreated ones reported as `Parameter --xyz is not
treated by HaisosOS <command> v. <version>`), and one `--help` shape ending in
the `Not treated arguments:` line -- are in the root `CLAUDE.md` (Builtin
Commands). Here they are built from `IBuiltinCommand::Options()`: one table
per command listing every option of the real one, `id` `kBuiltinNotTreated`
for those Haisos does not act on. `ParseBuiltinArgs`, `BeginBuiltin` (help,
version, usage errors, not-treated reports) and `BuiltinHelpText` all read
that table, so the help can never disagree with what is parsed. A command that
copies a real command of another name (hsh copies dash) says so in
`BuiltinHelp::basedOn`, and its "Based on Linux <command>:" line names that
command and links its man page.

| Command | Version | Treated | Documented exceptions |
|---------|---------|---------|-----------------------|
| `cat` | 1.2.0 | every option of GNU cat; with no FILE, or a FILE of `-`, the standard input is read | -- |
| `echo` | 1.1.0 | `-n -e -E`, the `-e` escapes; `--help`/`--version` only as the sole argument, as GNU echo | -- |
| `hsh` | 0.2.0 | dash's invocation (`-c`, a script file, standard input, `-a -c -C -e -f -i -n -o -s -u -x`), simple commands and `;` `&&` `||` `!` lists, every redirection (`<` `>` `>|` `>>` `<>` `n>&m` `n<&m` `n>&-` `&>`, heredocs `<<`-`<<-`, here-strings `<<<`, noclobber `-C` with `>|` overriding it), the shell builtins `:` `exec` `exit` `false` `true` | the reference command is dash, not hsh (`BuiltinHelp::basedOn`); `&>` and `<<<` are bash's operators; `--help`/`--version` only as the first argument; `$0` is `hsh` unless a script or `-c` command_name names it; pipelines, `&`, `$(...)`, compound commands and functions report `<what> is not supported yet` until their tasks land |
| `ls` | 1.3.0 | `-a -A -B -c -C -d -f -g -G -h -k -l -m -N -o -p -r -R -s -S -t -u -U -w -x -X -1`, `--file-type --format --full-time --group-directories-first --sort --time --time-style`; off a terminal (stdout a pipe, a file, a device), one name per line unless `-C`/`-x`/`-m`/`-l` asks for a layout, and names literal with control characters written raw -- GNU's own defaults when stdout is not a terminal; `-1` after `-l` keeps the long listing | owner and group are `haisos`, permissions `rwxrwxrwx` (no users or permissions yet), so a device shows as `crwxrwxrwx`, with its major and minor numbers in the size column as GNU ls shows them; columns are padded with spaces, not tabs; the width is 80 unless `-w` says otherwise; `--sort=version/width` and `--time=birth` are reported as not treated; on Windows, a `--time-style=+FORMAT` conversion the Microsoft C runtime lacks (`%k`, `%P`, ...) prints as written, as glibc prints one it does not know |
| `man` | 1.0.0 | `-f -k -i -I`, a section first (`man 1 ls`), several pages | pages are compiled in (each builtin's `ManPage()`, its `--help` unless overridden), all section 1, plain text, no pager; `-k` matches names and summaries only; everything else of man-db's reported as not treated |
| `mkdir` | 1.1.0 | `-p -v` | `-m`/`--mode`, `-Z`/`--context` not treated (no permissions or security contexts) |
| `pwd` | 1.1.0 | `-L -P` (the same: no symlinks) | -- |
| `wc` | 1.0.0 | `-c -m -l -L -w`, `--files0-from`, `--total`; with no FILE, or a FILE of `-`, the standard input is read | standard input has no size, so with it the columns are at least 7 wide (GNU sizes a redirected file); character classes and display widths come from the `Unicode` component's compact tables (unassigned code points count as printable; rare scripts' widths are approximate); `--debug` not treated |

`man` reaches the pages through `CreateStandardBuiltinCommands()` directly: a
pure function returning fresh, stateless command objects compiled into Haisos
-- program data, like a static table, not something outside the process. So
`man` is handed no `IHaisosOS`, no `IBuiltinCommands` and no filesystem, and
the `ICurrentProcess` rule holds untouched; the price is that man shows the
page of every builtin compiled into Haisos, whether or not it is placed on
the filesystem and whichever `IBuiltinCommands` the OS was created with.

`ls` follows GNU ls as it prints to a terminal: the column layout (a line
kept shorter than the width), `-l` with a `total` line in 1K blocks, link
counts, owner, group, right-aligned sizes and the locale time style (`Mon dd
HH:MM` for the last six months, `Mon dd  YYYY` otherwise, or `--time-style`),
quoting of names exactly as GNU's shell-escape style gives it
(`ShellEscapeQuoted`: `'with space'`, `"it's"`, control bytes out of the
quotes as `$'\001'`, as in `'ctl'$'\001''x'`; `]` and `{` elsewhere in a name
are not quoted) with unquoted names shifted by one to line up, and the sort
orders. Sizes, blocks, link counts and times come from `IFileIO::Stat`.

## Adding a builtin

Write `commands/<name>/<Name>.cpp` implementing `IBuiltinCommand` (it needs
only `BuiltinCommand.h`; a builtin made of several files puts them all in
that directory), list each source in `CMakeLists.txt`, declare its factory in
`BuiltinCommandList.h`, and add it to `CreateStandardBuiltinCommands()`.
Override `ManPage()` only if it has more to say than its `--help` text.
Tests go in `tests/unit/components/BuiltinCommands.unittests/<Name>Test.cpp`,
on the fixture in `BuiltinCommandsFixture.h` (its `RunCaptured` runs a
builtin with its standard streams connected to in-memory files, giving stdout
and stderr byte for byte); add the file to that directory's `CMakeLists.txt`.
That is all: `GetCommands()` then reports it, and since `haisos --init`
builds its template from `GetCommands()`, the generated haisosfile shows a
`# BUILTIN rootfs <name> /bin/<name>` line for it automatically. Add it to the
table above and to the root `CLAUDE.md` table.
