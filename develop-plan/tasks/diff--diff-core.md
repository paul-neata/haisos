# Task diff--diff-core: the diff builtin -- Myers' algorithm, normal, unified, context and ed output

- Rock: diff
- Depends on: coreutils--sort (`BuiltinText.h`: `OpenInputOperand`, `InputOpenFailure`, `OpenFailureText`, `GnuQuote`), coreutils--date (`FormatDateTime` in `BuiltinDate.h`) -- both merged on develop
- Size: ~1050 changed lines in ~7 files (at the upper limit: see "Out of scope" for what was left to keep it there)
- Plan checked against: develop @ e62c40e
- PR title: Add the diff builtin: Myers' algorithm, normal/-u/-c/-e output

## Goal

`diff FILE1 FILE2` prints what GNU diffutils 3.10 prints in the C locale
(`LC_ALL=C`), in the normal format, unified (`-u`, `-U NUM`,
`--unified[=NUM]`), context (`-c`, `-C NUM`, `--context[=NUM]`) and ed (`-e`)
formats; `-q`, `-s`, `--label`, `-i -w -b -E -Z -B --strip-trailing-cr
--tabsize -a -t -T --suppress-blank-empty -d --horizon-lines --normal`;
`\ No newline at end of file`; header times from `Stat`; `-` for standard
input; binary files; exit status 0 (same), 1 (different), 2 (trouble). This is
the `diff -u a b > p` half of acceptance scenario 2 (goal.md); the other half
is diff--patch-core.

The formats, headers, messages and exit statuses are matched byte for byte.
The hunks -- which lines are reported as changed -- are a **minimal** diff
computed with Myers' published algorithm (1986) plus a few placement rules,
each stated below as a property of the output and checked against the host's
GNU diff. These rules reproduce GNU's `diff -d` output exactly in every case
checked (nearly 2,000 inputs: random small-alphabet files, real source files
with random edits, `-i`, horizons), and GNU's default output on ordinary inputs:
identical on all 600 real-source cases with edits of one kind (lines changed,
inserted, deleted, blank lines and braces added, blocks moved or duplicated)
and on 346 of 350 with several kinds mixed. GNU's default output also uses
undocumented speed heuristics, so on some inputs Haisos places changes
differently -- see "Where Haisos's hunks may differ from GNU's".

Directories (`-r`, a directory operand) are diff--diff-recursive's, which
calls into what this task builds -- so build the pieces as described in
"Changes", with the entry points it names.

## Context

**Clean-room rule (the user's, above every other rule; root `CLAUDE.md`):**
never read, copy, port, translate or paraphrase another program's source --
not diffutils', not gnulib's, not any other diff's, whatever the licence, and
not from memory either. Everything here is written from scratch from this
plan, from Myers' paper ("An O(ND) Difference Algorithm and Its Variations",
Algorithmica 1986), from the GNU diffutils manual and man page, from POSIX,
and from the real program's observed output: where something is unclear, run
`LC_ALL=C TZ=UTC diff ...` and look at what it prints, never at how it is
written. Comments name the behaviour matched, not another program's
functions.

Read first: the root `CLAUDE.md` ("Clean-room rule", "Builtin Commands",
"Security: ICurrentProcess is the only door out"),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`
(`BuiltinContext` -- `Error` prepends `diff: `, `ErrorText` writes as given --
`ParseBuiltinArgs`, `BuiltinOption` with its `bool hidden`,
`kBuiltinOptionVersion`, `BuiltinHelpText`, `BuiltinVersionText`),
`commands/cmp/Cmp.cpp` (diffutils' conventions, which diff shares: no
`BeginBuiltin`, the Try line prefixed `diff: `, `-v` as `--version`, usage
errors exit 2 -- copy its `Run` opening and `UsageError` shape), and
`commands/cat/Cat.cpp` (reading in chunks with `StopRequested()` and
`kIOInterrupted`).

What earlier tasks provide (on develop):
- `src/components/BuiltinCommands/BuiltinText.h` (coreutils--sort):
  `std::shared_ptr<IFileDescriptor> OpenInputOperand(BuiltinContext&, const std::string& name, InputOpenFailure& failure);`
  (`-` is descriptor 0; a name is `Stat`-ed first: `InputOpenFailure { None,
  Missing, Directory, Denied, BadDescriptor }`), `OpenFailureText(failure)`
  ("No such file or directory", "Is a directory", ...), `GnuQuote`.
- `src/components/BuiltinCommands/BuiltinDate.h` (coreutils--date):
  `std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);`
  -- GNU date's strftime in the C locale, `%N`, `%z`, `%e`, `%T` included,
  local time when `utc` is false.
- `src/components/Filesystem/FilesystemUtils.h`: `CurrentFileDateTime()`.
  (Its `ReadWholeFile`/`ReadWholeDescriptor` cap at 10 MB and do not stop on
  `StopRequested()`: diff reads with its own loop, below.)

## Changes

Rules that bite (root `CLAUDE.md`):
- Clean room (above).
- `ICurrentProcess` is the only door out: files are opened through
  `context.IO()` (via `OpenInputOperand`) and stat-ed with `IO().Stat`; no
  `IFileSystem` or `IHaisosOS` is held.
- Builtin rules: GNU's output and messages byte for byte; every GNU diff
  option in `Options()` (untreated ones `kBuiltinNotTreated`, reported);
  `--help` from `BuiltinHelpText`; `--version`; `man diff` is the help;
  registered in `CreateStandardBuiltinCommands()` (which also puts it in the
  `haisos --init` template); sources in the CMakeLists.
- Portable C++17 (Linux, MSVC, WASM): no POSIX headers, no `<regex>`.
- Stops promptly on `TriggerStop()`: check `context.StopRequested()` in every
  read loop and once per edit-distance step of the comparison; treat
  `kIOInterrupted` as a quiet stop (exit status 2 is fine; nothing more
  printed).

### New `src/components/BuiltinCommands/commands/diff/DiffEngine.h` / `.cpp`

Pure functions over bytes, no I/O, namespace `Haisos`:

```cpp
// How lines are compared: one mode (the strongest given wins, see below).
enum class DiffWhiteSpace { None, TabExpansion, TrailingSpace, SpaceChange, AllSpace };

