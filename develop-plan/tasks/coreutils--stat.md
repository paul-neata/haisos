# Task coreutils--stat: stat

- Rock: coreutils
- Depends on: coreutils--date (`FormatDateTime`), coreutils--sort (`GnuQuote`), coreutils--printf-seq (`BuiltinPrintf`: `PrintfSpec`, `FormatPrintfUnsigned`, `FormatPrintfString`)
- Size: ~550 changed lines in ~6 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the stat builtin

(The second half of the planned coreutils--date-stat, split because the two
together came to ~1250 changed lines.)

## Goal

A new builtin `stat` (`BUILTIN rootfs stat /bin/stat`) printing what GNU
coreutils 9.4 `stat` prints in the C locale, from what `IFileIO::Stat` knows:
the default layout, `-c/--format`, `--printf`, `-t/--terse`, `-L`, and
`-f/--file-system` as far as data exists. What Haisos cannot know (device and
inode numbers, permissions, owners, birth time, filesystem statistics) is
shown with fixed values, each a documented exception, consistent with `ls -l`
(owner and group `haisos`, permissions `rwxrwxrwx`).

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md` (`ShellEscapeQuoted`, output
rules, the ls row's exceptions), `BuiltinCommand.h`,
`interfaces/IFileSystemService.h` (`FileStatus`: type, size, blocks,
linkCount, access/modification/change times, deviceMajor/Minor),
`commands/ls/Ls.cpp` (how it turns a `FileStatus` into `-rwxrwxrwx`).

What earlier tasks provide, as if on develop:
- coreutils--date: `std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);` in `BuiltinDate.h`.
- coreutils--sort: `GnuQuote` in `BuiltinText.h` (GNU's `quote()`: the `'%5%'` of `invalid directive`); file names in `cannot statx` are `ShellEscapeQuoted(name, true)` (GNU's `quoteaf`).
- coreutils--printf-seq: in `BuiltinPrintf.h`, `struct PrintfSpec { std::string flags; std::optional<int> width; std::optional<int> precision; bool widthFromArgument; bool precisionFromArgument; char conversion; };`,
  `std::string FormatPrintfUnsigned(const PrintfSpec&, uintmax_t);` (conversions o u x X),
  `std::string FormatPrintfString(const PrintfSpec&, std::string_view);` (s).

## Changes

### New `src/components/BuiltinCommands/commands/stat/Stat.cpp`

`CreateStatCommand()`. Name `stat`, version `1.0.0`. Help: summary "display
file or file system status", usage `stat [OPTION]... FILE...`; notes: the
directives in a few lines, and the exceptions below.

`Options()` (GNU stat 9.4):
`{'L', "dereference", kDereference, None, "", "follow links (no links here: no effect)"}`,
`{'f', "file-system", kFileSystem, None, "", "display file system status"}`,
`{'c', "format", kFormat, Required, "FORMAT", "use FORMAT, a newline after each file"}`,
`{0, "printf", kPrintf, Required, "FORMAT", "like --format, escapes, no newline"}`,
`{'t', "terse", kTerse, None, "", "print the information in terse form"}`,
`{0, "cached", kBuiltinNotTreated, Required, "MODE"}`.

Through `BeginBuiltin` (usage errors exit 1). No operand: `stat: missing
operand` + Try, exit 1. A later `-c`/`--printf` replaces an earlier one
(`-c` adds `\n` after each file and takes `\` literally; `--printf` adds
nothing and interprets escapes).

Per operand (go on after a failure; exit 1 if any failed, else 0):
`context.IO().Stat(name)`; failing: `stat: cannot statx 'NAME': No such file
or directory` (with `-f`: `stat: cannot read file system information for
'NAME': No such file or directory`), NAME through `ShellEscapeQuoted(name,
true)`. `-` is a file named `-` (exception: GNU stats standard input, which
a Haisos descriptor cannot report). Check `StopRequested()` between operands.

Default formats (stat.c `default_format`, C locale), byte for byte:

```
  File: %n
  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F
Device: %Hd,%Ld\tInode: %-10i  Links: %h
Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)
Access: %x
Modify: %y
Change: %z
 Birth: %w
```

For a character device the third line is `Device: %Hd,%Ld\tInode: %-10i
Links: %-5h Device type: %Hr,%Lr` (two spaces before `Links`, as in the
non-device line). The `File:` line shows the name as given, unquoted (GNU's
default quoting there is literal). `-t`: `%n %s %b %f %u %g %D %i %h %t %T %X
%Y %Z %W %o\n`. `-f`:

```
  File: "%n"
    ID: %-8i Namelen: %-7l Type: %T
