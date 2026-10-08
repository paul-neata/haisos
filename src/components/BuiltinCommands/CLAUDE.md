# BuiltinCommands

The commands compiled into Haisos itself -- `[`, `basename`, `cat`, `chmod`,
`cp`, `cut`, `date`, `dirname`, `echo`, `egrep`, `env`, `false`, `fgrep`,
`grep`, `hsh`, `ls`, `man`, `mkdir`, `mv`, `nl`, `printf`, `pwd`, `realpath`,
`rm`, `rmdir`, `seq`, `sleep`, `sort`, `tee`, `test`, `touch`, `tr`, `true`,
`uniq`, `wc`, `which` -- and what places them on filesystems. Implements `IBuiltinCommands`
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
- `BuiltinTestExpression.h/.cpp` - the one test/[ expression evaluator
  (`EvaluateTestExpression`), shared by hsh's `test` and `[` builtins (its
  Dash dialect, dash's `bltin/test.c`) and the `test` and `[` commands (its
  Gnu dialect, GNU coreutils 9.4's `src/test.c`): the POSIX reductions over
  an operand window, `-o`/`-a`/`!`/parentheses, the string and integer
  comparisons, and the file primaries over the process's `IFileIO` -- the one
  place every file test reads through, with the same semantics for both
  dialects (`-r`/`-w`/`-x`/`-O`/`-G` existence only, `-h`/`-L`/`-b`/`-p`/`-S`/
  `-u`/`-g`/`-k` never matching, `-ef` comparing resolved paths). Syntax
  errors are reported once through a caller-given callback (without the
  `<command>: ` prefix) and give status 2.
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
  command, so lines read ahead stay for the next question; rm, cp -i and
  mv -i use it.
- `BuiltinRemove.h` - `RemoveOperand`, removing one operand as GNU rm does:
  the messages and prompts of every mode (-r, -d, -i, -v, the root
  failsafe), one function for rm and for mv to call on a source it copied
  across filesystems.
- `BuiltinDate.h` - the date and time parsers and the formatter the
  time-taking builtins share (contract 4): `ParseDateString` (GNU date -d's
  subset: dates `YYYY-MM-DD` with a `T`-joined time and an attached zone,
  times of day, the zone words and offsets, relative items and `@` seconds),
  `ParseTouchStamp` (touch -t's `[[CC]YY]MMDDhhmm[.ss]`), `FormatDateTime`
  (strftime as GNU date has it, gnulib nstrftime's C-locale subset: the
  flags `_ - 0 ^ #`, a width, `%N %s %q %z` and its `:` forms, the
  composites, a conversion not known written out as it stands), each in a
  form that works in the host's local zone or in UTC (date -u's), and the
  helpers `LocalTimeOf`, `UtcTimeOf`, `SecondsFromLocalTime` and
  `SecondsFromUtc` (Howard Hinnant's days_from_civil: no POSIX-only
  `timegm`). touch, date and ls (`--time-style=+FORMAT`) use them; a
  builtin that takes or prints a date goes through here, not its own.
- `BuiltinCopy.h` - the copying cp is made of, for mv to reuse on a source
  on another filesystem: `CopyPath` (one source/destination pair through
  GNU cp's checks -- stat, -r, same file, into-itself, type mismatch -- and
  the messages and prompts of every mode: -i, -n/--update, backups,
  --attributes-only, --remove-destination, -p's timestamps, -v's lines),
  `ResolveCopyTargets` (the operand rules, `-t`, `-T`, `--parents`,
  `--strip-trailing-slashes`), `BackupPathFor` and `ParseBackupControl`
  (GNU's backup controls, `VERSION_CONTROL` and `SIMPLE_BACKUP_SUFFIX`),
  `BackupRequest`/`FinishBackupRequest` (the backup options resolved once,
  after the option parsing as GNU does: -b and -S take $VERSION_CONTROL's
  word, --backup=WORD its own), `ParseUpdateWord` (--update's words), and
  the message helpers `CopyQuoted` (GNU's quoteaf), `CopyJoinPath`,
  `CopyStatMissingReason` and `CopyCreateFailedReason` -- each declared
  here once, so cp and mv share them with no copies of their own.
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
- `BuiltinPrintf.h` - the printf format engine every printf-formatted output
  goes through: `ParsePrintfSpec` (one `%...` specification into a
  `PrintfSpec`), `FormatPrintfSigned`/`Unsigned`/`Float`/`String` (one value
  under it, glibc's output) and `AppendPrintfEscape` (GNU printf's backslash
  escapes). printf and `seq -f` use it now; `find -printf` and awk's
  `printf`/`sprintf` will reuse the same names.
- `BuiltinFnmatch.h` - contract 6, `FnMatch`: glibc's `fnmatch()` in the C
  locale, byte for byte (`*`, `?`, `\c`, brackets with the `[:class:]` items,
  reversed ranges empty, an unknown class matching nothing, a trailing lone
  `\` never matching), with glibc's flag values (`kFnmPathname`,
  `kFnmNoEscape`, `kFnmPeriod`, `kFnmLeadingDir`, `kFnmCaseFold`). Matching is
  iterative, single-star backtracking per `/`-segment -- no recursion per
  byte, so a pattern of many `*` on a long text stays linear-ish. grep's
  `--include`/`--exclude`/`--exclude-from`/`--exclude-dir` use it now; find
  (search--find-tests), rg, diff and tar are to reuse it too.
- `commands/grep/GrepMatcher.h` - the pattern engine grep, egrep and fgrep
  are made of: patterns compiled once (joined into a single regex for Basic/
  Extended, per-pattern for back-references, an `\A(?:p)\z` wrap for Perl
  `-x`, fixed strings never touching `Regex` at all), then
  "find the next match in a line" -- leftmost then longest for Basic/
  Extended/Fixed, GNU's shrink-then-move-on loop for `-w`, which every
  matcher gets, Perl's included (GNU's `(?<!\w)(?:p)(?!\w)` wrap, which
  `Regex` has no lookarounds to express). `rg` (search--rg-search) reuses it.
- `commands/grep/GrepContext.h` - the before/after context bookkeeping,
  GNU grep's way: which lines to print around the selected ones and where a
  group separator goes (before a line not following the one last printed,
  across files too, when context was asked for at all). One instance spans a
  whole run, `BeginFile` joining each input under its own name and numbering,
  and the caller prints (so `-o` can write no context lines yet keep the
  separators). `rg` (search--rg-search) reuses it.
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
for those Haisos does not act on. An option may also be `hidden`: an obsolete
spelling the real command accepts but never documents (uniq's `-N`) -- parsed
like any other option, left out of `--help` and of the not-treated reports.
`ParseBuiltinArgs`, `BeginBuiltin` (help,
version, usage errors, not-treated reports) and `BuiltinHelpText` all read
that table, so the help can never disagree with what is parsed. A command that
copies a real command of another name (hsh copies dash) says so in
`BuiltinHelp::basedOn`, and its "Based on Linux <command>:" line names that
command and links its man page.

| Command | Version | Treated | Documented exceptions |
|---------|---------|---------|-----------------------|
| `[` | 1.0.0 | one program with `test` (`BuiltinHelp::basedOn` names test): `--help`/`--version` only as its sole argument, then the last argument must be exactly `]` (`[: missing ']'`, status 2); the `]` dropped, the rest is `test`'s expression, errors named after `[`. The dropped `]` stays in the argument vector GNU's way, so a missing closing `)` is reported `')' expected, found ']'` | see `test` |
| `basename` | 1.0.0 | `-a -s SUFFIX -z`; one NAME with an optional SUFFIX operand, several with `-a` or `-s` (a suffix removed only when the base is longer than it), GNU's trailing-slash and all-slash rules | -- |
| `cat` | 1.2.0 | every option of GNU cat; with no FILE, or a FILE of `-`, the standard input is read | -- |
| `chmod` | 1.0.0 | `-c -f -v --no-preserve-root --preserve-root --reference=RFILE -R`; GNU 9.4's mode grammar, octal and symbolic (`ugoa`, `+-=`, `rwxXst`, `u`/`g`/`o` copies, clauses split on `,`), the mode words taken out of the arguments before the options are parsed (so `chmod -w f` works), GNU's messages, exit statuses and the `--preserve-root` failsafe (off by default, as 9.4) | Haisos has no permissions: the mode is parsed and validated and nothing changes; every file's mode is taken as 0777 (as `ls -l` shows `rwxrwxrwx`), the umask as 0; `--reference` takes the RFILE's mode as 0777 too; entries of a directory are changed in name order, not the disk's |
| `cp` | 1.0.0 | `-a -b --backup[=CONTROL] -d -f -H -i -L -l -s -n -P -p --preserve[=ATTR_LIST] --no-preserve=ATTR_LIST --parents -R -r --remove-destination --strip-trailing-slashes -S -t -T -u --update[=UPDATE] -v --attributes-only`; GNU 9.4's messages, prompts, exit statuses and operand rules; backups (simple, numbered, existing; `VERSION_CONTROL`, `SIMPLE_BACKUP_SUFFIX`), the `--parents` walk, `--attributes-only` keeping the destination's data, the `-n` warning | `-l` and `-s` fail: HaisosOS creates no links; `-d`, `-H`, `-L` and `-P` change nothing (no links); `--preserve` keeps timestamps only (no modes, owners or links; `context` and `xattr` accepted and reported as not treated); entries of a directory are copied in name order, not the disk's; a directory copied into itself is refused before anything is copied; a failed backup, open, create or `SetTimes` whose reason `IFileIO` does not give is `Permission denied` |
| `cut` | 1.0.0 | `-b --bytes=LIST -c --characters=LIST -d --delimiter=DELIM -f --fields=LIST -n --complement -s --only-delimited --output-delimiter=STRING -z --zero-terminated`; a LIST of `N`, `N-`, `N-M`, `-M` items separated by commas or blanks, each validated GNU's way, overlapping ranges merged (adjacent ones kept apart, which decides where `--output-delimiter` goes), `--complement` the other side over the rest of the line; with `-f`, a line with no delimiter is printed whole unless `-s` drops it | `-c` counts bytes, as GNU cut does (no multibyte characters); `-n` is ignored, as GNU's; `-d ''` and `--output-delimiter ''` are the NUL byte |
| `date` | 1.0.0 | `-d --date=STRING -f --file=DATEFILE -I[FMT] --iso-8601[=FMT] --resolution -R --rfc-email -r --reference=FILE -s --set=STRING -u --utc`, `--rfc-3339=FMT` (`--universal`, `--uct`, `--rfc-822` and `--rfc-2822` hidden aliases, `--debug` not treated); the sources `-d`/`-f`/`-r`/`--resolution` mutually exclusive (GNU's `the options to specify dates for printing are mutually exclusive`), each format option and `+FORMAT` one output format (`multiple output formats specified`); `+FORMAT` through `BuiltinDate`'s `FormatDateTime`: GNU's flags `_ - 0 ^ #`, a width, `%N %s %q` and `%z` with up to three colons, the composites, a conversion not known written out as it stands; the format words of `-I` (date, hours, minutes, seconds, ns) and `--rfc-3339` (date, seconds, ns), `-R` RFC 5322, `--resolution` printing `0.000000001`; `-f` a date per line, an invalid one reported and gone past (status 1); a lone operand `MMDDhhmm[[CC]YY][.ss]` sets the time, as `-s STRING` (parsed with `ParseDateString`); `-u` prints UTC and takes what has no zone in UTC (a zone in the string wins, as over the local zone) | setting the time is refused: the parsed time is printed, then `cannot set date: Operation not permitted` (no clock to set); `-d`'s and `-s`'s strings are touch `-d`'s subset (`ParseDateString`); the local zone is the host's and `TZ` is not consulted; `%Z` prints the long zone name on Windows (`Pacific Standard Time`, not `PST`); `--resolution` prints the nanosecond clock GNU's own shows for a filesystem that has none |
| `dirname` | 1.0.0 | `-z`; GNU's dir_len: the last component dropped, trailing slashes stripped, an empty name `.` and a name of only slashes `/`, as GNU's does | -- |
| `echo` | 1.1.0 | `-n -e -E`, the `-e` escapes; `--help`/`--version` only as the sole argument, as GNU echo | -- |
| `egrep` | 1.1.0 | grep with the extended matcher preselected (`grep -E`): every option of grep's, another matcher given refused with GNU's `conflicting matchers specified`; `-e`/`-f` and the first operand as grep's | see `grep`; and no obsolescence warning: Debian/Ubuntu's `egrep` is a script, and this is that, not upstream 3.8+'s warning-printing one |
| `env` | 1.0.0 | `-i -0 -u NAME -C DIR -S STRING`; a leading `-` operand as `-i`, then NAME=VALUE operands until COMMAND; the child gets the edited environment and COMMAND is looked up in the new PATH, as execvp does; `-S` splits GNU split-string's way (quotes, escapes, `${NAME}`, `#` comments, `\_` a word separator outside quotes and a space inside); usage errors 125, a not-runnable COMMAND 126, not found 127 | the signal options, `-v/--debug` and `--list-signal-handling` are not treated (no signals); with no COMMAND the variables print sorted by name -- `IEnvironment` keeps no order, where GNU prints the environment's own; `-i` empties the variables only (secrets and LLM identifiers stay); `${NAME}` in `-S` expands from the starting environment, as GNU's `getenv` does |
| `false` | 1.0.0 | nothing; `--help`/`--version` only as the sole argument | exits 1 even after `--help`/`--version`, as GNU's does |
| `fgrep` | 1.1.0 | grep with fixed strings preselected (`grep -F`): every option of grep's, a matcher option refused as grep refuses it; the patterns are never regexes (GNU's `[:space:]` refusal does not apply) | as `egrep` |
| `grep` | 1.1.0 | `-E -F -G -P -e PATTERNS -f FILE -i -y --no-ignore-case -v -w -x -c -l -L -q -o -n -b -H -h --label=LABEL -T -m NUM -s -a -I --binary-files=TYPE -U -u -z --line-buffered -r -R -d ACTION -D ACTION --include=GLOB --exclude=GLOB --exclude-from=FILE --exclude-dir=GLOB -Z --null -A NUM -B NUM -C NUM --group-separator=SEP --no-group-separator --color=WHEN --colour=WHEN` (long forms as GNU's, `--silent`, `-u` and the digit options `-0`..`-9` hidden, `-X` parsed and not acted on); patterns from `-e`/`-f` and the first operand, one per line of an argument or file (a trailing empty piece kept: `-e ''` is one empty pattern, matching every line), several patterns joined into one regex (Basic/Extended without back-references, per-pattern otherwise), GNU's `character class syntax is [[:space:]], not [:space:]` refusal, `-P` a single pattern only (`the -P option only supports a single pattern`), the matchers mutually exclusive (`conflicting matchers specified`); matching through `commands/grep/GrepMatcher.h` (leftmost then longest for `-G`/`-E`/`-F`, leftmost-first for `-P`, `-i` ASCII case, `-w` the same word-neighbour check for every matcher, `-x` exact); `-m NUM` stops the input after the NUMth matching line (0: the input is not read, a negative NUM no limit, `invalid max count`), trailing context still read and printed (a matching line in it printed as context, restarting nothing); binary files by the first NUL (`-a` plain text, `-I` as without a match, `--binary-files` binary/text/without-match, `unknown binary-files type`): once binary a NUL ends a line too, a selected line prints nothing and says `binary file matches` on stderr at input end unless `-c -l -L -q` (`-s` does not hide it); `-z` NUL-terminated lines and nothing binary; the prefixes `-n -b -H -h` (names on the standard input when more than one input or `-H`), `--label` naming the standard input, `-T` padding before the tab, `-Z` a NUL byte after a file name (bare, never coloured); recursion: no operand with `-r` searches `.` as an implicit operand (`src/a.c`, not `./src/a.c`), the walk depth first through `context.IO()`, children named parent + `/` + name (any run of trailing `/` on the operand cut to one: `src//` -> `src/sub/b.h`), directories per `-d read` (default) / `recurse` (an unambiguous prefix; GNU's argmatch failure with `Usage:` lines, exit 1) / `skip`, devices `-D read`/`skip` (named on the command line read, met while walking skipped), filters `--include`/`--exclude`/`--exclude-from` on base names walked from the last given to the first (none matching keeps the first given's side), operands unanchored, `--exclude-dir` on any match, the implicit `.` excepted; context `-A -B -C -NUM` (`-A`/`-B` win over `-C`/`-NUM` whatever the order) through `commands/grep/GrepContext.h`, the `--` group separator (`--group-separator`, `--no-group-separator`) also between groups of different files, an invalid NUM `invalid context length argument` exit 2; colour `--color`/`--colour` (`always`/`never`/`auto`, a WHEN GNU does not know printing the help, exit 0; `auto` needs a terminal and a `TERM` that is not `dumb`): GNU's `GREP_COLORS` capabilities and SGR bytes exactly, `GREP_COLOR` before it with the deprecation warning; every diagnostic names `grep` (egrep's and fgrep's too), usage errors carry the `Usage:` and `Try 'grep --help'` lines, exit statuses 0/1/2 as GNU's; reads in 96 KiB chunks, stopping promptly on a stop | the C locale only: bytes, ASCII case, a NUL alone makes a file binary; the binary decision is per 96 KiB read, exact for a file of 96 KiB or less, and lines already printed stay printed; `-T` pads the standard input to 19 columns, GNU's width for a pipe it cannot size; `-U` changes nothing (POSIX I/O is binary already); a recursive walk lists each directory in byte order, not GNU's unspecified readdir order; `-R` is `-r` (HaisosOS has no symbolic links of its own; a physical filesystem follows those on the disk); consecutive digit options form one number, so `-1 -5` is 15 where GNU starts a new number with each argument word (5) |
| `hsh` | 1.0.1 | dash's invocation (`-c`, a script file, standard input, interactive with `-i` or when both standard input and standard error are terminals, `-a -c -C -e -f -i -n -o -s -u -x`), simple commands and `;` `&&` `||` `!` lists, pipelines (`a \| b`, `! a \| b`; a stage that must run in the shell runs in an in-process subshell), background lists (`&`, `$!`) with `wait`, command substitution (`$(...)`, `` `...` ``), every redirection (`<` `>` `>|` `>>` `<>` `n>&m` `n<&m` `n>&-` `&>`, heredocs `<<`-`<<-`, here-strings `<<<`, noclobber `-C` with `>|` overriding it), the compound commands `{ ...; }` `( ... )` `if`/`elif`/`else` `while` `until` `for` `case` and functions `f() { ...; }` with `return`, the shell builtins `.` `:` `[` `break` `cd` `continue` `eval` `exec` `exit` `export` `false` `read` `readonly` `return` `set` `shift` `test` `true` `unset` `wait`, with the option behaviours `-e` (errexit with dash's tested-context exceptions), `-x` (xtrace), `-a` (allexport) and `-n` (noexec) | the reference command is dash, not hsh (`BuiltinHelp::basedOn`); `ManPage()` is overridden: a full manual page (`man hsh`), not the `--help` text; `&>` and `<<<` are bash's operators; `--help`/`--version` only as the first argument; `$0` is `hsh` unless a script or `-c` command_name names it; a background (`&`) builtin, function or compound command runs in a child hsh, which sees only exported variables; PS1/PS2/PS4 are not expanded and `-v` is accepted, not acted on; `test`'s `-r`/`-w`/`-x`/`-O`/`-G` only test existence (no permissions or users), `-h`/`-L` are always false (no links) and `-ef` compares resolved paths (no inode numbers); two in-shell pipeline stages run one after the other, so an endless one written into another in-shell stage never ends (its output held in memory) where dash ends at once -- a child stage (a program) has no such limit |
| `ls` | 1.3.1 | `-a -A -B -c -C -d -f -g -G -h -k -l -m -N -o -p -r -R -s -S -t -u -U -w -x -X -1`, `--file-type --format --full-time --group-directories-first --sort --time --time-style`; off a terminal (stdout a pipe, a file, a device), one name per line unless `-C`/`-x`/`-m`/`-l` asks for a layout, and names literal with control characters written raw -- GNU's own defaults when stdout is not a terminal; `-1` after `-l` keeps the long listing; `--time-style=+FORMAT` formatted through `BuiltinDate`'s `FormatDateTime`, as `date`'s own | owner and group are `haisos`, permissions `rwxrwxrwx` (no users or permissions yet), so a device shows as `crwxrwxrwx`, with its major and minor numbers in the size column as GNU ls shows them; columns are padded with spaces, not tabs; the width is 80 unless `-w` says otherwise; `--sort=version/width` and `--time=birth` are reported as not treated |
| `man` | 1.0.0 | `-f -k -i -I`, a section first (`man 1 ls`), several pages | pages are compiled in (each builtin's `ManPage()`, its `--help` unless overridden), all section 1, plain text, no pager; `-k` matches names and summaries only; everything else of man-db's reported as not treated |
| `mkdir` | 1.1.0 | `-p -v` | `-m`/`--mode`, `-Z`/`--context` not treated (no permissions or security contexts) |
| `mv` | 1.0.0 | `--backup[=CONTROL] -b -f -i -n --no-copy --strip-trailing-slashes -S -t -T --update[=UPDATE] -u -v`; GNU 9.4's messages, prompts, backups, `-n`/`-u`/`--update` interplay (the last of `-f`/`-i`/`-n` wins, `-n` keeping a later `--update` safe) and operand rules; one step through `IFileIO::Rename`, across a mount (EXDEV) by copy-then-remove as GNU mv does across devices: `CopyPath` keeping times, then `RemoveOperand`, its `-v` lines cp's then rm's | `--debug` and `-Z`/`--context` not treated; across filesystems the copy keeps modification and access times only (no modes, owners or links exist); a builtin cannot be moved; `.`/`..` are refused as a source, as GNU's EBUSY; a failed `Rename` whose reason `IFileIO` does not give is worded from the destination's parent |
| `nl` | 1.0.0 | `-b -d -f -h -i -l -n -p -s -v -w`; the body/header/footer styles `a` (all lines), `t` (non-empty, the body's default), `n` (none, the header's and footer's) and `pBRE` (a basic regular expression, compiled by the `Regex` component), the formats `ln`/`rn`/`rz` (`rn` the default), the logical page delimiters (`\:\:\:` header, `\:\:` body, `\:` footer; `-d CC` builds them, a one-character CC gaining `:`, an empty one turning the sections off), `-l N` numbering only every Nth blank, `-v`/`-i` intmax arithmetic that stops with `line number overflow` (a section start clearing it unless `-p`); the numbering carries across FILE operands, and `-` is the standard input | `pBRE` is Haisos's own regex engine, which follows glibc's BRE but may part from it on an exotic pattern |
| `printf` | 1.0.0 | FORMAT reused until the ARGUMENTs run out; every conversion `a A c d e E f F g G i o s u x X` with flags, widths and precisions (`*` from arguments), `%b` (escapes in the argument) and `%q` (`ShellEscapeQuoted`); `\` escapes (`\" \\ \a \b \c \e \f \n \r \t \v`, `\xHH`, octal, `\uHHHH`, `\UHHHHHHHH`); numeric arguments decimal, octal (`010`), hex (`0x1F`) or character constants (`'A`); GNU's numeric diagnostics (`expected a numeric value`, `value not completely converted`, `Numerical result out of range`) set exit 1, an invalid specification or bad escape ends the command with exit 1, excess arguments warn, `\c` ends the output with status 0; `--help`/`--version` only as the sole argument | `%q` keeps bytes >= 0x80 as they are, where GNU's shell-escape quoting writes an invalid UTF-8 byte as `$'\200'`; `\u` and `\U` always write UTF-8; `%a`/`%A` follow the platform's `long double` |
| `pwd` | 1.1.0 | `-L -P` (the same: no symlinks) | -- |
| `realpath` | 1.0.0 | `-e -m -L -P -q --relative-to=DIR --relative-base=DIR -s -z`; paths resolved from the working directory, `.` and `..` walked, every non-final segment an existing directory unless `-m`, the final one existing with `-e`; `--relative-to`/`--relative-base` resolved by the same rules, a path outside the base printed absolute, both dropped when `--relative-to` is outside `--relative-base` | there are no symbolic links, so `-L`, `-P` and `-s` change nothing |
| `rm` | 1.0.0 | `-f -i -I --interactive[=WHEN] -r -R -d -v --no-preserve-root --preserve-root[=all]`; prompts on standard error, answers from standard input (end of input is no); `-I` asks once before more than three operands or a recursive removal; entries of a directory removed in name order, the last of `-f`/`-i`/`-I`/`--interactive` wins; a failed `RemoveFile` is reported as `Permission denied` | no write-protection prompts (no permissions: a builtin's path, and every write refusal, are `Permission denied`); a failed removal of an empty directory is reported as `Device or resource busy` -- no reason is known, a read-only filesystem included; entries are removed in name order, not the disk's; `--one-file-system` not treated; `--preserve-root=all` is taken as `--preserve-root` |
| `rmdir` | 1.0.0 | `--ignore-fail-on-non-empty -p -v` | a failed removal of an empty directory is reported as `Device or resource busy` -- no reason is known, a read-only filesystem included |
| `seq` | 1.0.0 | `-f FORMAT -s STRING -w`; LAST, FIRST LAST, FIRST INCREMENT LAST; the options split out before the operands, so a negative number (`seq -5 -1 -10`) is an operand; GNU's default format (`%.PRECf` for fixed-point decimals, padded by `-w`, `%Lg` otherwise), `-f` validated against GNU's rules, the all-digits fast path (numbers of any size, decimal-string arithmetic) and the `%.0Lf` one, the stop rule with its rounding fix; checks for a stop on every number, so an endless sequence (`seq 1 inf`) ends on `TriggerStop()` | `%a`/`%A` in `-f` follow the platform's `long double` |
| `sleep` | 1.0.0 | NUMBER with one of the suffixes `s m h d` (or none), operands summed; `inf` sleeps until stopped | checks for a stop at least every 50 ms, so a stopped sleep ends promptly (GNU's has no such notion) |
| `sort` | 1.1.0 | `-b -d -f -g -h -i -M -n -r -R -s -u -V -z`, `--sort=WORD`, `-k KEYDEF` (with `-t SEP`), `-c`/`-C`/`--check[=WHEN]`, `-m`, `-o FILE`, `--files0-from=F`; with no FILE, or a FILE of `-`, the standard input is read; GNU's last-resort whole-line comparison unless `-u` or `-s`; lines compared byte by byte, as GNU sort with `LC_ALL=C`; the orders of `BuiltinCompare.h`: `-n` exact at any length, `-g` by strtold, `-h` by unit then numeric, `-M` by month, `-V` by gnulib's filevercmp (`CompareVersion`, for `ls -v` to reuse) | `-R` orders by a salted hash of each key, not GNU's MD5, so its order changes from run to run as GNU's does (`--random-source` not treated); `-S`, `-T`, `--parallel` and `--batch-size` accepted and not acted on (Haisos sorts in memory); `--compress-program` and `--debug` not treated |
| `tee` | 1.0.0 | `-a --append`, `-p` (the same as `--output-error=warn-nopipe`), `--output-error[=MODE]` (warn, warn-nopipe, exit, exit-nopipe; GNU's prefix matching); every FILE opened up front, an unopenable one reported and gone past, the standard output written first and then the files, each read chunk copied to them all; the modes decide what a failed write does: default stops quietly (141, as SIGPIPE), the warn modes diagnose and go on (status 1), the nopipe ones go past a broken pipe silently, the exit modes stop (status 1) | `-i`/`--ignore-interrupts` not treated (no signals); the standard output is written chunk by chunk as input arrives, not stdio-buffered |
| `test` | 1.0.0 | GNU coreutils 9.4's expression, no options: the POSIX 1-4-operand reductions, then `-o`/`-a`/`!`/parentheses, string comparisons `= == !=`, integers of any length `-eq -ne -lt -le -gt -ge` (blanks and a sign around the digits), `-l STRING` its length as an operand, the file primaries and `-t FD`; syntax errors GNU's messages and status 2, GNU's quirks kept (an operator that is not one of its is an error with its operand there or not; a string comparison with a right `-l` shifts both operands past it, so the operator's word is what is compared); `--help`/`--version` are ordinary non-empty strings (true, as GNU's test) | `-r`/`-w`/`-x`/`-O`/`-G` only test that the file is there (no permissions or users); `-h`/`-L`/`-b`/`-p`/`-S`/`-u`/`-g`/`-k` never match (no links, block devices, fifos, sockets or set-id bits); `-ef` compares the paths as `ResolvePath` resolves them (no inode numbers); `-t FD` is false past the descriptor table's size |
| `touch` | 1.0.0 | `-a -c -d STRING -f -h -m -r FILE -t STAMP --time=WORD`; GNU 9.4's messages, `-d`/`-t`/`-r` as time sources (`-d` with `-r` the one combination of two, the string parsed against each of the reference's times), `--time` as `-a`/`-m`'s word form; a missing file is created (never truncating), `-c` leaves it alone, the times set through `IFileIO::SetTimes`, now taken once without a source | `-` changes nothing (GNU touches the file open on standard output; HaisosOS has none); `-h` is the same as without it (no links); `-f` is accepted and ignored; times are set to the second |
| `tr` | 1.0.0 | `-c -C --complement -d --delete -s --squeeze-repeats -t --truncate-set1`; SETs of backslash escapes, octal `\NNN` (GNU's ambiguous-octal warning), ranges, `[:class:]`, `[=c=]` and the `[c*n]`/`[c*]` repeats; translation with set2 padded by set1's last byte, `-t` truncating set1 to set2's length, `-d`, `-s` and their combinations (`-c` flipping either's set), GNU's every operand-count and set error message; streamed in 64 KiB chunks, so the translation's state (a squeeze run) carries across a chunk | works on bytes, as GNU tr does with `LC_ALL=C` (no multibyte characters) |
| `true` | 1.0.0 | nothing; `--help`/`--version` only as the sole argument | -- |
| `uniq` | 1.0.0 | `-c --count -d --repeated -D --all-repeated[=METHOD] -f --skip-fields=N -s --skip-chars=N -u --unique -i --ignore-case -w --check-chars=N -z --zero-terminated --group[=METHOD]`; adjacent lines compared after the skipped fields (blanks then non-blanks each) and chars, limited to `-w` bytes, `-c`'s count right-aligned in seven columns; streamed one group at a time, so a file bigger than memory reads a line at a time; an OUTPUT operand is opened before the input is read | the obsolete `-N` (skip N fields) and `+N` (skip N chars) spellings work as GNU's but are never documented (`BuiltinOption::hidden`); `-i` folds ASCII case only, there being no locale |
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