struct DiffLineOptions {
    DiffWhiteSpace whiteSpace = DiffWhiteSpace::None;
    bool ignoreCase = false;
    size_t tabSize = 8;
};

// One file's text: its bytes split into lines. lineStarts has lineCount + 1
// entries (the last is bytes.size()); line i is bytes[lineStarts[i],
// lineStarts[i+1]), its '\n' included -- except an incomplete last line.
struct DiffText {
    std::string bytes;
    std::vector<size_t> lineStarts;
    bool missingNewline = false;   // the last line has no '\n'
    size_t LineCount() const { return lineStarts.size() - 1; }
};

// Splits |bytes| into lines; with stripTrailingCr, every "\r\n" becomes "\n"
// first. When !robustOutput (ed), a missing final newline is appended
// (missingNewline still set, for the warning).
DiffText MakeDiffText(std::string bytes, bool stripTrailingCr, bool robustOutput);

// One change: |deleted| lines of file 0 from line0, |inserted| lines of
// file 1 from line1 (0-based). With deleted == 0, line0 is the line before
// which the insertion goes; likewise line1 with inserted == 0.
struct DiffChange { int64_t line0, line1, deleted, inserted; };

struct DiffAnalysisOptions {
    DiffLineOptions lines;
    int64_t horizonLines = 0;   // see "Fixed ends"
    bool robustOutput = true;   // false for -e
};

// The changes, in file order, or empty when the files are the same under
// the options. |stop| is checked once per edit-distance step; when it
// becomes true the result is empty and |stopped| is set.
std::vector<DiffChange> ComputeDiff(const DiffText& a, const DiffText& b,
                                    const DiffAnalysisOptions& options,
                                    const std::function<bool()>& stop, bool& stopped);
