# Task search--grep-core: grep, egrep and fgrep -- matching and output

- Rock: search
- Depends on: base--regex-match, coreutils--sort, coreutils--uniq-cut
- Size: ~950 changed lines in ~11 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add grep, egrep, fgrep builtins: patterns, matching, output

## Goal

`grep` is a builtin command, placed with `BUILTIN rootfs grep /bin/grep`,
behaving as GNU grep 3.11 (Ubuntu 24.04, the task container's) run with
`LC_ALL=C`, for everything but recursion, context and colour (the next task,
`search--grep-recursive`): patterns from `-e`, `-f` and the first operand,
several patterns per argument (one per line), the four matchers `-G -E -F
-P`, `-i -y --no-ignore-case -v -w -x`, the output modes `-c -l -L -q -o`,
the prefixes `-n -b -H -h --label -T`, `-m`, `-s`, binary files (`-a -I
--binary-files -U`), `-z`, `--line-buffered`, GNU's messages and exit
statuses (0 a line selected, 1 none, 2 trouble) byte for byte. `egrep` and
`fgrep` are builtins too: Ubuntu's wrappers -- `grep -E` / `grep -F`,
without upstream's obsolescence warning. So `grep -n TODO a.c b.c`, `cmd | grep -c x`, `grep -qv ok log &&
...` print and exit as on Linux.

This task also writes `GrepMatcher` (patterns -> "find the next match in a
line", with `-w`/`-x`), which `rg` (`search--rg-search`) reuses.

## Context

Read first: the root `CLAUDE.md` (sections "Security: ICurrentProcess is the
only door out of a process", "Builtin Commands", "Exit codes", "Automatic
Development Rules" rule 9), `src/components/BuiltinCommands/CLAUDE.md`,
`src/components/Regex/CLAUDE.md`, and the code of
`commands/wc/Wc.cpp` and `commands/cat/Cat.cpp` (option table, `Help()`,
`Run()`, reading a descriptor with stop and error handling).

What exists on develop, by exact name (as if already merged):
- `src/components/Regex/Regex.h` (base--regex-syntax, base--regex-match):
  `RegexSyntax { Basic, Extended, Perl }`, `RegexOptions { syntax,
  ignoreCase, multiline }`, `kRegexNotBol`, `kRegexNotEol`, `RegexMatch {
  groups }`, `Regex::Compile(pattern, options, error)` (null + GNU's message
  on a bad pattern), `Regex::Search(text, start, match, flags)` (leftmost;
  Basic/Extended leftmost-longest, Perl leftmost-first; `match.groups[0]` is
  the whole match), `GroupCount()`. `BuiltinCommands` already links `Regex`:
  `#include "src/components/Regex/Regex.h"`.
- `src/components/BuiltinCommands/BuiltinText.h` (coreutils--sort):
  `GnuQuote`, `ArgChoice`/`ArgMatch`, `InputOpenFailure { None, Missing,
  Directory, Denied, BadDescriptor }`, `OpenInputOperand(context, name,
  failure)` ("-" is descriptor 0), `BuiltinLineReader(context, input,
  delimiter)` with `Next(line, delimited)` -> `LineReadResult { Line, End,
  Error, Stopped }`.
- `BuiltinOption::hidden` (coreutils--uniq-cut): an option parsed like any
  other but never shown in `--help` (neither described nor in the "Not
  treated arguments" line).
- `ParseBuiltinArgs(args, options, stopAtFirstOperand = false)`
  (coreutils--names-env), `BuiltinContext` (`Out`, `Error`, `ErrorText`,
  `TryHelp`, `Flush`, `StopRequested`, `IO()`, `Process()`,
  `OutIsTerminal`, `ReportNotTreated`, `NotTreated`), `BuiltinHelpText`,
  `BuiltinVersionText`, `kBuiltinOptionHelp`, `kBuiltinOptionVersion`.
- `FilesystemUtils.h`: `EntryTypeOf`, `kFileOpenReadOnly`. `IFileIO::Stat`
  gives `FileStatus.size` and `.type`.

Reference behaviour: GNU grep 3.11 in the container, always run as
`LC_ALL=C grep ...` (the C locale: bytes, ASCII case, only NUL makes a file
binary, `'...'` quotes). Every expected output written below was checked
against it; check any other with the same command before pinning it.

## Changes

### Rules that bite (root CLAUDE.md)

- ICurrentProcess is the only door out: files only through
  `context.IO()` (`OpenInputOperand`, `IFileIO::Stat`); no `IFileSystem`,
  no `IHaisosOS`.
- Builtins: GNU's output byte for byte; every option of GNU grep 3.11 in
  `Options()` (the ones this task does not act on as `kBuiltinNotTreated`,
  reported by the parse step); `--help` only from `BuiltinHelpText`;
  `--version`; the default `ManPage()`; registered in
  `CreateStandardBuiltinCommands()` -- which also writes the `# BUILTIN
  rootfs grep /bin/grep` line of the `haisos --init` template (never by
  hand); sources in `src/components/BuiltinCommands/CMakeLists.txt`.
- Portable C++17: no POSIX headers, no `<regex>`, no `<cctype>`
  classification (locale-dependent): write ASCII predicates.
- Reads stop promptly on `TriggerStop()` (check `StopRequested()` per chunk;
  a `Read` returning `kIOInterrupted` ends the command quietly); a broken
  stdout pipe is `BuiltinContext`'s business (141).

### `commands/grep/GrepMatcher.h` / `GrepMatcher.cpp` (new)

Namespace `Haisos`. A plain class (no interface): exact API --

```cpp
enum class GrepSyntax { Basic, Extended, Fixed, Perl };
// How -w decides a match is a whole word.
enum class GrepWholeWord {
    No,
    NonWordNeighbours,  // GNU grep -G/-E/-F (and rg): the bytes before and after are not word bytes
    PerlBoundaries,     // GNU grep -P: the pattern wrapped as \b(?:...)\b
};
struct GrepMatcherOptions {
    GrepSyntax syntax = GrepSyntax::Basic;
    bool ignoreCase = false;                       // ASCII
    GrepWholeWord wholeWord = GrepWholeWord::No;   // -w
    bool wholeLine = false;                        // -x
};

class GrepMatcher {
public:
    // |patterns| one pattern each (the caller has split on '\n'); may be
    // empty (then nothing ever matches). Perl: at most one (the caller
    // reports GNU's message for more). Null with |error| set to the message
    // grep prints after "grep: ".
    static std::shared_ptr<const GrepMatcher> Create(const std::vector<std::string>& patterns,
                                                     const GrepMatcherOptions& options, std::string& error);
    // The first match in |line| (one line, without its terminator) starting
    // at or after |start| that -w/-x accept: [begin, end). Basic/Extended/
    // Fixed: leftmost, then longest across all patterns; Perl: the regex's
    // leftmost-first match.
    bool Find(std::string_view line, size_t start, size_t& begin, size_t& end) const;
    // Whether any match exists (Find from 0).
    bool Matches(std::string_view line) const;
};
```

Logic:
- **Word byte**: `[A-Za-z0-9_]`.
- **Compiling** (`Basic`/`Extended`/`Perl`): `RegexOptions{syntax,
  ignoreCase}`. When no pattern holds a back-reference (`\1`-`\9`) and the
  syntax is Basic or Extended, join them into one regex (Basic:
  `\(p1\)\|\(p2\)...`, Extended: `(p1)|(p2)...`; one pattern is compiled
  as is); otherwise compile each separately. Compile each pattern alone
  first anyway when joining fails, so the error is the one of the pattern
  at fault. Perl: `wholeLine` wraps the pattern as `\A(?:` p `)\z`,
  `PerlBoundaries` as `\b(?:` p `)\b` (GNU's PCRE2_EXTRA_MATCH_LINE/WORD).
- **GNU's own refusal** (Basic/Extended only, checked before compiling):
  a bracket expression whose content starts and ends with `:` (`[:space:]`,
  `x[:a:]`) -> error `character class syntax is [[:space:]], not [:space:]`
  -- that exact text whatever the class name (GNU's dfa hard-codes it).
  `[[:space:]]` is fine. To find brackets, scan the pattern: skip `\x`
  pairs outside brackets; a `[` opens a bracket whose content runs to the
  first `]` that is not right after the `[` (or after `[^`), skipping
  `[:...:]`, `[.….]`, `[=...=]` inside it.
- **Fixed** (`-F`): no regex: each pattern is a byte string; find each
  one's leftmost occurrence (ASCII-folded with `ignoreCase`); the
  leftmost start wins, then the longest. An empty pattern matches the
  empty string at `start`.
- **wholeLine** (non-Perl): a line matches only if a match spans it all:
  for Basic/Extended `Search(line, 0)` must give `(0, line.size())`
  (exact for leftmost-longest); Fixed: some pattern equals the line.
  `Find` then returns `(0, size)`.
- **NonWordNeighbours** (GNU's EGexecute loop, emulated exactly):
  ```
  s = start
  loop: find the leftmost match (b, e) at or after s; none -> false
     len = e - b
     loop:
        if (b == 0 or line[b-1] not word) and (e == size or line[e] not word): return (b, e)
        if len > 0:                      # try a shorter match anchored at b
           len -= 1
           search line[0 .. b+len) from b with kRegexNotEol (per pattern for
           several regexes; Fixed: the longest pattern occurring at b with
           length <= len)
           if a match starts exactly at b and is non-empty: e = its end; len = e - b; continue
        s = b + 1; if s > size -> false; break   # look further on
  ```
  (`Search` on the truncated text gives the longest match starting at `b`
  when one exists there: leftmost-longest.) For Perl with this mode (rg)
  the same loop runs on Perl's leftmost-first matches.
- **Several regexes** (back-references): run each, keep the smallest
  begin, then the largest end.
- `Find` with `start > line.size()` is false. Callers iterating `-o`
  advance past an empty match by one byte themselves.

### `commands/grep/GrepSettings.h` (new)

A plain struct with everything the options decide -- matcher options and
patterns, `invert`, `count`, `listFiles` (`None`, `Matching`,
`NonMatching`), `quiet`, `onlyMatching`, `lineNumbers`, `byteOffsets`,
`withFilename` (optional<bool>: unset = by operand count), `label`
(default `(standard input)`), `initialTab`, `maxCount` (optional;
negative -> unlimited), `noMessages`, `binaryFiles` (`Binary`, `Text`,
`WithoutMatch`), `nullData` (`-z`: lines end in `\0`), `lineBuffered`.
`search--grep-recursive` adds its fields here.

### `commands/grep/GrepFile.h` / `GrepFile.cpp` (new)

```cpp
struct GrepFileResult { uint64_t selected = 0; bool error = false; bool stopped = false; };
// Searches one input and prints what the settings ask for. |shownName| is
// the name printed (the operand, or the label for standard input);
// |sizeForTab| the file's size when known (regular file), for -T.
GrepFileResult GrepOneInput(BuiltinContext& context, const GrepSettings& settings, const GrepMatcher& matcher,
                            IFileDescriptor& input, const std::string& shownName,
                            std::optional<uint64_t> sizeForTab);
```

Reading: grep's own chunk loop, not `BuiltinLineReader` -- GNU decides a
file is binary per buffer *before* printing any line of it. Each `Read`
asks for up to 98304 bytes (GNU's 96 KiB buffer); keep a carry of the
partial last line. Line terminator `eol` = `'\n'`, or `'\0'` with `-z`.

Binary files (not `-a`, not `-z`; `--binary-files=binary` the default):
- A chunk holding a NUL byte makes the input binary from that chunk on
  (for a regular file under 96 KiB: from the start, as GNU). Lines already
  printed stay.
- `--binary-files=without-match` (`-I`): the input counts as having no
  selected line at all -- stop reading; `-c` prints a count of 0, `-L`
  lists it, exactly as for a file with no match (checked: `printf 'x\0y\n' | grep -c -I x`
  prints `0`, exit 1).
- Otherwise, from then on each NUL also ends a line (for matching,
  counting and line numbers), nothing is printed for selected lines, and
  at the first selected line the search of this input ends (unless `-c`,
  which counts on). Afterwards, when at least one line was selected after
  the input became binary and none of `-c -l -L -q` is in effect, write to
  **stderr** (`context.ErrorText`, which flushes stdout first)
  `grep: <shownName>: binary file matches\n` -- `-s` does not hide it; it
  is not an error (exit status untouched).

Per line (`-v` inverts "selected"):
- `-q`: at the first selected line, stop everything (the caller exits 0).
- `-l`/`-L`: stop this input at its first selected line; print afterwards.
- `-m N`: stop the input after N selected lines (`-m 0`: the input is not
  read at all).
- `-c`: count only; at the end print `[name:]count\n`.
- Otherwise print the selected line: prefix, then the line, then `eol`.
  With `-o` (and not `-v`): for each non-empty match in the line (`Find`
  from 0, then from the match's end; an empty match advances one byte and
  prints nothing), print prefix + match + `eol`; `-o -v` prints nothing.
- Prefix, in this order: name + `:` (when filenames are shown; with `-Z`,
  added next task, `\0` instead of `:`), line number + `:` (`-n`), byte
  offset + `:` (`-b`: of the line's first byte, or of the match with
  `-o`), then with `-T` a tab when anything was printed before it.
- `-T` pads line numbers and offsets to a width: the number of decimal
  digits of `size + (lineNumbers ? 1 : 0)` when `sizeForTab` is known, else
  19 (GNU's `INTMAX_MAX`, what it uses for a pipe; standard input is
  always 19 in Haisos -- a descriptor has no size: documented exception).
  Verified: a 99-byte file, `grep -Tn` -> `  5:\t5`, `grep -Tb` -> ` 8:\t5`,
  `grep -Tbn` -> `  5:  8:\t5`; `grep -T x` with no prefix prints no tab;
  `printf 'TODO\n' | grep -Tn TODO` -> 18 spaces then `1:\tTODO`.
- A last line without terminator is printed with one added.
- `--line-buffered`: `context.Flush()` after each output line.
- Read errors: `grep: <name>: Input/output error`, error flag, go on.

At the end of the input: `-c` -> `[name:]count\n`; `-l` -> `name\n` if
selected > 0; `-L` -> `name\n` if selected == 0 (output terminator `\n`;
`-Z` changes it next task).

### `commands/grep/Grep.cpp` (new)

Three commands sharing one implementation: `CreateGrepCommand()`,
`CreateEgrepCommand()`, `CreateFgrepCommand()` (declared in
`BuiltinCommandList.h`, added to `CreateStandardBuiltinCommands()` in
alphabetical order). Names `grep`, `egrep`, `fgrep`; version `1.0.0` each.
`Help()`: summary `print lines that match patterns`; usage
`grep [OPTION]... PATTERNS [FILE]...` (egrep/fgrep: their own name);
egrep/fgrep `basedOn` `grep` (their `--help` links grep's man page, which
is where GNU documents them). Notes (a few lines): PATTERNS holds one
pattern per line; with no FILE, standard input; bytes and the C locale
(only a NUL makes a file binary, `-i` folds ASCII); `-T` on standard input
pads to 19; `-U` does nothing (as on Linux); exit 0/1/2.

Option table -- every option of GNU grep 3.11 (one row each; a long-only
alias is its own row with the same id):
`-E --extended-regexp`, `-F --fixed-strings`, `--fixed-regexp` (hidden,
same as -F), `-G --basic-regexp`, `-P --perl-regexp`, `-e --regexp=PATTERNS`,
`-f --file=FILE`, `-i --ignore-case`, `-y` (hidden, same as -i),
`--no-ignore-case`, `-w --word-regexp`, `-x --line-regexp`, `-z --null-data`,
`-s --no-messages`, `-v --invert-match`, `-V` (id `kBuiltinOptionVersion`,
"display version information"), `-m --max-count=NUM`, `-b --byte-offset`,
`-n --line-number`, `--line-buffered`, `-H --with-filename`,
`-h --no-filename`, `--label=LABEL`, `-o --only-matching`,
`-q --quiet`, `--silent` (same id as -q), `--binary-files=TYPE`,
`-a --text`, `-I`, `-L --files-without-match`, `-l --files-with-matches`,
`-c --count`, `-T --initial-tab`, `-U --binary`, `-u --unix-byte-offsets`
(hidden, treated: prints `grep: warning: --unix-byte-offsets (-u) is
obsolete` to stderr, then nothing), `-X` (hidden, Required, not treated);
not treated in this task (next task acts on them): `-d --directories=ACTION`,
`-D --devices=ACTION`, `-r --recursive`, `-R --dereference-recursive`,
`--include=GLOB`, `--exclude=GLOB`, `--exclude-from=FILE`,
`--exclude-dir=GLOB`, `-Z --null`, `-A --after-context=NUM`,
`-B --before-context=NUM`, `-C --context=NUM`, `0`-`9` (hidden short
options, one id: GNU's `-NUM`), `--group-separator=SEP`,
`--no-group-separator`, `--color[=WHEN]`, `--colour[=WHEN]` (`Optional`).

Run, step by step:
1. Parse with `ParseBuiltinArgs` directly (not `BeginBuiltin`: GNU grep's
   usage errors carry a `Usage:` line). On `parsed.error`:
   `grep: <error>\n` + `Usage: grep [OPTION]... PATTERNS [FILE]...\n` +
   `Try 'grep --help' for more information.\n`, exit 2. Then `--help` /
   `--version` (first one given wins, as `BeginBuiltin` does), then
   `context.ReportNotTreated(parsed)`.
   **All of grep's diagnostics name `grep`, also when run as egrep or
   fgrep** (GNU's wrappers exec grep): write them with
   `context.ErrorText("grep: " + message + "\n")` and the literal `Try
   'grep --help' ...` line -- never `context.Error`/`TryHelp`, which would
   say `egrep:`.
2. egrep / fgrep: behave as grep with `-E` / `-F` given first, writing
   nothing extra: Debian/Ubuntu's scripts (the user's choice, matching the
   Ubuntu container) do not print upstream 3.8+'s `egrep: warning: egrep is
   obsolescent; using grep -E`.
3. Options in order: the matcher options set the syntax; a second,
   different one -> `grep: conflicting matchers specified`, exit 2 (the
   same one twice is fine). `-e` adds its argument's lines (split on
   `\n`; `-e ''` is one empty pattern); `-f FILE` adds the file's lines
   (`-` = stdin; read with `OpenInputOperand` + `BuiltinLineReader`; an
   empty file adds none; a missing one: `grep: FILE: No such file or
   directory`, exit 2). `-i`/`-y` and `--no-ignore-case`: last wins.
   `-h`/`-H`: last wins. `-l`/`-L`: last wins. `-a` = `--binary-files=text`,
   `-I` = `without-match`; `--binary-files` takes exactly `binary`,
   `text`, `without-match`, else `grep: unknown binary-files type`, exit 2.
   `-m`: a decimal integer, optionally negative (negative = unlimited,
   overflow = unlimited); anything else `grep: invalid max count`, exit 2.
4. No `-e`/`-f`: the first operand is the patterns; none ->
   `Usage: grep [OPTION]... PATTERNS [FILE]...\nTry 'grep --help' for more
   information.\n`, exit 2.
5. `-P` with more than one pattern: `grep: the -P option only supports a
   single pattern`, exit 2.
6. `GrepMatcher::Create`; on error `grep: <error>`, exit 2 (the message is
   Regex's; for GNU's dfa wordings that differ from glibc's -- a lone `[`
   is `Invalid regular expression` in GNU grep -- do not pin a test).
   Whole word: `PerlBoundaries` for `-P`, else `NonWordNeighbours`.
7. Files: the remaining operands, or `-` when none. Filenames are shown
   when `-H`, or (no `-h`) when there is more than one operand. For each:
   `-` is standard input, shown as the `--label` text or `(standard
   input)`; a name goes through `OpenInputOperand`: `Missing` -> `grep:
   NAME: No such file or directory`, `Directory` -> `grep: NAME: Is a
   directory` (GNU's default `-d read`), `Denied` -> `grep: NAME:
   Permission denied`; each sets the error flag (messages hidden by `-s`,
   the flag kept). `sizeForTab` from `IFileIO::Stat` for a regular file.
8. Exit: `-q` and a selected line -> 0 at once (no more files read);
   otherwise 2 if any error, else 0 if any line was selected in any file,
   else 1 (also with `-L`: GNU 3.5+ reverted to "a line selected", so
   `grep -L x f` where f holds x prints nothing and exits 0).

### `BuiltinCommandList.h`, `CMakeLists.txt`

The three factories; `commands/grep/Grep.cpp`, `GrepMatcher.cpp`,
`GrepFile.cpp` in `add_library(BuiltinCommands ...)`.

## Tests

`tests/unit/components/BuiltinCommands.unittests/GrepTest.cpp` (new, in that
directory's `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, Grep...)`, all
with `RunCaptured` (stdout a file: not a terminal) from a directory the test
fills, e.g. `/g` with `a.c` = `one\ntwo TODO\nthree\nfour\nfive TODO\nsix\nseven\neight\nnine\nten TODO\n`
(64 bytes), `b.h` = `TODO sub\n`, `t1` = `TODO\n`. Also add `egrep`,
`fgrep`, `grep` to the exact list in `ListsEveryBuiltinSortedWithAVersion`
(`BuiltinCommandsTest.cpp`). Expected outputs (verified with GNU grep 3.11,
`LC_ALL=C`):

- `GrepPrintsMatchingLines`: `grep TODO a.c` -> `two TODO\nfive TODO\nten TODO\n`, status 0.
- `GrepNoMatchExitsOne`: `grep nope a.c` -> empty, 1.
- `GrepMissingFileExitsTwo`: `grep x nosuch` -> err `grep: nosuch: No such file or directory\n`, 2; with `-s` err empty, 2; `-q TODO nosuch a.c` -> err the message, status 0.
- `GrepDirectoryWithoutRecursion`: `grep TODO /g` -> err `grep: /g: Is a directory\n`, 2.
- `GrepSeveralFilesShowNames`: `grep -c TODO a.c b.h` -> `a.c:3\nb.h:1\n`; `-h` drops the names; `-H` on one file adds it.
- `GrepPrefixes`: `grep -nbH TODO a.c` -> `a.c:2:4:two TODO\na.c:5:24:five TODO\na.c:10:55:ten TODO\n`; `-ob TODO a.c` -> `8:TODO\n29:TODO\n59:TODO\n`.
- `GrepInitialTab`: `grep -T -n TODO a.c b.h` -> `a.c: 2:\ttwo TODO\na.c: 5:\tfive TODO\na.c:10:\tten TODO\nb.h: 1:\tTODO sub\n`; `grep -T -H TODO t1` -> `t1:\tTODO\n`; stdin `TODO\n` with `-Tn TODO` -> 18 spaces + `1:\tTODO\n`.
- `GrepStdinAndLabel`: input `a.c`'s text, `-H --label=foo -c TODO` -> `foo:3\n`; `-c TODO - t1` -> `(standard input):3\nt1:1\n`.
- `GrepMatchers`: `-E 'two|ten'` two lines; `-F 'T.D'` on `t1` -> 1; `-E -F x` -> err `grep: conflicting matchers specified\n`, 2; `-P` with `-e a -e b` -> err `grep: the -P option only supports a single pattern\n`, 2; `-P '\d+'` on `x 42\n` -> `x 42\n`.
- `GrepSeveralPatterns`: `-e two -e ten`; a pattern argument `two\nten` (one argument with a newline) -> the same two lines; `-f` a file `two\nten\n`; `-f` an empty file -> no output, 1; `-e ''` matches every line (`-c` on a.c -> `10`).
- `GrepCaseAndInvert`: `-i todo t1` -> `TODO\n`; `-y todo` the same; `-i --no-ignore-case todo t1` -> 1; `-vc TODO a.c` -> `7\n`.
- `GrepWordsAndLines`: stdin `foo_bar foo\n`, `-ob -w foo` -> `8:foo\n`; stdin `aab ab\n`, `-o -w 'a*b'` -> `aab\nab\n`; stdin `xfoo foox\n`, `-c -w -F -e foo -e foox` -> `1\n`; stdin `a\n\n b\n`, `-c -w ''` -> `2\n` and `-n -x ''` -> `2:\n`; `-x six a.c` -> `six\n`.
- `GrepOnlyMatching`: stdin `abc\n`, `-o -e b -e bc -e a` -> `a\nbc\n`; `-o 'x*' t1` -> no output, status 0.
- `GrepListModes`: `-l TODO a.c b.h k` (k without TODO) -> `a.c\nb.h\n`; `-L TODO t1 k` -> `k\n`, 0; `-L TODO t1` -> empty, 0; `-lc TODO a.c` -> `a.c\n`.
- `GrepMaxCount`: `-m2 -n TODO a.c` -> `2:two TODO\n5:five TODO\n`; `-m 0 TODO t1` -> 1; `-m -1 TODO t1` -> `TODO\n`; `-m x y t1` -> err `grep: invalid max count\n`, 2; `-c -v -m2 TODO a.c` -> `2\n`.
- `GrepQuietStopsAtFirstMatch`: `-q TODO a.c` -> nothing, 0.
- `GrepBinaryFiles`: a file `bin` = `x\0y TODO\n`: `grep TODO bin` -> out empty, err `grep: bin: binary file matches\n`, 0; with `-s` the same; `-c TODO bin` -> `1\n`, no err; `-a -n y bin` -> `1:x\0y TODO\n`; stdin `a\0a\n` with `-c a` -> `2\n`; stdin `x\0y\n` with `-c -I x` -> `0\n`, 1; stdin `x\0y\n` with `-z -c x` -> `1\n`; `--binary-files=foo` -> err `grep: unknown binary-files type\n`, 2.
- `GrepNullData`: stdin `a\0b\0`, `-z b` -> `b\0`.
- `GrepUsageErrors`: no arguments -> err `Usage: grep [OPTION]... PATTERNS [FILE]...\nTry 'grep --help' for more information.\n`, 2; `-k x` -> err `grep: invalid option -- 'k'\n` + those two lines, 2; `--foo x` -> `grep: unrecognized option '--foo'\n` + the two lines.
- `GrepClassSyntaxError`: `'[:space:]'` and `-E 'x[:a:]'` -> err `grep: character class syntax is [[:space:]], not [:space:]\n`, 2; `'[[:space:]]' t1` -> 1.
- `GrepBadRegex`: `'a\{1'` -> err `grep: Unmatched \{\n`, 2; `-E '('` -> `grep: Unmatched ( or \(\n`, 2.
- `GrepObsoleteU`: `-u x t1` -> err `grep: warning: --unix-byte-offsets (-u) is obsolete\n`, 1.
- `EgrepAndFgrepRunAsGrepEAndF`: `egrep 'T|x' t1` -> out `TODO\n`, err empty; `fgrep -c 'TO.O' t1` -> `0\n`, err empty, 1; `egrep -F x t1` -> err `grep: conflicting matchers specified\n`, 2.
- `GrepAddsMissingNewline`: stdin `TODO` (no newline) -> `TODO\n`.
- `GrepReportsNotTreatedOptions`: `-r TODO t1` -> err `Parameter -r is not treated by HaisosOS grep v. 1.0.0\n`, out `TODO\n` (temporary until the next task; the next task replaces this test).

`GrepMatcher` unit tests in the same file (plain `TEST(GrepMatcherTest, ...)`):
`FixedLeftmostLongest` (`-F` patterns `b`, `bc`, `a` on `abc` from 0 ->
(0,1), from 1 -> (1,3)), `BasicJoinedAndSeparate` (patterns with and
without a back-reference give the same answers), `WordShrinks` (`a*b` on
`aab ab`, NonWordNeighbours -> (0,3) then from 3 -> (4,6)), `WholeLinePerl`
(`a|ab` Perl, wholeLine, on `ab` -> (0,2)).

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='*Grep*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: rows for `egrep`, `fgrep`,
  `grep` in "The commands" table (version, treated options, documented
  exceptions: the C locale -- bytes, ASCII case, only NUL makes a file
  binary; `-T` pads standard input to 19; the binary decision per 96 KiB
  read, exact for files under 96 KiB; egrep/fgrep print no obsolescence
  warning, as Debian/Ubuntu's scripts (upstream 3.8+ prints one); recursion, context and
  colour not yet treated). A sentence on `commands/grep/GrepMatcher.h` as
  the shared matcher (rg uses it).
- Root `CLAUDE.md`: the Builtin Commands table gets `egrep`, `fgrep`,
  `grep` rows; the BuiltinCommands line of the directory tree lists them.

## Acceptance

- [ ] `grep`, `egrep`, `fgrep` registered; `--help` in the standard shape
      (hidden options absent), `--version`, `man grep` works.
- [ ] Every GNU grep 3.11 option in the table; untreated ones reported.
- [ ] All diagnostics say `grep:` (also from egrep/fgrep); usage errors
      carry the `Usage:` line; exit statuses 0/1/2 as GNU.
- [ ] Fixed strings never touch `Regex`; `-w` follows GNU's shrink-then-
      move-on loop; `-x` exact.
- [ ] Binary files as GNU in the C locale; the message on stderr.
- [ ] Reads in chunks, stops promptly, no 10 MB cap, no whole-file read.
- [ ] Files only through `context.IO()`; no POSIX headers, no `<regex>`.
- [ ] `TheInitTemplatesBuiltinsAllApplyOnceUncommented` and the full unit
      suite pass; tables in both CLAUDE.md files updated.

## Out of scope

- `-r -R -d -D --include --exclude --exclude-from --exclude-dir`, context
  (`-A -B -C -NUM`, separators), `--color`, `-Z`: `search--grep-recursive`.
- rg: `search--rg-search`.
- UTF-8 awareness (encoding errors making a file binary, multibyte `.`).
