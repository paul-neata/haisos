# Task diff--diff-core: the diff builtin -- GNU's algorithm, normal, unified, context and ed output

- Rock: diff
- Depends on: coreutils--sort (`BuiltinText.h`: `OpenInputOperand`, `InputOpenFailure`, `GnuQuote`), coreutils--date (`FormatDateTime` in `BuiltinDate.h`)
- Size: ~1050 changed lines in ~7 files (at the upper limit: see "Out of scope" for what was left to keep it there)
- Plan checked against: develop @ ccb9dbe
- PR title: Add the diff builtin: GNU's algorithm, normal/-u/-c/-e output

## Goal

`diff FILE1 FILE2` prints, byte for byte, what GNU diffutils 3.10 prints in
the C locale (`LC_ALL=C`): the same hunks in the same places -- GNU's
algorithm is reproduced with every heuristic that changes the output -- in
the normal format, unified (`-u`, `-U NUM`, `--unified[=NUM]`), context
(`-c`, `-C NUM`, `--context[=NUM]`) and ed (`-e`) formats; `-q`, `-s`,
`--label`, `-i -w -b -E -Z -B --strip-trailing-cr --tabsize -a -t -T
--suppress-blank-empty -d --horizon-lines --normal`; `\ No newline at end
of file`; header times from `Stat`; `-` for standard input; binary files;
exit status 0 (same), 1 (different), 2 (trouble). This is the `diff -u a b
> p` half of acceptance scenario 2 (goal.md); the other half is
diff--patch-core.

Directories (`-r`, a directory operand) are diff--diff-recursive's, which
calls into what this task builds -- so build the pieces as described in
"Changes", with the entry points it names.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security: ICurrentProcess
is the only door out"), `src/components/BuiltinCommands/CLAUDE.md`,
`BuiltinCommand.h` (`BuiltinContext`, `ParseBuiltinArgs`, `BeginBuiltin`,
`ShellEscapeQuoted`, `BuiltinOption` -- with its `bool hidden`, added by
coreutils--uniq-cut), `commands/cat/Cat.cpp` (reading `-` and files in
chunks with `StopRequested()` and `kIOInterrupted`), the plan
`develop-plan/tasks/coreutils--du-cmp.md` (cmp: diffutils' conventions -- the
prefixed Try line, `-v` as `--version`; diff follows the same ones), and
`develop-plan/tasks/coreutils--sort.md` (`BuiltinText.h`).

What earlier tasks provide, as if on develop:
- coreutils--sort, `src/components/BuiltinCommands/BuiltinText.h`:
  `std::shared_ptr<IFileDescriptor> OpenInputOperand(BuiltinContext&, const std::string& name, InputOpenFailure& failure);`
  (`-` is descriptor 0; `InputOpenFailure { None, Missing, Directory, Denied, BadDescriptor }`),
  `std::string GnuQuote(std::string_view)`.
- coreutils--date, `src/components/BuiltinCommands/BuiltinDate.h`:
  `std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);`
  -- GNU's strftime in the C locale, `%N` included, local time when `utc` is false.
- coreutils--uniq-cut: `BuiltinOption::hidden` (parsed, not shown in `--help`).

Reference: GNU diffutils 3.10 (`src/analyze.c`, `src/io.c`, `lib/diffseq.h`,
`src/context.c`, `src/normal.c`, `src/ed.c`, `src/util.c`). This plan
describes their behaviour; implement it from the description (do not copy
GPL code). The container has GNU diff 3.10 (`diff --version`): run any case
the plan does not spell out there with `LC_ALL=C TZ=UTC` and compare.

## Changes

Rules that bite (root `CLAUDE.md`):
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
  read loop and in the comparison's outer loop (between `diag` calls), and
  treat `kIOInterrupted` as a quiet stop (exit status 2 is fine; nothing more
  printed).

### New `src/components/BuiltinCommands/commands/diff/DiffEngine.h` / `.cpp`

Pure functions over bytes, no I/O, namespace `Haisos`:

```cpp
enum class DiffWhiteSpace { None, TabExpansion, TrailingSpace, TabExpansionAndTrailingSpace, SpaceChange, AllSpace };

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
// first (GNU's prepare_text). In a non-robust output style (ed), a missing
// final newline is appended (missingNewline still set, for the warning).
DiffText MakeDiffText(std::string bytes, bool stripTrailingCr, bool robustOutput);

// One change: |deleted| lines of file 0 from line0, |inserted| lines of
// file 1 from line1 (0-based). With deleted == 0, line0 is the line before
// which the insertion goes; likewise line1 with inserted == 0.
struct DiffChange { int64_t line0, line1, deleted, inserted; };

struct DiffAnalysisOptions {
    DiffLineOptions lines;
    int64_t horizonLines = 0;   // max(--horizon-lines, context)
    bool minimal = false;       // -d
    bool robustOutput = true;   // false for -e
};

// GNU's edit script, in file order (build_script), or empty when the files
// are the same under the options. |stop| is checked between steps; when it
// becomes true the result is empty and |stopped| is set.
std::vector<DiffChange> ComputeDiff(const DiffText& a, const DiffText& b,
                                    const DiffAnalysisOptions& options,
                                    const std::function<bool()>& stop, bool& stopped);
```

The algorithm, step by step -- each step changes which lines GNU matches, so
none may be skipped or "improved":

