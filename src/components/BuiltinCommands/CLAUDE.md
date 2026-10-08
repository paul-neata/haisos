# BuiltinCommands

The commands compiled into Haisos itself -- `cat`, `cp`, `echo`, `env`,
`false`, `hsh`, `ls`, `man`, `mkdir`, `pwd`, `rm`, `rmdir`, `sleep`, `sort`,
`true`, `wc`, `which` -- and what places them on filesystems. Implements `IBuiltinCommands`
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
  `stopAtFirstOperand` is GNU getopt's "+" mode (env's): the first argument
  that is not an option ("-" alone included) ends the options, and every
  argument from it on is an operand untouched.
  `BuiltinArgument::OptionalAttached` is man-db's `-Tutf8` kind: the argument
  is taken only when attached (`-Tutf8`, `--troff-device=utf8`), never from the
  next word (`-T utf8` is `-T`, then the operand utf8).
- `BuiltinText.h` - the helpers text commands share: `GnuQuote` (GNU's
  `quote()` byte for byte), `ArgMatch` (XARGMATCH value matching with its
  `Valid arguments are:` diagnostic), `OpenInputOperand` + `BuiltinLineReader`
  (a file operand opened, and read a delimiter at a time), and `WriteFully`.
- `BuiltinCompare.h` - `CompareNumeric`, GNU strnumcmp's exact comparison:
  signs first, then digits of any length, never through a float;
  `CompareGeneralNumeric` (-g, by strtold: not-a-number < NaN < numbers),
  `CompareHumanNumeric` (-h, unit order then numeric), `CompareMonth`
  (-M) and `CompareVersion` (-V, gnulib's filevercmp -- a later `ls -v`
  reuses it).
- `BuiltinPrompt.h` - `BuiltinPrompt`, GNU's yesno(): writes the question to
  standard error exactly as given, reads one line from standard input, and
  answers yes only when its first byte is 'y' or 'Y' (end of input, a failed
  read and a stop while waiting are all no). One instance per run of a
  command, so lines read ahead stay for the next question; rm and cp -i use
  it, and mv -i will too.
- `BuiltinRemove.h` - `RemoveOperand`, removing one operand as GNU rm does:
  the messages and prompts of every mode (-r, -d, -i, -v, the root
  failsafe), one function for rm and for mv to call on a source it copied
  across filesystems.
