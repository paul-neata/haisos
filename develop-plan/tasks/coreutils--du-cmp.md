# Task coreutils--du-cmp: du and cmp, with GNU size suffixes shared

- Rock: coreutils
- Depends on: coreutils--date (`FormatDateTime`, for `du --time`), coreutils--sort (`GnuQuote`, `ArgMatch`, `OpenInputOperand` in `BuiltinText.h`)
- Size: ~800 changed lines in ~9 files
- Plan checked against: develop @ f561090
- PR title: Add du and cmp builtins and shared GNU size parsing

(The planned coreutils--du-cmp-test came to ~1250 changed lines; `test` and
`[` are split off into coreutils--test-program.)

## Goal

Two new builtins, printing what GNU prints in the C locale:

- `du` (coreutils 9.4): disk usage from `FileStatus.blocks`, with `-a -s -c
  -h -k -m -b -0 -x -S -L -l -P -H -D -d/--max-depth -B/--block-size -t/--threshold
  --apparent-size --si --inodes --exclude --exclude-from -X --files0-from
  --time[=WORD] --time-style`.
- `cmp` (diffutils 3.10): `-b -c -i -l -n -s -v` and their long forms, the
  optional SKIP operands, `a b differ: char N, line M`, the EOF messages, exit
  0 / 1 / 2.

And `BuiltinSize.h/.cpp`: GNU's number-with-suffix parser (`xstrtoumax`'s
rules) and human-readable sizes, used here by du and cmp, and by `head`/`tail`
(coreutils--head-tail); `ls` moves its `HumanSize` there.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`,
`commands/ls/Ls.cpp` (`HumanSize`, `AllocatedSize`, how it walks
directories with `IO().ReadDirectory` and `IO().Stat`, `-R`),
`commands/wc/Wc.cpp` (`--files0-from`, reading standard input),
`commands/cat/Cat.cpp` (reading a file or `-` in chunks with
`StopRequested()` and `kIOInterrupted`),
`BuiltinFnmatch.h` (`FnMatch(pattern, text, flags)`, glibc's fnmatch, used by
grep's `--exclude`; du's exclusion uses it too),
`src/components/Filesystem/FilesystemUtils.h` (`BlocksForSize`: an in-memory
file of N bytes reports ceil(N/512) blocks, a directory 0, a placed builtin 0).

What earlier tasks provide (all on develop now): coreutils--date,
`std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);`
in `BuiltinDate.h` (ls already formats its times through it; `FileStatus` has
`accessTime`, `modificationTime`, `changeTime`, each a `FileDateTime`).

Also (coreutils--sort, `src/components/BuiltinCommands/BuiltinText.h`, read
the header for the exact API): `std::string GnuQuote(std::string_view text);` -- GNU's `quote()` in
the C locale, which is what GNU puts around a value in a message (`'abc'`;
`a'b` becomes `'a\'b'`) -- and `ArgMatch(context, "--opt", value, choices)`
(GNU's `XARGMATCH` with its "Valid arguments are:" block). Every `'x'` in a
diagnostic below that GNU writes with `quote()` is `GnuQuote(x)`; file names
GNU writes with `quoteaf` are `ShellEscapeQuoted(name, true)`, with `quotef`
`ShellEscapeQuoted(name)`.
`OpenInputOperand(context, name, failure)` (`failure` an `InputOpenFailure`)
opens cmp's operands (`-` is descriptor 0) and says why one failed (`Missing`,
`Directory`, `Denied`, `BadDescriptor`); cmp
words it (`cmp: NAME: No such file or directory`, unquoted, as diffutils
prints it). cmp's own usage messages put plain `'...'` around values (they
are printf strings in diffutils, not `quote()`).

## Changes

### New `src/components/BuiltinCommands/BuiltinSize.h` / `BuiltinSize.cpp`

Namespace `Haisos`:

```cpp
enum class SizeParse { Ok, Invalid, InvalidSuffix, Overflow };

// gnulib xstrtoumax, base 10: optional leading blanks, digits (none: the
// value is 1 when what follows is a valid suffix, else Invalid; a '-' sign is
// Invalid), then at most one suffix from |validSuffixes|. Suffix values: b 512,
// c 1, w 2, k K 1024, m M 1024^2, G 1024^3, T, P, E, Z, Y, R, Q the next
// powers of 1024 (only the case listed in validSuffixes is accepted). When
// validSuffixes contains '0', a suffix may be followed by "iB" (still 1024-based:
// "KiB") or "B" / "D" (1000-based: "KB" "kB" 1000, "MB" 10^6). Anything left
// after that: InvalidSuffix. Too big for uintmax_t: Overflow (out is the max).
SizeParse ParseSizeWithSuffix(std::string_view text, std::string_view validSuffixes, uintmax_t& out);

