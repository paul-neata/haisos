# Task coreutils--sort-orders: sort -g -h -M -R -V, --sort, -c/-C, -m

- Rock: coreutils
- Depends on: coreutils--sort
- Size: ~700 changed lines in ~7 files
- Plan checked against: develop @ ccb9dbe
- PR title: sort: general, human, month, version, random order; -c, -m

## Goal

`sort` gains the rest of GNU coreutils 9.4's orderings and modes, run with
`LC_ALL=C`: `-g` (general numeric), `-h` (human numeric: `2K < 1M`), `-M`
(month), `-V` (version: `foo-1.9 < foo-1.10`), `-R` (random, equal keys kept
together), `--sort=WORD`, `-c`/`-C`/`--check[=quiet|silent|diagnose-first]`
(`sort: -:3: disorder: line`, exit 1) and `-m` (merge). The version
comparison is written as a shared helper, `CompareVersion` in
`BuiltinCompare.h`, so `ls --sort=version` can reuse it later.

## Context

Read first: `develop-plan/tasks/coreutils--sort.md` (what the previous task
built, and its rules), the root `CLAUDE.md` ("Builtin Commands"),
`src/components/BuiltinCommands/CLAUDE.md`, then the code:
`commands/sort/Sort.cpp`, `commands/sort/SortKeys.h/.cpp`,
`BuiltinCompare.h/.cpp`, `BuiltinText.h/.cpp`.