```

#### Which lines are equal

Two lines are equal when their *keys* are equal. Give each distinct key a
number (one `std::unordered_map<std::string, int64_t>` shared by both files)
and compare numbers from then on. The key is the line without its `'\n'`,
normalised for the mode (bytes; whitespace is C's `isspace`: space `\t \n \v
\f \r`):
- `None`: the text as is.
- `AllSpace` (`-w`): every whitespace byte removed.
- `SpaceChange` (`-b`): each run of whitespace becomes one space; a run
  reaching the end of the line is removed.
- `TrailingSpace` (`-Z`): a final run of whitespace removed.
- `TabExpansion` (`-E`): each tab replaced by the spaces up to the next
  multiple of `tabSize` (columns from 0, every byte one column) -- up to the
  first `\b` or `\r` in the line; from there on the rest of the line is kept
  as is. (Observed: `a<TAB>b` equals `a` + 7 spaces + `b`, `a<TAB>b\b`
  equals `a` + 7 spaces + `b\b`, but after a `\b` or `\r` no number of spaces
  ever equals a tab.)
- `ignoreCase` (`-i`), with any mode: every byte through ASCII `tolower`.

The mode is the strongest given, whatever the order: `-w` over `-b` over `-Z`
over `-E`. Note `-E -Z` is `-Z` alone (observed with GNU diff 3.10:
`a<TAB>b` and `a` + 7 spaces + `b` are equal under `-E`, different under
`-E -Z`).

An incomplete last line (no `'\n'`) equals only the other file's incomplete
last line, in every style but ed -- add a marker to its key -- unless the
mode is `TrailingSpace` or stronger, where it is keyed like any line
(observed: `a\nb` vs `a\nb\n` differ under `-E` and `-i`, are the same under
`-Z`, `-b`, `-w`).

#### Fixed ends (the horizon)

`diff --help` documents `--horizon-lines=NUM` as "keep NUM lines of the
common prefix and suffix": the lines both files share at their start and end
are left out of the comparison, except the NUM nearest the differences. In
Haisos: let H = `horizonLines` -- the `--horizon-lines` value,
raised to the context length for `-u`/`-c` (observed: `diff -u` and `diff`
place changes differently, below).
- P = the number of leading lines byte-identical in both files (the whole
  line, `'\n'` included -- byte-identical, not equal under the options: this
  matters with `-i`/`-b`/...). The first `max(0, P - H)` lines are unchanged.
- S = the number of trailing lines byte-identical in both files, counted only
  among the lines after those fixed leading ones. The last `max(0, S - H)`
  lines are unchanged.
- Everything below works on the lines between, the *region* (`[pre, n0 -
  suf)` of file 0, `[pre, n1 - suf)` of file 1). A change is never moved into
  the fixed ends.

Example (`DiffHorizonMovesChanges`): `h1` = `b\na\n`, `h2` = `a\nb\na\na\n`.
`diff h1 h2` -> `0a1\n> a\n1a3\n> a\n` (the last `a` is fixed, so the second
insertion cannot move below it); `diff -u` and `--horizon-lines=3` print the
second insertion one line lower (`2a4`).

#### The comparison: Myers' linear-space algorithm

Compare the region's key sequences A (file 0) and B (file 1) with the
divide-and-conquer form of Myers' algorithm (the paper, section 4b: find the
"middle snake" by running the greedy search forward from the top-left corner
and backward from the bottom-right one at once, then recurse on the two
sides). The paper leaves some choices open; each changes which of several
equally short scripts comes out, so make exactly these (they are what
reproduces GNU's output -- checked by running it, not by reading it):

- **One box** is `A[x0, x1)` against `B[y0, y1)`. First, while the first lines
  are equal, they are matched and the box shrinks from the top-left; then
  likewise from the bottom-right. If either side is then empty, every line
  left on the other side is deleted (A) or inserted (B). Otherwise find the
  split point below and handle the two boxes it makes (top-left part first or
  not -- the result is the same; use an explicit stack, not recursion, so a
  deep split cannot overflow the stack).
- **Coordinates**: N = x1 - x0, M = y1 - y0, positions relative to the box,
  diagonal k = x - y, Delta = N - M. The forward search keeps, per diagonal,
  the furthest x reached (Vf); the backward search is the same search run on
  both sequences read from their ends: u = lines of A consumed from the end,
  v = of B, its diagonal k' = u - v, its table Vb of the furthest u.
- **One step D** (D = 0, 1, 2, ...), in both directions the same rule: for
  each diagonal k from -D to D (step 2), the two ways in are *down* from k+1
  (x = V[k+1], an insertion) and *right* from k-1 (x = V[k-1] + 1, a
  deletion). A way is usable when its diagonal has a value from step D-1 and
  it stays inside the box (down: x - k <= M; right: x <= N). With both
  usable, take down when k == -D, or when k != D and V[k-1] < V[k+1]; else
  right (the paper's rule). With one usable, take it; with none, the
  diagonal has no value at this step (skip it). Skip diagonals outside
  `[-M, N]`. At D = 0, k = 0 starts at x = 0. Then follow the snake: while
  x < N and y < M and the next lines are equal, x++ and y++. Store V[k] = x.
- **Order**: the forward step visits its diagonals from k = D down to k = -D;
  the backward step visits k' from -D up to D (both: from the most deletions
  to the most insertions, in the box's own coordinates). The first overlap
  found ends the search.
- **Overlap** (the paper's test): with Delta odd it is checked in the forward
  step, after storing Vf[k]: the backward diagonal k' = Delta - k, if it has a
  value from step D-1 and |k'| <= D-1, overlaps when Vf[k] + Vb[k'] >= N; the
  split point is then the forward snake's end (x, x - k). With Delta even it
  is checked in the backward step, after storing Vb[k']: the forward diagonal
  k = Delta - k', if it has a value from step D and |k| <= D, overlaps when
  Vf[k] + Vb[k'] >= N; the split point is then the backward snake's end in
  box coordinates, (N - u, M - v). The split point always matters: split
  exactly there, not at the snake's other end.
- **Changed flags**: every line of a region gets a flag, changed or not.
- **Cost**: O((N+M) D) time, O(N+M) memory per box (the two tables), so a
  file of 30,000 lines that differs on most lines takes seconds; check `stop`
  once per step D. Haisos has no cut-off: its diff is always minimal (GNU's
  default gives up on minimality for very large differences; `-d` asks it
  not to).

Examples (all verified on GNU diff 3.10, default and `-d` alike):
- `a\nb\nc\n` vs `c\nb\nb\n` (two scripts of 4 lines are possible: keep `c`
  or keep `b`) -> `1,2d0\n< a\n< b\n3a2,3\n> b\n> b\n`.
- `d1` = `.\na\na\na\n.\n.\n`, `d2` = `a\nc\nc\nc\na\nd\n` ->
  `1,2d0\n< .\n< a\n3a2,4\n> c\n> c\n> c\n5,6c6\n< .\n< .\n---\n> d\n`
  (GNU's `-d` output; GNU's default prints another 8-line script here -- see
  "Where Haisos's hunks may differ").

#### Placing the blocks

After the comparison, a *block* is a maximal run of consecutive changed lines
of one file. A block whose first line equals the line just after it (or whose
last line equals the line just before it) can slide: moved one line, the
file still reads the same. GNU's output, observed, places blocks by these
rules, applied to file 0's blocks, top to bottom, then to file 1's (against
file 0's placed flags), all inside the region:
1. A block sits at the **lowest** position it can slide to. Sliding may make
   it touch another block of the same file; they then merge and slide as one.
2. Except: if at some position it can reach, the block *lines up* with a
   change of the other file -- the two would be printed as one hunk (a `c`)
   -- it sits at the lowest such position instead.

One way to get there, per block: (a) slide it up while the line above it
equals its last line, merging with any block it reaches; (b) slide it down
while the line below it equals its first line, merging likewise, and note
every position (the top one included) where it lines up; (c) if (a) or (b)
merged anything, repeat from (a); (d) move it back up to the lowest position
noted in the last (b), if any. Lining up, at a position where the block ends
before line e: pair line e with the line j of the other file that has as
many unchanged lines before it in the region (and is not itself changed);
the block lines up when the other file's line j-1 is changed. Keep j in step
as the block moves (recounting from the region start each time is
quadratic).

Examples (verified on GNU diff 3.10):
- Lowest: `a\nb\n` vs `a\nb\na\nb\n` -> `2a3,4`; with `-u` the
  `DiffHorizonMovesChanges` pair above (`+a` after ` b\n a`, not before
  ` a`).
- Lining up: `a\na\nb\n` vs `c\na\nb\nc\n` -> `1c1\n< a\n---\n> c\n3a4\n> c\n`
  (the deleted `a` could be line 1 or 2; at line 1 it forms one change with
  the inserted `c`; by rule 1 alone it would be `0a1`, `2d2`, `3a4`).
- Both: `{\n  a;\n}\n{\n  b;\n}\n` vs `{\n  a;\n}\n{\n  x;\n}\n{\n  b;\n}\n`
  -> `4a5,7\n>   x;\n> }\n> {\n`; `x\ny\nx\ny\n` vs `x\ny\n` -> `3,4d2`.

#### The change list

Walk both files' flags together from the start: where either is changed,
the change is the run of changed lines in file 0 and the run in file 1 from
there; otherwise both step one line. Line numbers in `DiffChange` are
whole-file indexes. (`-B` is applied at output time, below.)

#### Where Haisos's hunks may differ from GNU's

GNU's default output (without `-d`) is shaped by speed heuristics its manual
does not document ("usually good enough", the manual says of its
near-minimal output). Haisos does not reproduce them: it prints the minimal
diff above, which is GNU's `diff -d` output. So `diff` and `diff -d` print
the same in Haisos, and the requirement where GNU's default differs is: a
correct, minimal diff in the same format. Observed, they differ:
- on inputs where the changed region has lines that repeat many times, or
  several kinds of edits close together (random files over 3-16 distinct
  lines: 3-13 % of cases; real source with several mixed edits: about 1 %,
  e.g. moved blocks near repeated `}`/blank/`return 0;` lines) -- both
  outputs have the same number of changed lines, placed differently;
- on very large, very different inputs (thousands of changed lines), where
  GNU's default gives up on minimality and prints more changed lines (two
  30,000-line random files over 41 distinct lines: GNU 43,992 `<`/`>` lines,
  `diff -d` and Haisos 43,924) -- and where Haisos takes longer.

Tests therefore compare with GNU byte for byte only on inputs whose output is
the same with and without `-d` (as every example in this plan), or stating
that the expected text is GNU's `-d` output.

This is a documented exception (root `CLAUDE.md`, builtin rules): in the help
notes (see "Diff.h / Diff.cpp") and in the `BuiltinCommands/CLAUDE.md` row.

### New `src/components/BuiltinCommands/commands/diff/DiffOutput.h` / `.cpp`

```cpp
enum class DiffStyle { Normal, Unified, Context, Ed };

