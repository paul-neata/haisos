# Task coreutils--uniq-cut: the uniq and cut builtins

- Rock: coreutils
- Depends on: coreutils--sort (its `BuiltinText.h` helpers)
- Size: ~900 changed lines in ~9 files
- Plan checked against: develop @ 90728a3
- PR title: Add the uniq and cut builtins

## Goal

`uniq` and `cut` are builtin commands, placed with `BUILTIN rootfs uniq
/bin/uniq` (and `cut`), behaving as GNU coreutils 9.4's (Ubuntu 24.04, the
task container's) with `LC_ALL=C`: the same options, output byte for byte
(`uniq -c`'s `%7d ` counts, `cut`'s output delimiters), the same messages
(`cut: you must specify a list of bytes, characters, or fields`) and exit
codes. So `rg -n -w Create src | cut -d: -f1 | uniq -c | sort -rn | head -3`
prints what it prints on Linux. Both stream: each line is written as soon as
it is decided, so they work in the middle of a long pipeline.

## Context

Read first: the root `CLAUDE.md` (sections "Security", "Builtin Commands",
"Automatic Development Rules" rule 9), `src/components/BuiltinCommands/CLAUDE.md`,
`develop-plan/tasks/coreutils--sort.md` (section `BuiltinText.h`), and the
code of `commands/wc/Wc.cpp` and `commands/sort/Sort.cpp` (models: option
table, `Help()`, `Run()` on `BeginBuiltin`, reading inputs).

