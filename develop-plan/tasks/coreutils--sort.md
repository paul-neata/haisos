# Task coreutils--sort: the sort builtin -- keys, text and numeric order

- Rock: coreutils
- Depends on: none
- Size: ~950 changed lines in ~10 files
- Plan checked against: develop @ f1e1dc9
- PR title: Add the sort builtin: keys, -n, -u, -s, -o, -z

## Goal

`sort` is a builtin command, placed with `BUILTIN rootfs sort /bin/sort`,
behaving as GNU coreutils 9.4's `sort` (Ubuntu 24.04, the task container's)
run with `LC_ALL=C`: lines compared byte by byte, keys (`-k`, `-t`, `-b`), the
text modifiers `-d -f -i`, numeric order `-n`, `-r`, `-u`, `-s`, GNU's
last-resort comparison, `-o` (which may name one of the inputs), `-z`,
`--files0-from`, and GNU's messages and exit codes byte for byte. So
`grep -rn TODO src | sort -t: -k1,1 -k2,2n` and `... | sort -rn | head -3`
print what they print on Linux.

This task also writes, once, three helpers that the later text-filter tasks
(`coreutils--sort-orders`, `coreutils--uniq-cut`, `coreutils--tr-tee-nl`)
use: `GnuQuote` (GNU's `quote()`), `ArgMatch` (GNU's `XARGMATCH`
diagnostics) and `BuiltinLineReader` with `OpenInputOperand` (reading
lines of a file operand or of standard input).

The other orderings (`-g -h -M -R -V`, `--sort=WORD`) and `-c`/`-C`/`--check`
and `-m` are the next task, `coreutils--sort-orders`: here they are listed
in the option table as not treated (reported, ignored), and the key
modifiers `g h M R V` are parsed into the key but compared as plain text.

## Context