// GNU's human-readable size (-h: powers of 1024, units K M G T P E Z Y; --si:
// powers of 1000, units k M G T ...), rounded up, one decimal below 10:
// 0, 1023, 1.0K, 8.0K, 20K, 1.5M; --si 8.2k, 21k. Below one unit, the plain number.
std::string FormatHumanSize(uint64_t bytes, bool si);
```

`FormatHumanSize(bytes, false)` is exactly ls's current `HumanSize` (move it;
ls's `AllocatedSize` and the `-h` size column in the long listing both call
`FormatHumanSize(..., false)`; ls output must not change).

### New `src/components/BuiltinCommands/commands/du/Du.cpp`

`CreateDuCommand()`, name `du`, version `1.0.0`. Help: summary "estimate file
space usage", usage `du [OPTION]... [FILE]...` and `du [OPTION]...
--files0-from=F`.

`Options()` -- every option of GNU du 9.4 (all treated unless marked):
`-0/--null`, `-a/--all`, `--apparent-size`, `-B/--block-size=SIZE`,
`-b/--bytes` (= `--apparent-size --block-size=1`), `-c/--total`,
`-D/--dereference-args`, `-d/--max-depth=N`, `--files0-from=F`, `-H` (same as
`-D`), `-h/--human-readable`, `--inodes`, `-k` (`-B 1K`), `-L/--dereference`,
`-l/--count-links`, `-m` (`-B 1M`), `-P/--no-dereference`, `-S/--separate-dirs`,
`--si`, `-s/--summarize`, `-t/--threshold=SIZE`, `--time[=WORD]` (`Optional`),
`--time-style=STYLE`, `-X/--exclude-from=FILE`, `--exclude=PATTERN`,
`-x/--one-file-system`. The link options (`-D -H -L -P -l`) are accepted
with no effect (no symbolic or hard links in Haisos; `-l` would count a file
seen twice twice -- do that: without `-l` an operand or entry already counted,
by resolved path, is skipped as GNU skips a second hard link to it). `-x`:
no effect (exception: no device numbers; mounts are not told apart).

Through `BeginBuiltin` (usage errors exit 1). Size settings are applied in
the order given, the last winning (`-h -k` is `-k`, `-k -h` is `-h`).

Validation, each exit 1:
- `-B`/`--block-size` value (du.c via `human_options`): `human-readable` and
  `si` select -h and --si; a leading `'` is ignored; else
  `ParseSizeWithSuffix(v, "eEgGkKmMpPtTyYzZ0", n)`: Invalid or 0 -> `du:
  invalid -B argument 'x'` (for the long spelling `du: invalid --block-size
  argument 'x'`), InvalidSuffix -> `du: invalid suffix in -B argument '1x'`.
  When the value has no leading digit (`K`, `KB`, `KiB`, `M`), each size is
  printed with that suffix text after it, `kB` written with a lowercase k
  (`-B K` -> `8K`, `-B KB` -> `9kB`, `-B KiB` -> `8KiB`); with digits (`1K`,
  `1KB`, `1000`) the number alone.
- `-d N`: non-negative decimal, else `du: invalid maximum depth 'x'` + Try.
- `-t SIZE`: optional `-` sign then `ParseSizeWithSuffix(..., "kKmMGTPEZYRQ0")`,
  else `du: invalid -t argument 'x'` (no Try). Positive: show only entries of
  at least SIZE; negative: at most |SIZE| (`-t -4k`).
- `--time=WORD` through `ArgMatch(context, "--time", ...)`: `atime access use` / `ctime status` (mtime is the default and
  not a valid WORD in 9.4), else
  ```
  du: invalid argument 'x' for '--time'
  Valid arguments are:
    - 'atime', 'access', 'use'
    - 'ctime', 'status'
  Try 'du --help' for more information.
  ```
- `--time-style`: `full-iso`, `long-iso` (default), `iso`, `+FORMAT`, else
  the same argmatch shape (`'full-iso'`, `'long-iso'`, `'iso'`).