What coreutils--sort provides (by exact name):
- `BuiltinText.h`: `GnuQuote`, `ArgChoice`, `ArgMatch(context, longOption,
  value, choices)` (GNU's argmatch diagnostics; caller returns 1),
  `OpenInputOperand`, `InputOpenFailure`, `BuiltinLineReader` /
  `LineReadResult`, `WriteFully`.
- `BuiltinCompare.h`: `int CompareNumeric(std::string_view, std::string_view)`.
- `SortKeys.h`: `SortKey` (with `generalNumeric humanNumeric month random
  version` flags already parsed from `-k` modifiers and inherited, but
  compared as text), `SortSettings`, `ParseSortKey`, `ApplySortModifier`,
  `SortKeyRange`, `CompareLines`, `IncompatibleOptions` (already counting
  g h M R V).
- `Sort.cpp`: option table with `-g -M -h -R -V --sort -c -C --check -m`
  marked `kBuiltinNotTreated`; inputs read fully into memory; stable sort;
  `-u`; `-o` opened after reading; exit status 2 for errors.

Rules that bite (root `CLAUDE.md`, restated): `ICurrentProcess` is the only
door out (files via `context.IO()`); GNU's output and messages byte for
byte; every option in `Options()`, the untreated ones `kBuiltinNotTreated`;
`--help` from `BuiltinHelpText`; bump the version when behaviour changes;
portable C++17, no POSIX headers, no `<regex>`; reads stop on
`StopRequested`/`kIOInterrupted`.

## Changes

### `BuiltinCompare.h` / `BuiltinCompare.cpp`

Add (each negative/zero/positive; the caller has skipped leading blanks
where noted):

```cpp
// sort -g: strtold() of each (on a NUL-terminated copy; strtold skips
// leading whitespace itself and accepts inf, nan, hex, exponents). A text
// strtold cannot convert at all (end == start) is "not a number".
// Order: not-a-number < NaN < numbers by value (-0 == +0); two
// not-a-numbers are equal, two NaNs are equal.
int CompareGeneralNumeric(std::string_view a, std::string_view b);

// sort -h (leading blanks skipped by the caller): first by unit order, then
// CompareNumeric. Unit order of a text: after an optional '-', walk digits,
// then an optional '.' and digits; if any digit walked is non-zero, the
// order is that of the byte right after the number -- K or k 1, M 2, G 3,
// T 4, P 5, E 6, Z 7, Y 8, R 9, Q 10, anything else 0 -- negated when the
// '-' was there; a number with no non-zero digit has order 0.
int CompareHumanNumeric(std::string_view a, std::string_view b);

// sort -M: each text's month -- after skipping leading blanks, its first
// three bytes compared case-insensitively (ASCII) with JAN FEB MAR APR MAY
// JUN JUL AUG SEP OCT NOV DEC: 1..12, else 0 -- compared as numbers.
int CompareMonth(std::string_view a, std::string_view b);

// GNU's version order (gnulib filenvercmp), as sort -V and ls -v use it.
int CompareVersion(std::string_view a, std::string_view b);
```

`CompareVersion`, step by step (gnulib `filevercmp.c`, 2022+; verify each
test against `sort -V` in the container):
1. Equal texts: 0. An empty text sorts before any other.
2. Leading dots: if `a` starts with `.` and `b` does not, `a` is first (and
   the reverse). If both do: `.` sorts first, then `..`, then the rest
   (compared on as below, dots included).
3. Cut each text's *suffix*: the longest tail matching
   `(\.[A-Za-z~][A-Za-z0-9~]*)*$` -- scanning as gnulib's `file_prefixlen`
   does: `prefixlen` starts at 0; for i from 0: i++ and prefixlen = i; then
   while `i + 1 < len` and `s[i] == '.'` and `s[i+1]` is an ASCII letter or
   `~`: i += 2 and skip ASCII alphanumerics and `~`; repeat until i == len.
   The suffix is `s[prefixlen..]`.
4. `r = verrevcmp(a without suffix, b without suffix)`; if `r != 0`, or
   neither text had a suffix, return `r`; else return `verrevcmp(a, b)` on
   the whole texts.
5. `verrevcmp` (dpkg's): while either text has bytes left: first, while
   either current byte is a non-digit, compare `order(a)` with `order(b)`
   and advance both; `order` of the end of a text is -1, of a digit 0, of
   an ASCII letter its byte value, of `~` -2, of any other byte its value +
   256. Then skip leading `0`s in both; compare the digit runs: the longer
   run is larger, else the first differing digit decides. Equal: continue.
Expected (`sort -V`, verified): `.`, `..`, `.a`, `foo~`, `foo`,
`foo-1.9`, `foo-1.9.tar.gz`, `foo-1.10`.

### `commands/sort/SortKeys.h` / `.cpp`

- Key comparison by kind (the leading blanks of the key text are skipped
  first for `n`, `h`, `M`; `g` relies on strtold): `numeric` ->
  `CompareNumeric`, `generalNumeric` -> `CompareGeneralNumeric`,
  `humanNumeric` -> `CompareHumanNumeric`, `month` -> `CompareMonth`,
  `version` -> `CompareVersion`, `random` -> the random comparison below,
  else text as before; `reverse` still negates.
- `SortSettings` gains `uint64_t randomSalt[2]` (set once per run in
  `Sort.cpp` from `std::random_device`).
- Random comparison: hash each key's text after the key's text transforms
  (`foldCase` -> `toupper`; `dictionary`/`ignoreNonprinting` skip bytes) with
  a 64-bit hash seeded by the salt (FNV-1a over the salt bytes then the
  key's bytes, finished with a splitmix64 step, is enough); compare the
  hashes; equal hashes -> compare the transformed bytes, so distinct keys
  never tie and equal keys always do (then the last resort applies, as GNU).

### `commands/sort/Sort.cpp`

Version `1.1.0`. In the option table, make treated (with a description):
`-g --general-numeric-sort`, `-M --month-sort`, `-h --human-numeric-sort`,
`-R --random-sort`, `--sort=WORD`, `-V --version-sort`, `-c --check[=WHEN]`
(argument `Optional`), `-C`, `-m --merge`. `--random-source`,
`--compress-program` and `--debug` stay `kBuiltinNotTreated`.

- `--sort=WORD`: `ArgMatch(context, "--sort", value, {general-numeric g,
  human-numeric h, month M, numeric n, random R, version V})` (one value per
  name); failure -> return 1. Sets the global flag like the short option.
- Check mode: `-c` (and `--check` without a value, `--check=diagnose-first`)
  is mode `c`; `-C`, `--check=quiet`, `--check=silent` mode `C`
  (`ArgMatch` choices `quiet`/`silent` one value, `diagnose-first` another:
  its listing prints `  - 'quiet', 'silent'` then `  - 'diagnose-first'`).
  Both modes given (any order) -> `options '-cC' are incompatible`, exit 2.
  With `-o` -> `options '-co' are incompatible`, exit 2 (checked before
  reading). More than one input -> `extra operand 'NAME' not allowed with
  -c` (the second operand, GnuQuote), exit 2, no Try line.
- Check: read the one input (stdin if none) line by line with
  `BuiltinLineReader`; for each line after the first, `d =
  CompareLines(previous, line, settings)`; disorder when `d > 0`, or `d >= 0`
  with `-u`. On the first disorder: in mode `c` write to stderr, raw
  (`ErrorText`), `sort: NAME:N: disorder: LINE` followed by the delimiter
  (`\n`, or `\0` with `-z`) -- NAME as given (`-` for stdin), N the 1-based
  line number of the offending line; return 1 (mode `C`: no message, 1).
  Sorted: return 0, no output. Open failures in check mode are worded
  `open failed: NAME: No such file or directory` (not `cannot read`), exit 2.
- Merge (`-m`): read every input fully (as now; `-o` may still name an
  input), then a k-way merge: repeatedly output the smallest head line
  among the inputs by `CompareLines`, the earliest input winning ties; with
  `-u`, skip a line comparing equal (keys only) to the last one output.
  Unsorted inputs are merged the same way, never re-sorted (GNU: `sort -m`
  of two copies of `b\na\n` prints `b\na\nb\na\n`).
- `Help()` notes: add that `-R` orders by a hash with a random salt (not
  GNU's MD5; `--random-source` not treated), so its order changes from run
  to run as GNU's does.

## Tests

Add to `tests/unit/components/BuiltinCommands.unittests/SortTest.cpp`
(expected outputs are GNU sort 9.4's with `LC_ALL=C`; anything not written
here: run it in the container and paste the result):

- `SortGeneralNumeric`: `-g` on `10\n9\n1e1\n0x10\nnan\n-inf\nabc\n\n` -> `\nabc\nnan\n-inf\n9\n10\n1e1\n0x10\n`.
- `SortHumanNumeric`: `-h` on `1K\n2M\n-1G\n0\n500\n1k\n-3\n` -> `-1G\n-3\n0\n500\n1K\n1k\n2M\n`.
- `SortMonth`: `-M` on `feb\nJan x\n  dec\nfoo\n` -> `foo\nJan x\nfeb\n  dec\n`.
- `SortVersion`: `-V` on `foo-1.10\nfoo-1.9\nfoo-1.9.tar.gz\n.a\n..\n.\nfoo~\nfoo\n` -> `.\n..\n.a\nfoo~\nfoo\nfoo-1.9\nfoo-1.9.tar.gz\nfoo-1.10\n`; `--version-sort` the same; `--version` still prints the version.
- `SortVersionKey`: `-t- -k2V` on `a-1.10\nb-1.9\n` -> `b-1.9\na-1.10\n`.
- `SortRandomGroupsEqualKeys`: `-R` on `a\nb\na\nc\nb\na\n` -> six lines, a permutation of the input, each value's copies adjacent; `-R -u` -> three lines.
- `SortSortWord`: `--sort=numeric` == `-n`; `--sort=num` (prefix) works; `--sort=foo` -> stderr `sort: invalid argument 'foo' for '--sort'\nValid arguments are:\n  - 'general-numeric'\n  - 'human-numeric'\n  - 'month'\n  - 'numeric'\n  - 'random'\n  - 'version'\nTry 'sort --help' for more information.\n`, exit 1; `--sort=h -n` -> `sort: options '-hn' are incompatible\n`, 2.
- `SortIncompatibleOrders`: `-gn` -> `'-gn'`; `-Mn` -> `'-Mn'`; `-nR` -> `'-nR'`; `-i -g` -> `'-gi'`; `-fgn` -> `'-fgn'`; `-k1,1Vn` -> `'-nV'`; all exit 2.
- `SortCheck`: input `a\nb\n\nc` with `-c -` -> stderr `sort: -:3: disorder: \n`, exit 1; sorted input -> nothing, 0; `-C` on disorder -> no output, 1; `--check=silent` -> like `-C`; `-cu` on `a\na\n` -> `sort: -:2: disorder: a\n`, 1; `-c -f` on `a\nA\n` -> `sort: -:2: disorder: A\n` (last resort); `-c -k2n` on `x 2\ny 10\n` -> 0, and `-c -k2` -> `sort: -:2: disorder: y 10\n`, 1; `-cz` on `b\0a\0` -> stderr `sort: -:2: disorder: a\0`; with a file operand the name is the operand (`sort: f.txt:2: disorder: a`).
- `SortCheckErrors`: `-c f g` -> `sort: extra operand 'g' not allowed with -c\n`, 2; `-c -o z f` -> `sort: options '-co' are incompatible\n`, 2; `-C -c f` -> `sort: options '-cC' are incompatible\n`, 2; `--check=foo` -> the argmatch listing with `  - 'quiet', 'silent'\n  - 'diagnose-first'\n`, 1; `-c nofile` -> `sort: open failed: nofile: No such file or directory\n`, 2.
- `SortMerge`: files `x` = `a\nb\n`, `y` = `a\nc\n`: `-m x y` -> `a\na\nb\nc\n`; `-mu x y` -> `a\nb\nc\n`; `-m` of two copies of `b\na\n` -> `b\na\nb\na\n`; `-m x nofile` -> `sort: cannot read: nofile: No such file or directory\n`, 2.
- `BuiltinTextTest.cpp`: `CompareVersionCases` (the list above pairwise;
  `1.02` vs `1.2` -> 0; `a~` < `a`; `` < `a`), `CompareHumanCases`,
  `CompareGeneralCases`, `CompareMonthCases`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Sort*:*Compare*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the `sort` row -- version
  1.1.0, treated now includes `-g -h -M -R -V --sort -c -C --check -m`;
  exceptions: `-R`'s hash is not GNU's MD5 (`--random-source` not treated),
  `--debug`/`--compress-program` not treated; mention `CompareVersion`
  (shareable with `ls -v`) in the helpers paragraph.
- Root `CLAUDE.md`: the `sort` row lists the new orderings, `-c`, `-m`.

## Acceptance

- [ ] `-g -h -M -V -R` and `--sort=WORD` order exactly as GNU sort 9.4
      with `LC_ALL=C` on every test; incompatible combinations rejected
      with GNU's messages.
- [ ] `-c`/`-C`/`--check` print GNU's disorder line (raw name, line number,
      delimiter) and exit 1; their errors exit 2 (argmatch 1).
- [ ] `-m` merges without re-sorting, ties to the earlier input, `-u` works.
- [ ] `CompareVersion`, `CompareHumanNumeric`, `CompareGeneralNumeric`,
      `CompareMonth` are in `BuiltinCompare.h` with the exact signatures.
- [ ] Version bumped to 1.1.0; help still has the standard shape.
- [ ] All tests pass on Linux.

## Out of scope

- GNU's MD5-based `-R` and `--random-source`; `--debug`;
  `--compress-program` (stay not treated).
- `ls --sort=version` / `ls -v` (a later change may reuse `CompareVersion`).
- Locales other than C.
