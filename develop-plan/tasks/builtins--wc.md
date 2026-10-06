# Task builtins--wc: The wc builtin, GNU wc byte for byte

- Rock: builtins
- Depends on: builtins--directories, builtins--unicode
- Size: ~620 changed lines in ~9 files (about half of it tests)
- Plan checked against: develop @ 0d92271
- PR title: Add the wc builtin, GNU wc byte for byte

## Goal

A new builtin `wc` prints newline, word, character, byte and maximum-line-width
counts exactly as GNU coreutils `wc` (9.4) prints them, from files and from
standard input: same columns and widths, same messages, same exit status.
`cat /abc.txt | wc -l` prints `3`; `wc -c /out.txt` prints `14 /out.txt`.
`haisos --init` lists it (`# BUILTIN rootfs wc /bin/wc`) automatically.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md` ("Adding a builtin"),
`src/components/BuiltinCommands/BuiltinCommand.h`, and
`src/components/BuiltinCommands/commands/cat/Cat.cpp` (the closest model:
files and stdin, read in chunks).

What earlier tasks provide (as if on develop):

- builtins--directories: the `commands/<name>/<Name>.cpp` layout;
  `BuiltinContext::ErrorText(text)` (stderr, as given) and `Error(message)`
  (stderr, `wc: ` prepended, newline added); `ShellEscapeQuoted(name, always)`
  -- GNU's `quotef` (`always` false) and `quoteaf` (`always` true); the test
  fixture header `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h`
  with `RunCaptured(command, args, input, workingDirectory) -> Captured{out, err, status}`.
- fd--process-table / streams--console-and-start: stdin is
  `context.IO().GetDescriptor(IFileIO::kStdIn)`; `IFileIO::OpenFile(path, kFileOpenReadOnly)`
  returns a `std::shared_ptr<IFileDescriptor>` (null on failure);
  `Read` returns bytes, 0 at end, negative on failure (`kIOInterrupted` when
  the process is asked to stop). `IFileIO::Stat(path, FileStatus&)` gives
  `type` (`DirectoryEntryType::File`, `Dir`, `CharDevice`) and `size`.

- builtins--unicode: the library `Unicode` (`src/components/Unicode/`,
  `#include "src/components/Unicode/Unicode.h"`, namespace `Haisos::Unicode`):
  `enum class DecodeStatus { Ok, Invalid, Incomplete }`;
  `struct DecodedChar { DecodeStatus status; char32_t codePoint; size_t length; }`
  (`length`: the sequence's for Ok, 1 for Invalid, 0 for Incomplete);
  `DecodedChar DecodeUtf8(const char* data, size_t size)` (size >= 1; strict
  UTF-8, an invalid byte reported alone, as glibc's `mbrtowc` returning -1);
  `bool IsPrintable(char32_t)`, `bool IsSpace(char32_t)`,
  `bool IsNoBreakSpace(char32_t)` (U+00A0, U+2007, U+202F, U+2060),
  `int DisplayWidth(char32_t)` (-1, 0, 1, 2). Read
  `src/components/Unicode/CLAUDE.md`. `BuiltinCommands` does not link it yet:
  this task adds the link.

The reference is GNU coreutils 9.4 `src/wc.c`; every output below was checked
against GNU wc 9.4 in a `C.UTF-8` locale (messages as in the `C` locale:
ASCII quotes, as the other builtins print them).

## Changes

### New `src/components/BuiltinCommands/commands/wc/Wc.cpp`

`class WcCommand : public IBuiltinCommand` in an anonymous namespace, and
`std::shared_ptr<IBuiltinCommand> CreateWcCommand()`. `Name()` `"wc"`,
`Version()` `"1.0.0"`. Usage error status 1 (`BeginBuiltin(context, *this, 1, status)`).

Option table (every GNU wc option; all treated except `--debug`):

| short | long | argument | treated | description (help) |
|-------|------|----------|---------|--------------------|
| `c` | `bytes` | none | yes | `print the byte counts` |
| `m` | `chars` | none | yes | `print the character counts` |
| `l` | `lines` | none | yes | `print the newline counts` |
| `L` | `max-line-length` | none | yes | `print the maximum display width` |
| `w` | `words` | none | yes | `print the word counts` |
| -- | `files0-from` | Required `F` | yes | `read NUL-separated names from F (- for stdin)` |
| -- | `total` | Required `WHEN` | yes | `auto, always, only, never` |
| -- | `debug` | none | no (GNU's hidden option) | -- |

`Help()`: summary `print newline, word, and byte counts for each file`;
usage `wc [OPTION]... [FILE]...` and `wc [OPTION]... --files0-from=F`;
notes (the documented exceptions, two lines):
```
Standard input has no file size here, so with it the columns are at least 7 wide.
-m -w -L use compact Unicode tables: unassigned characters count as printable, rare scripts' widths are approximate.
```

#### Options

- No count option given: lines, words, bytes. Printed order is always
  lines, words, chars, bytes, max-line-length, whatever order they were given in.
- `--total=WHEN`: `WHEN` matched like GNU's `argmatch`: an exact value, or
  an unambiguous prefix of one of `auto always only never` (`al` = always,
  `o` = only). Otherwise, on stderr, exit 1, nothing else done:
  ```
  wc: invalid argument 'x' for '--total'
  Valid arguments are:
    - 'auto'
    - 'always'
    - 'only'
    - 'never'
  Try 'wc --help' for more information.
  ```
  with `ambiguous argument` instead of `invalid argument` when the value is a
  prefix of two or more (`a`, and the empty value). The value is quoted
  `'...'` as written. The first line through `Error`, the rest through
  `ErrorText`/`TryHelp`.
- `POSIXLY_CORRECT`: when the process environment
  (`context.Process().GetEnvironment()->GetVariable("POSIXLY_CORRECT")`) has
  it, the no-break spaces below are not word separators.

#### The inputs

1. With `--files0-from=F`:
   - Operands given too: stderr `wc: extra operand '<first operand, quoteaf>'`,
     then `file operands cannot be combined with --files0-from`, then
     `Try 'wc --help' for more information.`; exit 1.
   - `F` is `-`: the names are read from stdin (all of it, then processed).
     Otherwise `F` is opened as a file; missing or unopenable:
     `wc: cannot open '<F, quoteaf>' for reading: No such file or directory`
     (`Permission denied` if it exists but cannot be opened), exit 1,
     nothing else printed. `F` a directory: `wc: <F, quotef>: read error: Is a directory`,
     exit 1, nothing else printed. A read failure of `F` or stdin:
     `wc: <F, quotef>: read error: Input/output error`, exit 1, after
     whatever was printed.
   - Splitting: every NUL ends a name (an empty name if two NULs meet); bytes
     after the last NUL form one more name only if there are any. No names:
     nothing at all is printed, exit 0.
   - A name `-` reads stdin -- except when `F` is `-`:
     `wc: when reading file names from stdin, no file name of '-' allowed`,
     that name skipped, exit status 1.
   - An empty name: `wc: <F, quotef>:<N>: invalid zero-length file name`
     (N the 1-based number of the name), skipped, exit status 1.
2. Otherwise each operand is a name (an empty operand:
   `wc: invalid zero-length file name`, skipped, exit 1). No operands: stdin,
   printed **without** a name.

A **name counted** for the total rule is every name given (operand or from
F), whether it failed or not; reading stdin for lack of operands counts none.

#### Column width (GNU's `get_input_fstatus` + `compute_number_width`)

Computed once, before reading anything:

- `--total=only`: width 1.
- Names from `--files0-from=-` (stdin): width 1 (GNU reads them as a stream
  and does not know them in advance).
- Exactly one input and exactly one count printed: width 1 (no stat at all).
- Otherwise, over every input (in Haisos stdin -- no operand, or `-` --
  counts as "not a regular file", the documented exception; GNU would use the
  size of a redirected file): for a name, `IO().Stat`; an empty name or a
  failed `Stat` is skipped; a `File` adds its `size` to a total; a `Dir` or
  `CharDevice` (or stdin) sets a minimum of 7. Width = number of decimal
  digits of the total (1 for 0-9), raised to the minimum if lower.
- A count wider than the width is printed whole (no truncation).

#### Output line (GNU's `write_counts`)

Each selected count right-aligned in `width` columns, joined by one space,
in the order lines, words, chars, bytes, max-line-length; then, if there is a
name, one space and the name -- as given, unquoted, unless it holds a
newline: then `ShellEscapeQuoted(name)` (`'q'$'\n''r'`); then `\n`.

#### Per input (GNU's `wc_file`)

- Name `-` or no name: stdin. The name shown is `-`, or none.
- A name: `Stat` fails -> `wc: <quotef name>: No such file or directory`, no
  counts line, status 1. A directory -> `wc: <quotef name>: Is a directory`
  **and** a counts line of zeros for it (GNU opens it, the read fails, the
  counts are printed), status 1. `OpenFile` fails otherwise ->
  `wc: <quotef name>: Permission denied`, no line, status 1.
- Read in chunks (16 KiB); check `context.StopRequested()` between chunks
  (stop quietly, status 1); `kIOInterrupted` likewise; another negative
  result -> `wc: <quotef name>: Input/output error` (for stdin without a name
  the name is `standard input`, so the message is
  `wc: 'standard input': Input/output error`), then the counts so far are
  still printed, status 1.
- Unless `--total=only`, the counts line. All counts add into the totals; the
  total max-line-length is the largest of the inputs'.

#### Total line

Printed after everything when `--total=always`, or `--total=only`, or
`--total=auto` (the default) with **more than one** name counted. With
`only` it has no name (`3 5 25`); otherwise its name is `total`.

#### Counting (GNU's multibyte loop; Haisos is UTF-8 throughout)

Decoding and character classes come from the `Unicode` component
(builtins--unicode); `Wc.cpp` holds no Unicode table or decoder of its own.

State per input: `lines words chars bytes linepos linelength inWord`, plus a
carry buffer of at most 3 bytes. Every byte read adds to `bytes`. The bytes
(the carry first, then the new chunk) are decoded with
`Unicode::DecodeUtf8(data, size)`:

- `DecodeStatus::Invalid` (always `length` 1): the byte counts as a byte only,
  changes nothing else; decoding goes on at the next byte (so `C0 80` is two
  skipped bytes, `ED A0 80` three).
- `DecodeStatus::Incomplete` (`length` 0): the remaining bytes go to the carry
  for the next chunk; at the very end of the input they count as bytes only.
- `DecodeStatus::Ok`: one character `c` of `length` bytes. It (NUL and
  controls included) adds 1 to `chars`, then:
  - `\n`: `lines++`, then as `\r`.
  - `\r`, `\f`: `linelength = max(linelength, linepos)`, `linepos = 0`; separator.
  - `\t`: `linepos += 8 - linepos % 8`; separator.
  - space (U+0020): `linepos++`; separator.
  - `\v`: separator.
  - anything else, if `Unicode::IsPrintable(c)`: `linepos +=
    Unicode::DisplayWidth(c)`; then a separator if `Unicode::IsSpace(c)` or
    (unless `POSIXLY_CORRECT`) `Unicode::IsNoBreakSpace(c)`, else
    `inWord = true`. Not printable: nothing.
  - separator: `words += inWord; inWord = false`.
- At the end: `linelength = max(linelength, linepos)`, `words += inWord`.

Worked counting examples (`printf` input -> `wc -lwmcL` columns lines, words,
chars, bytes, width), all from GNU wc 9.4:

| input | l w m c L |
|-------|-----------|
| `hello world\n` | 1 2 12 12 11 |
| `a\tb\n` | 1 2 4 4 9 |
| `abcdefgh\tx\n` | 1 2 11 11 17 |
| `abc\rde\n` | 1 2 7 7 3 |
| `ab\vcd` | 0 2 5 5 4 |
| `a\001b` | 0 1 3 3 2 |
| `\001\002` | 0 0 2 2 0 |
| `a\000b` | 0 1 3 3 2 |
| `caf\303\251\n` (café) | 1 1 5 6 4 |
| `e\314\201x\n` (e + U+0301) | 1 1 4 5 2 |
| `\344\270\255\346\226\207\n` (中文) | 1 1 3 7 4 |
| `\360\237\230\200\n` (U+1F600) | 1 1 2 5 2 |
| `a\377b\n` | 1 1 3 4 2 |
| `ab\342\202` (cut short at end) | 0 1 2 4 2 |
| `a\302\240b` (U+00A0) | 0 2 3 4 3 |
| `a\342\201\240b` (U+2060) | 0 2 3 5 2 |
| `a\343\200\200b` (U+3000) | 0 2 3 5 4 |
| `a\302\205b` (U+0085) | 0 1 3 4 2 |
| `a\300\200b` (overlong) | 0 1 2 4 2 |

With `POSIXLY_CORRECT` set, `a\302\240b` gives 1 word.

#### Worked output examples (GNU wc 9.4)

Files: `abc.txt` = `one\ntwo\nthree\n` (14 bytes), `h.txt` = `hello world`
(11 bytes, no newline), `d` a directory, `nope` missing; `|` shows stdin
from a pipe. `$` marks the end of a line.

```
wc abc.txt                     -> " 3  3 14 abc.txt$"
wc abc.txt h.txt               -> " 3  3 14 abc.txt$" " 0  2 11 h.txt$" " 3  5 25 total$"
wc -l abc.txt                  -> "3 abc.txt$"
wc -c abc.txt                  -> "14 abc.txt$"
| wc                           -> "      3       3      14$"
| wc -l                        -> "3$"
| wc -lw                       -> "      3       3$"
| wc - abc.txt                 -> "      3       3      14 -$" "      3       3      14 abc.txt$" "      6       6      28 total$"
| wc --total=always            -> "      3       3      14$" "      3       3      14 total$"
wc nope                        -> err "wc: nope: No such file or directory$", exit 1, no out
wc nope abc.txt                -> " 3  3 14 abc.txt$" " 3  3 14 total$", err as above, exit 1
wc d                           -> "      0       0       0 d$", err "wc: d: Is a directory$", exit 1
wc -l d                        -> "0 d$", same err, exit 1
wc d abc.txt                   -> "      0       0       0 d$" "      3       3      14 abc.txt$" "      3       3      14 total$", exit 1
wc 'a b' nope2  ('a b' = "x\n")-> "1 1 2 a b$" "1 1 2 total$", err "wc: nope2: No such file or directory$"
wc "it's"   (missing)          -> err "wc: \"it's\": No such file or directory$"
wc ''                          -> err "wc: invalid zero-length file name$", exit 1
wc -L abc.txt h.txt            -> " 5 abc.txt$" "11 h.txt$" "11 total$"
wc --total=only abc.txt h.txt  -> "3 5 25$"
wc -l --total=only abc.txt h.txt -> "3$"
wc --total=never abc.txt h.txt -> " 3  3 14 abc.txt$" " 0  2 11 h.txt$"
wc --total=always abc.txt      -> " 3  3 14 abc.txt$" " 3  3 14 total$"
wc -clmwL abc.txt              -> " 3  3 14 14  5 abc.txt$"
wc --files0-from=l0  (l0 = "abc.txt\0h.txt\0") -> as `wc abc.txt h.txt`
wc --files0-from=l1  (l1 = "abc.txt\0\0h.txt") -> same lines, err "wc: l1:2: invalid zero-length file name$", exit 1
wc --files0-from=l2  (l2 = "abc.txt")          -> " 3  3 14 abc.txt$"
wc --files0-from=l3  (l3 empty)                -> nothing, exit 0
wc --files0-from=l4  (l4 = "abc.txt\0nope\0d\0") -> "      3       3      14 abc.txt$" "      0       0       0 d$" "      3       3      14 total$", errs for nope and d, exit 1
| wc --files0-from=-   (stdin = l0)  -> "3 3 14 abc.txt$" "0 2 11 h.txt$" "3 5 25 total$"
| wc --files0-from=-   (stdin = "-\0abc.txt\0") -> "3 3 14 abc.txt$" "3 3 14 total$", err "wc: when reading file names from stdin, no file name of '-' allowed$", exit 1
wc --files0-from=nofile        -> err "wc: cannot open 'nofile' for reading: No such file or directory$", exit 1
wc --files0-from=abc.txt x     -> err "wc: extra operand 'x'$" "file operands cannot be combined with --files0-from$" "Try 'wc --help' for more information.$", exit 1
wc --files0-from=d             -> err "wc: d: read error: Is a directory$", exit 1
wc -x                          -> err "wc: invalid option -- 'x'$" "Try 'wc --help' for more information.$", exit 1
```

Note on `wc nope abc.txt`: a failed `Stat` is skipped for the width, so the
width comes from `abc.txt` alone (2). On `wc 'a b' nope2`: 2 bytes -> width 1.

### `src/components/BuiltinCommands/BuiltinCommandList.h`

Declare `std::shared_ptr<IBuiltinCommand> CreateWcCommand();` and add
`CreateWcCommand()` to `CreateStandardBuiltinCommands()` (keep the list in
name order: cat, echo, ls, mkdir, pwd, wc). That is what makes it runnable,
listed by `GetCommands()`, and written into the `haisos --init` template
(rule 9 -- never hand-write a `BUILTIN` line into `GetHaisosFileTemplate`).

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `commands/wc/Wc.cpp`, and `Unicode` to `target_link_libraries(BuiltinCommands PUBLIC ...)`.

### Root `CLAUDE.md` rules that bite

- Output identical to GNU wc's; every GNU option in the table; `--help`
  generated from it, its notes stating the two exceptions; `--version`
  `wc (HaisosOS builtin) 1.0.0`.
- Rule 9: registered in `CreateStandardBuiltinCommands()`; added to the root
  "Builtin Commands" table.
- Files and stdin reached only through `context.IO()`, the environment only
  through `context.Process()`.

## Tests

### `tests/unit/components/BuiltinCommands.unittests/WcTest.cpp` (new)

Add it to `add_executable(BuiltinCommands.unittests ...)` in that
directory's `CMakeLists.txt`. `#include "BuiltinCommandsFixture.h"`; every
test is `TEST_F(BuiltinCommandsTest, Wc...)`. Each test creates the files it
needs with `WriteFile` (`/w/abc.txt`, `/w/h.txt`, `/w/a b`, directory `/w/d`,
NUL lists `/w/l0`..`/w/l4` -- strings with embedded NULs built with
`std::string(ptr, len)`), runs with `RunCaptured(..., workingDirectory "/w")`,
and compares `out` and `err` **exactly** (whole strings) and `status`.

- `WcDefaultCountsOneFile`, `WcSeveralFilesPrintATotal`, `WcSingleCountOfOneFileIsUnpadded`
  (`-l`, `-c`), `WcStdinFromAPipeIsSevenWide` (input `one\ntwo\nthree\n`:
  no options, `-l`, `-lw`), `WcDashAmongFiles`, `WcTotalAlwaysOnStdin`.
- `WcMissingFileIsReportedAndOthersStillCounted` (`nope`, `nope abc.txt`),
  `WcDirectoryPrintsZerosAndAnError` (`d`, `-l d`, `d abc.txt`),
  `WcNameWithASpaceIsQuotedOnlyInErrors` (`'a b' nope2`, missing `it's`),
  `WcNameWithANewlineIsQuoted` (a file `q\nr` holding `x\n`;
  `wc "q\nr" abc.txt` -> out
  `" 1  1  2 'q'$'\n''r'\n 3  3 14 abc.txt\n 4  4 16 total\n"`, checked
  against GNU wc 9.4),
  `WcZeroLengthName`.
- `WcTotalModes` (always/only/never, `-l --total=only`, prefix `--total=al`,
  `--tot=o`), `WcTotalBadValue` (`x` invalid, `a` ambiguous: full stderr text).
- `WcOrderOfCountsIsFixed` (`-clmwL` and `-Lc` print in the fixed order),
  `WcMaxLineLength` (`-L abc.txt h.txt`; input `abcdefgh\tx\n` -> `17`;
  `abc\rde\n` -> `3`).
- `WcCountsUtf8` -- the counting table above, one assertion per row on
  `RunCaptured("wc", {"-lwmcL"}, input).out` (7-wide columns, e.g.
  `"      1       2      12      12      11\n"`); `WcPosixlyCorrectKeepsNoBreakSpaceInWords`
  (set `POSIXLY_CORRECT` in the environment the process is started with --
  give `RunCaptured` an optional environment parameter, or start that one
  process by hand).
- `WcFilesFromAFile` (`l0`, `l1`, `l2`, `l3`, `l4`), `WcFilesFromStdin`
  (`--files0-from=-` with `l0`'s bytes as input; with `-\0abc.txt\0`),
  `WcFilesFromErrors` (`nofile`, extra operand, a directory).
- `WcUsageErrors` (`-x`, `--frob`, `--files0-from` without a value: GNU's
  `wc: option '--files0-from' requires an argument`), `WcDebugIsNotTreated`.
- `WcReadsTheBuiltinNoteOfAPath`: `wc -c /bin/ls` = size of
  `BuiltinCommandFileContent("ls")` followed by ` /bin/ls`.

Also update `ListsEveryBuiltinSortedWithAVersion` in
`BuiltinCommandsTest.cpp` (the names now include `wc`). The generic tests
(`EveryBuiltinsHelpHasTheSameShape`, `EveryBuiltinHasAVersion`,
`EveryUntreatedOptionIsAcceptedAndReported`, `EveryBuiltinsManPageIsItsHelp`)
cover `wc` with no change.

### `tests/haisos/builtins.haisostest/builtins.haisostest.js`

Add `BUILTIN rootfs wc /bin/wc` to the generated haisosfile and a check:
`RUN /bin/wc /notes/hello.txt` prints `" 0  2 15 /notes/hello.txt"`
(`Hello, builtins`: 15 bytes, 2 words, no newline).

### `tests/unit/haisos/haisos.unittests/HaisosFileOperationsTest.cpp`

Nothing to change: `TheInitTemplatesBuiltinsAllApplyOnceUncommented` must
pass with `wc` in the template.

### Commands

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/CliParser.unittests --gtest_filter='*TheInitTemplates*'
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H builtins
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: first paragraph lists `wc`;
  table row `wc` | 1.0.0 | `-c -m -l -L -w`, `--files0-from`, `--total`,
  stdin with no FILE or `-` | stdin has no size, so with it columns are at
  least 7 wide (GNU sizes a redirected file); character classes and widths
  from the `Unicode` component's compact tables (unassigned code points
  printable; rare scripts' widths approximate); `--debug` not treated.
- Root `CLAUDE.md`: "Builtin Commands" table row `wc` -- "Counts lines,
  words, characters, bytes and the widest line (`-c -m -l -L -w`,
  `--files0-from`, `--total`), GNU's columns"; the Directory Structure line
  and the intro sentence listing builtins include `wc`.
- `interfaces/IBuiltinCommands.h`: the class comment listing the builtins
  ("echo, cat, ls, pwd, mkdir") mentions wc -- or better, says "the commands
  in `CreateStandardBuiltinCommands()`" so it never goes stale.

## Acceptance

- [ ] `BuiltinCommands` links `Unicode`.
- [ ] `Wc.cpp` holds no decoder and no Unicode table; it calls `Unicode::DecodeUtf8`, `IsPrintable`, `IsSpace`, `IsNoBreakSpace`, `DisplayWidth`.
- [ ] `commands/wc/Wc.cpp` exists, registered in `CreateStandardBuiltinCommands()`, listed in `CMakeLists.txt`.
- [ ] Every worked output example above holds byte for byte (they are the unit tests).
- [ ] Every row of the counting table holds.
- [ ] Width: 1 for one count of one input and for `--total=only`; from regular file sizes; at least 7 with stdin, a directory or a device.
- [ ] Errors go to stderr only, with GNU's wording and quoting; exit 1 on any failure, the other inputs and the total still printed.
- [ ] `--help` has the standard shape with the two exception notes; `--debug` is in the "Not treated arguments" line.
- [ ] `haisos --init` output contains `# BUILTIN rootfs wc /bin/wc` (via the registry only).
- [ ] Both Builtin Commands tables (root and component `CLAUDE.md`) have wc.
- [ ] Linux build passes; all unit tests and the haisos tests pass.

## Out of scope

- `man` (builtins--man); hsh.
- Changing the `Unicode` component (builtins--unicode); using it anywhere
  but `wc`.
- Locales other than UTF-8; GNU's lseek shortcut for `-c` (the result
  is the same).
- Statting stdin (no `IFileDescriptor` status in Haisos).