- `-s` with `-a` -> `du: cannot both summarize and show all entries` + Try;
  `-s` with `-d N`, N > 0 -> `du: warning: summarizing conflicts with
  --max-depth=N` + Try, exit 1; `-s -d 0` is fine.

Walking (du.c over fts): operands default to `.`; `--files0-from=F` (`-`
standard input) gives NUL-separated operands, with no operands allowed on
the command line: `du: extra operand 'y'`, then the line `file operands
cannot be combined with --files0-from` (no `du: ` prefix), then the Try line,
exit 1, as `wc` does. For each operand, depth-first in `ReadDirectory` order
sorted by name (GNU follows directory order; Haisos sorts by name: exception),
post-order: an entry's size is its own (`blocks * 512`, or with
`--apparent-size`/`-b` its `size` for a file and 0 for a directory, as GNU 9.4
counts apparent sizes; with `--inodes` 1 per entry) plus, for a directory,
its children's (with `-S` a directory line leaves out its subdirectories').
A line `SIZE\tPATH\n` (`\0` instead of `\n` with `-0`) for each directory at
depth <= max-depth (operand depth 0), for each file too with `-a`, for the
operand itself always (a file operand prints its line even without `-a`),
and for nothing below the operand with `-s`. PATH is the operand joined with
the names below it with one `/` (`.` gives `./a/b`; `a/` prints itself as
`a/` and its children as `a/b`). Exclusions (`--exclude=PATTERN`,
`-X FILE` one pattern per line): a path is excluded when the pattern matches
the whole path or any part of it after a `/` (gnulib's unanchored exclude),
with `FnMatch(pattern, text, 0)` (`BuiltinFnmatch.h`); an excluded entry is neither shown nor counted.
`-c`: a last line `SIZE\ttotal`. Check `StopRequested()` per entry.

Printing a size: blocks mode `ceil(bytes / blockSize)` (default block size
1024; `-m` 1048576; `-b` 1), then the suffix rule above; `-h`/`--si`
`FormatHumanSize(bytes, si)`. `--time`: `SIZE\tTIME\tPATH`, TIME the latest
modification (or access/change) time of the entry and everything below it,
formatted with `FormatDateTime` in local time: `long-iso` `%Y-%m-%d %H:%M`,
`full-iso` `%Y-%m-%d %H:%M:%S.%N %z`, `iso` `%Y-%m-%d`, `+FORMAT` as given.

A missing operand: `du: cannot access 'x': No such file or directory`
(`ShellEscapeQuoted(x, true)`), go on, exit 1 at the end. An unreadable
directory: `du: cannot read directory 'x': Permission denied`, exit 1.

Documented exceptions: sizes come from `FileStatus.blocks` (an in-memory file
of N bytes is ceil(N/512) blocks, directories 0 blocks, placed builtins 0);
entries are visited in name order; `-x`, `-D -H -L -P` have no effect (no
devices or links); a file seen twice is recognised by its resolved path;
`DU_BLOCK_SIZE`/`BLOCK_SIZE`/`BLOCKSIZE` are not read.

### New `src/components/BuiltinCommands/commands/cmp/Cmp.cpp`

`CreateCmpCommand()`, name `cmp`, version `1.0.0`. Help: summary "compare two
files byte by byte", usage `cmp [OPTION]... FILE1 [FILE2 [SKIP1 [SKIP2]]]`,
`basedOn` empty (man7 has cmp.1).

`Options()` (diffutils 3.10): `{'b', "print-bytes"}`, `{'c', "print-chars"}`
(the same as `-b`), `{'i', "ignore-initial", Required, "SKIP"}`,
`{'l', "verbose"}`, `{'n', "bytes", Required, "LIMIT"}`, `{'s', "quiet"}`,
`{0, "silent"}` (same id as `-s`), and `{'v', "", kBuiltinOptionVersion, None, "", "output version information"}`
(cmp's `-v` is `--version`; using that id makes `BeginBuiltin`-style handling
print the version).

Do not use `BeginBuiltin`: diffutils prefixes its Try line. Call
`ParseBuiltinArgs` and on an error print `cmp: <error>` then `cmp: Try 'cmp
--help' for more information.`, exit 2. Then `--help`/`--version`/`-v` as
`BeginBuiltin` does, and `ReportNotTreated`. Other usage errors, same two-line
shape, exit 2: `options -l and -s are incompatible`; no operand: `missing
operand after 'cmp'`; more than 4: `extra operand '<5th>'`; a bad `-i` or
SKIP: `invalid --ignore-initial value 'x'`; a bad `-n`: `invalid --bytes value
'x'`. SKIP and LIMIT: `ParseSizeWithSuffix(..., "kKmMGTPEZYRQ0")` (`kB` 1000,
`K` 1024, ...); `-i A:B` gives the two skips, `-i A` both; SKIP1/SKIP2
operands override `-i`'s.

FILE2 defaults to `-`; `-` is standard input (descriptor 0); both `-` (or the
same resolved path) compare equal at once, exit 0. A file that cannot be
opened: `cmp: NAME: No such file or directory` (or `Is a directory`), exit 2;
with `-s` nothing is printed, still exit 2. Skip the initial bytes by reading
and discarding (descriptors cannot seek).

Comparison, counting byte numbers from 1 after the skip and lines from 1:
- Default: at the first difference `FILE1 FILE2 differ: char N, line M\n` on
  stdout (C locale wording: `char`), exit 1. With `-b`/`-c`: `FILE1 FILE2
  differ: byte N, line M is %3o C1 %3o C2\n` with each byte also shown by
  diffutils' `sprintc` (printable as is; >= 128 `M-` then the low 7 bits;
  below 32 `^` + (c+64); 127 `^?`).