What exists (by exact name):
- `BuiltinCommand.h`: `IBuiltinCommand`, `BuiltinOption` (`shortName,
  longName, id, argument, argumentName, description`), `BuiltinArgument`
  (`Optional`: a long option takes `=value` only; its short form never takes
  one), `kBuiltinNotTreated`, `ParseBuiltinArgs` (options in the order given;
  `-12` is the two short options `1` and `2`), `BeginBuiltin(context,
  command, usageErrorStatus, exitStatus)`, `BuiltinContext` (`Out` buffered
  off a terminal, `Error`, `ErrorText`, `TryHelp`, `StopRequested`, `IO()`),
  `ShellEscapeQuoted(name, always)` (GNU `quotef`/`quoteaf`),
  `BuiltinHelpText`. Also a `BeginBuiltin` overload taking the caller's own
  argument list (chmod's); uniq and cut use the plain one.
- Shared helpers already on develop (reuse, do not re-add): besides the
  `BuiltinText.h` ones below, `BuiltinCompare.*`, `BuiltinRemove.*`,
  `BuiltinCopy.*`, `BuiltinRunProgram.h`, `BuiltinPrintf.*`,
  `BuiltinTestExpression.*` -- none is needed by uniq/cut.
- From coreutils--sort (done, `BuiltinTextTest.cpp` tests them), `src/components/BuiltinCommands/BuiltinText.h`:
  `std::string GnuQuote(std::string_view)` (GNU `quote()`, C locale);
  `struct ArgChoice { std::string name; int value; }` and
  `std::optional<int> ArgMatch(BuiltinContext&, const std::string& longOption, const std::string& value, const std::vector<ArgChoice>&)`
  (prints GNU's "invalid argument ... Valid arguments are: ... Try" and
  returns nullopt; the caller returns 1);
  `OpenInputOperand(context, name, InputOpenFailure&)` (`-` is descriptor 0;
  `Missing`, `Directory`, `Denied`, `BadDescriptor`);
  `BuiltinLineReader(context, input, delimiter)` with
  `LineReadResult Next(std::string& line, bool& delimited)` (`Line`, `End`,
  `Error`, `Stopped`); `ssize_t WriteFully(IFileDescriptor&, std::string_view)`.
- `FilesystemUtils.h`: `kFileOpenWriteCreateTruncate`, `kFileCreateMode`.
- Tests: `BuiltinCommandsFixture.h` -- `BuiltinCommandsTest`, `WriteFile`,
  `RunCaptured(command, args, input, workingDirectory)` -> `out`, `err`,
  `status`; generic tests in `BuiltinCommandsTest.cpp`
  (`ListsEveryBuiltinSortedWithAVersion`, `EveryBuiltinsHelpHasTheSameShape`,
  `EveryUntreatedOptionIsAcceptedAndReported`).

Rules that bite (root `CLAUDE.md`, restated):
- `ICurrentProcess` is the only door out of a process: files only through
  `context.IO()`; nothing holds an `IFileSystem` or `IHaisosOS`.
- Builtins: GNU's output byte for byte (C locale, ASCII quotes); every option
  of the real command in `Options()`; `--help` only from `BuiltinHelpText`;
  `--version`; default `ManPage()`; registered in
  `CreateStandardBuiltinCommands()` (which writes the `haisos --init`
  template line -- never by hand); sources in `CMakeLists.txt`; tests listed.
- Portable C++17; no POSIX headers, no `<regex>`.
- Reads stop promptly on `TriggerStop()` (`LineReadResult::Stopped` -> return
  quietly); a broken stdout pipe is handled by `BuiltinContext` (141).

## Changes

### `BuiltinCommand.h` / `BuiltinCommand.cpp` -- hidden options

GNU uniq accepts the obsolete `-N` (skip N fields) without documenting it.
Add a last member to `BuiltinOption`:

```cpp
    // An obsolete spelling the real command accepts but does not document
    // (uniq's -N): parsed like any other option, never shown in --help.
    bool hidden = false;
```

(Existing brace initializers keep working: it defaults to false.)
`BuiltinHelpText` skips hidden options in both the described list and the
"Not treated arguments" line. Update the generic tests in
`BuiltinCommandsTest.cpp`: `EveryBuiltinsHelpHasTheSameShape` skips hidden
options. (If an earlier task of this develop -- `coreutils--head-tail`
needs `head -5` -- already added an equivalent, use it instead and skip this
section.)

### `commands/uniq/Uniq.cpp` (new)

`CreateUniqCommand()`; name `uniq`, version `1.0.0`, `usageErrorStatus` 1.

Options (every one of GNU uniq 9.4, all treated):
`-c --count`, `-d --repeated`, `-D` (short only), `--all-repeated[=METHOD]`
(`Optional`), `-f --skip-fields=N`, `--group[=METHOD]` (`Optional`), `-i
--ignore-case`, `-s --skip-chars=N`, `-u --unique`, `-z --zero-terminated`,
`-w --check-chars=N`; and `0`-`9` as hidden short options (one id).

`Help()`: summary `report or omit repeated lines`; usage
`uniq [OPTION]... [INPUT [OUTPUT]]`; notes: `-N` is the obsolete `-f N`,
`+N` (an operand) the obsolete `-s N`; comparison is byte by byte (`-i`:
ASCII case).

Option handling, in the order given:
- digits (GNU's obsolete form): if the last skip-fields setter was `-f`,
  reset the count to 0 first; then `count = count * 10 + digit`
  (saturating). `-f N` sets the count and marks `-f` as last setter. So
  `-1 -2` is 12 fields, `-1 -f 0 -2` is 2 (verified).
- `-f`, `-s`, `-w` values: a non-negative decimal (saturating on overflow);
  otherwise `uniq: x: invalid number of fields to skip` / `...of bytes to
  skip` / `...of bytes to compare` (the value raw, unquoted), exit 1, no
  Try line.
- `--all-repeated`: `ArgMatch` over `none prepend separate` (no value:
  none). `--group`: `ArgMatch` over `separate prepend append both` (no
  value: separate). Failure -> return 1.
- Operands: one starting with `+` followed only by digits is `-s N`
  (`uniq +1` skips one byte; verified); the others are INPUT then OUTPUT; a
  third -> `uniq: extra operand 'c'` (GnuQuote) + Try line, exit 1.
- After the options: `--group` with any of `-c -d -D/--all-repeated -u` ->
  `uniq: --group is mutually exclusive with -c/-d/-D/-u` + Try, 1. `-c` with
  `-D`/`--all-repeated` -> `uniq: printing all duplicated lines and repeat
  counts is meaningless` + Try, 1.

Behaviour:
- Flags as GNU: `printUnique` (true; `-d`, `-D` clear it), `printFirstRepeated`
  (true; `-u` clears it), `printLaterRepeated` (`-D`/`--all-repeated` set it).
- The compared part of a line (GNU `find_field`): skip N fields (each: blanks
  -- space, tab, `\n` -- then non-blanks), then M more bytes (bounded by the
  line), then at most `-w` bytes. Equal: same length and bytes, ASCII
  case-insensitive with `-i`.
- A *group* is a run of adjacent lines whose compared parts are equal; a
  group of one prints iff `printUnique`; a larger group prints its first line
  iff `printFirstRepeated` and its other lines iff `printLaterRepeated`. The
  printed text is the line itself, not its compared part.
- `-c`: the first line of a printed group is prefixed with its size,
  `printf("%7jd ")` -- right-aligned in 7, wider when needed (`     10 a`).
- `--all-repeated=prepend`: an empty line before each printed group;
  `separate`: an empty line between printed groups.
- `--group`: every line is printed; `separate`: an empty line between groups;
  `prepend`: before each group; `append`: after each group; `both`: before
  each group and after the last.
- "Empty line" and every output line end with the delimiter (`\n`, or `\0`
  with `-z`); a last input line without its delimiter is printed with one.
- Streaming: keep only the current group's first line, its size and (for
  `-D`/`--group`) print later lines as they come; decide a group when the
  next differing line or the end arrives.
- Input: INPUT or `-`/none = stdin, through `OpenInputOperand`; failures:
  missing -> `uniq: NAME: No such file or directory` (ShellEscapeQuoted),
  a directory -> `uniq: error reading 'NAME': Is a directory`
  (`ShellEscapeQuoted(name, true)`), denied -> `uniq: NAME: Permission
  denied`; a read error -> `uniq: error reading 'NAME': Input/output error`;
  all exit 1.
- OUTPUT (when given and not `-`): opened right after the input, before
  reading (as GNU: naming the input itself empties it), with
  `kFileOpenWriteCreateTruncate, kFileCreateMode`; a missing directory ->
  `uniq: NAME: No such file or directory`, a directory -> `uniq: NAME: Is a
  directory`; exit 1. Output to it through a local buffer flushed with
  `WriteFully` every 4096 bytes and at the end (a failed write: `uniq: write
  error: Input/output error`, 1); otherwise through `context.Out`.

### `commands/cut/Cut.cpp` (new)

`CreateCutCommand()`; name `cut`, version `1.0.0`, `usageErrorStatus` 1.

Options (all treated): `-b --bytes=LIST`, `-c --characters=LIST`, `-d
--delimiter=DELIM`, `-f --fields=LIST`, `-n` ("(ignored)" -- GNU ignores it
too), `--complement`, `-s --only-delimited`, `--output-delimiter=STRING`,
`-z --zero-terminated`.

`Help()`: summary `remove sections from each line of files`; usage
`cut OPTION... [FILE]...`; notes: the LIST forms (`N`, `N-`, `N-M`, `-M`,
comma or blank separated); `-c` counts bytes, as GNU cut does.

Validation, in GNU's order (each `cut: <message>` + Try line, exit 1):
1. While walking the options: a second `-b`/`-c`/`-f` (any kind, even the
   same) -> `only one list may be specified`; `-d` longer than one byte ->
   `the delimiter must be a single character` (`-d ''` is the byte `\0`).
2. After them: no list -> `you must specify a list of bytes, characters,
   or fields`; `-d` (or `--output-delimiter` with `-d`) without `-f` -> `an
   input delimiter may be specified only when operating on fields`; `-s`
   without `-f` -> `suppressing non-delimited lines makes sense\n\tonly
   when operating on fields` (one `Error` with the embedded `\n\t`).
3. Then the list (`ParseCutList`, below) with field-mode or byte-mode words.

List parsing (`std::optional<std::vector<std::pair<size_t,size_t>>>
ParseCutList(const std::string& list, bool fields, std::string& error)`,
inclusive 1-based ranges, `SIZE_MAX` for an open end). Items separated by
`,` or blanks. Messages, field mode / byte mode:
- an empty item (`''`, `1,,2`, `1-2,`), or a 0 anywhere a start is
  (`0`, `0-2`): `fields are numbered from 1` / `byte/character positions
  are numbered from 1`;
- `-` alone: `invalid range with no endpoint: -`;
- `N-M` with M < N (`3-1`, `2-0`, `-0`): `invalid decreasing range`;
- a second `-` in an item (`1-2-3`, `--2`): `invalid field range` /
  `invalid byte or character range`;
- a byte that is not a digit/`-`: `invalid field value 'x'` / `invalid
  byte/character position 'x'` -- quoted (GnuQuote) is the rest of the item
  from the first bad byte when it follows digits (`1x` -> `'x'`, `1-x` ->
  `'x'`), else the whole item (`x-1` -> `'x-1'`);
- a number too large for `size_t`: `field number '9999...' is too large`
  / `byte/character offset '9999...' is too large`.
Then sort the ranges by start and merge **overlapping** ones only (`1-2,2-4`
becomes `1-4`; adjacent `1-2,3-4` stay two ranges -- this decides where
`--output-delimiter` goes: verified, `echo abcdef | cut -b 1-2,3-4
--output-delimiter=X` prints `abXcd`). `--complement` replaces the set by
its complement over 1..SIZE_MAX (as ranges).

Byte mode (`-b`, `-c`): for each line, output the selected bytes in order;
with `--output-delimiter`, write it before the first byte of each range
after the first range that printed anything. Then the line delimiter.
Field mode (`-f`): delimiter `-d` (default TAB); a line with no delimiter
byte is printed whole (with `-s`: dropped); else split on it and print the
selected fields joined by the output delimiter (default: the input
delimiter); fields past the end are simply absent (`printf 'a:b' | cut -d:
-f3` prints an empty line). `--output-delimiter=''` writes a `\0` byte
(GNU 9.4; verify with `| od -c`). Each output line ends with the line
delimiter (`\n`, `\0` with `-z`), also for a last line that had none.
Stream line by line with `BuiltinLineReader`.

Inputs: operands in order (`-` or none = stdin) through
`OpenInputOperand`; missing -> `cut: NAME: No such file or directory`,
directory -> `cut: NAME: Is a directory`, denied -> `cut: NAME: Permission
denied`, read error -> `cut: NAME: Input/output error` (NAME
ShellEscapeQuoted; stdin `-`); each reported, the rest still processed, exit
1 at the end.

### Registration and build

`BuiltinCommandList.h`: declare `CreateCutCommand()`, `CreateUniqCommand()`
and add both to `CreateStandardBuiltinCommands()` in sorted position (the
list is alphabetical: `CreateCutCommand()` between `CreateCpCommand()` and
`CreateDirnameCommand()`, `CreateUniqCommand()` between `CreateTrueCommand()`
and `CreateWcCommand()`; head-tail etc. may have added more by then).
`src/components/BuiltinCommands/CMakeLists.txt` (not the root one): add
`commands/cut/Cut.cpp` after `commands/cp/Cp.cpp` and `commands/uniq/Uniq.cpp`
after `commands/true/True.cpp`.

## Tests

New `UniqTest.cpp` and `CutTest.cpp` in
`tests/unit/components/BuiltinCommands.unittests/` (both added to the
`add_executable` list in its `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, ...)`. Expected outputs are
GNU 9.4's with `LC_ALL=C`; for any case not written out, run it in the
container and paste the result. With `G` = `a\na\nb\nc\nc\nc\nd` (no final
newline):

- `UniqDefault`: `G` -> `a\nb\nc\nd\n`.
- `UniqCount`: `-c G` -> `      2 a\n      1 b\n      3 c\n      1 d\n`; ten `a` lines -> `     10 a\n`.
- `UniqRepeatedAndUnique`: `-d` -> `a\nc\n`; `-D` -> `a\na\nc\nc\nc\n`; `-u` -> `b\nd\n`; `-cd` -> `      2 a\n      3 c\n`; `-du` -> nothing; `-D -u` on `a\na\nb\nb\nc\n` -> `a\nb\n`.
- `UniqAllRepeatedMethods`: `--all-repeated=separate` -> `a\na\n\nc\nc\nc\n`; `=prepend` -> `\na\na\n\nc\nc\nc\n`; `--all-repeated=se` (prefix) works.
- `UniqGroupMethods`: `--group` -> `a\na\n\nb\n\nc\nc\nc\n\nd\n`; `=prepend` -> `\na\na\n\nb\n\nc\nc\nc\n\nd\n`; `=append` -> `a\na\n\nb\n\nc\nc\nc\n\nd\n\n`; `=both` -> `\na\na\n\nb\n\nc\nc\nc\n\nd\n\n`.
- `UniqSkipAndCheck`: `-f1 -c` on `x a\ny a\nz b\n` -> `      2 x a\n      1 z b\n`; `-s1` on `xa\nya\nzb\n` -> `xa\nzb\n`; `-w2` on `ab1\nab2\nac\n` -> `ab1\nac\n`; `-i` on `A\na\nb\n` -> `A\nb\n`.
- `UniqObsoleteForms`: with `L` = `a b c d e f g h i j k l 1\nx y z w v u t s r q p o 1\n`: `-12` -> first line only; `-1 -2` -> the same; `-1 -f 0 -2` -> both lines; `+1` on `ab\ncb\n` -> `ab\n`.
- `UniqZeroTerminated`: `-z` on `a\0a\0b` -> `a\0b\0`.
- `UniqOutputFile`: `uniq in out` in `/w` -> stdout empty, `/w/out` holds the result.
- `UniqErrors`: `--group -c` -> `uniq: --group is mutually exclusive with -c/-d/-D/-u\nTry 'uniq --help' for more information.\n`, 1; `-D -c` -> `uniq: printing all duplicated lines and repeat counts is meaningless\n` + Try, 1; `--all-repeated=foo` -> `uniq: invalid argument 'foo' for '--all-repeated'\nValid arguments are:\n  - 'none'\n  - 'prepend'\n  - 'separate'\nTry 'uniq --help' for more information.\n`, 1; `-f x` -> `uniq: x: invalid number of fields to skip\n`, 1; `-s -1` -> `uniq: -1: invalid number of bytes to skip\n`; `-w x` -> `uniq: x: invalid number of bytes to compare\n`; `a b c` -> `uniq: extra operand 'c'\n` + Try, 1; `nofile` -> `uniq: nofile: No such file or directory\n`, 1; `/docs` -> `uniq: error reading '/docs': Is a directory\n`, 1; `in /nodir/out` -> `uniq: /nodir/out: No such file or directory\n`, 1.
- With `C` = `a:b:c\nnodelim\n:x:\nabcdef\n`:
- `CutFields`: `-d: -f2` -> `b\nnodelim\nx\nabcdef\n`; `-s` too -> `b\nx\n`; `-f1,3 --output-delimiter=--` -> `a--c\nnodelim\n--\nabcdef\n`; `-f2 --complement` -> `a:c\nnodelim\n:\nabcdef\n`; `-f 1-2,2-3 --output-delimiter=X` -> `aXbXc\nnodelim\nXxX\nabcdef\n`; `-f '1 3'` and `-f 3,1` -> `a:c\nnodelim\n:\nabcdef\n`; `printf 'a\tb\n' | cut -f2` -> `b\n`; `printf 'a:b' | cut -d: -f3` -> `\n`.
- `CutBytes`: `-b 2-3,5-` -> `:bc\nodlim\nx:\nbcef\n`; `-b -2` -> `a:\nno\n:x\nab\n`; `-b 1,3 --output-delimiter=X` -> `aXb\nnXd\n:X:\naXc\n`; `-b 1-2,4-5 --output-delimiter=X` -> `a:X:c\nnoXel\n:x\nabXde\n`; `-b 1-2,2-4 --output-delimiter=X` -> `a:b:\nnode\n:x:\nabcd\n`; `echo abcdef | cut -b 2,4 --complement --output-delimiter=X` -> `aXcXef\n`; `-c1` on `\xC3\xA9\n` -> `\xC3\n` (bytes); `-n -b1` works.
- `CutZeroTerminated`: `-z -f1` on `C` -> `C` with its final `\n` kept and a `\0` added.
- `CutUsageErrors`: each message of "Validation" and of the list parser above, byte for byte, with `Try 'cut --help' for more information.\n`, exit 1 (e.g. `cut C.txt` -> `cut: you must specify a list of bytes, characters, or fields\n...`; `-s -b1` -> `cut: suppressing non-delimited lines makes sense\n\tonly when operating on fields\n...`; `-f0`, `-f 3-1`, `-f x`, `-f ''`, `-f 1-2-3`, `-f -`, `-f 1x`, `-f x-1`, `-b x`, `-c 0`, `-b 1-2-3`, `-f 99999999999999999999`, `-b 1 -d:`, `-f1 -b1`, `-d ab -f1`).
- `CutFileErrors`: `cut -b1 /docs nofile c.txt` -> stderr `cut: /docs: Is a directory\ncut: nofile: No such file or directory\n`, stdout c.txt's result, exit 1.

Generic tests: `ListsEveryBuiltinSortedWithAVersion` -- insert `"cut"` (after
`"cp"`, before `"dirname"`) and `"uniq"` (after `"true"`, before `"wc"`) into the
list as it is on develop (at 90728a3: `"[", "basename", "cat", "chmod", "cp",
"dirname", "echo", "env", "false", "hsh", "ls", "man", "mkdir", "printf", "pwd",
"realpath", "rm", "rmdir", "seq", "sleep", "sort", "test", "true", "wc", "which"`);
`EveryBuiltinsHelpHasTheSameShape` -- skip `hidden` options (above).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Uniq*:BuiltinCommandsTest.Cut*:BuiltinCommandsTest.Every*'
bash ./scripts/test_linux.sh L U
```
(The script's filter must match the executable's name; the direct run narrows.)

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `uniq` and `cut` in the
  opening list; rows in "The commands" (both 1.0.0; uniq: every option,
  `-N`/`+N` obsolete forms, ASCII `-i`; cut: every option, `-c` = bytes as
  GNU); a sentence on `BuiltinOption::hidden` where options are described.
- Root `CLAUDE.md`: both in the Builtin Commands sentence and table
  (`uniq`: `Filters adjacent repeated lines (-c -d -D -u -i -f -s -w -z, --group, --all-repeated)`;
  `cut`: `Selects bytes or fields of each line (-b -c -f -d -s, --complement, --output-delimiter, -z)`);
  the `BuiltinCommands/` directory-tree line.

## Acceptance

- [ ] Both registered; `--help` standard shape (no hidden digits shown);
      `--version`; `man uniq`/`man cut` print the help.
- [ ] Every GNU 9.4 option of each accepted; output and messages byte-exact
      on every test, exit codes 0/1 as GNU.
- [ ] uniq: grouping methods, `-c` format, obsolete `-N`/`+N`, OUTPUT operand.
- [ ] cut: list parsing and every message; overlapping-only merge;
      output delimiter placement; `-s`; `--complement`; `-z`.
- [ ] Streaming line by line; stop honoured; all file access via `context.IO()`.
- [ ] Builds on Linux; all unit tests pass.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- Multibyte characters (GNU's `cut -c` is byte-based too; uniq compares bytes).
- sort, tr, tee, nl (their own tasks).