- `BuiltinCopy.h` - the copying cp is made of, for mv to reuse on a source
  on another filesystem: `CopyPath` (one source/destination pair through
  GNU cp's checks -- stat, -r, same file, into-itself, type mismatch -- and
  the messages and prompts of every mode: -i, -n/--update, backups,
  --attributes-only, --remove-destination, -p's timestamps, -v's lines),
  `ResolveCopyTargets` (the operand rules, `-t`, `-T`, `--parents`,
  `--strip-trailing-slashes`), `BackupPathFor` and `ParseBackupControl`
  (GNU's backup controls, `VERSION_CONTROL` and `SIMPLE_BACKUP_SUFFIX`).
- `BuiltinRunProgram.h` - how a builtin runs another program, for env now and
  for xargs, find -exec and awk's `system()` later: `SearchPathEntries` (a
  PATH split at ':' keeping empty entries, `kBuiltinDefaultSearchPath` when
  unset), `FindProgramInPath` (a name with a '/' taken as it is, otherwise
  the first PATH entry holding a non-directory, an empty entry the working
  directory) and `RunProgramAndWait` (flushes the caller's buffered stdout
  first, so the child's output lands after it; starts the program only
  through `context.Process().OS()->StartProcess` -- the OS is asked for and
  released, never kept -- and waits in 50 ms slices, passing a stop on once
  and giving the child up to 5 s more; empty descriptor slots go to the
  child as closed, never null). Returns the child's exit code, 143 when it
  would not stop, and 127 with *started false when it never started --
  reporting that is the caller's business, as a shell's 127 is the
  launcher's).
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
| `cp` | 1.0.0 | `-a -b --backup[=CONTROL] -d -f -H -i -L -l -s -n -P -p --preserve[=ATTR_LIST] --no-preserve=ATTR_LIST --parents -R -r --remove-destination --strip-trailing-slashes -S -t -T -u --update[=UPDATE] -v --attributes-only`; GNU 9.4's messages, prompts, exit statuses and operand rules; backups (simple, numbered, existing; `VERSION_CONTROL`, `SIMPLE_BACKUP_SUFFIX`), the `--parents` walk, `--attributes-only` keeping the destination's data, the `-n` warning | `-l` and `-s` fail: HaisosOS creates no links; `-d`, `-H`, `-L` and `-P` change nothing (no links); `--preserve` keeps timestamps only (no modes, owners or links; `context` and `xattr` accepted and reported as not treated); entries of a directory are copied in name order, not the disk's; a directory copied into itself is refused before anything is copied; a failed backup, open, create or `SetTimes` whose reason `IFileIO` does not give is `Permission denied` |
| `echo` | 1.1.0 | `-n -e -E`, the `-e` escapes; `--help`/`--version` only as the sole argument, as GNU echo | -- |
| `env` | 1.0.0 | `-i -0 -u NAME -C DIR -S STRING`; a leading `-` operand as `-i`, then NAME=VALUE operands until COMMAND; the child gets the edited environment and COMMAND is looked up in the new PATH, as execvp does; `-S` splits GNU split-string's way (quotes, escapes, `${NAME}`, `#` comments, `\_` a word separator outside quotes and a space inside); usage errors 125, a not-runnable COMMAND 126, not found 127 | the signal options, `-v/--debug` and `--list-signal-handling` are not treated (no signals); with no COMMAND the variables print sorted by name -- `IEnvironment` keeps no order, where GNU prints the environment's own; `-i` empties the variables only (secrets and LLM identifiers stay); `${NAME}` in `-S` expands from the starting environment, as GNU's `getenv` does |
| `false` | 1.0.0 | nothing; `--help`/`--version` only as the sole argument | exits 1 even after `--help`/`--version`, as GNU's does |
| `hsh` | 1.0.1 | dash's invocation (`-c`, a script file, standard input, interactive with `-i` or when both standard input and standard error are terminals, `-a -c -C -e -f -i -n -o -s -u -x`), simple commands and `;` `&&` `||` `!` lists, pipelines (`a \| b`, `! a \| b`; a stage that must run in the shell runs in an in-process subshell), background lists (`&`, `$!`) with `wait`, command substitution (`$(...)`, `` `...` ``), every redirection (`<` `>` `>|` `>>` `<>` `n>&m` `n<&m` `n>&-` `&>`, heredocs `<<`-`<<-`, here-strings `<<<`, noclobber `-C` with `>|` overriding it), the compound commands `{ ...; }` `( ... )` `if`/`elif`/`else` `while` `until` `for` `case` and functions `f() { ...; }` with `return`, the shell builtins `.` `:` `[` `break` `cd` `continue` `eval` `exec` `exit` `export` `false` `read` `readonly` `return` `set` `shift` `test` `true` `unset` `wait`, with the option behaviours `-e` (errexit with dash's tested-context exceptions), `-x` (xtrace), `-a` (allexport) and `-n` (noexec) | the reference command is dash, not hsh (`BuiltinHelp::basedOn`); `ManPage()` is overridden: a full manual page (`man hsh`), not the `--help` text; `&>` and `<<<` are bash's operators; `--help`/`--version` only as the first argument; `$0` is `hsh` unless a script or `-c` command_name names it; a background (`&`) builtin, function or compound command runs in a child hsh, which sees only exported variables; PS1/PS2/PS4 are not expanded and `-v` is accepted, not acted on; `test`'s `-r`/`-w`/`-x`/`-O`/`-G` only test existence (no permissions or users), `-h`/`-L` are always false (no links) and `-ef` compares resolved paths (no inode numbers); two in-shell pipeline stages run one after the other, so an endless one written into another in-shell stage never ends (its output held in memory) where dash ends at once -- a child stage (a program) has no such limit |
| `ls` | 1.3.0 | `-a -A -B -c -C -d -f -g -G -h -k -l -m -N -o -p -r -R -s -S -t -u -U -w -x -X -1`, `--file-type --format --full-time --group-directories-first --sort --time --time-style`; off a terminal (stdout a pipe, a file, a device), one name per line unless `-C`/`-x`/`-m`/`-l` asks for a layout, and names literal with control characters written raw -- GNU's own defaults when stdout is not a terminal; `-1` after `-l` keeps the long listing | owner and group are `haisos`, permissions `rwxrwxrwx` (no users or permissions yet), so a device shows as `crwxrwxrwx`, with its major and minor numbers in the size column as GNU ls shows them; columns are padded with spaces, not tabs; the width is 80 unless `-w` says otherwise; `--sort=version/width` and `--time=birth` are reported as not treated; on Windows, a `--time-style=+FORMAT` conversion the Microsoft C runtime lacks (`%k`, `%P`, ...) prints as written, as glibc prints one it does not know |
| `man` | 1.0.0 | `-f -k -i -I`, a section first (`man 1 ls`), several pages | pages are compiled in (each builtin's `ManPage()`, its `--help` unless overridden), all section 1, plain text, no pager; `-k` matches names and summaries only; everything else of man-db's reported as not treated |
| `mkdir` | 1.1.0 | `-p -v` | `-m`/`--mode`, `-Z`/`--context` not treated (no permissions or security contexts) |
| `pwd` | 1.1.0 | `-L -P` (the same: no symlinks) | -- |
| `rm` | 1.0.0 | `-f -i -I --interactive[=WHEN] -r -R -d -v --no-preserve-root --preserve-root[=all]`; prompts on standard error, answers from standard input (end of input is no); `-I` asks once before more than three operands or a recursive removal; entries of a directory removed in name order, the last of `-f`/`-i`/`-I`/`--interactive` wins; a failed `RemoveFile` is reported as `Permission denied` | no write-protection prompts (no permissions: a builtin's path, and every write refusal, are `Permission denied`); a failed removal of an empty directory is reported as `Device or resource busy` -- no reason is known, a read-only filesystem included; entries are removed in name order, not the disk's; `--one-file-system` not treated; `--preserve-root=all` is taken as `--preserve-root` |
| `rmdir` | 1.0.0 | `--ignore-fail-on-non-empty -p -v` | a failed removal of an empty directory is reported as `Device or resource busy` -- no reason is known, a read-only filesystem included |
| `sleep` | 1.0.0 | NUMBER with one of the suffixes `s m h d` (or none), operands summed; `inf` sleeps until stopped | checks for a stop at least every 50 ms, so a stopped sleep ends promptly (GNU's has no such notion) |
| `sort` | 1.1.0 | `-b -d -f -g -h -i -M -n -r -R -s -u -V -z`, `--sort=WORD`, `-k KEYDEF` (with `-t SEP`), `-c`/`-C`/`--check[=WHEN]`, `-m`, `-o FILE`, `--files0-from=F`; with no FILE, or a FILE of `-`, the standard input is read; GNU's last-resort whole-line comparison unless `-u` or `-s`; lines compared byte by byte, as GNU sort with `LC_ALL=C`; the orders of `BuiltinCompare.h`: `-n` exact at any length, `-g` by strtold, `-h` by unit then numeric, `-M` by month, `-V` by gnulib's filevercmp (`CompareVersion`, for `ls -v` to reuse) | `-R` orders by a salted hash of each key, not GNU's MD5, so its order changes from run to run as GNU's does (`--random-source` not treated); `-S`, `-T`, `--parallel` and `--batch-size` accepted and not acted on (Haisos sorts in memory); `--compress-program` and `--debug` not treated |
| `true` | 1.0.0 | nothing; `--help`/`--version` only as the sole argument | -- |
| `wc` | 1.0.1 | `-c -m -l -L -w`, `--files0-from`, `--total`; with no FILE, or a FILE of `-`, the standard input is read | standard input has no size, so with it the columns are at least 7 wide (GNU sizes a redirected file); character classes and display widths come from the `Unicode` component's compact tables (unassigned code points count as printable; rare scripts' widths are approximate); `--debug` not treated |
| `which` | 1.0.0 | `-a -s`; a name with a `/` taken as it is when a file is there, otherwise each PATH entry in order, an empty one the working directory, the candidate printed as built; exit 1 when any operand is missed or there is none, 2 on an unknown option | Debian's which (debianutils), not GNU's: `Illegal option -x` on stderr and `Usage: <path> [-as] args` on stdout, its own shape; `--help`/`--version` are Haisos's (Debian's which has none) |

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