- `-l`: every difference, `%*s %3o %3o\n` (`-lb`: `%*s %3o %-4s %3o %s\n`),
  the byte-number width being the number of digits of the smaller file size
  after the skip (or of LIMIT if smaller) -- `cmp -l` of two 300-byte files
  differing in the last byte prints `300   0 170`. Exit 1 if any differed.
- `-s`: nothing printed, exit 1 on any difference or length difference.
- `-n LIMIT`: compare at most LIMIT bytes.
- One input ends first (and no difference was printed by the default mode):
  on stderr `cmp: EOF on NAME which is empty\n` if it had no byte after the
  skip; else, default mode, `cmp: EOF on NAME after byte N, line M\n` when the
  last byte read was `\n` (M the number of lines read), otherwise `cmp: EOF on
  NAME after byte N, in line M\n`; with `-l`, `cmp: EOF on NAME after byte N\n`.
  Exit 1. (`-s` prints none of them.)
Read both in chunks, check `StopRequested()`, treat `kIOInterrupted` as a stop.

### `src/components/BuiltinCommands/commands/ls/Ls.cpp`

`HumanSize` (its two uses: `AllocatedSize` and the `-h` size column) moved to
`BuiltinSize` (above); include `BuiltinSize.h`. No version bump (no behaviour
change; `ls` is at 1.3.1 now).

### `BuiltinCommandList.h`, `CMakeLists.txt`

Declare `CreateCmpCommand()` (after `CreateChmodCommand()`) and
`CreateDuCommand()` (after `CreateDirnameCommand()`), add both at the same
places to `CreateStandardBuiltinCommands()` (that puts them in the `haisos
--init` template); in `src/components/BuiltinCommands/CMakeLists.txt` add
`BuiltinSize.cpp` (between `BuiltinRunProgram.cpp` and
`BuiltinTestExpression.cpp`), `commands/cmp/Cmp.cpp` (after `Chmod.cpp`) and
`commands/du/Du.cpp` (after `Dirname.cpp`).

Rules that bite: files only through `context.IO()` (`Stat`, `ReadDirectory`,
`OpenFile`, `GetDescriptor(0)`); every GNU option in `Options()`; `--help`
from `BuiltinHelpText`; GNU's messages; portable C++17, no POSIX headers.

## Tests

New `DuTest.cpp`, `CmpTest.cpp` and `BuiltinSizeTest.cpp` in
`tests/unit/components/BuiltinCommands.unittests/` (add to the one
`add_executable(BuiltinCommands.unittests ...)` line of its `CMakeLists.txt`,
in alphabetical order), on the fixture's `RunCaptured`
(`BuiltinCommandsFixture.h`; tests are `TEST_F(BuiltinCommandsTest, ...)`). The fixture's files are in memory, so
blocks are ceil(size/512) and directories 0: set up a tree for du in each
test, e.g. `/t/a/f1` of 5000 bytes (10 blocks), `/t/a/b/f2` of 100 bytes (1
block), `/t/c/e` empty. Expected du numbers follow from that (5120 + 512 =
5632 bytes under `a`: 6 in 1K blocks; `b` 1). Where an expectation is about
format, it is GNU's (verified on coreutils 9.4 / diffutils 3.10, `LC_ALL=C`);
verify new ones in the container.

