# Task search--grep-recursive: grep -r, include/exclude, context, colour, -Z

- Rock: search
- Depends on: search--grep-core
- Size: ~900 changed lines in ~10 files
- Plan checked against: develop @ ccb9dbe
- PR title: grep: recursion, --include/--exclude, context, --color, -Z

## Goal

`grep` (and `egrep`, `fgrep`) gain the rest of GNU grep 3.11: recursive
search (`-r -R -d -D`, `.` by default), the file filters `--include
--exclude --exclude-from --exclude-dir`, context (`-A -B -C -NUM`, `--`
group separators, `--group-separator`, `--no-group-separator`), colour
(`--color`/`--colour` never/always/auto with GNU's `GREP_COLORS` and SGR
bytes) and `-Z`/`--null`. So `grep -rn --include="*.cpp" TODO src`,
`grep -rl x .`, `grep -C2 -n err log` print what they print on Linux.

This task also introduces **contract 6**, `FnMatch` (glibc `fnmatch()`),
used later by find (`search--find-tests`), rg, diff and tar, and
`GrepContext` (the before/after context bookkeeping), which rg reuses.

## Context

Read first: `develop-plan/tasks/search--grep-core.md` and what it built in
`src/components/BuiltinCommands/commands/grep/` (`Grep.cpp`, `GrepFile.*`,
`GrepMatcher.*`, `GrepSettings.h`), `src/components/BuiltinCommands/CLAUDE.md`,
`commands/hsh/HshPattern.cpp` (a hand-written glob matcher in the house
style), and the root `CLAUDE.md` sections "Security", "Builtin Commands".

What exists, by exact name: everything of `search--grep-core` (the three
commands at version 1.0.0; the option table already lists every option of
this task as `kBuiltinNotTreated`, the digits `0`-`9` as hidden options;
`GrepOneInput`, `GrepSettings`, `GrepMatcher`); `BuiltinText.h`
(`GnuQuote`, `OpenInputOperand`, `BuiltinLineReader`); `IFileIO::
ReadDirectory(path)` (`DirectoryEntry { name, type }`, `.` and `..`
first), `IFileIO::Stat` (`FileStatus.type`: `DirectoryEntryType::File`,
`Dir`, `CharDevice`); the process environment via
`context.Process().GetEnvironment()->GetVariable(name)`.

Reference: GNU grep 3.11, `LC_ALL=C grep ...` in the container. glibc's
`fnmatch` can be asked directly: `python3 -I -c "import ctypes;
l=ctypes.CDLL(None); print(l.fnmatch(b'*.c', b'dir/a.c', 1))"` (0 = match;
flags FNM_PATHNAME 1, FNM_NOESCAPE 2, FNM_PERIOD 4, FNM_LEADING_DIR 8,
FNM_CASEFOLD 16).

## Changes

### Rules that bite (root CLAUDE.md)

ICurrentProcess is the only door out (directories listed and files opened
through `context.IO()` only); GNU output byte for byte; the option table
stays complete, now with these options treated (ids instead of
`kBuiltinNotTreated`); `--help` from `BuiltinHelpText`; bump the three
commands to version `1.1.0`; new sources in `CMakeLists.txt`; portable
C++17 (no POSIX `fnmatch`, no `<regex>`, no `<cctype>` classification); a
recursive walk checks `StopRequested()` per entry.

### `src/components/BuiltinCommands/BuiltinFnmatch.h` / `.cpp` (new) -- contract 6, exact

```cpp
#pragma once
#include <string_view>

namespace Haisos {

// glibc's fnmatch() flags, same values.
constexpr int kFnmPathname = 1;    // '*', '?' and brackets never match '/'
constexpr int kFnmNoEscape = 2;    // '\' is an ordinary character
constexpr int kFnmPeriod = 4;      // a leading '.' (at the start, or after '/' with kFnmPathname) only matches a literal '.'
constexpr int kFnmLeadingDir = 8;  // also match when the pattern matches a prefix of text followed by '/'
constexpr int kFnmCaseFold = 16;   // ASCII letters match either case

// Whether |text| as a whole matches the shell pattern |pattern|, as glibc's
// fnmatch(pattern, text, flags) == 0 in the C locale.
bool FnMatch(std::string_view pattern, std::string_view text, int flags = 0);

}
```