1. **Equivalence classes.** Each line gets a class number; two lines are in
   the same class iff their *keys* are equal. The key of a line is its text
   without the `'\n'`, normalised for the options (bytes; `isspace` is C's:
   space `\t \n \v \f \r`):
   - `None`: the text as is.
   - `AllSpace` (`-w`): every `isspace` byte removed.
   - `SpaceChange` (`-b`): each run of `isspace` bytes becomes one space; a
     run reaching the end of the line is removed.
   - `TrailingSpace` (`-Z`): a final run of `isspace` bytes removed.
   - `TabExpansion` (`-E`): tabs expanded to spaces at `tabSize` stops
     (column from 0; `\b` moves back one column if not at 0, `\r` resets to
     0, any other byte advances 1).
   - `TabExpansionAndTrailingSpace` (`-E -Z`): both.
   - `ignoreCase` (`-i`): additionally every byte through `tolower` (ASCII).
   Option precedence (GNU's): `-w` wins over everything; `-b` over `-E` and
   `-Z`; `-E` and `-Z` combine.
   In a robust style, an incomplete last line can only be equal to the other
   file's incomplete last line -- add a marker to its key -- unless the mode is
   `TrailingSpace` or stronger (`TrailingSpace`, `TabExpansionAndTrailingSpace`,
   `SpaceChange`, `AllSpace`), where it is keyed like any line. Use one
   `std::unordered_map<std::string, int64_t>` shared by both files.
2. **Identical prefix and suffix, with a horizon** (GNU's
   `find_identical_ends`). Let H = `horizonLines`.
   - P = the number of leading lines byte-identical in both files (the whole
     line, `'\n'` included; an incomplete line equals only an incomplete
     line). `prefix = max(0, P - H)`.
   - S = the number of trailing lines byte-identical in both files, counted
     only among lines at index >= `prefix` in each file (the suffix never
     overlaps the prefix); S = 0 in a robust style when exactly one file is
     missing its final newline. `suffix = max(0, S - H)`.
   - The *region* is lines `[prefix, count0 - suffix)` of file 0 and
     `[prefix, count1 - suffix)` of file 1. Everything below works on the
     region only; lines outside it are unchanged. (So `diff` and `diff -u`
     can place a change differently: `-u` has H = 3. See the test
     `DiffHorizonMovesChanges`.)
3. **Discarding confusing lines** (GNU's `discard_confusing_lines`), per file
   f over its region lines (n = region length of f):
   - `many = 5`; `tem = n / 64`; while `(tem >>= 2) > 0`: `many *= 2`.
   - For each region line: `nmatch` = how many region lines of the *other*
     file have its class. `nmatch == 0` -> mark 1 (discard); `nmatch > many`
     -> mark 2 (provisional); else 0.
   - Then scan i from 0: a mark 2 met on its own becomes 0. At a mark 1:
     find j, the end of the run of non-zero marks from i, counting the
     provisionals in it; drop trailing provisionals from the run (they become
     0, j shrinks); `length = j - i`. If `provisional * 4 > length`, every
     provisional in the run becomes 0. Otherwise: `minimum = 1`, `tem =
     length >> 2`, while `(tem >>= 2) > 0`: `minimum <<= 1`; then
     `minimum++`. Walk the run with a counter `consec` of consecutive
     provisionals: a non-provisional resets it; when `++consec == minimum`,
     back up `consec` positions (to re-walk the subrun); when `consec >
     minimum`, that provisional becomes 0 -- so every subrun of at least
     `minimum` provisionals is cancelled whole. Then from the run's start,
     for k = 0..length-1: stop if `k >= 8` and mark is 1; a mark 2 becomes 0
     and resets `consec`; a 0 resets `consec`; a 1 increments it; stop when
     `consec == 3`. Then i moves to the run's last line, and the same scan
     runs backwards from there. Continue the outer scan after the run.
   - Lines still marked non-zero are discarded: marked *changed* now, and
     left out of the sequence the next step sees -- except with `minimal`
     (`-d`), where nothing is discarded. Keep, for each file, the list of the
     region indexes of the undiscarded lines (GNU's `realindexes`).
4. **Myers' comparison** of the undiscarded class sequences X (file 0) and Y
   (file 1), GNU's `compareseq`/`diag` from `lib/diffseq.h`, exactly:
   - `compareseq(xoff, xlim, yoff, ylim, findMinimal)`: slide `xoff/yoff`
     forward while `X[xoff] == Y[yoff]`; slide `xlim/ylim` back while
     `X[xlim-1] == Y[ylim-1]`; if `xoff == xlim` every `Y[yoff..ylim)` is an
     insertion; else if `yoff == ylim` every `X[xoff..xlim)` a deletion;
     else `diag(...)` gives `(xmid, ymid, loMinimal, hiMinimal)` and
     `compareseq(xoff, xmid, yoff, ymid, loMinimal)`,
     `compareseq(xmid, xlim, ymid, ylim, hiMinimal)` (either order; iterate
     one of them to bound the recursion depth). Marking an insertion or a
     deletion sets *changed* on the real line (through the realindexes).
   - `diag`: two arrays fd, bd indexed by diagonal k = x - y over
     `[xoff - ylim - 1, xlim - yoff + 1]`. `dmin = xoff - ylim`, `dmax =
     xlim - yoff`, `fmid = xoff - yoff`, `bmid = xlim - ylim`, `fmin = fmax =
     fmid`, `bmin = bmax = bmid`, `odd = (fmid - bmid) & 1`; `fd[fmid] =
     xoff`, `bd[bmid] = xlim`. For c = 1, 2, ...:
     - forward: if `fmin > dmin` then `fd[--fmin - 1] = -1` else `++fmin`;
       if `fmax < dmax` then `fd[++fmax + 1] = -1` else `--fmax`. For d from
       fmax down to fmin step 2: `x0 = fd[d-1] < fd[d+1] ? fd[d+1] :
       fd[d-1] + 1`; x = x0, y = x0 - d; while `x < xlim && y < ylim &&
       X[x] == Y[y]`: x++, y++; if `x - x0 > 20` (SNAKE_LIMIT) note a big
       snake; `fd[d] = x`; if `odd && bmin <= d && d <= bmax && bd[d] <= x`:
       return (x, y, true, true).
     - backward: if `bmin > dmin` then `bd[--bmin - 1] = INT64_MAX` else
       `++bmin`; if `bmax < dmax` then `bd[++bmax + 1] = INT64_MAX` else
       `--bmax`. For d from bmax down to bmin step 2: `x0 = bd[d-1] <
       bd[d+1] ? bd[d-1] : bd[d+1] - 1`; x = x0, y = x0 - d; while `xoff < x
       && yoff < y && X[x-1] == Y[y-1]`: x--, y--; `bd[d] = x`; if `!odd &&
       fmin <= d && d <= fmax && x <= fd[d]`: return (x, y, true, true).
     - if `findMinimal`, continue with c + 1.
     - (GNU's `-H` heuristic is not treated: skip it.)
     - if `c >= tooExpensive`: the forward diagonal maximising x + y (for d
       from fmax down to fmin step 2: `x = min(fd[d], xlim)`, `y = x - d`; if
       `ylim < y`: `x = ylim + d`, `y = ylim`; keep the first strictly better
       `x + y`) and the backward one minimising it (`x = max(xoff, bd[d])`,
       `y = x - d`; if `y < yoff`: `x = yoff + d`, `y = yoff`; first strictly
       smaller); if `(xlim + ylim) - bxybest < fxybest - (xoff + yoff)`
       return the forward point with (true, false), else the backward one
       with (false, true).
   - The top call is `compareseq(0, |X|, 0, |Y|, minimal)`, with
     `tooExpensive`: `diags = |X| + |Y| + 3`; `t = 1`; while `diags != 0`:
     `t <<= 1`, `diags >>= 2`; `tooExpensive = max(4096, t)`.
5. **Shifting the boundaries** (GNU's `shift_boundaries`), on the region's
   changed flags, for f = 0 then f = 1 (`changed` = f's flags, `other` = the
   other file's, `equivs` = f's classes, each flag array with a `false`
   sentinel before index 0 and at index n): i = 0, j = 0; loop:
   - while `i < n && !changed[i]`: `while (other[j++]) {}`; `i++`. If i == n,
     done.
   - `start = i`; `while (changed[++i]) {}`; `while (other[j]) j++`.
   - repeat: `runlength = i - start`;
     while `start > 0 && equivs[start-1] == equivs[i-1]`: `changed[--start]
     = 1`, `changed[--i] = 0`, `while (changed[start-1]) start--`,
     `while (other[--j]) {}`;
     `corresponding = other[j-1] ? i : n`;
     while `i != n && equivs[start] == equivs[i]`: `changed[start++] = 0`,
     `changed[i++] = 1`, `while (changed[i]) i++`, `while (other[++j])
     corresponding = i`;
     until `runlength == i - start`.
   - while `corresponding < i`: `changed[--start] = 1`, `changed[--i] = 0`,
     `while (other[--j]) {}`.
6. **The script**: walk both flag arrays together from the region start: at
   a position where either is changed, the change is the run of changed
   lines in file 0 and the run in file 1 from there; then both indexes step
   one matched line. Line numbers in `DiffChange` are whole-file indexes
   (region offset added).

The `-B` decision (GNU's `analyze_hunk`) is applied at output time (below),
not here.

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

Exact formats (`n` lines are 1-based on output):
- **Hunks**: normal and ed print one hunk per change. Unified and context
  group changes: a change joins the current hunk while the number of
  unchanged lines between it and the previous one is `< 2 * context + 1`
  (with `-B`: `< context` when the next change is ignorable). Each hunk
  shows `context` lines before its first and after its last change,
  clamped to the file.
- **`-B`** (GNU's `analyze_hunk`): a change (or a hunk) is ignorable when
  every deleted and inserted line is blank: empty, or -- when the mode is
  `TrailingSpace` or stronger -- only whitespace (a line counts from its
  first non-space byte only when the mode is `SpaceChange` or `AllSpace`).
  An ignorable hunk is not printed at all; a hunk's letter/marks come from
  its non-ignored content (`show_from`/`show_to` count all its lines: a hunk
  of only deletions is OLD, of only insertions NEW, both CHANGED).
- **Normal**: `print_number_range` -- a range of one line is `n`, of several
  `a,b`, of none the line before it -- `<range0><letter><range1>` with
  letter `a` (only inserted), `d` (only deleted), `c` (both); deleted lines
  `< text`, then `---` (only for `c`), then inserted lines `> text`.
- **Unified**: header `--- NAME\tTIME` and `+++ NAME\tTIME` (or `--- LABEL`,
  no tab, when labelled), TIME = `FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z",
  mtime, false)`. Hunk line `@@ -R0 +R1 @@` where a range of one line is
  `n`, of none `n,0` (n = the line *before* it, 0 at the start -- patch
  relies on this), else `first,count`. Then context lines `' ' + text`,
  and per change its deleted lines `-text` then its inserted lines `+text`.
- **Context**: header `*** NAME\tTIME` and `--- NAME\tTIME` with TIME =
  `FormatDateTime("%a %b %e %T %Y", mtime, false)` (GNU in the C locale:
  `Tue Jan  2 03:04:05 2024`). Each hunk: `***************`, `*** R0 ****`,
  then -- only if it deletes something -- every line of the file-0 range
  marked `  ` (two spaces: mark ' ' and the separator), `- ` (deleted) or
  `! ` (deleted where the same change inserts); `--- R1 ----`, then -- only
  if it inserts -- the file-1 range marked `  `, `+ `, `! `. A context range
  is `a,b`, or just `b` when it has one line or none (b then the line
  before it).
- **Ed** (`-e`): the changes in *reverse* order (last first): `R0a`, `R0d`
  or `R0c` (normal ranges), then for `a`/`c` the inserted lines raw, then
  `.`. A line that is exactly `.` is written `..`, then `.`, `s/.//`, and
  insertion resumes with `a` before the next line (only if one follows; a
  closing `.` is printed only if insert mode is on at the end). Ed is not a
  robust style: a file missing its final newline is compared with the
  newline appended, and after the output `diff: NAME: No newline at end of
  file\n\n` (note the empty line: GNU's message has its own `\n`) is written
  to stderr per such file, and the exit status is 2.
- **Marks with -T / --suppress-blank-empty**: normal lines are `<`/`>` then
  a space (or a tab with `-T`) then the text; unified `-`/`+`/' ' then the
  text directly (with `-T`: the mark, a tab, the text -- for context lines
  the tab replaces the space); context marks are two characters (`- `, `! `,
  `+ `, `  `), with `-T` the mark's first character then a tab. With
  `--suppress-blank-empty`, an empty line gets no trailing blank after its
  mark (normal `<`, unified `-`/`+`/nothing for context, context `-`/`!`/`+`).
- **`-t`**: tabs in the text expanded to spaces, columns counted from the
  start of the text (not of the output line); `\b` moves back a column (and
  is written) when not at column 0, `\r` resets the column.
- **`\ No newline at end of file`**: in normal, unified and context output,
  after an incomplete last line is printed: `\n\\ No newline at end of
  file\n` (the line's missing newline, then the marker line).
- **Names in headers**: GNU's `c_escape`: a name holding a space, `"`, `\`
  or a byte below 0x20 is written in double quotes with `\a \b \t \n \v \f
  \r \" \\` and other control bytes as 3-digit octal (`\001`); otherwise as
  is. Labels are written as given, never escaped.

### New `src/components/BuiltinCommands/commands/diff/Diff.h` / `Diff.cpp`

`CreateDiffCommand()`, name `diff`, version `1.0.0`. Help: summary "compare
files line by line", usage `diff [OPTION]... FILES`, notes: "FILES are
'FILE1 FILE2'. If a FILE is '-', read standard input. Exit status is 0 if
inputs are the same, 1 if different, 2 if trouble." plus the exceptions
under "Docs".

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
  separate table entries, one id), `-d/--minimal`, `-e/--ed`,
  `-E/--ignore-tab-expansion`, `-i/--ignore-case`, `-L/--label LABEL`
  (Required), `--normal`, `-q/--brief`, `-s/--report-identical-files`,
  `-t/--expand-tabs`, `-T/--initial-tab`, `-u` (context 3), `-U NUM` and
  `--unified[=NUM]` (as `-C`), `-w/--ignore-all-space`,
  `-Z/--ignore-trailing-space`, `--strip-trailing-cr`, `--tabsize=NUM`,
  `--suppress-blank-empty`, `--horizon-lines=NUM`, `--binary`, `-h` and
  `--inhibit-hunk-merge` (accepted with no effect, as GNU on Linux), `-v`
  (`kBuiltinOptionVersion`, "output version information").
- not treated here, treated by diff--diff-recursive (keep `kBuiltinNotTreated`
  for now): `-r/--recursive`, `-N/--new-file`, `--unidirectional-new-file`
  (GNU 3.10 has no short `-P` in `--help`, but `P` is in its short options:
  list `{'P', "unidirectional-new-file"}`), `-x/--exclude PAT`,
  `-X/--exclude-from FILE`, `-S/--starting-file FILE`, `--from-file FILE`,
  `--to-file FILE`, `--no-dereference`, `--ignore-file-name-case`,
  `--no-ignore-file-name-case`.
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
   `ParseBuiltinArgs`; on an error print `diff: <error>` then `diff: Try 'diff
   --help' for more information.` (both via `ErrorText`), exit 2. Then
   `--help`/`--version`/`-v` as `BeginBuiltin` does, then
   `ReportNotTreated`.
2. Options in order, GNU's rules: `-b` sets SpaceChange unless AllSpace;
   `-E`/`-Z` add to the mode only while it is below SpaceChange; `-w`
   AllSpace. `-u`/`-c` set the style and `context = max(context, 3)`;
   `-U N`/`-C N`/`--unified=N`/`--context=N` set the style and `context =
   max(context, N)` (no value: 3); N must be a non-negative decimal, else
   `invalid context length 'N'` (usage error shape, exit 2). Two different
   styles: `diff: conflicting output style options` + Try, exit 2. `-L`
   fills label0 then label1; a third: `diff: too many file label options`
   (no Try), exit 2. `--tabsize`: a positive decimal, else `invalid tabsize
   'X'` + Try, 2. `--horizon-lines`: non-negative, else `invalid horizon
   length 'X'` + Try, 2. Labels and values are printed with plain `'...'`
   (printf strings in diffutils, not `quote()`).
3. `horizonLines = max(horizon, context)` for unified/context, `horizon`
   otherwise. `robustOutput = style != Ed`.
4. Operands: fewer than two -> `diff: missing operand after 'X'` (X the last
   argument, or `diff` when there is none) + Try, 2; more than two -> `diff:
   extra operand 'Z'` (the third) + Try, 2. Then `return
   DiffTwoFiles(context, settings, op0, op1, "")`.

`DiffTwoFiles`:
1. Each operand: `-` is standard input (read once even when both are `-`:
   then they are identical); otherwise `IO().Stat` -- missing -> `diff:
   NAME: No such file or directory` (stderr, unquoted), status 2 (report
   both operands before returning). A directory operand -> for now `diff:
   NAME: Is a directory`, 2 (diff--diff-recursive replaces this).
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
   current time (`CurrentFileDateTime()` from `FilesystemUtils.h` -- GNU uses
   the current time for stdin, POSIX's rule).

### Registration and build

- `BuiltinCommandList.h`: declare `CreateDiffCommand()`; add it to
  `CreateStandardBuiltinCommands()` in alphabetical position.
- `src/components/BuiltinCommands/CMakeLists.txt`: add
  `commands/diff/Diff.cpp`, `commands/diff/DiffEngine.cpp`,
  `commands/diff/DiffOutput.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/DiffTest.cpp` (add to
that directory's `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, Diff...)`,
files written with `WriteFile` under `/d`, run with `RunCaptured("diff",
{...}, input, "/d")`. Every expected output below is GNU diff 3.10's
(`LC_ALL=C`); headers are avoided with `-L`/`--label`, except where a test
checks them. `\0` means a NUL byte.

- `DiffNormalChangeAndMissingNewline`: `a` = `a\nb\nc\n`, `b` = `a\nB\nc\nd`
  -> out `2c2\n< b\n---\n> B\n3a4\n> d\n\\ No newline at end of file\n`,
  status 1.
- `DiffIdenticalIsSilent`: `diff a a` -> out empty, 0; `-s a a` -> `Files a
  and a are identical\n`, 0.
- `DiffUnifiedWithLabels`: `-u -L one -L two a b` (files above) -> `--- one\n+++ two\n@@ -1,3 +1,4 @@\n a\n-b\n+B\n c\n+d\n\\ No newline at end of file\n`.
- `DiffUnifiedZeroContextRanges`: `-U0 -L a -L b a b` -> `--- a\n+++ b\n@@ -2 +2 @@\n-b\n+B\n@@ -3,0 +4 @@\n+d\n\\ No newline at end of file\n`.
- `DiffUnifiedHeaderTimes`: `diff -u a b` without labels: the first two
  lines are `--- a\t` + `FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", mtime,
  false)` and `+++ b\t...`, mtime from `root->Stat` (include `BuiltinDate.h`
  in the test); a name with a space is `"sp ace"` in the header.
- `DiffContextFormat`: `-c -L a -L b a b` -> `*** a\n--- b\n***************\n*** 1,3 ****\n  a\n! b\n  c\n--- 1,4 ----\n  a\n! B\n  c\n+ d\n\\ No newline at end of file\n`;
  `-C 0` -> `*** a\n--- b\n***************\n*** 2 ****\n! b\n--- 2 ----\n! B\n***************\n*** 3 ****\n--- 4 ----\n+ d\n\\ No newline at end of file\n`;
  without labels the header time is `FormatDateTime("%a %b %e %T %Y", mtime, false)`.
- `DiffEdScript`: `n1` = `a\nb\nc\nd\n`, `n2` = `a\nB\nc\nd\ne\n`: `-e n1 n2`
  -> `4a\ne\n.\n2c\nB\n.\n`, 1; `y\n` vs `.\nx\n` -> `1c\n..\n.\ns/.//\na\nx\n.\n`;
  `-e a b` (b lacks its newline) -> out `3a\nd\n.\n2c\nB\n.\n`, err
  `diff: b: No newline at end of file\n\n`, status 2.
- `DiffHorizonMovesChanges` (step 2): `h1` = `b\na\n`, `h2` = `a\nb\na\na\n`:
  `diff h1 h2` -> `0a1\n> a\n1a3\n> a\n`; `-u -L x -L y` -> `--- x\n+++ y\n@@ -1,2 +1,4 @@\n+a\n b\n a\n+a\n`;
  `--horizon-lines=3 h1 h2` -> `0a1\n> a\n2a4\n> a\n`.
- `DiffDiscardsConfusingLines` (step 3): `d1` = `.\na\na\na\n.\n.\n`, `d2` =
  `a\nc\nc\nc\na\nd\n`: `diff d1 d2` -> `1d0\n< .\n2a2,4\n> c\n> c\n> c\n4,6c6\n< a\n< .\n< .\n---\n> d\n`;
  `-d d1 d2` -> `1,2d0\n< .\n< a\n3a2,4\n> c\n> c\n> c\n5,6c6\n< .\n< .\n---\n> d\n`.
- `DiffShiftsBoundaries` (step 5): `s1` = `{\n  a;\n}\n{\n  b;\n}\n`, `s2` =
  `{\n  a;\n}\n{\n  x;\n}\n{\n  b;\n}\n` -> `4a5,7\n>   x;\n> }\n> {\n`; and
  `x\ny\nx\ny\n` vs `x\ny\n` -> `3,4d2\n< x\n< y\n`.
- `DiffWhiteSpaceOptions`: `w1` = `a b\nfoo  bar\nend \n`, `w2` = `a  b\nfoo
  bar\nend\n`: plain -> `1,3c1,3` with all six lines; `-b` and `-w` ->
  empty, 0; `-Z` -> `1,2c1,2\n< a b\n< foo  bar\n---\n> a  b\n> foo bar\n`;
  `-i` on `ABC\n` vs `abc\n` -> empty, 0; `--strip-trailing-cr` on
  `a\r\nb\r\n` vs `a\nb\n` -> empty, 0; `-E` on `a\tb\n` vs `a       b\n`
  (seven spaces) -> empty, 0.
- `DiffIgnoreBlankLines`: `a\n\nb\n` vs `a\nb\n\n`: plain -> `2d1\n< \n3a3\n> \n`;
  `-B` -> empty, 0. `a\n\nb\nc\n` vs `a\nb\nX\nc\n` with `-B` -> `3a3\n> X\n`,
  with `-B -u -L x -L y` -> `--- x\n+++ y\n@@ -1,4 +1,4 @@\n a\n-\n b\n+X\n c\n`.
- `DiffTabsAndMarks`: `-T a b` -> `2c2\n<\tb\n---\n>\tB\n3a4\n>\td\n\\ No newline at end of file\n`;
  `-uT -L a -L b a b` -> context lines `\ta`, `-\tb`, `+\tB`; `-t` on
  `a\tb\n` vs `a\tc\n` -> `1c1\n< a       b\n---\n> a       c\n`;
  `--suppress-blank-empty -u -L x -L y` on `a\n\nb\n` vs `a\nb\n\n` ->
  `--- x\n+++ y\n@@ -1,3 +1,3 @@\n a\n-\n b\n+\n`.
- `DiffStdinAndBinary`: stdin `a\nb\nc\n` with `- b` -> the normal output
  above, 1; `x\0y\n` vs `x\0z\n` -> `Binary files bin1 and bin2 differ\n`,
  1; with `-a` -> `1c1\n< x\0y\n---\n> x\0z\n`; `-q a b` -> `Files a and b
  differ\n`, 1.
- `DiffErrors`: `diff a nope` -> err `diff: nope: No such file or
  directory\n`, 2; `diff a` -> err `diff: missing operand after
  'a'\ndiff: Try 'diff --help' for more information.\n`, 2; `diff a b c` ->
  `diff: extra operand 'c'\n` + the Try line, 2; `-u -c a b` -> `diff:
  conflicting output style options\n` + Try, 2; `-U x a b` -> `diff:
  invalid context length 'x'\n` + Try, 2; `-y a b` -> the not-treated
  report on stderr, then the normal output; `-v` -> `diff (HaisosOS
  builtin) 1.0.0\n`, 0.
- Engine unit tests in the same file, plain `TEST(DiffEngineTest, ...)`
  calling `ComputeDiff` directly: `EmptyFiles` (no changes), `AllInserted`
  (empty vs 3 lines -> one change `{0,0,0,3}`), `TooExpensiveStillCorrect`
  (two 5000-line files of different random lines from a fixed seed: every
  change's lines really differ and applying the script to file 0 gives
  file 1 -- a correctness check of the approximation path).

Update `BuiltinCommandsTest.cpp`: insert `"diff"` in sorted position in the
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

- `src/components/BuiltinCommands/CLAUDE.md`: `diff` in the opening list; a
  row in "The commands" (version 1.0.0; treated as above; exceptions: the
  C locale -- `-c` headers in the POSIX `%a %b %e %T %Y` form; standard
  input's header time is the current time; `-y`/`-W`, `-f`, `-n`, `-D` and
  the line/group formats, `-p`/`-F`/`-I` (no regex here), `-H`, `-l`,
  `--color` not treated; until diff--diff-recursive, directories are
  refused), and one paragraph on `commands/diff/` (engine / output /
  command, GNU's algorithm steps kept exactly).
- Root `CLAUDE.md`: `diff` in the Builtin Commands sentence and table
  (`Compares files line by line, GNU's hunks (normal, -u, -c, -e, -q, -i -w
  -b -B ...)`) and the `BuiltinCommands/` line of the directory tree.

## Acceptance

- [ ] `diff` registered; `--help` has the standard shape; `-v`/`--version`
      print the builtin version; `man diff` is the help.
- [ ] Every GNU diff 3.10 option is in `Options()`; untreated ones are
      reported, never rejected; `-0`..`-9` hidden.
- [ ] The five engine steps are all implemented as described (horizon,
      discarding, diag with the too-expensive cut-off, shift_boundaries,
      script); the `DiffHorizonMovesChanges`, `DiffDiscardsConfusingLines`
      and `DiffShiftsBoundaries` outputs match GNU byte for byte.
- [ ] Normal, unified, context and ed output, `\ No newline at end of
      file`, `-T -t --suppress-blank-empty -B -i -w -b -E -Z
      --strip-trailing-cr`, labels and header times as specified.
- [ ] Usage errors carry diffutils' prefixed Try line; exit 0/1/2.
- [ ] Files reached only through `context.IO()`; reading stops on
      `TriggerStop()`; no POSIX headers, no `<regex>`.
- [ ] CLAUDE.md rows added; `ListsEveryBuiltinSortedWithAVersion` updated;
      all unit tests pass.

## Out of scope

- Directories, `-r -N -P -x -X -S --from-file --to-file` (diff--diff-recursive).
- Side by side (`-y -W --left-column --suppress-common-lines`), `-f`, `-n`,
  `-D` and the line/group formats, `-p -F -I`, `-H`, `-l`, colours: not
  treated (decided by size: this task is already at the limit).
- patch (diff--patch-core).