Read first: the root `CLAUDE.md` (sections "Security", "Builtin Commands",
"Automatic Development Rules" rule 9), `src/components/BuiltinCommands/CLAUDE.md`,
and the code of `commands/wc/Wc.cpp` (the model: option table, `Help()`,
`Run()` on `BeginBuiltin`, `--files0-from`, reading a descriptor with the
stop and error handling, `MatchTotalValue` -- GNU's argmatch done by hand)
and `commands/cat/Cat.cpp`.

What exists (by exact name):
- `src/components/BuiltinCommands/BuiltinCommand.h`: `IBuiltinCommand`
  (`Name`, `Version`, `Options`, `Help`, `Run`), `BuiltinOption`
  (`shortName, longName, id, argument, argumentName, description`),
  `BuiltinArgument::{None, Required, Optional, OptionalAttached}`
  (`Optional`: a short option never takes the argument, a long one only as
  `--x=value`), `kBuiltinNotTreated`, `ParsedBuiltinArgs`, `BeginBuiltin(context,
  command, usageErrorStatus, exitStatus)`, `BuiltinContext` (`Out` --
  buffered off a terminal --, `Error` = `"<name>: " + message + "\n"`,
  `ErrorText` = raw stderr bytes, `TryHelp`, `StopRequested`, `IO()`,
  `Process()`, `Flush`), `ShellEscapeQuoted(name, always)` (GNU's
  `quotef`/`quoteaf`), `BuiltinHelpText`.
- `src/components/Filesystem/FilesystemUtils.h`: `kFileOpenReadOnly`,
  `kFileOpenWriteCreateTruncate`, `kFileOpenWriteCreateAppend`,
  `kFileCreateMode`, `EntryTypeOf`. Its `ReadWholeDescriptor` caps at 10 MB:
  do **not** use it for sort's input.
- `IFileIO` (`interfaces/IFileIO.h`): `OpenFile`, `Stat` (`FileStatus.type`
  is `DirectoryEntryType::File/Dir/CharDevice`), `GetDescriptor(IFileIO::kStdIn)`.
  `IFileDescriptor::Read/Write` return the count or `kIOError` (-1),
  `kIOBrokenPipe` (-2), `kIOInterrupted` (-3: the process was stopped).
  `OpenFile` of a directory fails (returns null), so a directory is found
  with `Stat` first, as `cat` and `wc` do.
- `src/components/libheaders/DescriptorLineReader.h` exists but strips `\r`
  and hides errors: not suitable here (a byte-exact reader is written below).
- Tests: `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h`
  -- `BuiltinCommandsTest` (an in-memory root, every builtin in `/bin`,
  `WriteFile(path, content)`, `RunCaptured(command, args, input,
  workingDirectory, environment)` returning `out`, `err`, `status` byte for
  byte; stdout is a file, not a terminal).

Rules that bite (root `CLAUDE.md`, restated):
- `ICurrentProcess` is the only door out of a process: every file is opened
  through `context.IO()`; nothing holds an `IFileSystem` or `IHaisosOS`.
- Builtins: GNU's output byte for byte (C locale: ASCII quotes `'x'`);
  every option of GNU sort in `Options()`, untreated ones with id
  `kBuiltinNotTreated` (reported by `BeginBuiltin`); `--help` only from
  `BuiltinHelpText` (never hand-written); `--version`; the default
  `ManPage()`; registered in `CreateStandardBuiltinCommands()` (which also
  writes `# BUILTIN rootfs sort /bin/sort` into the `haisos --init`
  template -- never hand-write it); the source listed in
  `src/components/BuiltinCommands/CMakeLists.txt`; the test file listed in
  `tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt`.
- Portable C++17 (Linux, Windows/MSVC, WASM): no POSIX headers, no `<regex>`,
  no `std::locale`; `std::strtold` etc. are fine.
- A command that reads input stops promptly on `TriggerStop()` (check
  `context.StopRequested()` in read loops; `kIOInterrupted` ends quietly) and
  a broken pipe on stdout is handled by `BuiltinContext` (exit 141).

## Changes

### `src/components/BuiltinCommands/BuiltinText.h` / `BuiltinText.cpp` (new)

Shared helpers for text commands, namespace `Haisos`, needing only
`BuiltinCommand.h`, `interfaces/IFileIO.h` and `FilesystemUtils.h`.

```cpp
// GNU's quote() in the C locale (locale_quoting_style): the text in '...',
// with \ written \\, ' written \', \a \b \f \n \r \t \v as those escapes, and
// every other byte below 0x20, 0x7F and every byte >= 0x80 as a 3-digit octal
// escape (\001, \303\251). Used in "invalid argument 'x' for '--sort'",
// "extra operand 'x'", "multi-character tab 'ab'", tr's and cut's messages.
std::string GnuQuote(std::string_view text);

struct ArgChoice {
    std::string name;   // "quiet"
    int value;          // what it means; synonyms share a value
};
// GNU's XARGMATCH: |value| matched against the names exactly, else as a prefix
// of them -- unambiguous when every name it prefixes has the same value.
// Returns that value; otherwise prints, and returns nullopt:
//   <cmd>: invalid argument 'foo' for '--sort'      (or "ambiguous argument")
//   Valid arguments are:
//     - 'general-numeric'
//     - 'quiet', 'silent'          <- names sharing a value, on one line, in table order
//   Try '<cmd> --help' for more information.
// (all quoting with GnuQuote). The caller then returns 1 -- GNU's exit status
// for an argmatch failure, whatever the command's usual error status.
std::optional<int> ArgMatch(BuiltinContext& context, const std::string& longOption /* "--sort" */,
                            const std::string& value, const std::vector<ArgChoice>& choices);

enum class InputOpenFailure { None, Missing, Directory, Denied, BadDescriptor };
// An input operand as a text filter takes it: "-" is descriptor 0 (null slot ->
// BadDescriptor); a name is Stat-ed first (absent -> Missing, a directory ->
// Directory -- GNU opens it and fails on read, so callers word that as their
// read error), then opened read-only (null -> Denied). Each command words the
// failure itself; this only says which it was.
std::shared_ptr<IFileDescriptor> OpenInputOperand(BuiltinContext& context, const std::string& name,
                                                  InputOpenFailure& failure);

enum class LineReadResult { Line, End, Error, Stopped };
// Lines split on |delimiter| ('\n', or '\0' for -z), byte for byte: nothing
// stripped (a '\r' stays). Reads 64 KiB at a time. Next() gives the line
// without its delimiter; |delimited| is false only for a last line the input
// ended without one. Stopped when context.StopRequested() or a read returned
// kIOInterrupted; Error on any other negative read (the caller reports it).
class BuiltinLineReader {
public:
    BuiltinLineReader(BuiltinContext& context, IFileDescriptor& input, char delimiter);
    LineReadResult Next(std::string& line, bool& delimited);
};

// Writes all of |bytes| to |out|, looping over partial writes. Returns
// bytes.size(), or the first negative result (kIOError, kIOBrokenPipe, ...).
// For outputs that are not stdout (sort -o, uniq OUTPUT, tee's files);
// stdout goes through context.Out.
ssize_t WriteFully(IFileDescriptor& out, std::string_view bytes);
```

`BuiltinLineReader` is a plain class (it implements no interface), held by
value on the stack -- fine; the `Create()` rule applies to classes
implementing `interfaces/`.

### `src/components/BuiltinCommands/BuiltinCompare.h` / `BuiltinCompare.cpp` (new)

Comparison helpers worth sharing (a later `ls --sort=version` reuses the
version one, added by `coreutils--sort-orders`). This task adds:

```cpp
// GNU sort -n in the C locale (gnulib strnumcmp, decimal point '.', no
// thousands separator), on keys whose leading blanks the caller has already
// skipped. Negative, zero or positive, as memcmp.
int CompareNumeric(std::string_view a, std::string_view b);
```

Semantics (verify against `LC_ALL=C sort -n`): a number is an optional
`-`, digits, optionally `.` and digits; parsing stops at the first byte that
does not fit (so `+1`, `1,000`'s `,000`, `abc` contribute nothing: `+1` and
`abc` and the empty string are 0). `-0`, `-`, `-.`, `0`, `0.000`, `` are all
equal. Compare: by sign first (a negative non-zero < zero < positive); two
positives by integer part with leading zeros ignored (more significant
digits = larger, then digit by digit), then by fraction digit by digit with
trailing zeros ignored; two negatives the same, reversed. Never convert to
a floating type (arbitrary length must work: 40-digit numbers compare
exactly).

### `src/components/BuiltinCommands/commands/sort/SortKeys.h` / `SortKeys.cpp` (new)

The key machinery, separate from the command so the next task extends it.

```cpp
struct SortKey {
    size_t startField = 0;   // 0-based (KEYDEF F - 1)
    size_t startChar = 0;    // 0-based (C - 1)
    size_t endField = SIZE_MAX;  // 0-based; SIZE_MAX: to the end of the line
    size_t endChar = 0;      // 0: the end of field endField
    bool skipStartBlanks = false;  // b on POS1 (or global -b)
    bool skipEndBlanks = false;    // b on POS2 (or global -b)
    bool dictionary = false;       // d
    bool foldCase = false;         // f
    bool ignoreNonprinting = false;// i
    bool numeric = false;          // n
    bool generalNumeric = false;   // g   (compared by coreutils--sort-orders)
    bool humanNumeric = false;     // h   (idem)
    bool month = false;            // M   (idem)
    bool random = false;           // R   (idem)
    bool version = false;          // V   (idem)
    bool reverse = false;          // r
};

struct SortSettings {
    std::vector<SortKey> keys;   // after inheritance; empty: whole line, no options
    int tab = -1;                // -1: blank-to-nonblank fields; else the byte (0..255)
    bool reverse = false;        // global -r (also reverses the last resort)
    bool unique = false;         // -u
    bool stable = false;         // -s
    char delimiter = '\n';       // '\0' with -z
};

// Parses one -k KEYDEF into |key|. On error returns false with |error| the
// whole diagnostic after "sort: " (see the messages below).
bool ParseSortKey(const std::string& keydef, SortKey& key, std::string& error);

// Adds one modifier letter (bdfgiMhnRrV) to a key; false for any other letter.
bool ApplySortModifier(char letter, SortKey& key, bool forStart);

// GNU's begfield/limfield: [start, end) of |key| in |line|.
std::pair<size_t, size_t> SortKeyRange(std::string_view line, const SortKey& key, int tab);

// The comparison: keys in order, then -- unless unique or stable -- the last
// resort. Negative, zero, positive.
int CompareLines(std::string_view a, std::string_view b, const SortSettings& settings);

// "options '-gn' are incompatible" (the text after "sort: "), or empty when
// every key is consistent. See below.
std::string IncompatibleOptions(const SortSettings& settings);
```

**KEYDEF parsing** -- `F[.C][OPTS][,F[.C][OPTS]]`, numbers decimal,
overflow saturating to `SIZE_MAX` (no error: `-k 99999999999999999999` is
accepted). Messages, exactly (exit 2, no `Try` line):
- not a number at the start: `invalid number at field start: invalid count at start of 'a'` -- the quoted part is the rest of KEYDEF from that point (`GnuQuote`);
- field 0 (start or end): `field number is zero: invalid field specification '0'` (the whole KEYDEF quoted, e.g. `'1,0'`);
- start char 0: `character offset is zero: invalid field specification '1.0'` (an end `.0` is valid: end of field);
- not a number after `.`: `invalid number after '.': invalid count at start of 'a'`;
- not a number after `,`: `invalid number after ',': invalid count at start of 'a'`;
- a letter outside `bdfgiMhnRrV`, or anything else left over: `stray character in field spec: invalid field specification '1x'`.
`b` in the start part sets `skipStartBlanks`, in the end part
`skipEndBlanks`; every other letter applies to the key wherever it is.

**Inheritance** (GNU): a key with no modifier at all (none of b d f g i M h
n R r V on either part) takes every global ordering option (`-b` sets both
blank flags, `-d -f -i -n -r`, and later `-g -h -M -R -V`); a key with any
modifier takes none of them. With no `-k`, if any global ordering option is
set, the keys are one whole-line key (`startField 0`, `endField SIZE_MAX`)
carrying them; with none, `keys` is empty and lines compare as whole lines.

**Incompatibility** (GNU's `check_ordering_compatibility`, after
inheritance): a key is inconsistent when `numeric + generalNumeric +
humanNumeric + month + (version || random || dictionary ||
ignoreNonprinting)` exceeds 1. The message lists the key's options in this
fixed order, `b` and `r` left out: `d f g h i M n R V` -- e.g. `-g -n` ->
`options '-gn' are incompatible`, `-i -g` -> `'-gi'`, `-d -n` -> `'-dn'`,
`-k1,1Vn` -> `'-nV'`, `-fgn` -> `'-fgn'`. Exit 2.

**Key range** (GNU's begfield/limfield; *blank* is space, tab, and also
`\n` -- a `-z` record may hold newlines):

```
begin: p = 0; repeat startField times (while p < len):
         tab given: advance to the next tab byte, then past it if p < len
         default:   skip blanks, then skip non-blanks
       if skipStartBlanks: skip blanks
       p = min(len, p + startChar)
end (only when endField != SIZE_MAX, else len):
       q = 0; words = endField + (endChar == 0 ? 1 : 0)
       repeat words times (while q < len):
         tab given: advance to the next tab byte; step past it only if
                    (words still to go) or endChar != 0
         default:   skip blanks, then skip non-blanks
       if endChar != 0: if skipEndBlanks: skip blanks; q = min(len, q + endChar)
if end < begin: the key is empty (end = begin)
```

So with default fields a field includes its leading blanks (`a  b` vs `a b`
on `-k2`: `"  b"` < `" b"`, verified: `printf 'a b\na  b\n' | sort -k2`
prints `a  b` first).

**Key comparison**, per key, on the two key texts:
- `numeric`: skip leading blanks of each, then `CompareNumeric`;
- `g h M R V` set: for now, as text (the next task replaces this);
- text: without `dictionary`/`ignoreNonprinting`/`foldCase`, memcmp of the
  common length, then the shorter is smaller; with them, walk both texts
  skipping ignored bytes (`dictionary`: keep only blanks and ASCII
  alphanumerics; `ignoreNonprinting`: keep only 0x20-0x7E), compare bytes
  after `toupper` when `foldCase`, the one that runs out first is smaller.
- `reverse` negates that key's result. The first non-zero key decides.

**Last resort**: all keys equal (or no keys) and neither `unique` nor
`stable`: memcmp of the whole lines (shorter smaller), negated when the
global `-r` is set. With no keys at all and `-u`, the whole-line compare is
still done (it is the only comparison); `-s` alone with no keys still
compares whole lines.

### `src/components/BuiltinCommands/commands/sort/Sort.cpp` (new)

`CreateSortCommand()`. Name `sort`, version `1.0.0`, `usageErrorStatus` 2.

Option table -- every option of GNU sort 9.4, in `sort --help`'s order:

| short | long | argument | this task |
|---|---|---|---|
| b | ignore-leading-blanks | | treated: "ignore leading blanks" |
| d | dictionary-order | | treated |
| f | ignore-case | | treated |
| g | general-numeric-sort | | not treated (next task) |
| i | ignore-nonprinting | | treated |
| M | month-sort | | not treated (next task) |
| h | human-numeric-sort | | not treated (next task) |
| n | numeric-sort | | treated |
| R | random-sort | | not treated (next task) |
| | random-source | Required FILE | not treated (stays so) |
| r | reverse | | treated |
| | sort | Required WORD | not treated (next task) |
| V | version-sort | | not treated (next task) |
| | batch-size | Required NMERGE | treated: accepted, ignored |
| c | check | Optional (long only) | not treated (next task) |
| C | | | not treated (next task) |
| | compress-program | Required PROG | not treated (stays so) |
| | debug | | not treated (stays so) |
| | files0-from | Required F | treated |
| k | key | Required KEYDEF | treated |
| m | merge | | not treated (next task) |
| o | output | Required FILE | treated |
| s | stable | | treated |
| S | buffer-size | Required SIZE | treated: accepted, ignored |
| t | field-separator | Required SEP | treated |
| T | temporary-directory | Required DIR | treated: accepted, ignored |
| | parallel | Required N | treated: accepted, ignored |
| u | unique | | treated |
| z | zero-terminated | | treated |

`--version` stays the builtin's own: `ParseBuiltinArgs` matches an exact
long name before prefixes, so `--version` prints the version and
`--version-sort` sorts (a test checks both). `--vers` is ambiguous (the
shared parser's message; GNU adds the possibilities -- not this task's to change).

`Help()`: summary `sort lines of text files`; usage
`sort [OPTION]... [FILE]...` and `sort [OPTION]... --files0-from=F`;
notes: lines are compared byte by byte, as GNU sort does with LC_ALL=C;
-S, -T, --parallel and --batch-size are accepted and not acted on (Haisos
sorts in memory, with no temporary files).

`Run`:
1. `BeginBuiltin(context, *this, 2, status)`.
2. Walk the options in order: global flags; `-k` -> `ParseSortKey` (error:
   `context.Error(error)`, return 2); `-t` (value `\0` -> byte 0; empty ->
   `empty tab`; longer than one byte -> `multi-character tab 'ab'`
   (GnuQuote); a second `-t` with a different byte -> `incompatible tabs`;
   all exit 2, no Try line); `-o` (a second `-o` with a different name ->
   `multiple output files specified`, exit 2); `--files0-from`.
3. Inheritance, then `IncompatibleOptions` (non-empty -> `Error`, return 2).
4. Inputs: the operands, or `--files0-from=F`'s NUL-separated names, or
   `-` when there are none. `--files0-from` exactly as `wc` does it, with
   sort's words: operands too -> `extra operand 'x'` + `file operands
   cannot be combined with --files0-from` + Try line, exit 2; F missing ->
   `open failed: F: No such file or directory` (ShellEscapeQuoted), exit 2;
   an empty list -> `no input from 'F'` (GnuQuote), exit 2; an empty name ->
   `F:N: invalid zero-length file name` (F as ShellEscapeQuoted, `-` for
   stdin), exit 2; with F `-`, a name `-` -> `when reading file names from
   stdin, no file name of '-' allowed`, exit 2.
5. Read every input fully into memory (one `std::string` per input, lines as
   `string_view`s into them, no size cap), in order, through
   `OpenInputOperand` + `BuiltinLineReader` (or a plain read loop that
   splits on the delimiter -- either way a last line without its delimiter
   is still a line). Failures, exit 2, before anything is printed:
   `cannot read: NAME: No such file or directory`, `read failed: NAME: Is a
   directory`, `cannot read: NAME: Permission denied`, `read failed: NAME:
   Input/output error` (NAME as ShellEscapeQuoted; stdin is `-`). Stopped ->
   return 2 quietly (the process reports 143).
6. `std::stable_sort` of the line views with `CompareLines` (stability is
   what makes `-s` and `-u`'s "first of a run" right: with `-u`, the first
   line of a run of lines comparing equal, in input order -- `printf
   'b\nB\na\nA\n' | sort -fu` prints `a` then `b`).
7. `-u`: drop a line comparing equal (by `CompareLines` with unique set, i.e.
   keys only) to the last line kept.
8. Output each line followed by the delimiter. To stdout through
   `context.Out` (buffered); with `-o FILE`, only now -- after every input is
   read, which is what makes `sort -o f f` safe -- `Stat` and open FILE with
   `kFileOpenWriteCreateTruncate, kFileCreateMode` and write with
   `WriteFully`: a directory -> `open failed: FILE: Is a directory`, a
   missing parent -> `open failed: FILE: No such file or directory`, null
   otherwise -> `open failed: FILE: Permission denied`, a failed write ->
   `write failed: FILE: Input/output error`; all exit 2.
9. Return 0.

### Registration and build

- `BuiltinCommandList.h`: declare `CreateSortCommand()`, add it to
  `CreateStandardBuiltinCommands()` (alphabetical among the others there).
- `src/components/BuiltinCommands/CMakeLists.txt`: add `BuiltinText.cpp`,
  `BuiltinCompare.cpp`, `commands/sort/Sort.cpp`, `commands/sort/SortKeys.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/SortTest.cpp` (in that
directory's `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, ...)`, inputs
given as `RunCaptured`'s `input` or files written with `WriteFile`. Every
expected output below is GNU sort 9.4's with `LC_ALL=C`; for any case not
written out here, run the same command in the container with `LC_ALL=C`
and paste what it prints.

- `SortDefaultIsByteOrder`: input `b\nB\na\n10\n9\n` -> `10\n9\nB\na\nb\n`, err empty, 0.
- `SortAddsAMissingFinalNewline`: input `b\na` -> `a\nb\n`.
- `SortReverseAndUnique`: `-r` on `a\nc\nb\n` -> `c\nb\na\n`; `-u` on `b\na\nb\n` -> `a\nb\n`.
- `SortFoldCaseUniqueKeepsFirstOfRun`: `-fu` on `b\nB\na\nA\n` -> `a\nb\n`; `-f` -> `A\na\nB\nb\n`.
- `SortNumeric`: `-n` on `-0\n0\n+1\n.5\n-.5\n1,000\n 2\n` -> `-.5\n+1\n-0\n0\n.5\n1,000\n 2\n`; `-rn` on `1\n10\n9\n` -> `10\n9\n1\n`; 40-digit numbers ordered exactly.
- `SortKeysWithSeparator`: input `b:2\na:10\nb:1\n` with `-t: -k1,1 -k2,2n` -> `a:10\nb:1\nb:2\n`.
- `SortDefaultFieldsKeepLeadingBlanks`: `-k2` on `a b\na  b\n` -> `a  b\na b\n`; with `-k2b` -> `a b\na  b\n` (equal keys, last resort).
- `SortKeyCharacterPositions`: `-k1.2,1.3` on `xbc\nyab\n` -> `yab\nxbc\n`; `-t, -k2.2` on `x,ab\ny,aa\n` -> `y,aa\nx,ab\n`.
- `SortKeyModifiersOverrideGlobals`: input `b 1\na 2\nc 10\n`: `-r -k1,1` -> `c 10\nb 1\na 2\n`; `-r -k2,2n` -> `b 1\na 2\nc 10\n` (a key with `n` loses the global `-r`).
- `SortStableKeepsInputOrder`: `-s -k1,1` on `a 2\nb 1\na 1\n` -> `a 2\na 1\nb 1\n`; without `-s` -> `a 1\na 2\nb 1\n`.
- `SortDictionaryAndNonprinting`: `-d` on `b-c\nb a\n` -> `b a\nb-c\n`; `-i` on `a\001c\nab\n` -> `ab\na\001c\n`.
- `SortZeroTerminated`: input `b\0a\0` with `-z` -> `a\0b\0`; `-z -k2` on `a\nz\0b\ny\0` -> `b\ny\0a\nz\0` (newline is a blank).
- `SortOutputMayBeAnInput`: file `/w/f` = `3\n1\n2\n`, `sort -o f f` in `/w` -> stdout empty, `/w/f` is `1\n2\n3\n`.
- `SortSeveralFilesAndStdin`: files and `-` mixed; all lines sorted together.
- `SortFiles0From`: a NUL list file of two names -> both sorted; the error cases above with their exact messages and exit 2.
- `SortKeyErrors`: each KEYDEF message above (`0`, `1.0`, `a`, `1,a`, `1.a`, `1x`, `1,0`) -> its line on stderr, exit 2, stdout empty.
- `SortTabErrors`: `-t ab` -> `sort: multi-character tab 'ab'\n`; `-t ''` -> `sort: empty tab\n`; `-t a -t b` -> `sort: incompatible tabs\n`; `-t '\0'` accepted.
- `SortIncompatibleOptions`: `-dn` -> `sort: options '-dn' are incompatible\n`, 2; `-k1,1in` -> `'-in'`; (`-gn` is in the next task).
- `SortReadErrors`: `sort nofile` -> `sort: cannot read: nofile: No such file or directory\n`, 2, no output; `sort /docs` -> `sort: read failed: /docs: Is a directory\n`, 2.
- `SortOutputErrors`: `-o /docs` -> `sort: open failed: /docs: Is a directory\n`; `-o /nodir/x` -> `... No such file or directory`; `-o a -o b` -> `sort: multiple output files specified\n`; all 2.
- `SortUsageErrors`: `-X` -> `sort: invalid option -- 'X'\nTry 'sort --help' for more information.\n`, 2; `-k` alone -> `option requires an argument -- 'k'` + Try, 2.
- `SortVersionIsNotVersionSort`: `sort --version` prints `sort (HaisosOS builtin) 1.0.0\n`.
- `SortIgnoredSizeOptions`: `-S 1M -T /tmp --parallel=2 --batch-size=4` sorts normally, nothing on stderr.

Helper tests in a new `tests/unit/components/BuiltinCommands.unittests/BuiltinTextTest.cpp`
(plain `TEST`s, listed in the CMakeLists): `GnuQuoteEscapes` (`a'b` ->
`'a\'b'`, `a\nb\001` + bytes 0xC3 0xA9 -> `'a\nb\001\303\251'`, `\` ->
`'\\'`); `CompareNumericCases` (the -n cases above, long numbers, `-` vs
`0`, `0.10` vs `0.1` equal). `ArgMatch` is tested through sort's
`--sort`/`--check` in the next task, `BuiltinLineReader` through every
sort test (`SortAddsAMissingFinalNewline`, `SortZeroTerminated`).

Update `BuiltinCommandsTest.cpp`: `ListsEveryBuiltinSortedWithAVersion` --
insert `"sort"` in sorted position into the expected list as it is on
develop. The generic tests (`EveryBuiltinsHelpHasTheSameShape`,
`EveryBuiltinsManPageIsItsHelp`, `EveryUntreatedOptionIsAcceptedAndReported`
-- it runs e.g. `sort --debug /docs`: the report must appear) and
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` (in `CliParser.unittests`)
cover the rest.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Sort*:*GnuQuote*:*CompareNumeric*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```
(The script's filter must match the test executable's name, so
`BuiltinCommands` is the narrowest it takes; the direct run narrows further.)

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `sort` in the opening list of
  commands; a row in "The commands" table (version 1.0.0; treated: `-b -d -f
  -i -n -r -k -t -u -s -o -z --files0-from`, GNU's last-resort comparison,
  C-locale byte order; exceptions: `-S -T --parallel --batch-size` accepted
  and ignored; `-g -h -M -R -V --sort -c -C -m --random-source
  --compress-program --debug` not treated); a short paragraph naming
  `BuiltinText.h` (`GnuQuote`, `ArgMatch`, `OpenInputOperand`,
  `BuiltinLineReader`, `WriteFully`) and `BuiltinCompare.h` as the shared
  helpers for text commands.
- Root `CLAUDE.md`: `sort` in the Builtin Commands list sentence and a row in
  its table (`Sorts lines (keys -k/-t, -b -d -f -i -n -r -u -s, -o, -z), byte order`);
  the `BuiltinCommands/` line of the directory tree lists sort.

## Acceptance

- [ ] `sort` is registered; `--help` has the standard shape; `--version`
      prints the builtin version; `man sort` prints the help.
- [ ] Every GNU sort 9.4 option is in the table; untreated ones are reported.
- [ ] Byte order, keys (begfield/limfield as specified), modifiers,
      inheritance, last resort, `-u`, `-s`, `-r` match GNU on every test.
- [ ] Every message listed is byte-exact, with GNU's exit status (2, or 1
      for argmatch).
- [ ] `-o` naming an input works; output opened only after all input is read.
- [ ] All file access through `context.IO()`; reads stop on `StopRequested`
      / `kIOInterrupted`; no 10 MB cap on input.
- [ ] `BuiltinText.h` and `BuiltinCompare.h` exist with the exact signatures
      above; no POSIX headers, no `<regex>`.
- [ ] Builds on Linux; `BuiltinCommands.unittests` and `CliParser.unittests` pass.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- `-g -h -M -R -V`, `--sort=WORD`, `-c`/`-C`/`--check`, `-m`: the next task,
  `coreutils--sort-orders` (here only parsed: global ones not treated; as key
  modifiers stored and compared as text).
- `--debug`, `--random-source`, `--compress-program` (not treated).
- The obsolete `+POS1 -POS2` key syntax (GNU 9.4 with its default POSIX
  version treats `+1` as a file name too).
- Locales: comparison is always the C locale's.
- uniq, cut, tr, tee, nl (`coreutils--uniq-cut`, `coreutils--tr-tee-nl`).