Block size: %-10s Fundamental block size: %S
Blocks: Total: %-10b Free: %-10f Available: %a
Inodes: Total: %-10c Free: %d
```

and `-f -t`: `%n %i %l %t %s %S %b %f %a %c %d\n`.

Directive interpreter (stat.c `print_it`): a `%` is followed by flags from
`'-+ #0I`, a width, `.precision`, then the directive character. `%%` (with
nothing between) prints `%`; `%` with flags/width then `%` -> `stat: '%5%':
invalid directive`, exit 1 at once; a `%` ending the format prints `%`. `H`
or `L` before `d` or `r` selects the major/minor half. A directive the table
does not know prints `?` (GNU does the same). Each value goes through
`BuiltinPrintf` with a `PrintfSpec` built from the parsed flags, width and
precision: strings with conversion `s` (`FormatPrintfString`), numbers with
`u`, `o` or `x` (`FormatPrintfUnsigned`) as listed:

| Directive | Value in Haisos | Printed as |
|---|---|---|
| `%n` | the operand as given | s |
| `%N` | `ShellEscapeQuoted(name, true)` (`'st/f'`) | s |
| `%s` | `size` | u |
| `%b` | `blocks` | u |
| `%B` | 512 | u |
| `%o` | 4096 (I/O block) | u |
| `%F` | `regular empty file` (size 0) / `regular file` / `directory` / `character special file` | s |
| `%a` | 777 | o (so `%04a` is `0777`) |
| `%A` | `-rwxrwxrwx`, `drwxrwxrwx`, `crwxrwxrwx` | s |
| `%f` | raw mode: 81ff / 41ff / 21ff | x |
| `%h` | `linkCount` | u |
| `%i` | 0 | u |
| `%d`, `%Hd`, `%Ld`, `%D` | 0 (`%D` hex) | u / x |
| `%r` | device number: `deviceMajor` and `deviceMinor` as `%Hr`/`%Lr` (u), `%r` itself makedev-encoded `(major << 8) | minor` for small numbers; `%R` the same in hex; `%t`/`%T` major/minor in hex | u / x |
| `%u`, `%g` | 0 | u |
| `%U`, `%G` | `haisos` | s |
| `%m` | `/` (no mount information) | s |
| `%C` | `?`, no message (no security contexts) | s |
| `%x %y %z` | access, modification, change time: `FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", t, false)` | s |
| `%X %Y %Z` | the same as seconds since the epoch, see below | -- |
| `%w` | `-` (birth unknown) | s |
| `%W` | 0 | u |

`%X %Y %Z` with a precision (stat.c `out_epoch_sec`): `%.Y` is 9 fractional
digits, `%.3Y` three -- truncated, not rounded (`%.1Y` of .5 s is
`1700000000.5`, `%.3Y` `1700000000.500`); without a precision just the
seconds. A width applies to the whole text: `%10.3Y` and `%-12.2Y` -- follow
`out_epoch_sec`'s rules; verify every width case you implement against
`stat -c` in the task container (`touch -d @1700000000.5 f; TZ=UTC stat -c
'%10.3Y|%-12.2Y|' f` gives `1700000000.500|1700000000.50 |`).

`-f` directives: `%n` name, `%i` 0 (as hex), `%l` 255, `%t` 0 (hex), `%T`
`haisos`, `%s` and `%S` 4096, `%b %f %a %c %d` 0. (Exception: Haisos has no
filesystem statistics; `IFileIO` has nothing like `statvfs`.)