`DuTest.cpp` (working directory `/t`):
- `DuListsDirectoriesPostOrder`: `du` -> `"1\t./a/b\n6\t./a\n0\t./c\n6\t.\n"`.
- `DuAllSummarizeTotal`: `-a .` ->
  `"1\t./a/b/f2\n1\t./a/b\n5\t./a/f1\n6\t./a\n0\t./c/e\n0\t./c\n6\t.\n"`; `-s` -> `"6\t.\n"`; `-sc a c` -> `"6\ta\n0\tc\n6\ttotal\n"`; `-d 1 .` ->
  `"6\t./a\n0\t./c\n6\t.\n"`; `--max-depth=0 a` -> `"6\ta\n"`.
- `DuUnits`: `-b a` -> `"100\ta/b\n5100\ta\n"`; `--apparent-size a` -> `"1\ta/b\n5\ta\n"`;
  `-B 1 a/f1` -> `"5120\ta/f1\n"`; `-B K a/f1` -> `"5K\ta/f1\n"`; `-B KB a/f1` ->
  `"6kB\ta/f1\n"`; `-B 1KB a/f1` -> `"6\ta/f1\n"`; `-h a/f1` -> `"5.0K\ta/f1\n"`;
  `--si a/f1` -> `"5.2k\ta/f1\n"`; `-m a` -> `"1\ta/b\n1\ta\n"`; `-h -k a/f1` -> `"5\ta/f1\n"`.
- `DuNullThresholdExclude`: `-0 -s a` -> `"6\ta\0"`; `-t 2k -a a` -> only `"5\ta/f1\n6\ta\n"`;
  `--exclude=b a` -> `"5\ta\n"` (b and what is in it neither shown nor counted);
  `--exclude='*/b' a` -> `"5\ta\n"`; `--inodes a` -> `"2\ta/b\n4\ta\n"`.
- `DuTime`: `--time a/b` matches `1\t[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}\ta/b\n`.
- `DuErrors`: `missing` -> err `"du: cannot access 'missing': No such file or directory\n"`,
  status 1; `-B x a` -> `"du: invalid -B argument 'x'\n"`, status 1; `-B 1x a` ->
  `"du: invalid suffix in -B argument '1x'\n"`; `-d x a` -> `"du: invalid maximum depth 'x'\nTry 'du --help' for more information.\n"`;
  `-s -a a` -> `"du: cannot both summarize and show all entries\nTry ..."`; `--time=x a` -> the argmatch block above.

`CmpTest.cpp` (files `c1` `"abc\ndef\n"`, `c2` `"abc\ndXf\n"`, `c3` `"abc\n"`, `e0` empty):
- `CmpIdenticalAndDifferent`: `c1 c1` -> status 0, no output; `c1 c2` -> out
  `"c1 c2 differ: char 6, line 2\n"`, status 1; `-b c1 c2` -> `"c1 c2 differ: byte 6, line 2 is 145 e 130 X\n"`.
- `CmpVerbose`: `-l c1 c2` -> `"6 145 130\n"`; `-lb c1 c2` -> `"6 145 e    130 X\n"`;
  300-byte files differing in the last byte (0 vs `x`) -> `"300   0 170\n"`.
- `CmpEof`: `c1 c3` -> err `"cmp: EOF on c3 after byte 4, line 1\n"`, status 1;
  `e0 c1` -> `"cmp: EOF on e0 which is empty\n"`; `-l c1 c3` -> `"cmp: EOF on c3 after byte 4\n"`;
  `"ab"` vs `"abc"` -> `"cmp: EOF on p1 after byte 2, in line 1\n"`; `"a\nb"` vs `"a\nbc"` ->
  `"cmp: EOF on q1 after byte 3, in line 2\n"`; `-s c1 c3` -> nothing, status 1.
- `CmpSkipAndLimit`: `-n 5 c1 c2` -> status 0; `-i 2 c1 c2` -> `"c1 c2 differ: char 4, line 2\n"`; `c1 c2 5 6` -> `"c1 c2 differ: char 1, line 1\n"`; `-i 1k c1 c2` -> status 0.
- `CmpStandardInput`: stdin `"abc\ndXf\n"`, `- c1` -> `"- c1 differ: char 6, line 2\n"`;
  `c1` alone with stdin `"abc\ndef\n"` -> status 0.