struct DiffOutputOptions {
    DiffStyle style = DiffStyle::Normal;
    int64_t context = 3;               // -u/-c/-U/-C
    bool initialTab = false;           // -T
    bool expandTabs = false;           // -t
    bool suppressBlankEmpty = false;   // --suppress-blank-empty
    bool ignoreBlankLines = false;     // -B
    size_t tabSize = 8;
    DiffWhiteSpace whiteSpace = DiffWhiteSpace::None; // for -B's blank test
};

// What a header line shows for one file: its label (--label), or its name
// and modification time.
struct DiffHeaderFile {
    std::string name;                  // as given/joined, before quoting
    std::optional<std::string> label;
    FileDateTime modificationTime;
};

// True when some change survives -B (else the files count as the same).
bool DiffHasRealChanges(const std::vector<DiffChange>&, const DiffText& a, const DiffText& b, const DiffOutputOptions&);

// The whole output for one pair of files, headers included; empty when
// every change is ignorable under -B.
std::string FormatDiff(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                       const DiffHeaderFile& h0, const DiffHeaderFile& h1, const DiffOutputOptions& options);
```

Exact formats (`n` lines are 1-based on output), all observed with GNU diff
3.10 (`LC_ALL=C TZ=UTC`):
- **Hunks**: normal and ed print one hunk per change. Unified and context
  group changes in file order: a change joins the hunk being built when the
  unchanged lines between it and the hunk's last change are fewer than
  `2 * context + 1` -- with `-B`, fewer than `context` when the joining
  change is ignorable. Each hunk shows `context` lines before its first and
  after its last change, clamped to the file.
- **`-B`**: a change is ignorable when every line it deletes or inserts is
  blank: empty, or -- when the mode is `TrailingSpace` or stronger (`-Z`,
  `-b`, `-w`) -- only whitespace (`a\n  \nb\n` vs `a\nb\n`: `-B` prints
  `2d1`, `-B -Z`/`-B -b`/`-B -w`/`-B -E -Z` print nothing). Normal and ed
  output leave ignorable changes out; a unified/context hunk made only of
  ignorable changes is left out, and one that also holds a real change shows
  them all (marks as usual). Nothing left -> the files count as the same
  (status 0; `-s` reports them identical; `-q` prints nothing). Observed
  with `-u` (context 3) on `R1\n1\n2\n<blank>\n3\n...` vs `Q1\n1\n2\n3\n...`:
  a blank-line deletion 2 lines after the `R1` change joins its hunk, 3
  lines after it does not; 4 lines *before* a real change it joins (`< 7`).
- **Normal**: a range of one line is `n`, of several `a,b`, of none the line
  before it; `<range0><letter><range1>` with letter `a` (only inserted), `d`
  (only deleted), `c` (both); deleted lines `< text`, then `---` (only for
  `c`), then inserted lines `> text`.
- **Unified**: header `--- NAME\tTIME` and `+++ NAME\tTIME` (or `--- LABEL`,
  no tab, when labelled), TIME = `FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z",
  mtime, false)` (`2024-01-02 03:04:05.123456789 +0000`). Hunk line `@@ -R0
  +R1 @@` where a range of one line is `n`, of none `n,0` (n = the line
  *before* it, 0 at the start: `@@ -0,0 +1,3 @@` -- patch relies on this),
  else `first,count`. Then context lines `' ' + text`, and per change its
  deleted lines `-text` then its inserted lines `+text`.
- **Context**: header `*** NAME\tTIME` and `--- NAME\tTIME` with TIME =
  `FormatDateTime("%a %b %e %T %Y", mtime, false)` (`Tue Jan  2 03:04:05
  2024`). Each hunk: `***************`, `*** R0 ****`, then -- only if it
  deletes something -- every line of the file-0 range marked `  ` (two
  spaces), `- ` (deleted) or `! ` (deleted by a change that also inserts);
  `--- R1 ----`, then -- only if it inserts -- the file-1 range marked `  `,
  `+ `, `! `. A context range is `a,b`, or just `b` when it has one line or
  none (b then the line before it: `*** 0 ****`).
- **Ed** (`-e`): the changes in *reverse* order (last first): `R0a`, `R0d`
  or `R0c` (normal ranges), then for `a`/`c` the inserted lines raw, then
  `.`. A line that is exactly `.` is written `..`, then `.`, `s/.//`, and
  insertion resumes with `a` before the next line (only if one follows; a
  closing `.` is printed only if insert mode is on at the end). Ed is not a
  robust style: a file missing its final newline is compared with the
  newline appended, and after the output `diff: NAME: No newline at end of
  file\n\n` (note the empty line) is written to stderr per such file, and
  the exit status is 2.