Semantics (bytes): `*` any string (empty too), `?` one byte, `\c` the byte
c (a trailing lone `\` never matches -- glibc's "trailing \ loses"; with
`kFnmNoEscape` it is a literal `\`), `[...]` a bracket: `!` or `^` right
after `[` negates; `]` right after `[`/`[!`/`[^` is a member; `a-z` byte
ranges (a reversed range matches nothing); `-` first or last is a member;
`[:alnum:]` ... `[:xdigit:]` ASCII classes (an unknown class name makes the
bracket match nothing); `\` escapes inside unless `kFnmNoEscape`; a `[`
with no closing `]` is a literal `[`. `kFnmPathname`: `*`, `?` and a
bracket never match `/`. `kFnmPeriod`: a `.` at the text's start (or after
`/` with `kFnmPathname`) must be matched by a literal `.` in the pattern
(not `*`, `?` or a bracket). `kFnmLeadingDir`: true also when the pattern
matches `text[0, i)` and `text[i] == '/'`. Iterative matching with
single-star backtracking per `/`-segment (no recursion per byte; a
pattern of many `*` on a long text stays linear-ish).

### `commands/grep/GrepContext.h` / `GrepContext.cpp` (new) -- shared with rg

```cpp
// Before/after context, GNU grep's way: which lines to print around the
// selected ones, and where a group separator goes. The caller prints.
class GrepContext {
public:
    using PrintLine = std::function<void(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected)>;
    using PrintSeparator = std::function<void()>;
    // |before|/|after|: -B/-A (0 allowed). |enabled|: whether context was
    // asked for at all (GNU: -A/-B/-C/-NUM given, even 0) -- separators are
    // printed only then. |separatorAcrossFiles|: a separator also goes
    // between groups of different files (grep; rg without headings).
    GrepContext(size_t before, size_t after, bool enabled, bool separatorAcrossFiles,
                PrintLine printLine, PrintSeparator printSeparator);
    void BeginFile();
    // Every line of the file, in order. |extendsAfter| false: a selected line
    // that does not restart the trailing context (rg after -m).
    void Line(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected, bool extendsAfter = true);
    // Trailing context still owed after the last selected line.
    bool AfterPending() const;
};
```

Logic: keep the last `before` non-printed lines (copies, with number and
offset) in a ring, cleared by `BeginFile`. On a selected line: flush the
ring (oldest first) as context, then the line, then owe `after` lines. On
a non-selected line: if lines are owed, print it as context (one less
owed), else push it into the ring. Before printing any line, when
`enabled` and something was printed earlier (in this file, or in any
earlier one when `separatorAcrossFiles`) and this line is not the line
right after the last printed one of the same file, call `printSeparator`.
A line "printed" counts even when the caller's `printLine` writes nothing
(grep `-o` writes no context lines but keeps the separators: verified
`grep -o -n -A1 TODO a.c` -> `2:TODO\n--\n5:TODO\n--\n10:TODO\n`).

### `GrepSettings.h`

Add: `directories` (`Read`, `Recurse`, `Skip`), `devices`
(`ReadCommandLine` -- GNU's default: devices named on the command line are
read, those met while recursing skipped --, `Read`, `Skip`), an ordered list
of file filters `{ std::string pattern; bool include; }`, the
`--exclude-dir` patterns, `before`/`after`/`contextEnabled`,
`groupSeparator` (optional<std::string>: unset = none; default `--`),
`nullAfterName` (`-Z`), and the colour settings below.

### `Grep.cpp` -- options

- `-r` = `-d recurse`; `-R` the same (Haisos has no symbolic links of its
  own; a physical filesystem's are followed as the filesystem follows
  them -- documented). `-d ACTION`: exact or unambiguous prefix of `read`,
  `recurse`, `skip`; otherwise, to stderr, exactly (GNU's argmatch plus
  grep's usage):
  ```
  grep: invalid argument 'foo' for '--directories'
  Valid arguments are:
    - 'read'
    - 'recurse'
    - 'skip'
  Usage: grep [OPTION]... PATTERNS [FILE]...
  Try 'grep --help' for more information.
  ```
  exit **1** (verified); an ambiguous prefix (`r`) says `ambiguous
  argument 'r' for '--directories'` with the same tail. (Quote with
  `GnuQuote`; write it with `ErrorText` -- not `ArgMatch`, which has no
  `Usage:` line and names the command, egrep included.)
- `-D ACTION`: exactly `read` or `skip`, else `grep: unknown devices
  method`, exit 2.
- `--include=GLOB` / `--exclude=GLOB` append to the filter list in order;
  `--exclude-from=FILE` appends one exclude per line of FILE (read with
  `OpenInputOperand` + `BuiltinLineReader`; missing: `grep: FILE: No such
  file or directory`, exit 2); `--exclude-dir=GLOB` appends with its
  trailing `/`s removed.
- `-A NUM`, `-B NUM`, `-C NUM`: a non-negative decimal (overflow saturates),
  else `grep: NUM: invalid context length argument` (the value raw), exit
  2. `-A`/`-B` win over `-C`/`-NUM` whatever the order (`-C1 -A0 -n five`
  -> `4-four\n5:five TODO\n`). Digit options: consecutive digit options
  form one number (`-15` is 15; `-5n` is 5 and `-n`) -- GNU starts a new
  number with each argument word (`-1 -5` is 5 there, 15 here: a
  documented approximation, since the parse does not say where words
  start).
- `--group-separator=SEP`, `--no-group-separator` (last wins).
- `-Z`/`--null`.
- `--color[=WHEN]`, `--colour[=WHEN]`: no value = `auto`; values,
  ASCII-case-insensitively: `always`/`yes`/`force`, `never`/`no`/`none`,
  `auto`/`tty`/`if-tty`. Anything else: GNU prints its full `--help` and
  exits 0 -- so do that with `BuiltinHelpText(*this)` to stdout, exit 0.
  `auto` colours only when `context.OutIsTerminal()` and the process
  environment's `TERM` is set and is not `dumb` (GNU's should_colorize).

### Recursion and filters (`Grep.cpp`, or a new `commands/grep/GrepWalk.cpp`)

- No file operand with `-r`: search `.` as an **implicit** operand, whose
  names are printed without the leading `./` (`grep -r x` -> `src/a.c:...`;
  `grep -r x .` -> `./src/a.c:...`).
- Names shown: `-H`; or, without `-h`, more than one operand, or the
  single operand (the implicit `.` included) is a directory searched
  recursively (`grep -r ten src/a.c` prints no name).
- An operand that is a directory: `-d read` (default) -> `grep: NAME: Is a
  directory` (error, as core does); `-d skip` -> silently skipped;
  recurse -> walk it. `-` is standard input even with `-r`.
- The walk: entries of `IO().ReadDirectory(dir)` minus `.`/`..`, **sorted
  by name in byte order** (GNU uses readdir order, which is unspecified --
  documented), files and directories in that one order, depth first. A
  child's name is the parent's path + `/` + name, where the parent's path
  is the operand as given with any run of trailing `/` cut to one (`src/`
  -> `src/sub/b.h`, `src//` -> `src/sub/b.h`, `.//src` -> `.//src/sub/b.h`,
  all verified). Directories inside are walked unless excluded; a
  `CharDevice` met while walking is skipped unless `-D read`; a device
  operand is read unless `-D skip`.
- Filters (gnulib's `excluded_file_name`, as GNU grep calls it). For an
  entry met while walking, the pattern is matched against its **base name**,
  with `FnMatch(pattern, name, 0)`. For an operand (not `-`), against the
  operand as given **unanchored**: it matches if `FnMatch` matches the
  whole operand or the part after any `/` (that is not followed by another
  `/`). Files: walk the `--include`/`--exclude` list **from the last given
  to the first**; the first pattern that matches decides (include ->
  searched, exclude -> skipped); when none matches, the file is skipped if
  the **first** given was an `--include`, searched otherwise. Directories:
  skipped if any `--exclude-dir` pattern matches -- an operand directory
  too (`grep -r --exclude-dir=src x src` searches nothing), except the
  implicit `.`. `--include`/`--exclude` never apply to directories.
  Verified: `--include='*.c'` -> only `src/a.c`; `--exclude='a*'
  --include='*.h'` -> everything but `a.c`; `--include='*.h'
  --exclude='b*'` -> nothing; `-l --include='*.h' TODO src/a.c t1` ->
  nothing (operands filtered too); `--exclude-dir=sub TODO src/sub` ->
  nothing.

### Context and `-Z` (`GrepFile.cpp`)

- `GrepOneInput` drives a `GrepContext` (one per run, `BeginFile` per
  input; `separatorAcrossFiles` true) when context is enabled and none of
  `-c -l -L -q` is in effect. Context lines' prefix uses `-` where
  selected lines use `:` (`a.c-4-19-\tfour` with `-HnbT`).
- `-m N`: after the N-th selected line, keep reading only while trailing
  context is owed, passing every line as **not selected** (GNU prints them
  as context: `grep -n -m1 -A4 TODO a.c` -> `2:two TODO\n3-three\n4-four\n5-five TODO\n6-six\n`).
- The separator is the group separator + `\n` (coloured, see below).
- `-Z`: after a file name, `\0` instead of the `:`/`-` separator in line
  prefixes (`a.c\0` + `2:two TODO`); `-c` -> `name\0count\n`; `-l`/`-L` ->
  `name\0` (no newline).

### Colour (`GrepFile.cpp`)

When colour is on: `GREP_COLORS` (process environment) over the defaults
`ms=01;31:mc=01;31:sl=:cx=:fn=35:ln=32:bn=32:se=36`. Parse it as
`:`-separated entries: `mt=V` sets ms and mc, `ms=`, `mc=`, `sl=`, `cx=`,
`fn=`, `ln=`, `bn=`, `se=` (V: digits and `;`, may be empty), booleans
`rv` and `ne`; stop at the first malformed or unknown entry, keeping what
was read. Before it, the deprecated `GREP_COLOR` (non-empty, only digits
and `;`) sets ms and mc, and if it is still in effect after `GREP_COLORS`,
write `grep: warning: GREP_COLOR='V' is deprecated; use GREP_COLORS='mt=V'`
to stderr once.

A coloured piece is `ESC [ V m ESC [ K` + text + `ESC [ m ESC [ K`
(`ne`: without both `ESC [ K`); a capability with an empty value adds
nothing. Where: the file name (fn), each `:`/`-` separator after a name or
number (se; `-Z`'s `\0` is written bare), line numbers (ln), byte offsets
(bn), the group separator (se). In a line, matches are highlighted when
(selected XOR `-v`): ms in a selected line, mc in a context line; empty
matches are not. sl colours selected lines, cx context lines (swapped by
`rv` with `-v`): its start is written at the line's start and again after
each highlighted match, its end before the line terminator. `-o` prints
each match ms-coloured. `-c` and `-l` colour the name (and `-c`'s
separator). Byte-exact, verified (`ESC` written `\e`):
- `grep --color=always -n TODO a.c b.h` first line:
  `\e[35m\e[Ka.c\e[m\e[K\e[36m\e[K:\e[m\e[K\e[32m\e[K2\e[m\e[K\e[36m\e[K:\e[m\e[Ktwo \e[01;31m\e[KTODO\e[m\e[K\n`
- context line (`-n -H -C1 five a.c`):
  `\e[35m\e[Ka.c\e[m\e[K\e[36m\e[K-\e[m\e[K\e[32m\e[K4\e[m\e[K\e[36m\e[K-\e[m\e[Kfour\n`;
  group separator `\e[36m\e[K--\e[m\e[K\n`
- `-c`: `\e[35m\e[Ka.c\e[m\e[K\e[36m\e[K:\e[m\e[K3\n`; `-l`: `\e[35m\e[Ka.c\e[m\e[K\n`
- `-o -b TODO a.c`: `\e[32m\e[K8\e[m\e[K\e[36m\e[K:\e[m\e[K\e[01;31m\e[KTODO\e[m\e[K\n`
- `-v -n TODO a.c` first line: `\e[32m\e[K1\e[m\e[K\e[36m\e[K:\e[m\e[Kone\n`
- `GREP_COLORS=ne`: `\e[01;31mTODO\e[m\n`; `GREP_COLORS='mt=01;34'`: `\e[01;34m\e[KTODO\e[m\e[K\n`
- `GREP_COLORS='sl=1:cx=2'`, stdin `TODO\n`, `--color=always -A1 T`:
  `\e[1m\e[K\e[01;31m\e[KT\e[m\e[K\e[1m\e[KODO\e[m\e[K\n`
- `-Z -n` with two files: `\e[35m\e[Kb.h\e[m\e[K\0\e[32m\e[K1\e[m\e[K\e[36m\e[K:\e[m\e[K\e[01;31m\e[KTODO\e[m\e[K sub\n`

### Help notes and docs

`Help()` notes gain: the walk visits names in byte order; `-R` = `-r`;
colours as GNU's `GREP_COLORS`. Remove grep-core's "not yet treated"
sentence.

## Tests

`tests/unit/components/BuiltinCommands.unittests/GrepTest.cpp`: replace
`GrepReportsNotTreatedOptions`; add (fixture tree under `/g`: `src/a.c`
(grep-core's a.c), `src/sub/b.h` = `TODO sub\n`, `src/bin.dat` =
`x\0y TODO\n`, `t1` = `TODO\n`, `.hid/h.c` = `TODO hidden\n`; run from `/g`
with `RunCaptured`, expected verified with GNU grep 3.11):
- `GrepRecursiveDefaultsToDot`: `-rl TODO` -> `.hid/h.c\nsrc/a.c\nsrc/bin.dat\nsrc/sub/b.h\nt1\n` (sorted; no `./`); `-rl TODO .` -> the same with `./` in front.
- `GrepRecursivePrefixes`: `-rl 'TODO sub' src//` -> `src/sub/b.h\n`; `.//src` -> `.//src/sub/b.h\n`; `-r ten src/a.c` -> `ten TODO\n`; `-rh 'TODO sub' src` -> `TODO sub\n`.
- `GrepRecursiveLineNumbers`: `-rn TODO src` -> `src/a.c:2:two TODO`, `src/a.c:5:five TODO`, `src/a.c:10:ten TODO`, `src/sub/b.h:1:TODO sub` (out), and err `grep: src/bin.dat: binary file matches\n` -- order: the binary message is written when `bin.dat` is searched, between `a.c`'s and `b.h`'s lines (stdout flushed before it).
- `GrepIncludeExclude`: the five verified cases under "Filters" above, each with `-rl`.
- `GrepExcludeFrom`: a file listing `a*` and `*.dat` -> `-rl --exclude-from=F TODO src` -> `src/sub/b.h\n`.
- `GrepDirectoriesOption`: `-d skip TODO src` -> empty, 1; `-d recurse -l 'TODO sub' src` -> `src/sub/b.h\n`; `-d foo x t1` -> the argmatch text above, 1; `-d r x t1` -> the ambiguous form, 1; `-D foo x t1` -> `grep: unknown devices method\n`, 2.
- `GrepDevicesSkippedWhileRecursing`: in the test, before running, `root->CreateDirectory("/g/dev", kDirMode)` and `root->Mount("/g/dev", factory->CreateServicesCreator()->CreateFileSystemService()->CreateDeviceFileSystem())` (`null` and `zero` inside). `-rl x dev` -> nothing, 1 (both devices skipped while recursing; never reads `zero`'s endless bytes); `-c x dev/null` -> `0\n`, 1 (a device operand is read); `-D skip -c x dev/null` -> nothing, 1.
- `GrepContextLines`: `-n -B1 -A1 'two\|five' src/a.c` -> `1-one\n2:two TODO\n3-three\n4-four\n5:five TODO\n6-six\n`; `-2 -n five src/a.c` -> `3-three\n4-four\n5:five TODO\n6-six\n7-seven\n`; `-A0 TODO src/a.c` -> `two TODO\n--\nfive TODO\n--\nten TODO\n`; `-n -A1 --group-separator=XX 'two\|ten' src/a.c` -> `2:two TODO\n3-three\nXX\n10:ten TODO\n`; `--no-group-separator` drops it; `-n -C1 TODO src/a.c src/sub/b.h` ends `--\nsrc/sub/b.h:1:TODO sub\n` (separator across files); `-A x y t1` -> `grep: x: invalid context length argument\n`, 2.
- `GrepContextWithMaxCountAndOnly`: the `-m1 -A4` and `-o -A1` outputs above; `-C1 -c TODO src/a.c` -> `3\n`.
- `GrepNullAfterNames`: `-lZ TODO src/a.c src/sub/b.h` -> `src/a.c\0src/sub/b.h\0`; `-Z -c` -> `src/a.c\0` `3\n` ...; `-Z -n TODO src/sub/b.h` (one file) -> `1:TODO sub\n`.
- `GrepColorAlways`: every byte string of the Colour section (as `std::string` with `\x1b` and an explicit `\0` where shown).
- `GrepColorAutoAndNever`: `RunCaptured` (not a terminal) `--color=auto` -> plain; `Run` (the console: a terminal) after `os->GetOsEnvironment()->SetVariable("TERM", "xterm")` -> coloured; with `TERM=dumb` plain; `--color=ALWAYS` coloured; `--colour=bad x t1` -> the `--help` text on stdout, 0.
- `GrepColorEnvironment`: `GREP_COLOR=01;32` -> match `\e[01;32m\e[K...`, err `grep: warning: GREP_COLOR='01;32' is deprecated; use GREP_COLORS='mt=01;32'\n`; `GREP_COLORS=ne` and `mt=01;34` as above (pass an environment clone with the variable to `RunCaptured`).

`tests/unit/components/BuiltinCommands.unittests/FnmatchTest.cpp` (new,
listed in that CMakeLists; plain `TEST(FnmatchTest, ...)`), table-driven,
every row verified with glibc through ctypes:
`*.c`/`a.c`/0 match; `*.c`/`dir/a.c`/0 match; same with Pathname no match;
`*`/`.hidden`/Period no match, /0 match; `?a`/`.a`/Period no; `[.]a`/`.a`/Period no;
`a/*`/`a/.b`/Pathname|Period no, /Pathname match; `[!a]b`/`cb` match;
`[^a]b`/`ab` no; `[]a]`/`a` and `]` match; `[a-c]`/`b` match; `[z-a]`/`b` no;
`[`/`[` match; `a[`/`a[` match; `\*`/`*` match, `\*`/`a` no; `a\`/`a\` no,
with NoEscape match; `*.C`/`a.c`/CaseFold match; `src`/`src/a/b`/LeadingDir
match; `s*`/`src/a`/LeadingDir|Pathname match; `a?c`/`a/c`/Pathname no;
`a[/]c`/`a/c`/Pathname no, /0 match; `[[:digit:]]x`/`1x` match;
`[[:foo:]]`/`f` no; `*`/`` match; ``/`` match; `a*b*c`/`axxbyyc` match;
`[a-]`/`-` match; `[!]]`/`a` match. Plus `LongTextDoesNotBlowUp`: 30 `*`
then `b` against 100000 `a` -> no match, quickly.

Also update the version expectations if a test pins `grep`'s version.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='*Grep*:*Fnmatch*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the grep/egrep/fgrep rows
  (version 1.1.0; now treated: recursion, filters, context, colour, -Z;
  documented exceptions: byte-order walk, `-R` = `-r`, the digit-option
  approximation); a short paragraph on `BuiltinFnmatch.h` (contract 6: who
  uses it) and `commands/grep/GrepContext.h` (shared with rg).
- Root `CLAUDE.md`: the grep row of the Builtin Commands table mentions
  `-r`, `--include`, context and `--color`.

## Acceptance

- [ ] `FnMatch` with exactly the contract's name, flags and values; agrees
      with glibc on the table; no recursion per byte.
- [ ] Include/exclude/exclude-dir follow gnulib's last-match-wins and
      first-option default; operands filtered unanchored.
- [ ] Names printed exactly as GNU (implicit `.`, trailing slashes).
- [ ] Context, separators (`-A0` included), `-m` with context, `-o` with
      context as GNU.
- [ ] Colour bytes exactly as listed; `auto` needs a terminal and a usable
      `TERM`; a bad WHEN prints the help and exits 0.
- [ ] Walk only through `context.IO()`; stops promptly when stopped.
- [ ] Full unit suite passes; both CLAUDE.md files updated.

## Out of scope

- rg (its own walk, ignore files, globs): `search--rg-search`,
  `search--rg-ignore` (which reuse `FnMatch`, `GrepContext`, `GrepMatcher`).
- find's use of `FnMatch` (`search--find-tests`).
- Following or detecting symbolic-link loops (Haisos creates no links).
