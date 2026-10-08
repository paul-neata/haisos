# Task coreutils--head-tail: head and tail, with tail -f/-F

- Rock: coreutils
- Depends on: coreutils--du-cmp (`ParseSizeWithSuffix` in `BuiltinSize.h`), coreutils--sort (`BuiltinText.h`), coreutils--uniq-cut (`BuiltinOption::hidden`)
- Size: ~850 changed lines in ~7 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add head and tail builtins, tail following files by polling

## Goal

Two new builtins printing what GNU coreutils 9.4 prints in the C locale:

- `head` (`BUILTIN rootfs head /bin/head`): `-n [-]NUM`, `-c [-]NUM` (a
  leading `-` prints all but the last NUM), size suffixes (`1k`, `1kB`,
  `1KiB`, `b`), the obsolete `-NUM[bkmclqvz]`, `-q -v -z`, `==> NAME <==`
  headers.
- `tail` (`BUILTIN rootfs tail /bin/tail`): `-n`/`-c` with `+NUM` (from the
  start) or `[-]NUM` (from the end), the obsolete `+NUM`/`-NUM[bcl][f]`,
  `-q -v -z`, headers, and following: `-f`/`--follow[=descriptor|name]`, `-F`
  (`--follow=name --retry`), `--retry`, `-s/--sleep-interval`, `--pid` --
  by polling the file with `IFileIO::Stat` every interval (no inotify), until
  `TriggerStop()` (exit 143), a broken pipe (141), or the `--pid` process
  ends (0).

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security", "Exit
codes"), `src/components/BuiltinCommands/CLAUDE.md` (output buffering: a
non-terminal stdout is block-buffered -- `tail -f` must `Flush()` what it
prints before it sleeps), `BuiltinCommand.h`, `commands/cat/Cat.cpp`
(opening `-` as descriptor 0, reading in chunks, `StopRequested()`,
`kIOInterrupted` meaning stopped), `interfaces/IFileIO.h` (`Stat`,
`OpenFile`, `GetDescriptor`), `interfaces/IHaisosOS.h`
(`GetRunningProcesses()`, reached as `context.Process().OS()`),
`interfaces/IProcess.h` (`GetPid`). File descriptors cannot seek: skip by
reading and discarding. A descriptor keeps reading where it stopped, and a
read at end-of-file returns 0 but returns data appended later (in-memory and
physical files alike) -- what descriptor-mode following relies on.

What earlier tasks provide, as if on develop (coreutils--du-cmp,
`src/components/BuiltinCommands/BuiltinSize.h`):

```cpp
enum class SizeParse { Ok, Invalid, InvalidSuffix, Overflow };
SizeParse ParseSizeWithSuffix(std::string_view text, std::string_view validSuffixes, uintmax_t& out);
```

(gnulib `xstrtoumax`: `b` 512, `k K` 1024, `m M` 1024^2, `G T P E Z Y R Q`;
with `0` in the list, `kB` 1000 and `KiB` 1024; no digits but a suffix is 1
of it.)

From coreutils--sort (`src/components/BuiltinCommands/BuiltinText.h`; read
that plan for the exact API): `GnuQuote(text)` -- GNU's `quote()`, used for
every value in a message below (`invalid number of lines: 'x'`);
`ArgMatch(context, "--follow", value, choices)` -- GNU's `XARGMATCH`, which
prints the "invalid argument ... Valid arguments are: ..." block and leaves
the exit (1) to the caller; `OpenInputOperand(context, name, failure)` --
`-` as descriptor 0, a name Stat-ed then opened, failure `Missing` /
`Directory` / `Denied` / `BadDescriptor`, which each command words itself;
`BuiltinLineReader(context, input, delimiter)` with `Next(line, delimited)`
-- lines split on `\n` or `\0`, the last one possibly without its delimiter.
Use them; do not write second copies. File names in messages are
`ShellEscapeQuoted(name, true)` where GNU uses `quoteaf` (`cannot open`,
`error reading`, `has become inaccessible`, `has appeared`) and
`ShellEscapeQuoted(name)` where it uses `quotef` (`file truncated`, `cannot
follow end of this type of file`).