- **Marks with -T / --suppress-blank-empty**: normal lines are `<`/`>` then
  a space (or a tab with `-T`) then the text; unified `-`/`+`/' ' then the
  text directly (with `-T`: the mark, a tab, the text -- for context lines
  the tab replaces the space); context marks are two characters (`- `, `! `,
  `+ `, `  `), with `-T` the mark's first character then a tab. With
  `--suppress-blank-empty`, an empty line gets no trailing blank after its
  mark (normal `<`/`>`, unified `-`/`+`/nothing for context, context
  `-`/`!`/`+`/nothing).
- **`-t`**: tabs in the text expanded to spaces, columns counted from the
  start of the text (not of the output line); `\b` is written and moves back
  a column when not at column 0; `\r` is written and resets the column to 0
  -- and in normal and context output, when more text follows it on the
  line, the line's mark is written again after it (`< a\r<` + 9 spaces +
  `c` for `a\r\tc`; with `-T` `<\ta\r<\t` + 8 spaces + `c`; context `! a\r! `
  + 8 spaces + `c`; unified writes no mark again: `-a\r` + 8 spaces + `c`).
- **`\ No newline at end of file`**: in normal, unified and context output,
  after an incomplete last line is printed: `\n\\ No newline at end of
  file\n` (the line's missing newline, then the marker line).
- **Names in headers**: a name holding a space, `"`, `\`, a byte below 0x20
  or a byte 0x80 or above is written in double quotes, with `\a \b \t \n \v
  \f \r \" \\` and the other such bytes as 3-digit octal (`"x\001y"`,
  `"\303\251"`); 0x7f and everything else as is (`it's`, `a$b`). Labels are
  written as given, never escaped. The `-q`/`-s`/`Binary files` messages
  use the plain names (`Files sp ace and b differ`).

### New `src/components/BuiltinCommands/commands/diff/Diff.h` / `Diff.cpp`

`CreateDiffCommand()`, name `diff`, version `1.0.0`. Help: summary "compare
files line by line", usage `diff [OPTION]... FILES`, notes: "FILES are
'FILE1 FILE2'. If a FILE is '-', read standard input. Exit status is 0 if
inputs are the same, 1 if different, 2 if trouble." then the exception, in
two lines: "The changes are always a minimal set, as with -d (Myers' algorithm);
GNU's default output may place some of them differently, or list more on very
large inputs."

`Diff.h` exposes, for diff--diff-recursive:

```cpp
struct DiffSettings {
    DiffAnalysisOptions analysis;
    DiffOutputOptions output;
    bool brief = false;                 // -q
    bool reportIdentical = false;       // -s
    bool text = false;                  // -a
    std::optional<std::string> label0, label1;
};

// Compares two non-directory operands -- "-" is standard input -- and prints
// the result; returns 0 same, 1 different, 2 trouble (already reported).
// |header|, when not empty, is printed (with a newline) before any output
// for this pair -- the recursive task's "diff -r a/x b/x" line.
int DiffTwoFiles(BuiltinContext& context, const DiffSettings& settings,
                 const std::string& name0, const std::string& name1, const std::string& header);
```

`Options()` -- every option of GNU diff 3.10 (`diff --help`), with these ids
(treated) or `kBuiltinNotTreated`:
- treated: `-a/--text`, `-b/--ignore-space-change`, `-B/--ignore-blank-lines`,
  `-c` (context 3), `-C NUM` (Required) and `--context[=NUM]` (Optional;
  separate table entries, one id), `-d/--minimal` (accepted, changes nothing:
  the diff is always minimal), `-e/--ed`, `-E/--ignore-tab-expansion`,
  `-i/--ignore-case`, `-L/--label LABEL` (Required), `--normal`,
  `-q/--brief`, `-s/--report-identical-files`, `-t/--expand-tabs`,
  `-T/--initial-tab`, `-u` (context 3), `-U NUM` and `--unified[=NUM]` (as
  `-C`), `-w/--ignore-all-space`, `-Z/--ignore-trailing-space`,
  `--strip-trailing-cr`, `--tabsize=NUM`, `--suppress-blank-empty`,
  `--horizon-lines=NUM`, `--binary`, `-h` and `--inhibit-hunk-merge`
  (accepted with no effect, as GNU on Linux), `-v` (`kBuiltinOptionVersion`,
  "output version information").
- not treated here, treated by diff--diff-recursive (keep `kBuiltinNotTreated`
  for now): `-r/--recursive`, `-N/--new-file`, `--unidirectional-new-file`
  (GNU 3.10 shows no short `-P` in `--help` but accepts it: list `{'P',
  "unidirectional-new-file"}`), `-x/--exclude PAT`, `-X/--exclude-from FILE`,
  `-S/--starting-file FILE`, `--from-file FILE`, `--to-file FILE`,
  `--no-dereference`, `--ignore-file-name-case`, `--no-ignore-file-name-case`.
- not treated: `-y/--side-by-side`, `-W/--width NUM`, `--left-column`,
  `--suppress-common-lines`, `-f/--forward-ed`, `-n/--rcs`, `-D/--ifdef
  NAME`, `--line-format`, `--old-line-format`, `--new-line-format`,
  `--unchanged-line-format`, `--old-group-format`, `--new-group-format`,
  `--unchanged-group-format`, `--changed-group-format`, `-F/--show-function-line RE`,
  `-p/--show-c-function`, `-I/--ignore-matching-lines RE`,
  `-H/--speed-large-files`, `-l/--paginate`, `--color[=WHEN]`,
  `--palette=PALETTE`, `--sdiff-merge-assist`; and `-0` ... `-9` (GNU's
  obsolete `-NUM` context), each `hidden`.

`Run`:
1. Do **not** use `BeginBuiltin` (diffutils prefixes its Try line, as cmp):
   `ParseBuiltinArgs`; on an error `context.Error(error)` then
   `context.Error("Try 'diff --help' for more information.")`, exit 2. Then
   `--help`/`-v`/`--version` (`BuiltinHelpText`/`BuiltinVersionText`), then
   `ReportNotTreated`.