`--printf` escapes (stat.c `print_esc_char`, not printf's): `\NNN` 1-3 octal
digits, `\xH`/`\xHH`, `\a \b \e \f \n \r \t \v \" \\`; any other `\c` prints
`c` after `stat: warning: unrecognized escape '\c'` on stderr (verify `\u` in
the container: GNU 9.4 printed `\u0041` as `A` without a warning -- follow
what it does). With `-c` a backslash is printed as is.

Rules that bite: only `context.IO()` reaches files; `--help` from
`BuiltinHelpText`; every GNU option listed; GNU's messages and exit codes;
portable C++17.

Documented exceptions (`--help` notes, CLAUDE.md row): device and inode
numbers 0; permissions `0777`/`rwxrwxrwx`; Uid/Gid `0/haisos`; I/O block
4096; Birth `-`; `%m` `/`; `%C` `?` without an error; `-L` changes nothing
(no symbolic links); `-f` values fixed (ID 0, Namelen 255, Type `haisos`,
sizes 4096, counts 0); `QUOTING_STYLE` is not read (`%N` is always
shell-escape-always); `-` is a file name, not standard input; `--cached`
not treated.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Declare `CreateStatCommand()`, add it to `CreateStandardBuiltinCommands()`
(the `haisos --init` template follows), add `commands/stat/Stat.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/StatTest.cpp` (in that
directory's `CMakeLists.txt`), on `RunCaptured` and the fixture's in-memory
files (`/notes.txt` is 17 bytes, 1 block; `/docs` a directory with `a.md` and
`sub`). Time lines depend on the clock: compare them with a regex
(`[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{9} [+-][0-9]{4}`),
everything else exactly.

- `StatDefaultLayoutOfAFile`: `stat /notes.txt` -> lines 1-4 exactly
  `"  File: /notes.txt"`, `"  Size: 17        \tBlocks: 1          IO Block: 4096   regular file"`,
  `"Device: 0,0\tInode: 0           Links: 1"`,
  `"Access: (0777/-rwxrwxrwx)  Uid: (    0/  haisos)   Gid: (    0/  haisos)"`;
  lines 5-7 `Access: `/`Modify: `/`Change: ` plus the time regex; line 8
  `" Birth: -"`; status 0.
- `StatDefaultLayoutOfADirectoryAndAnEmptyFile`: `/docs` -> `directory`,
  `(0777/drwxrwxrwx)`, `Links: 3` (its linkCount); an empty file -> `regular empty file`.
- `StatOfADevice`: mount a `DEV` filesystem as `ls`'s device test does
  (see `LsShowsDevicesAsTheRealOneDoes`), `stat /dev/null` -> line 2 ends in
  `character special file`, line 3 `"Device: 0,0\tInode: 0           Links: 1     Device type: 1,3"`.
- `StatFormatDirectives`: `-c '%n %s %b %B %F %A %a %h %i %u %U %g %G %w %W %d %D %o %f %m %C %N' /notes.txt`
  -> `"/notes.txt 17 1 512 regular file -rwxrwxrwx 777 1 0 0 haisos 0 haisos - 0 0 0 4096 81ff / ? '/notes.txt'\n"`.
- `StatFormatWidthsAndPrecision`: `-c '%10s|%-12n|%.3n|%04a|%%|%E' /notes.txt` ->
  `"        17|/notes.txt  |/no|0777|%|?\n"`; epoch precision on a file whose
  times are set: write a file, set its times with the fixture's physical-disk
  approach or (simpler) compare `-c '%Y'` with `root->Stat` seconds and
  `-c '%.3Y'` with those seconds + `.` + the first three nanosecond digits.
- `StatPrintfEscapesAndNoNewline`: `--printf '%n\t%s\n' /notes.txt /docs/a.md` ->
  `"/notes.txt\t17\n/docs/a.md\t5\n"`; `--printf 'a\q' /notes.txt` -> out `"aq"`,
  err `"stat: warning: unrecognized escape '\\q'\n"`; `-c 'a\tb' /notes.txt` -> `"a\\tb\n"`.
- `StatTerse`: `-t /notes.txt` -> `"/notes.txt 17 1 81ff 0 0 0 0 1 0 0 <X> <Y> <Z> 0 4096\n"`
  with X/Y/Z the seconds from `root->Stat` (build the expected line from them).
- `StatFileSystem`: `-f /notes.txt` -> exactly
  `"  File: \"/notes.txt\"\n    ID: 0        Namelen: 255     Type: haisos\nBlock size: 4096       Fundamental block size: 4096\nBlocks: Total: 0          Free: 0          Available: 0\nInodes: Total: 0          Free: 0\n"`;
  `-f -t /notes.txt` -> `"/notes.txt 0 255 0 4096 4096 0 0 0 0 0\n"`.
- `StatErrors`: `stat` -> `"stat: missing operand\nTry 'stat --help' for more information.\n"`,
  status 1; `-c %s /notes.txt /missing` -> out `"17\n"`, err
  `"stat: cannot statx '/missing': No such file or directory\n"`, status 1;
  `-f /missing` -> `"stat: cannot read file system information for '/missing': No such file or directory\n"`;
  `-c '%5%' /notes.txt` -> err `"stat: '%5%': invalid directive\n"`, status 1.

(The layout strings above follow GNU 9.4's format strings exactly; the
values are Haisos's. Check spacing against `stat` in the container on a real
file if in doubt: only the values differ.)

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Stat*'
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

- `src/components/BuiltinCommands/CLAUDE.md`: a `stat` row (version,
  treated, the exceptions above) and `stat` in the opening list.
- Root `CLAUDE.md`: a `stat` row in the Builtin Commands table, and in the
  builtin lists.

## Acceptance

- [ ] Default, `-t`, `-f`, `-f -t` layouts are GNU's format strings byte for byte; values per the table.
- [ ] `-c`/`--printf` directive parsing with flags/width/precision through `BuiltinPrintf`; `%H`/`%L` prefixes; unknown directive `?`; invalid `%...%` directive message; `--printf` escapes and warning.
- [ ] Times through `FormatDateTime`; epoch precision truncated.
- [ ] Every exception documented in `--help` notes and the CLAUDE.md row.
- [ ] Registered; CMakeLists; tests green on Linux.

## Out of scope

- Real device/inode/owner/permission data (no such data in `FileStatus`),
  `statvfs`, `QUOTING_STYLE`, symbolic links.
- `date` and `FormatDateTime` (coreutils--date).