- `CmpErrors` (each status 2): `c1 missing` -> err `"cmp: missing: No such file or directory\n"`;
  `-s c1 missing` -> no output; `c1 /docs` -> `"cmp: /docs: Is a directory\n"`; `-x c1 c2` ->
  `"cmp: invalid option -- 'x'\ncmp: Try 'cmp --help' for more information.\n"`; no operand ->
  `"cmp: missing operand after 'cmp'\ncmp: Try ...\n"`; `-l -s c1 c2` -> `"cmp: options -l and -s are incompatible\ncmp: Try ...\n"`;
  `c1 c2 1 2 3` -> `"cmp: extra operand '3'\ncmp: Try ...\n"`; `-n x c1 c2` -> `"cmp: invalid --bytes value 'x'\ncmp: Try ...\n"`;
  `-v` -> out `"cmp (HaisosOS builtin) 1.0.0\n"`, status 0.

`BuiltinSizeTest.cpp` (plain `TEST(BuiltinSizeTest, ...)`, direct calls):
- `ParseSizeWithSuffixFollowsXstrtoumax`: with `"bkKmMGTPEZYRQ0"`: `"10"` 10,
  `"1k"` 1024, `"1kB"` 1000, `"1KiB"` 1024, `"2M"` 2097152, `"1b"` 512, `"K"` 1024,
  `"1w"` InvalidSuffix, `"x"` Invalid, `"-1"` Invalid, `"99999999999999999999"` Overflow.
- `FormatHumanSizeRoundsUp`: (0) `"0"`, (1023) `"1023"`, (1024) `"1.0K"`, (8192)
  `"8.0K"`, (20480) `"20K"`, (1536*1024) `"1.5M"`; si: (8192) `"8.2k"`, (20480) `"21k"`.

The du numbers above follow from the rule (blocks = ceil(size/512) per file,
0 per directory, sizes summed in bytes, then ceil(bytes / block size) at print
time); if a tree you build differs, derive the expectations from that rule.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Du*:BuiltinCommandsTest.Cmp*:*BuiltinSize*:BuiltinCommandsTest.Ls*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

(The script's filter matches test executable names, so `BuiltinCommands`
is the narrowest it takes; the direct run narrows to this task's tests.
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` lives in
`CliParser.unittests`.) Add the new builtin names to the exact list in
`ListsEveryBuiltinSortedWithAVersion` (`BuiltinCommandsTest.cpp`), in byte
order: `"cmp"` between `"chmod"` and `"cp"`, `"du"` between `"dirname"` and
`"echo"`.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: rows for `cmp` (after `chmod`)
  and `du` (after `dirname`) in "The commands" table (version, treated,
  exceptions above), both in the opening list; a "Key Classes" bullet on
  `BuiltinSize.h` (`ParseSizeWithSuffix`, `FormatHumanSize`), next to the
  `BuiltinText.h` and `BuiltinDate.h` ones.
- Root `CLAUDE.md`: rows for `cmp` and `du` in the "Builtin Commands" table, and
  in its lists of builtins (the directory tree's `BuiltinCommands/` line, the
  "Commands compiled into Haisos" paragraph).

## Acceptance

- [ ] `BuiltinSize.h` with `ParseSizeWithSuffix` and `FormatHumanSize`; ls uses `FormatHumanSize`, its output unchanged.
- [ ] du: every option listed; sizes from blocks (or sizes with `--apparent-size`/`-b`, entries with `--inodes`); `-B` suffix display rule; `-h`/`--si`; `-a -s -c -d -t -0 -S --exclude -X --files0-from --time --time-style`; every message above.
- [ ] cmp: `char`/`byte` messages, `-l` widths, `sprintc`, EOF messages to stderr, skips and limit, `-s`, `-v` as version, diffutils' prefixed Try line, exit 0/1/2.
- [ ] Registered; CMakeLists; tests green on Linux; docs rows with the exceptions.

## Out of scope

- `test` and `[` (coreutils--test-program).
- `head`/`tail` (coreutils--head-tail; they reuse `ParseSizeWithSuffix`).
- Hard links, symbolic links, devices: Haisos has none to count.