2. Options in order: the whitespace mode is the strongest of `-w`, `-b`,
   `-Z`, `-E` given (see "Which lines are equal"). `-u`/`-c` set the style
   and `context = max(context, 3)`; `-U N`/`-C N`/`--unified=N`/`--context=N`
   set the style and `context = max(context, N)` (no value: 3); N must be a
   non-negative decimal, else `invalid context length 'N'` + Try, exit 2.
   Two different styles (`-e`, `--normal`, unified, context):
   `conflicting output style options` + Try, exit 2. `-L` fills label0 then
   label1; a third: `too many file label options` (no Try), exit 2.
   `--tabsize`: a positive decimal, else `invalid tabsize 'X'` + Try, 2.
   `--horizon-lines`: non-negative, else `invalid horizon length 'X'` + Try,
   2. The values are printed in plain `'...'` (not `GnuQuote`).
3. `horizonLines = max(horizon, context)` for unified/context, `horizon`
   otherwise. `robustOutput = style != Ed`.
4. Operands: fewer than two -> `missing operand after 'X'` (X the last
   argument, or `diff` when there is none) + Try, 2; more than two -> `extra
   operand 'Z'` (the third) + Try, 2. Then `return DiffTwoFiles(context,
   settings, op0, op1, "")`.

`DiffTwoFiles`:
1. Each operand: `-` is standard input (read once even when both are `-`:
   then they are identical); otherwise `OpenInputOperand` -- `Missing` ->
   `diff: NAME: No such file or directory` (stderr, unquoted), status 2
   (report both operands before returning). A directory operand -> for now
   `diff: NAME: Is a directory`, 2 (diff--diff-recursive replaces this).
2. Read each whole (64 KiB reads, no size cap, `StopRequested()` and
   `kIOInterrupted` as in cat; a read error -> `diff: NAME: Input/output
   error`, 2).
3. Binary: unless `-a`, a file holding a NUL byte in its first 4096 bytes is
   binary; if either is, compare bytes: equal -> as identical below;
   different -> `Binary files NAME0 and NAME1 differ\n` on stdout (labels
   in place of names when given), status 1. Nothing else is printed.
4. Text: `MakeDiffText` both, `ComputeDiff`. No changes (or none surviving
   `-B`) -> same.
5. `-q` and different -> `Files NAME0 and NAME1 differ\n` (labels when
   given), 1. Different -> header (if any) then `FormatDiff`, 1.
6. Same -> with `-s`: `Files NAME0 and NAME1 are identical\n`; status 0.
7. Header times: `Stat`'s `modificationTime`; for standard input, the
   current time (`CurrentFileDateTime()`), as GNU prints it.

### Registration and build

- `BuiltinCommandList.h`: declare `CreateDiffCommand()` after
  `CreateDateCommand()`; add `CreateDiffCommand()` after `CreateDateCommand(),`
  in `CreateStandardBuiltinCommands()` (alphabetical: `date`, `diff`,
  `dirname`).