From coreutils--uniq-cut: `BuiltinOption::hidden` (a `bool`, default false):
an option parsed like any other but never shown in `--help`.

## Changes

### New `src/components/BuiltinCommands/commands/head/Head.cpp`

`CreateHeadCommand()`, name `head`, version `1.0.0`. Help: summary "output
the first part of files", usage `head [OPTION]... [FILE]...`.

`Options()`: `{'c', "bytes", Required, "[-]NUM"}`, `{'n', "lines", Required, "[-]NUM"}`,
`{'q', "quiet"}` and `{0, "silent"}` (same id), `{'v', "verbose"}`,
`{'z', "zero-terminated"}`, `{0, "-presume-input-pipe", kBuiltinNotTreated}`
(GNU's hidden `---presume-input-pipe`), and `'0'`-`'9'` as hidden short
options sharing one id `kDigit` (GNU's getopt string has them, so `head -n1
-5` parses and is then refused by the command, not by the parser).

Parsing (head.c `main`), usage errors exit 1:
1. Obsolete form: a first argument `-` + digits + letters from `cbkmlqvz`
   (`-5`, `-5c`, `-2v`): digits are the count; `c` bytes; `b`/`k`/`m` bytes
   with that suffix; `l` lines; `q`/`v` header modes; `z` NUL lines; any other
   letter -> `head: invalid trailing option -- X` + Try. Then parse the rest.
2. `BeginBuiltin` (errors + Try, help, version, not-treated reports). A
   `kDigit` option given -> `head: invalid trailing option -- D` + Try, exit 1.
3. `-n`/`-c` value: a leading `-` means "all but the last"; then
   `ParseSizeWithSuffix(v, "bkKmMGTPEZYRQ0")`: Invalid/InvalidSuffix ->
   `head: invalid number of lines: 'x'` (`bytes` for `-c`; the value as given,
   after the `-`), Overflow -> `head: invalid number of lines: 'x': Value too
   large for defined data type`; exit 1. The last of `-n`/`-c` wins. Default
   10 lines.

Output (head.c): no FILE is `-`; each FILE is opened with
`OpenInputOperand` (`Missing` and `Denied` -> the `cannot open` message,
`Directory` -> the header, then the `error reading` message); `-` is standard input, named `standard
input` in headers. Headers when more than one FILE or `-v`, never with `-q`:
`==> NAME <==\n`, preceded by `\n` for every header after the first one
printed (a file that could not be opened prints nothing, not even a
header: `head -n1 missing n12` prints `==> n12 <==` first, with no blank line
before it). Then:
- `-n N`: the first N lines (line end `\n`, or `\0` with `-z`); `-n -N`: all
  but the last N lines (stream with a ring of N lines; never hold more).
- `-c N` / `-c -N` likewise in bytes.
Errors, each going on to the next FILE, exit 1 at the end: missing ->
`head: cannot open 'NAME' for reading: No such file or directory`; a
directory -> its header is printed, then `head: error reading 'NAME': Is a
directory` (names through `ShellEscapeQuoted(name, true)`). Read in chunks
with `StopRequested()` checked; `kIOInterrupted` -> stop quietly.

### New `src/components/BuiltinCommands/commands/tail/Tail.cpp`

`CreateTailCommand()`, name `tail`, version `1.0.0`. Help: summary "output
the last part of files", usage `tail [OPTION]... [FILE]...`.

`Options()` (tail.c 9.4): `{'c', "bytes", Required, "[+]NUM"}`,
`{'f', "", kFollow}`, `{0, "follow", kFollow, Optional, "HOW"}`,
`{'F', "", kFollowNameRetry}`, `{'n', "lines", Required, "[+]NUM"}`,
`{0, "max-unchanged-stats", kBuiltinNotTreated, Required, "N"}`,
`{0, "pid", kPid, Required, "PID"}`, `{0, "-presume-input-pipe", kBuiltinNotTreated}`,
`{0, "-disable-inotify", kBuiltinNotTreated}`, `{'q', "quiet"}`, `{0, "silent"}`,
`{0, "retry", kRetry}`, `{'s', "sleep-interval", Required, "N"}`,
`{'v', "verbose"}`, `{'z', "zero-terminated"}`, and `'0'`-`'9'` as hidden
short options sharing one id `kDigit`, as head has them.

Parsing (tail.c `parse_obsolete_option`, `parse_options`), usage errors exit 1:
1. Obsolete form, only when there are 1 argument, or 2 with the second not
   an option (`-x`), or 3-4 with the second `--`: a first argument `+` or `-`,
   digits (none: 10, or 5120 for `b`), then optionally `b` (x512, bytes), `c`
   (bytes) or `l` (lines), then optionally `f` (follow), and nothing else --
   else it is not the obsolete form. `-` alone with no digits is not it (`-`
   is standard input; `-c` alone needs its argument). An unparsable number ->
   `tail: invalid number: '+x'`.
2. `BeginBuiltin`. A `kDigit` option given -> `tail: option used in
   invalid context -- D` (no Try), exit 1.
3. `-n`/`-c` value: leading `+` -> from the start (`+1` and `+0` are the
   whole input); leading `-` dropped; `ParseSizeWithSuffix(v,
   "bkKmMGTPEZYRQ0")` with head's messages (`tail: invalid number of lines:
   'x'`). `--follow` without a value is `descriptor`; its value through
   `ArgMatch(context, "--follow", value, {{"name", ...}, {"descriptor", ...}})`
   (nullopt -> exit 1). `-s N`: a decimal number >= 0
   (`std::strtod`, C locale), else `tail: invalid number of seconds: 'x'`.
   `--pid=PID`: decimal, else `tail: invalid PID: 'x'`.
4. Warnings (go on): `--retry` without following: `tail: warning: --retry
   ignored; --retry is useful only when following`; with `-f` in descriptor
   mode: `tail: warning: --retry only effective for the initial open`;
   `--pid` without following: `tail: warning: PID ignored; --pid=PID is
   useful only when following`.

Output: as head's for FILEs, headers and errors (`tail: cannot open 'NAME'
for reading: No such file or directory`, `tail: error reading 'NAME': Is a
directory` after its header). From the end: keep a ring of the last N lines
(or bytes) while reading -- never the whole file; from the start: discard
N-1 lines (bytes) then copy the rest. `-n 0` prints nothing.

Following (`-f`, `-F`, the obsolete `f`), after the initial output, unless
no FILE was given or every FILE is `-` (standard input is not followed: a
documented exception -- GNU follows it only when it is a regular file):
- A directory: `tail: NAME: cannot follow end of this type of file; giving
  up on this name`.
- A FILE missing at the start: dropped, unless `--retry`/`-F` (then watched
  until it appears). Nothing left to follow -> `tail: no files remaining`,
  exit 1.
- Every interval (`-s`, default 1.0 s; sleep in slices of at most 10 ms,
  checking `StopRequested()`), for each followed FILE:
  - descriptor mode (`-f`): keep the open descriptor; read what is new and
    print it; when `Stat` shows a size smaller than what was read so far:
    `tail: NAME: file truncated`, then reopen and print from the start. If the
    path disappears, keep reading the open descriptor (as GNU does: the file
    is still open).
  - name mode (`-F`, `--follow=name`): the same, plus: `Stat` failing ->
    `tail: 'NAME' has become inaccessible: No such file or directory` (once),
    the descriptor dropped; the path existing again (or for the first time
    with `--retry`) -> `tail: 'NAME' has appeared;  following new file` (two
    spaces, as GNU), reopen and print it from the start.
  - When output switches to a different FILE than the one last printed, and
    headers are on, print `\n==> NAME <==\n` first.
  - `Flush()` after printing (a non-terminal stdout is block-buffered).
- `--pid=PID`: after each round, if no process in
  `context.Process().OS()->GetRunningProcesses()` has that pid, do one last
  read of every file and exit 0. (The OS is reached only through
  `ICurrentProcess::OS()`.)
- Stopped (`TriggerStop()`, or a broken pipe on stdout) -> return at once;
  `BuiltinProcess` reports 143 / 141.

Documented exceptions (`--help` notes and the CLAUDE.md row): following is by
polling (`Stat` every interval), never inotify, so `---disable-inotify` and
`--max-unchanged-stats` are not treated; a file replaced by another of the
same or larger size is not noticed in name mode until its size shrinks
(there are no inode numbers); standard input is never followed.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Declare `CreateHeadCommand()` and `CreateTailCommand()`, add both to
`CreateStandardBuiltinCommands()` (that alone puts them in the `haisos --init`
template); add `commands/head/Head.cpp` and `commands/tail/Tail.cpp`.

Rules that bite: files only through `context.IO()`, processes only through
`context.Process().OS()`; every GNU option in `Options()`; `--help` from
`BuiltinHelpText`; GNU's messages; stops promptly on `TriggerStop()`; a
broken pipe exits 141 (as `BuiltinContext` already arranges); portable C++17
(`std::this_thread::sleep_for` for the sleep).

## Tests

New `HeadTest.cpp` and `TailTest.cpp` in
`tests/unit/components/BuiltinCommands.unittests/` (add to its
`CMakeLists.txt`), on `RunCaptured`. Files: `/n12` holding `seq 12`'s output
(`"1\n2\n...\n12\n"`, 27 bytes), `/abc` = `"a\nb\nc"`. Expected outputs are
GNU coreutils 9.4's (`LC_ALL=C`); verify any new one in the container.

`HeadTest.cpp`:
- `HeadLinesAndBytes`: `-3 /n12` -> `"1\n2\n3\n"`; `-n -9 /n12` -> `"1\n2\n3\n"`;
  `-c 5 /n12` -> `"1\n2\n3"`; `-c -20 /n12` -> `"1\n2\n3\n4"`; `-c 1k /n12` -> the whole 27 bytes;
  `-5c /n12` -> `"1\n2\n3"`; `-n 0 /n12` -> `""`; `-c -0 /abc` -> `"a\nb\nc"`; `-n -1 /abc` -> `"a\nb\n"`.
- `HeadHeaders`: `-n 2 /n12 /abc` -> `"==> /n12 <==\n1\n2\n\n==> /abc <==\na\nb\n"`;
  `-q -n1 /n12 /abc` -> `"1\na\n"`; `-v -n1 /abc` -> `"==> /abc <==\na\n"`;
  `-2v /n12` -> `"==> /n12 <==\n1\n2\n"`; stdin `"a\nb\n"`, `-n1 - /n12` ->
  `"==> standard input <==\na\n\n==> /n12 <==\n1\n"`.
- `HeadZeroTerminated`: stdin `"a\0b\0c\0"`, `-z -n 2` -> `"a\0b\0"`.
- `HeadErrors`: `-n 1 /missing /n12` -> out `"==> /n12 <==\n1\n"`, err
  `"head: cannot open '/missing' for reading: No such file or directory\n"`, status 1;
  `-n1 /n12 /docs /abc` -> out `"==> /n12 <==\n1\n\n==> /docs <==\n\n==> /abc <==\na\n"`, err
  `"head: error reading '/docs': Is a directory\n"`, status 1; `-n x /n12` ->
  `"head: invalid number of lines: 'x'\n"`, status 1; `-c '' /n12` -> `"head: invalid number of bytes: ''\n"`;
  `-n 99999999999999999999 /abc` -> `"head: invalid number of lines: '99999999999999999999': Value too large for defined data type\n"`;
  `-n1 -5 /n12` -> `"head: invalid trailing option -- 5\nTry 'head --help' for more information.\n"`.

`TailTest.cpp`:
- `TailLinesAndBytes`: `-3 /n12` -> `"10\n11\n12\n"`; `-n +10 /n12` -> same; `+11 /n12` ->
  `"11\n12\n"`; `-c 5 /n12` -> `"1\n12\n"`; `-c +25 /n12` -> `"12\n"`; `-2c /n12` -> `"2\n"`;
  `-n 2 /abc` -> `"b\nc"`; `-n 0 /n12` -> `""`; `-n +0 /abc` -> `"a\nb\nc"`; stdin `"x\0y\0z"`,
  `-z -n 2` -> `"y\0z"`.
- `TailHeaders`: `-n 1 /abc /n12` -> `"==> /abc <==\nc\n==> /n12 <==\n12\n"` (the
  `\n` before the second header follows the unterminated `c`); `-q -n1 /abc /n12` -> `"c12\n"`.
- `TailErrorsAndWarnings`: `-n 1 /missing /n12` -> out `"==> /n12 <==\n12\n"`, err
  `"tail: cannot open '/missing' for reading: No such file or directory\n"`, status 1;
  `-n x /n12` -> `"tail: invalid number of lines: 'x'\n"`; `-s x /n12` ->
  `"tail: invalid number of seconds: 'x'\n"`; `-n2 --pid=1 /n12` -> out `"11\n12\n"`, err
  `"tail: warning: PID ignored; --pid=PID is useful only when following\n"`, status 0;
  `--retry -n1 /n12` -> err `"tail: warning: --retry ignored; --retry is useful only when following\n"`;
  `-n 1 -1 /n12` -> err `"tail: option used in invalid context -- 1\n"`, status 1;
  `-f -n1 /missing` -> err `"tail: cannot open '/missing' for reading: No such file or directory\ntail: no files remaining\n"`, status 1;
  stdin `"hi\n"`, `-f` -> out `"hi\n"`, status 0 (standard input is not followed).
- `TailFollowsAppendedData` (descriptor mode): write `/g` = `"1\n2\n"`; start
  `/bin/tail -s 0.01 -f -n1 /g` with `os->StartProcess` and stdout an in-memory
  file (as `RunCaptured` sets up, but without waiting); poll the output file
  (up to `kWaitMs`) until it reads `"2\n"`; append `"3\n"` to `/g` through
  `root`; poll until it reads `"2\n3\n"`; truncate `/g` to `"x\n"`; poll
  until stderr holds `"tail: /g: file truncated\n"` and stdout ends with
  `"x\n"`; `TriggerStop()`; `WaitToFinish(kWaitMs)` true; exit code 143.
- `TailFollowNameNoticesRemovalAndReappearance` (`-F -s 0.01 -n1 /g`): remove
  `/g` -> stderr gets `"tail: '/g' has become inaccessible: No such file or directory\n"`;
  create it again with `"back\n"` -> `"tail: '/g' has appeared;  following new file\n"` and
  stdout ends with `"back\n"`; stop, 143.
- `TailFollowEndsWithThePidProcess`: start a long-running process (e.g.
  `/bin/tail -f /n12` itself, or the hsh `-c 'while :; do :; done'`), then
  `/bin/tail -s 0.01 -f --pid=<its pid> /n12`; stop the first; the second ends
  by itself with exit code 0 within `kWaitMs`.
- `TailFollowMultipleFilesPrintsHeadersOnSwitch`: `-s 0.01 -f /h1 /n12` with
  `/h1` = `"1\n"`; append `"more\n"` to `/h1`; the output ends with
  `"\n==> /h1 <==\nmore\n"`.

Use generous polling (10 ms steps up to `kWaitMs`), never fixed sleeps that
assume timing.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Head*:BuiltinCommandsTest.Tail*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

(The script's filter matches test executable names, so `BuiltinCommands`
is the narrowest it takes; the direct run narrows to this task's tests.
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` lives in
`CliParser.unittests`.) Add the new builtin names to the exact list in
`ListsEveryBuiltinSortedWithAVersion` (`BuiltinCommandsTest.cpp`), in byte
order.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: rows for `head` and `tail`
  (version, treated options, the exceptions above), both in the opening list.
- Root `CLAUDE.md`: rows for `head` and `tail` in the Builtin Commands table,
  and in the builtin lists.

## Acceptance

- [ ] head: `-n`/`-c` with negative counts and suffixes, obsolete `-NUM...`, headers with GNU's blank-line rule, `-q -v -z`, messages and exit codes above.
- [ ] tail: `+N`/`-N`, obsolete forms, ring buffers (never the whole input in memory for from-end counts), headers, follow by polling in descriptor and name modes with truncation/inaccessible/appeared messages, `--retry`, `-s`, `--pid` through `ICurrentProcess::OS()`, flush before sleeping, 143 on stop.
- [ ] Every GNU option in `Options()`; untreated ones reported; `--help` shape.
- [ ] Registered; CMakeLists; tests green on Linux and not timing-fragile; docs rows.

## Out of scope

- inotify, following standard input, `---presume-input-pipe`.
- `ParseSizeWithSuffix` itself (coreutils--du-cmp).