- `src/components/BuiltinCommands/CMakeLists.txt`: after
  `commands/date/Date.cpp`, add `commands/diff/Diff.cpp`,
  `commands/diff/DiffEngine.cpp`, `commands/diff/DiffOutput.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/DiffTest.cpp` (add it
after `DateTest.cpp` in that directory's `CMakeLists.txt`),
`TEST_F(BuiltinCommandsTest, Diff...)` (fixture in `BuiltinCommandsFixture.h`),
files written with `WriteFile` under `/d` (create it with
`root->CreateDirectory("/d", kDirMode)`), run with `RunCaptured("diff",
{...}, input, "/d")` (returns `out`, `err`, `status`). Every expected output
below was printed by GNU diff 3.10 (`LC_ALL=C`), and is the same with and
without `-d` unless said otherwise; headers are avoided with `-L`/`--label`,
except where a test checks them. `\0` means a NUL byte.

- `DiffNormalChangeAndMissingNewline`: `a` = `a\nb\nc\n`, `b` = `a\nB\nc\nd`
  -> out `2c2\n< b\n---\n> B\n3a4\n> d\n\\ No newline at end of file\n`,
  status 1.
- `DiffIdenticalIsSilent`: `diff a a` -> out empty, 0; `-s a a` -> `Files a
  and a are identical\n`, 0.
- `DiffUnifiedWithLabels`: `-u -L one -L two a b` (files above) -> `--- one\n+++ two\n@@ -1,3 +1,4 @@\n a\n-b\n+B\n c\n+d\n\\ No newline at end of file\n`.
- `DiffUnifiedZeroContextRanges`: `-U0 -L a -L b a b` -> `--- a\n+++ b\n@@ -2 +2 @@\n-b\n+B\n@@ -3,0 +4 @@\n+d\n\\ No newline at end of file\n`;
  an empty file `e` vs `a`: `-u -L x -L y e a` -> `--- x\n+++ y\n@@ -0,0 +1,3 @@\n+a\n+b\n+c\n`.
- `DiffUnifiedHeaderTimes`: `diff -u a b` without labels: the first two
  lines are `--- a\t` + `FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", mtime,
  false)` and `+++ b\t...`, mtime from `root->Stat` (include `BuiltinDate.h`
  in the test); names `sp ace` -> `"sp ace"`, `x\001y` -> `"x\001y"`, the
  UTF-8 `\303\251` -> `"\303\251"` in the header; `-q` with `sp ace` ->
  `Files sp ace and b differ\n`.
- `DiffContextFormat`: `-c -L a -L b a b` -> `*** a\n--- b\n***************\n*** 1,3 ****\n  a\n! b\n  c\n--- 1,4 ----\n  a\n! B\n  c\n+ d\n\\ No newline at end of file\n`;
  `-C 0` -> `*** a\n--- b\n***************\n*** 2 ****\n! b\n--- 2 ----\n! B\n***************\n*** 3 ****\n--- 4 ----\n+ d\n\\ No newline at end of file\n`;
  `-c -L x -L y a e` (e empty) -> `*** x\n--- y\n***************\n*** 1,3 ****\n- a\n- b\n- c\n--- 0 ----\n`;
  without labels the header time is `FormatDateTime("%a %b %e %T %Y", mtime, false)`.
- `DiffEdScript`: `n1` = `a\nb\nc\nd\n`, `n2` = `a\nB\nc\nd\ne\n`: `-e n1 n2`
  -> `4a\ne\n.\n2c\nB\n.\n`, 1; `y\n` vs `.\nx\n` -> `1c\n..\n.\ns/.//\na\nx\n.\n`;
  `-e a b` (b lacks its newline) -> out `3a\nd\n.\n2c\nB\n.\n`, err
  `diff: b: No newline at end of file\n\n`, status 2.
- `DiffHorizonMovesChanges`: `h1` = `b\na\n`, `h2` = `a\nb\na\na\n`:
  `diff h1 h2` -> `0a1\n> a\n1a3\n> a\n`; `-u -L x -L y` -> `--- x\n+++ y\n@@ -1,2 +1,4 @@\n+a\n b\n a\n+a\n`;
  `-U0 -L x -L y` -> `--- x\n+++ y\n@@ -0,0 +1 @@\n+a\n@@ -1,0 +3 @@\n+a\n`;
  `--horizon-lines=3 h1 h2` -> `0a1\n> a\n2a4\n> a\n`.
- `DiffPicksMyersScript`: `a\nb\nc\n` vs `c\nb\nb\n` ->
  `1,2d0\n< a\n< b\n3a2,3\n> b\n> b\n`.
- `DiffPlacesBlocks`: `a\nb\n` vs `a\nb\na\nb\n` -> `2a3,4\n> a\n> b\n`;
  `x\ny\nx\ny\n` vs `x\ny\n` -> `3,4d2\n< x\n< y\n`; `a\na\nb\n` vs
  `c\na\nb\nc\n` -> `1c1\n< a\n---\n> c\n3a4\n> c\n`; `s1` =
  `{\n  a;\n}\n{\n  b;\n}\n`, `s2` = `{\n  a;\n}\n{\n  x;\n}\n{\n  b;\n}\n`
  -> `4a5,7\n>   x;\n> }\n> {\n`.
- `DiffIsMinimalWhereGnuDefaultDiffers` (the documented exception): `d1` =
  `.\na\na\na\n.\n.\n`, `d2` = `a\nc\nc\nc\na\nd\n`: `diff d1 d2` and
  `diff -d d1 d2` both -> GNU's `-d` output
  `1,2d0\n< .\n< a\n3a2,4\n> c\n> c\n> c\n5,6c6\n< .\n< .\n---\n> d\n`
  (GNU's default prints `1d0`, `2a2,4`, `4,6c6` here; say so in a comment).
- `DiffWhiteSpaceOptions`: `w1` = `a b\nfoo  bar\nend \n`, `w2` = `a  b\nfoo
  bar\nend\n`: plain -> `1,3c1,3` with all six lines; `-b` and `-w` ->
  empty, 0; `-Z` -> `1,2c1,2\n< a b\n< foo  bar\n---\n> a  b\n> foo bar\n`;
  `-i` on `ABC\n` vs `abc\n` -> empty, 0; `--strip-trailing-cr` on
  `a\r\nb\r\n` vs `a\nb\n` -> empty, 0; `-E` on `a\tb\n` vs `a       b\n`
  (seven spaces) -> empty, 0, but `-E -Z` on the same -> `1c1`, status 1;
  `-E` on `a\bc\tb\n` vs `a\bc       b\n` -> status 1; `a\nb` vs `a\nb\n`:
  `-b` -> empty, 0, `-i` -> `2c2\n< b\n\\ No newline at end of file\n---\n> b\n`, 1.
- `DiffIgnoreBlankLines`: `a\n\nb\n` vs `a\nb\n\n`: plain -> `2d1\n< \n3a3\n> \n`;
  `-B` -> empty, 0; `-B -q` -> empty, 0. `a\n\nb\nc\n` vs `a\nb\nX\nc\n` with
  `-B` -> `3a3\n> X\n`, with `-B -u -L x -L y` -> `--- x\n+++ y\n@@ -1,4 +1,4 @@\n a\n-\n b\n+X\n c\n`.
  `a\n  \nb\n` vs `a\nb\n`: `-B` -> `2d1\n<   \n`, `-B -Z` -> empty, 0.
  Joining: `R1\n1\n2\n\n3\n4\n5\n6\n7\n8\n9\n10\n11\n` vs
  `Q1\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n` with `-B -u -L x -L y` ->
  `--- x\n+++ y\n@@ -1,7 +1,6 @@\n-R1\n+Q1\n 1\n 2\n-\n 3\n 4\n 5\n`; with the
  blank line one line lower (after `3`) -> `--- x\n+++ y\n@@ -1,4 +1,4 @@\n-R1\n+Q1\n 1\n 2\n 3\n`.
- `DiffTabsAndMarks`: `-T a b` -> `2c2\n<\tb\n---\n>\tB\n3a4\n>\td\n\\ No newline at end of file\n`;
  `-uT -L a -L b a b` -> context lines `\ta`, `-\tb`, `+\tB`; `-t` on
  `a\tb\n` vs `a\tc\n` -> `1c1\n< a       b\n---\n> a       c\n`; `-t` on
  `a\r\tc\n` vs `x\n` -> `1c1\n< a\r<         c\n---\n> x\n`;
  `--suppress-blank-empty -u -L x -L y` on `a\n\nb\n` vs `a\nb\n\n` ->
  `--- x\n+++ y\n@@ -1,3 +1,3 @@\n a\n-\n b\n+\n`; `--suppress-blank-empty`
  (normal) on the same -> `2d1\n<\n3a3\n>\n`.
- `DiffStdinAndBinary`: stdin `a\nb\nc\n` with `- b` -> the normal output
  above, 1; `x\0y\n` vs `x\0z\n` -> `Binary files bin1 and bin2 differ\n`,
  1; with `-a` -> `1c1\n< x\0y\n---\n> x\0z\n`; `-q a b` -> `Files a and b
  differ\n`, 1.
- `DiffErrors`: `diff a nope` -> err `diff: nope: No such file or
  directory\n`, 2; `diff a` -> err `diff: missing operand after
  'a'\ndiff: Try 'diff --help' for more information.\n`, 2; `diff` alone ->
  `diff: missing operand after 'diff'\n` + Try, 2; `diff a b c` -> `diff:
  extra operand 'c'\n` + the Try line, 2; `-u -c a b` -> `diff: conflicting
  output style options\n` + Try, 2; `-U x a b` -> `diff: invalid context
  length 'x'\n` + Try, 2; `-L 1 -L 2 -L 3 a b` -> `diff: too many file label
  options\n`, 2; `--tabsize=0 a b` -> `diff: invalid tabsize '0'\n` + Try,
  2; `-y a b` -> the not-treated report on stderr, then the normal output;
  `-v` -> `diff (HaisosOS builtin) 1.0.0\n`, 0.
- Engine tests in the same file, plain `TEST(DiffEngineTest, ...)` calling
  `ComputeDiff` directly: `EmptyFiles` (no changes), `AllInserted` (empty vs
  3 lines -> one change `{0,0,0,3}`), `MinimalAgainstLcs` (300 random pairs
  of 0-40 lines over 4 distinct lines, fixed seed: the deleted plus inserted
  line count equals `n0 + n1 - 2 * LCS`, the LCS from a plain O(n0 * n1)
  table in the test, and applying the changes to file 0 gives file 1),
  `LargeInputsStillCorrect` (two 5,000-line files over 40 distinct lines,
  fixed seed: applying the changes gives file 1, every deleted/inserted line
  is really in its file, and it finishes -- no timing assertion), `StopEndsEarly`
  (`stop` returning true at once on those files -> empty result, `stopped`
  set).

Update `BuiltinCommandsTest.cpp`: insert `"diff"` after `"date"` in the
expected list of `ListsEveryBuiltinSortedWithAVersion`. The generic tests
(`EveryBuiltinsHelpHasTheSameShape`, `EveryBuiltinsManPageIsItsHelp`,
`EveryUntreatedOptionIsAcceptedAndReported`) and
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` (CliParser) cover the rest.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Diff*:DiffEngineTest.*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `diff` in the opening list
  (after `date`); a row in "The commands" (version 1.0.0; treated as above;
  documented exceptions: the hunks are a minimal diff by Myers' algorithm and
  the placement rules, i.e. GNU's `diff -d` output -- GNU's default output,
  shaped by its own speed heuristics, can place changes differently on
  inputs with many repeated lines or mixed nearby edits, and lists more
  changes on very large ones, where Haisos takes longer; `-d` changes
  nothing; the C locale -- `-c` headers in the `%a %b %e %T %Y` form;
  standard input's header time is the current time; `-y`/`-W`, `-f`, `-n`,
  `-D` and the line/group formats, `-p`/`-F`/`-I`, `-H`, `-l`, `--color` not
  treated; until diff--diff-recursive, directories are refused), and one
  entry on `commands/diff/` in the file list (engine: line keys, fixed ends,
  Myers' linear-space comparison with its pinned choices, block placement;
  output; command).
- Root `CLAUDE.md`: `diff` in the Builtin Commands sentence (after `date`),
  a table row (`Compares files line by line (normal, -u, -c, -e, -q, -i -w
  -b -E -Z -B ...): a minimal diff by Myers' algorithm, GNU's diff -d
  hunks`), and the `BuiltinCommands/` line of the directory tree.

## Acceptance

- [ ] `diff` registered; `--help` has the standard shape, with the
      exception in its notes; `-v`/`--version` print the builtin version;
      `man diff` is the help.
- [ ] Every GNU diff 3.10 option is in `Options()`; untreated ones are
      reported, never rejected; `-0`..`-9` hidden.
- [ ] The engine is written from scratch from this plan: line keys, fixed
      ends with the horizon, Myers' linear-space comparison with the choices
      pinned down here, block placement, the change list -- no other
      program's source read or mirrored.
- [ ] The diff is always minimal (`MinimalAgainstLcs`); the
      `DiffHorizonMovesChanges`, `DiffPicksMyersScript`, `DiffPlacesBlocks`
      and `DiffIsMinimalWhereGnuDefaultDiffers` outputs match GNU byte for
      byte (the last one GNU's `-d`).
- [ ] Normal, unified, context and ed output, `\ No newline at end of
      file`, `-T -t --suppress-blank-empty -B -i -w -b -E -Z
      --strip-trailing-cr`, labels, header names and times as specified.
- [ ] Usage errors carry diffutils' prefixed Try line; exit 0/1/2.
- [ ] Files reached only through `context.IO()`; reading and comparing stop
      on `TriggerStop()`; no POSIX headers, no `<regex>`.
- [ ] CLAUDE.md rows added, the exception documented in both;
      `ListsEveryBuiltinSortedWithAVersion` updated; all unit tests pass.

## Out of scope

- Directories, `-r -N -P -x -X -S --from-file --to-file` (diff--diff-recursive).
- Side by side (`-y -W --left-column --suppress-common-lines`), `-f`, `-n`,
  `-D` and the line/group formats, `-p -F -I`, `-H`, `-l`, colours: not
  treated (decided by size: this task is already at the limit).
- GNU's default-mode speed heuristics (see "Where Haisos's hunks may
  differ"): not reproduced, by the clean-room rule and because they are not
  documented.
- patch (diff--patch-core).
