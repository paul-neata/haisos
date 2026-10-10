# Task diff--patch-core: the patch builtin -- unified diffs, offsets, reversed patches, rejects

- Rock: diff
- Depends on (all done): base--fs-rename-times (`IFileIO::Rename`), coreutils--sort (`OpenInputOperand` in `BuiltinText.h`), diff--diff-core and diff--diff-recursive (the `diff` builtin, used by the round-trip test; two small diff fixes ride along here)
- Size: ~1100 changed lines in ~10 files
- Plan checked against: develop @ 57d3de1
- PR title: Add the patch builtin: unified and git diffs, offsets, rejects

## Goal

`patch` applies unified diffs as GNU patch 2.7.6 does (C locale, no
terminal), printing exactly its messages: one or many files per patch,
`diff --git` headers with git's `a/` `b/` prefixes (`-p1`), `/dev/null` and
epoch time stamps for created and deleted files, `-p NUM`, `-R`,
`-N/--forward`, `-i FILE` (standard input by default), the `[ORIGFILE
[PATCHFILE]]` operands, `-o FILE`, `-d DIR`, `--dry-run`, `-s/--silent`,
`-f/--force`, `-t/--batch`, `-E/--remove-empty-files`, `--binary`, hunks
found at an offset (`Hunk #1 succeeded at 12 (offset 2 lines).`, the
original kept as `FILE.orig`), reversed patches detected (`Reversed (or
previously applied) patch detected!  Assume -R? [n] ` answered as without a
terminal), failed hunks reported and saved to `FILE.rej` in unified form,
exit status 0/1/2. Each patched file is written to a temporary file and
moved into place with `Rename`. This is the `patch -p0 < p` half of
acceptance scenario 2 (goal.md). Matching is exact here (no fuzz: what GNU
does with `-F 0`); fuzz arrives with diff--patch-fuzz-rej.

Two small `diff` fixes from the review of #73 ride along (see "diff fixes").

## Context

**Clean-room rule (the user's, above every other rule; root `CLAUDE.md`
"Clean-room rule"):** no code is copied from any other program, whatever its
licence. Never read, copy, port, translate or paraphrase another program's
source (GNU patch's included, and code recalled from memory), and never name
another program's internal functions or variables -- in code, comments,
tests or commit messages. Everything below is *behaviour*: the GNU patch
manual and man page, POSIX `patch`, and what GNU patch 2.7.6 printed on the
planning host with `LC_ALL=C` (every expected output below was taken that
way). Write the code from scratch, shaped by Haisos's own structure; the
names here (`PatchReader`, `LocateHunk`, ...) are Haisos's own.

**Write in pieces:** never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step (earlier
runs died on "response exceeded the 32000 output token maximum").

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`
(`ParseBuiltinArgs`, `BuiltinHelpText`, `BuiltinVersionText`,
`ShellEscapeQuoted`, `kBuiltinOptionHelp`/`kBuiltinOptionVersion`),
`commands/diff/Diff.cpp` (`DiffCommand::Run`'s start: parsing without
`BeginBuiltin` because of the prefixed Try line -- patch does the same; and
`ReadWholeInput`, the stop-aware whole read that moves to `BuiltinText.h`
below), `BuiltinText.h` (`OpenInputOperand`, `OpenFailureText`,
`WriteFully`), `BuiltinDate.h` (`ParseDateString`),
`src/components/Filesystem/FilesystemUtils.h` (`kFileOpenReadOnly`,
`kFileOpenWriteCreateTruncate`, `kFileOpenWriteCreateAppend`,
`kFileCreateMode`), `interfaces/IFileIO.h` (`Rename`, `ResolvePath`,
`Stat`, `RemoveFile`, `CreateDirectory`, `ChangeDirectory`).

What is on develop to use:
- `int IFileIO::Rename(oldPath, newPath)`: 0, `kFileSystemError`, or
  `kFileSystemCrossDevice` (`interfaces/IFileSystemService.h`); replaces an
  existing file.
- `BuiltinText.h`: `OpenInputOperand(context, name, failure)` (`-` is
  descriptor 0; `InputOpenFailure { None, Missing, Directory, Denied,
  BadDescriptor }`), `WriteFully(descriptor, bytes)`.
- `BuiltinCopy.h` (`BackupPathFor`, `BackupMode`) and `BuiltinPrompt.h`
  exist but are not for this task: backups beyond `.orig` are
  diff--patch-fuzz-rej's, and patch never reads an answer (below).
- `commands/diff/`: the `diff` builtin; `diff -u` and `diff -ruN` produce
  exactly the patches read here.

GNU patch is not in the task container: for a case not listed here, follow
these rules.

## Changes

Rules that bite: `ICurrentProcess` is the only door out -- every file is
read, written, renamed and removed through `context.IO()` (and `-d` moves
the process's own working directory with `IO().ChangeDirectory`); no
`IFileSystem`/`IHaisosOS` held. GNU's messages byte for byte; every GNU
patch option in `Options()` (untreated ones reported); `--help` from
`BuiltinHelpText`; registered in `CreateStandardBuiltinCommands()` (and so in
the `haisos --init` template); sources in the CMakeLists; portable C++17, no
POSIX headers, no `<regex>`; reading stops on `TriggerStop()`. Nothing here
creates a link.

Questions: where GNU would ask, Haisos always does what GNU does with no
terminal: the question is printed to **stdout** followed by `\n`, and the
default answer is taken (nothing is read). A documented exception (GNU
reads its answers from the terminal when there is one).

Messages: every notice and question goes to **stdout**; a fatal error goes
to stderr as `patch: **** MESSAGE\n` and exits 2 (no Try line); one caused
by a failed system call adds ` : REASON` (a space before the colon):
`patch: **** Can't open patch file nosuch : No such file or directory`.
File names in messages are `ShellEscapeQuoted(name)` (quoted only when
needed: `patching file 'sp ace'`, `patching file octé`).

### Move: the whole-input read to `BuiltinText.h`

Move `DiffReadOutcome`/`ReadWholeInput` out of `Diff.cpp`'s anonymous
namespace into `BuiltinText.h`/`.cpp` as
`enum class WholeReadOutcome { Done, Stopped, Error };`
`WholeReadOutcome ReadWholeInput(BuiltinContext&, IFileDescriptor&, std::string& out);`
(same body: 64 KiB reads, no cap, a stop or `kIOInterrupted` ends quietly).
`Diff.cpp` calls the shared one; patch reads the patch and every target file
with it.

### New `src/components/BuiltinCommands/commands/patch/PatchParse.h` / `.cpp`

```cpp
enum class PatchLineKind : char { Context = ' ', Delete = '-', Insert = '+' };
struct PatchLine {
    PatchLineKind kind;
    std::string text;       // the line, '\n' included unless noNewline
    bool noNewline = false; // followed by "\ No newline at end of file"
};
struct PatchHunk {
    // The ranges of the "@@ -A,B +C,D @@" line. oldStart is the first old
    // line the hunk covers: A, or A + 1 when B is 0 (an empty range "-A,0"
    // means "after line A"); likewise newStart.
    int64_t oldStart = 0, oldCount = 0;
    int64_t newStart = 0, newCount = 0;
    std::vector<PatchLine> lines;          // in the patch's order
    std::string function;                  // what follows the second "@@" (for the .rej)
    int64_t leadingContext = 0, trailingContext = 0; // context lines before the first / after the last change
    int64_t headerLine = 0;                // input line number of its "@@" line
};
enum class PatchFormat { Unified };        // diff--patch-fuzz-rej adds Context and Normal
// How sure the patch is that one side's file does not exist.
enum class SideAbsence { Present, Maybe, Surely };
struct FilePatch {
    PatchFormat format = PatchFormat::Unified;
    std::optional<std::string> oldName, newName, indexName; // after -p; nullopt: /dev/null, or too few components
    std::string oldTimeText, newTimeText;   // the rest of the ---/+++ line after the name, '\n' and '\r' removed
    SideAbsence oldAbsence = SideAbsence::Present, newAbsence = SideAbsence::Present;
    bool gitDiff = false, gitRename = false, gitCopy = false;
    bool stripTrailingCr = false;
    int64_t reportLine = 0;                 // the line "can't find file to patch at input line N" names
    std::string leadingText;                // the input lines from the end of the previous patch up to the hunks
    std::vector<PatchHunk> hunks;           // the well-formed hunks, in order
    std::string malformed;                  // non-empty: the fatal message met after |hunks|
};

class PatchReader {   // a plain class over the whole patch text
public:
    PatchReader(std::string text, bool binary /* --binary */);
    // The next file patch, or nullopt at the end of the input. |strip| is -p
    // (-1: not given). Sets |garbage| when the input is not empty and holds no
    // patch at all.
    std::optional<FilePatch> Next(int strip, bool& garbage);
};

// -R, and the answer "yes, it is reversed": swaps old and new (names, time
// texts, absences, each hunk's ranges, Delete <-> Insert; within each run of
// changed lines the deletions come first, as in any unified diff).
void SwapFilePatch(FilePatch& patch);
```

Recognising patches (unified only here):
- Scan lines. `Index: NAME` sets indexName. `diff --git A B` starts a git
  patch: oldName/newName from A and B (each a word, or a C-quoted `"..."`
  string), stripped by -p; then `index X..Y [mode]` (an all-zero X or Y:
  that side surely absent), `new file mode M` (old side surely absent),
  `deleted file mode M` (new side surely absent), `old mode`/`new mode`
  (ignored: no permissions), `rename from`/`rename to` (gitRename),
  `copy from`/`copy to` (gitCopy), `similarity index`. The next `diff
  --git` before any hunk ends a patch without hunks (a pure rename, copy,
  mode change, or the creation/deletion of an empty file).
- `--- NAME[\tTIME]` then `+++ NAME[\tTIME]`: the name runs to the first
  whitespace, except that it may hold spaces when a tab follows it (the
  time stamp separator): `--- sp ace\t2024-01-01 00:00:00 +0000` names `sp
  ace`, a bare `--- sp ace` names `sp`. A `"..."` name is C-unquoted (`\"`,
  `\\`, `\t`, `\n`, three-digit octal: `"oct\303\251"` is `octé`).
  `/dev/null` gives no name and makes that side surely absent. The time
  text is kept for the `.rej` header.
- Strip with -p NUM: remove NUM leading components, a run of `/` counting as
  one separator and a leading `/` as one component (`/x//d/e/f` with -p2 is
  `d/e/f`); a name with too few components is dropped (`d/f` with -p2 is
  no name). Without -p keep only the last component (`d/e/f` is `f`).
- A time stamp counts as the epoch -- that side surely absent -- when it
  parses (with `ParseDateString`, local zone when it names none; and the
  form `Www Mmm DD HH:MM:SS YYYY`) to a time more than 25 hours before and
  less than 26 hours after 1970-01-01 00:00:00 UTC. Verified:
  `1970-01-01 00:00:00.000000000 +0000`, `1969-12-31 16:00:00 -0800`,
  `1970-01-02 01:59:59 +0000`, `1969-12-30 23:00:01 +0000`, `1970-01-01`,
  `Thu Jan  1 00:00:00 1970` count; `1970-01-02 02:00:00 +0000`,
  `1969-12-30 23:00:00 +0000`, `garbage 1970`, no time at all do not.
- `@@ -A[,B] +C[,D] @@[ FUNC]` starts a hunk (B, D default 1). An old range
  `-0,0` makes the old side surely absent if its time was the epoch (or
  `/dev/null`), else maybe absent (unless git already said); `+0,0` likewise
  for the new side.
- CRs: unless --binary, when the header lines end in `\r\n`, every line of
  this file patch has its `\r` stripped and `(Stripping trailing CRs from
  patch; use --binary to disable.)\n` is printed once, before its
  `patching file` line (verified: an all-CRLF patch on an LF file applies;
  on a CRLF file it fails, `(different line endings)`; with --binary it
  applies to the CRLF file, and no message).
- Hunk body: lines starting with ' ', '-', '+' until B old and D new lines
  are read; an empty line is an empty context line; `\ No newline at end of
  file` (any line starting `\`) marks the line before it noNewline. Another
  `@@` line after a complete hunk continues the same file patch.
- A malformed hunk -- a line that is none of these before the counts are
  met -- is fatal: `patch: **** malformed patch at line N: LINE` (LINE as
  read, its newline included, so the message ends in a blank line), exit 2.
  It is reported only when that hunk is reached: the file's `patching file`
  line and the messages of its earlier hunks come first, that file is left
  unchanged, earlier files stay patched (verified: `--- t / +++ t / hunk 1
  at offset 1 / @@ -10,3 +10,3 @@ / 10 / xx` prints `patching file t`,
  `Hunk #1 succeeded at 2 (offset 1 line).`, then `patch: **** malformed
  patch at line 10: xx\n\n`, 2, `t` unchanged). The reader stores the
  well-formed hunks and the message in `malformed`.
- A hunk cut short by the end of the input: when it lacks the same number k
  of old and new lines and has at least one '-' or '+' line, it is completed
  with k empty context lines (trailing blank lines lost in transit);
  otherwise malformed, at the input's last line number with LINE ` \n`
  (`@@ -1,2 +1,2 @@\n-q\n` at the end: `patch: **** malformed patch at line
  4:  \n\n`; `@@ -1,3 +1,3 @@\n-q\n+Q\n` applies to `q\n\n\n`;
  `@@ -1,3 +1,3 @@\n a\n` is malformed).
- reportLine is the input line of the first hunk's `@@` (for a patch without
  hunks, its last header line). leadingText runs from the line after the
  previous file patch's last hunk line (or the input's start) to the
  `+++` line (or the last header line).
- No patch found in an input that is not empty: `patch: **** Only garbage
  was found in the patch input.`, 2 (also for `x\ny\n--- a\n`). Empty
  input: nothing, 0. Text after the last patch is ignored (it shows in the
  next patch's leadingText if one follows).

### New `src/components/BuiltinCommands/commands/patch/PatchApply.h` / `.cpp`

```cpp
struct PatchTarget {           // the input file as lines
    std::vector<std::string> lines;   // each with its '\n' (the last may lack it)
};
// Where |hunk| applies in |target|: the 1-based line its old lines start at,
// or 0 if nowhere. |fuzz| context lines may be ignored at each end
// (diff--patch-core always passes 0; diff--patch-fuzz-rej raises it).
// |consumedLines|: the lines earlier hunks of this file have used up.
// |runningOffset|: the offset the earlier hunks were found at; updated to
// this hunk's offset when it is found.
int64_t LocateHunk(const PatchTarget& target, const PatchHunk& hunk, int64_t fuzz,
                   int64_t consumedLines, int64_t& runningOffset);
```

Where a hunk applies (with no fuzz; each rule verified with `patch -F0`):
- The hunk's *old lines* are its Context and Delete lines; it matches at
  line L when they equal the file's lines from L on, byte for byte, the
  missing final newline included.
- Expected position E = oldStart + runningOffset. Candidates are the L with
  `L > consumedLines` and `L + oldLines - 1 <= fileLines`. They are tried
  nearest to E first and, at the same distance, the later one first
  (E+d before E-d): `p q r` expected at 5 in a file holding it at 3 and 7 is
  applied at 7 (`succeeded at 7 (offset 2 lines)`); held at 4 and 9, at 4
  (`offset -1 lines`); far away, anywhere (`offset 101 lines`).
- A hunk with fewer leading than trailing context lines and oldStart <= 1
  (the start of the file) may only match at line 1: `--- x / +++ x / @@
  -1,2 +1,2 @@ / -a / +A /  b` on `z\na\nb\n` fails, `Hunk #1 FAILED at 1.`
  (GNU's default, fuzz 2, applies it `with fuzz 1` -- diff--patch-fuzz-rej);
  on `a\nb\nz\n` it applies. (Elsewhere in the file, the shorter leading
  context is no constraint: `-5,3 +5,3 / -5 / +FIVE / 6 / 7` applies at
  offset 1 on a file with one extra line at the top.)
- A hunk with fewer trailing than leading context lines may only match at
  the end of the file, L = fileLines - oldLines + 1: `@@ -7,3 +7,3 @@ / 7 /
  8 / -9 / +NINE` on seq 1 9 applies; with two lines added at the top,
  `succeeded at 9 (offset 2 lines)`; with one line added at the bottom,
  `Hunk #1 FAILED at 7.`; on seq 3 9, `succeeded at 5 (offset -2 lines)`.
- A hunk with no old lines (an insertion with no context, `diff -U0`) is not
  compared: it goes at E, or at the end of a shorter file (`@@ -3,0 +4 @@
  +X` on `1\n` gives `1\nX\n`).
- Consumed lines: after a hunk applies at L, the lines before its trailing
  context are consumed (L + oldLines - trailingContext - 1); a later hunk
  may start on an earlier one's trailing context: on `a..f`, `@@ -1,4 +1,4
  @@ a -b +B c d` then `@@ -3,4 +3,4 @@ c d -e +E f` gives `a B c d E f`, no
  offset; a hunk whose context lies only in consumed lines fails (`s t u`
  before the first hunk: `Hunk #2 FAILED at 10.`).

Applying a hunk at L: write the input lines not yet written up to L - 1,
then the hunk's lines up to its last change (Context: the input line,
Delete: skip the input line, Insert: the patch line); its trailing context
is left for the next hunk or the final copy. After the last hunk, copy the
rest.

`.rej` text (unified): before the first rejected hunk of a file, `--- OLD`
+ oldTimeText and `+++ NEW` + newTimeText (each `\t`-separated when there is
a time text; names as stripped, `/dev/null` for no name; after -R or an
accepted reversal, the swapped ones); each hunk `@@ -R0 +R1 @@` + (` ` +
function, when there is one), R = `S` for a count of 1, `S-1,0` for 0 (so
`-0,0`), else `S,count`, S the side's start; then its lines with their marks.
A noNewline line is written as it is, without newline and without a `\ No
newline` line (verified: the hunk ` a`, `-b`, `\ No newline at end of
file`, `+B` is saved as ` a\n-b+B\n`; ` a`, `-b`, `+B`, `\ No newline at end
of file` as ` a\n-b\n+B`, the file ending there).
Verified example (-R of a patch with time texts and `@@ ... @@ fn`):
`--- t\t2024-01-02 00:00:00 +0000\n+++ t\t2024-01-01 00:00:00 +0000\n@@
-1,3 +1,4 @@ fn\n 1\n-TWO\n+2\n+3\n 4\n@@ -5,2 +0,0 @@\n-p\n-q\n`.

### New `src/components/BuiltinCommands/commands/patch/Patch.cpp`

`CreatePatchCommand()`, name `patch`, version `1.0.0`; help summary "apply a
diff file to an original", usage `patch [OPTION]... [ORIGFILE [PATCHFILE]]`;
notes: the no-terminal answers, unified diffs only and exact matching (until
the next task).

`Options()` -- every GNU patch 2.7.6 option (its `--help` and man page):
- treated here: `-d/--directory DIR`, `-E/--remove-empty-files`,
  `-f/--force`, `-i/--input FILE`, `-N/--forward`, `-o/--output FILE`,
  `-p/--strip NUM`, `-R/--reverse`, `-s/--silent` and `--quiet` (same id),
  `-t/--batch`, `-u/--unified` (the only format here), `--dry-run`,
  `--binary` (keeps the patch's CRs: no stripping, no message), `-v`
  (`kBuiltinOptionVersion`; `--version` is common).
- not treated here, treated by diff--patch-fuzz-rej: `-F/--fuzz NUM`,
  `-r/--reject-file FILE`, `--reject-format FORMAT`, `-b/--backup`,
  `-z/--suffix SUFFIX`, `-V/--version-control METHOD`,
  `--backup-if-mismatch`, `--no-backup-if-mismatch`, `-c/--context`,
  `-n/--normal`, `-l/--ignore-whitespace`.
- not treated: `-B/--prefix PREFIX`, `-Y/--basename-prefix PREFIX`,
  `-D/--ifdef NAME`, `-e/--ed`, `-g/--get NUM`, `-m/--merge[=STYLE]`
  (`{'m', "merge", Optional, "STYLE"}`), `-T/--set-time`, `-Z/--set-utc`,
  `-x/--debug NUM`, `--verbose`, `--posix`, `--quoting-style WORD`,
  `--read-only BEHAVIOR`, `--follow-symlinks`. (`-U` is not a patch option:
  `patch: invalid option -- 'U'` + Try, 2.)

`Run`:
1. Parse as `diff` does (`ParseBuiltinArgs`; an error is `patch: <error>` +
   `patch: Try 'patch --help' for more information.`, exit 2); then
   help/version, then `ReportNotTreated`. `-p X` not a number: `patch: ****
   strip count X is not a number` (X through `ShellEscapeQuoted`: `-p " 'q"`
   gives `strip count " 'q" is not a number`), 2; negative: `... is
   negative`. A third operand: `patch: X: extra operand` + Try, 2.
2. `-d DIR` first: `IO().ChangeDirectory(DIR)`; failing -> `patch: **** Can't
   change to directory DIR : No such file or directory`, 2.
3. The patch text: the PATCHFILE operand or `-i FILE` (the later wins), else
   standard input; read whole (`ReadWholeInput`). Unopenable: `patch: ****
   Can't open patch file NAME : No such file or directory`, 2.
4. For each `FilePatch`, in order (after -R, `SwapFilePatch` first):
   1. **Which file** (the GNU patch manual, "Multiple Patches in a File"):
      an ORIGFILE operand is the file for every patch. Otherwise the
      candidates are oldName and newName (indexName only when both are
      absent -- `Index: d/f` with `--- nosuch1`/`+++ nosuch2` finds no
      file). Among those that exist (`IO().Stat`), the best: fewest path
      components, then shortest last component, then shortest name, then
      the first (`d/e/f` vs `d/f` -p0 -> `d/f`; `d/e/f` vs `f` -> `f`). None
      exists and the old side is absent (maybe or surely): the best among
      those needing the fewest new directories. Else no file.
   2. **Creation and deletion** -- checked on the chosen name (or the first
      candidate when none exists), "empty" meaning missing or of size 0:
      | the file | the patch | printed (then answered as a reversed patch) |
      |---|---|---|
      | exists, not empty | old side surely absent | `The next patch would create the file X,\nwhich already exists!` |
      | missing | new side surely absent | `The next patch would delete the file X,\nwhich does not exist!` |
      | exists, empty | new side surely absent | `The next patch would empty out the file X,\nwhich is already empty!` |
      An empty existing file and a creation is no conflict (it is filled);
      neither is a git patch without hunks whose side git marks absent
      (`new file mode` on a missing file, `deleted file mode` on an empty
      one). Under -R the text is `The next patch, when reversed, would ...`.
      The answer follows on the same line (step 6's answers): `  Assume -R?
      [n] ` (`  Ignore -R? [n] ` under -R) + `\nApply anyway? [n] \n` +
      `Skipping patch.\n`; -N `  Skipping patch.\n`; -t `  Assuming -R.\n`
      (`  Ignoring -R.\n`), the patch then applied swapped; -f `  Applying it
      anyway.\n`, the patch then applied as it is (its hunk fails: `Hunk #1
      FAILED at 1.`). A skip here prints no `patching file` line and saves
      no `.rej`: just `N out of M hunk(s) ignored\n` (nothing when M is 0).
   3. **No file**: `can't find file to patch at input line N\n` (N =
      reportLine), `Perhaps you should have used the -p or --strip
      option?\n` (no -p given) or `Perhaps you used the wrong -p or --strip
      option?\n`, `The text leading up to this was:\n--------------------------\n`,
      each leadingText line prefixed with `|`, `--------------------------\n`;
      with -f or -t: `No file to patch.  Skipping patch.\n`; else `File to
      patch: \n`, `Skip this patch? [y] \n`, `Skipping patch.\n`. Then its
      hunks count as ignored (no `.rej`).
   4. Print `patching file NAME\n` (`checking file` with --dry-run; with -o,
      `patching file OUT (read from NAME)\n`; a git rename `patching file NEW
      (renamed from OLD)\n`, a copy `(copied from OLD)` -- the output is then
      newName, the input oldName; a rename whose OLD is missing and NEW
      exists, `(already renamed from OLD)`). A copy whose OLD is missing:
      `Cannot copy file without two valid file names\n`, skipped, exit 1.
   5. Hunks in order, runningOffset 0, nothing consumed: `LocateHunk(...,
      0, ...)`. Hunk #1 only, when it is not found and neither -f nor step
      2 decided already: swap and locate again; found -> `Reversed (or
      previously applied) patch detected!` (`Unreversed patch detected!`
      under -R) and the answer as in step 2 (`  Assume -R? [n] ` / `  Ignore
      -R? [n] `, `Apply anyway? [n] `, `Skipping patch.`; -N `  Skipping
      patch.`; -t `  Assuming -R.` / `  Ignoring -R.`, the whole file patch
      kept swapped). A skip ignores all its hunks. With -f no second look:
      the hunk just fails. If not accepted, swap back. A patch whose old
      side is surely absent fails on a non-empty file (`Hunk #1 FAILED at
      1.`); one only maybe absent (`-0,0` with a real time) inserts at the
      top (`new\nold\n`).
   6. A hunk not found: `Hunk #K FAILED at E.\n` (E as in `LocateHunk`; ` (different line
      endings)` before the `.` when exactly one of the hunk's first old line
      and the file's line E ends in `\r\n`). Found at an offset: `Hunk #K
      succeeded at L (offset N lines).\n` (`line` only for N = 1: `offset -1
      lines`). Failed and ignored hunks go to the `.rej` text. If the file
      patch has `malformed`, print it now (stderr, exit 2) and stop: this
      file is not written.
   7. **Output** (not with --dry-run): the new content goes to a temporary
      file next to the target (`.NAME.patchtmp` in the same directory,
      `kFileOpenWriteCreateTruncate`) and is moved into place with
      `IO().Rename(tmp, target)`; on any failure the temporary file is
      removed. Missing directories of a created file are created. If any
      hunk was found at an offset or failed, the original is first saved as
      `NAME.orig` (empty when the file did not exist: `-f` on the deletion
      of a missing `old` leaves an empty `old.orig`) -- once per file per
      run: a second patch of the same file in one run keeps the first
      `.orig`. With `-o FILE`, every patched content is appended to FILE
      (truncated once, at the start) and no `.orig` is made; rejects go to
      `FILE.rej`. A result that is empty is removed when the new side is
      surely absent, or with -E; when the new side is surely absent but the
      result is not empty: `Not deleting file X as content differs from
      patch\n`, exit 1. A git rename removes the old file after writing the
      new one.
   8. Failed or ignored hunks: `N out of M hunk FAILED` (`hunks` when M !=
      1; `ignored` when the file patch was skipped), then, unless
      --dry-run, ` -- saving rejects to file NAME.rej\n` and the `.rej`
      written (replaced by the first rejects of a run, appended to by later
      ones in the same run; `w~` gives `w~.rej`); with --dry-run just `\n`.
5. Exit 1 if any hunk failed or was ignored, any file patch was skipped, or
   a deletion was refused; else 0. Fatal errors 2.

**-s/--silent** (verified) drops: `patching file`/`checking file` lines,
`Hunk #K ...` lines, `can't find file ...` with its `Perhaps ...` line, the
`Skipping patch.` line after `Apply anyway?`/`Skip this patch?`, the
`(Stripping trailing CRs ...)` line, `Not deleting file ...`. It keeps:
every question and its lead-in (`Reversed ...  Assume -R? [n] `, `Apply
anyway? [n] `, `The next patch would ...`), the `The text leading up to this
was:` block, `File to patch: `, `Skip this patch? [y] `, the same-line
answers (`  Skipping patch.`, `  Assuming -R.`, `No file to patch.  Skipping
patch.`), the `N out of M ...` summaries and fatal errors.

### Registration and build

- `BuiltinCommandList.h`: declare and add `CreatePatchCommand()` in
  `CreateStandardBuiltinCommands()` alphabetically (after `mv`/`nl`, before
  `printf`).
- `src/components/BuiltinCommands/CMakeLists.txt`: `commands/patch/Patch.cpp`,
  `commands/patch/PatchParse.cpp`, `commands/patch/PatchApply.cpp`, after
  the `commands/nl/` line, in the list's order.
- `tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt`:
  `PatchTest.cpp` (after `NlTest.cpp`).

### diff fixes (review of #73)

- `Diff.cpp` (`DiffTwoFiles`, the ed-style missing-newline check, ~line
  655): today an identical pair is always silent in `-e` style. GNU diff
  3.10 (verified, `n1` and `n2` both `a\nb` without a final newline): `diff
  -e n1 n2` prints nothing on stdout, `diff: n1: No newline at end of
  file\n\ndiff: n2: No newline at end of file\n\n` on stderr, exit 2;
  `diff -s -e n1 n2` the same (no `are identical` line); `diff -e n1 m` (m
  = `a\nb\n`) warns for `n1` only, exit 2; `diff -e - n2 < n1` warns for
  both. Silent 0 only for the same file: `diff -e n1 n1`, `n1 ./n1`, `n1
  d/../n1`, `- -` (`-s`: `Files n1 and n1 are identical`, 0). Haisos has no
  links, so "the same file" is: both `-`, or neither `-` and
  `IO().ResolvePath` equal. -q stays as it is for `n1 n2` (silent 0), but
  compares the bytes as read: `diff -q -e n1 m` is `Files n1 and m
  differ`, 1 (today 0). Bump diff to 1.1.1; drop the exception from its
  CLAUDE.md row.
- `Diff.cpp` `DiffFindLongOption` (~line 73): returns null when two prefix
  matches come before a later exact match in the table. Look for an exact
  match over the whole table first, then a unique prefix, as the shared
  parser's `FindLongOption` (`BuiltinCommand.cpp`) does. No table today has
  the shape (only `exclude`/`exclude-from`), so move its declaration to
  `Diff.h` and test it on a small table directly.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/PatchTest.cpp`,
`TEST_F(BuiltinCommandsTest, Patch...)`, files under `/p`, the patch given as
`RunCaptured`'s input (`patch` reads stdin) unless stated. Every expected
output is GNU patch 2.7.6's (`LC_ALL=C`, no terminal), `-F0` where fuzz would
change it; `seq 1 10` means the lines `1\n` .. `10\n`; `P1` is `--- f\n+++
g\n@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n`.

- `PatchAppliesUnified`: `f` = seq 1 10, P1 with `-p0` -> out `patching
  file f\n`, 0; `f` has `five` in line 5; no `f.orig`, no temporary file
  left in `/p`.
- `PatchOffsetKeepsOrig`: `f` = `x\ny\n` + seq 1 10, P1 -> `patching file
  f\nHunk #1 succeeded at 4 (offset 2 lines).\n`, 0; `f.orig` holds the old
  content. Two hunks both at +2: `--- x\n+++ x\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n
  c\n@@ -10,3 +10,3 @@\n j\n-k\n+K\n l\n` on `z\nz\n` + `a`..`m` ->
  `patching file x\nHunk #1 succeeded at 3 (offset 2 lines).\nHunk #2
  succeeded at 12 (offset 2 lines).\n`, 0.
- `PatchSearchOrder`: the `LocateHunk` examples of "Where a hunk applies":
  `@@ -5,3 +5,3 @@\n p\n-q\n+Q\n r\n` on `1 2 p q r 6 p q r 10` -> `Hunk #1
  succeeded at 7 (offset 2 lines).`; on `1 2 3 p q r 7 8 p q r 12` ->
  `succeeded at 4 (offset -1 lines).`; the end-anchored `@@ -7,3 +7,3 @@\n
  7\n 8\n-9\n+NINE\n` on seq 1 9 + `a` -> `Hunk #1 FAILED at 7.`, on `a b` +
  seq 1 9 -> `succeeded at 9 (offset 2 lines).`; the overlapping pair on
  `a..f` -> `a B c d E f`, out `patching file x\n`; the `-U0` insertion
  `@@ -3,0 +4 @@\n+X\n` on `1\n` -> `1\nX\n`; a second hunk whose context
  is only before the first -> `Hunk #2 FAILED at 10.`.
- `PatchReversedWithoutTerminal`: `f` already patched, P1 -p0 -> `patching
  file f\nReversed (or previously applied) patch detected!  Assume -R? [n]
  \nApply anyway? [n] \nSkipping patch.\n1 out of 1 hunk ignored -- saving
  rejects to file f.rej\n`, 1; `f.rej` = P1 as given. `-N` -> `patching
  file f\nReversed (or previously applied) patch detected!  Skipping
  patch.\n1 out of 1 hunk ignored -- saving rejects to file f.rej\n`, 1.
  `-t` -> `patching file f\nReversed (or previously applied) patch
  detected!  Assuming -R.\n`, 0, `f` back to seq 1 10. `-f` -> `patching
  file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED -- saving rejects to
  file f.rej\n`, 1, `f.orig` made. `-R` on the patched file -> `patching
  file f\n`, 0. `-R -t` on the unpatched file -> `patching file
  f\nUnreversed patch detected!  Ignoring -R.\n`, 0, `f` patched. `u` =
  seq 1 20 with `2` -> `TWO`, and a patch `@@ -1,3 +1,3 @@` (1, -2 +TWO, 3)
  + `@@ -14,3 +14,3 @@` (14, -15 +FIFTEEN, 16) with `-t -F0` -> `patching
  file u\nReversed (or previously applied) patch detected!  Assuming
  -R.\nHunk #2 FAILED at 14.\n1 out of 2 hunks FAILED -- saving rejects to
  file u.rej\n`, 1; `u.rej` holds hunk #2 swapped (` 14\n-FIFTEEN\n+15\n
  16\n`).
- `PatchFailedHunkWritesRej`: `x` = `a`..`m`, patch with header times
  `--- x\t2024-01-02 03:04:05.000000000 +0000` / `+++ x\t2024-01-03
  03:04:05.000000000 +0000`, hunk 1 `-1,3 +1,3` (a, -b +B, c) and hunk 2
  `-10,3 +10,3` (j, -Q +K, l) -> `patching file x\nHunk #2 FAILED at
  10.\n1 out of 2 hunks FAILED -- saving rejects to file x.rej\n`, 1;
  `x.rej` = `--- x\t2024-01-02 03:04:05.000000000 +0000\n+++
  x\t2024-01-03 03:04:05.000000000 +0000\n@@ -10,3 +10,3 @@\n j\n-Q\n+K\n
  l\n`; `x.orig` exists; `x` has `B`. `--dry-run -R` on the result ->
  `checking file x\nHunk #2 FAILED at 10.\n1 out of 2 hunks FAILED\n`, 1,
  nothing written. The -R `.rej` of "PatchApply" (two failed hunks, `fn`)
  byte for byte; the two noNewline `.rej` examples. Two file patches of one
  `f` in one input, both failing: the second `.rej` appended to the first
  (a stale `f.rej` from before is replaced); both at an offset: `f.orig`
  holds the content before the first.
- `PatchCreatesAndDeletes`: the `diff -ruN a b` output of a tree with `keep`
  changed, `new` added, `old` removed (`1970-01-01 00:00:00.000000000
  +0000` on the missing sides), with `-p1` in a directory holding `keep` and
  `old` -> `patching file keep\npatching file new\npatching file old\n`, 0;
  `old` gone, `new` = `new\n`. Again -> `patching file keep\nReversed (or
  previously applied) patch detected!  Assume -R? [n] \nApply anyway? [n]
  \nSkipping patch.\n1 out of 1 hunk ignored -- saving rejects to file
  keep.rej\nThe next patch would create the file new,\nwhich already
  exists!  Assume -R? [n] \nApply anyway? [n] \nSkipping patch.\n1 out of 1
  hunk ignored\nThe next patch would delete the file old,\nwhich does not
  exist!  Assume -R? [n] \nApply anyway? [n] \nSkipping patch.\n1 out of 1
  hunk ignored\n`, 1 (no `new.rej`, no `old.rej`). With `-N` the two
  conflicts end `which already exists!  Skipping patch.\n1 out of 1 hunk
  ignored\n` (and `does not exist!`). With `-f` (`keep` unpatched, `new`
  existing, `old` missing): `patching file keep\nThe next patch would
  create the file new,\nwhich already exists!  Applying it anyway.\npatching file
  new\nHunk #1 FAILED at 1.\n1 out of 1 hunk FAILED -- saving rejects to
  file new.rej\nThe next patch would delete the file old,\nwhich does not
  exist!  Applying it anyway.\npatching file old\nHunk #1 FAILED at 1.\n1
  out of 1 hunk FAILED -- saving rejects to file old.rej\n`, 1; `old.orig`
  empty, `old` not created; `old.rej` = `--- old\t<time>\n+++
  old\t1970-01-01 00:00:00.000000000 +0000\n@@ -1 +0,0 @@\n-gone\n`.
  Single-file cases (`--- f\t<epoch>` / `+++ f\t<2024>` creations and the
  reverse deletions): an empty `f` and a creation -> `patching file f\n`,
  `f` = `new\n`; an empty `f` and a deletion -> `The next patch would empty
  out the file f,\nwhich is already empty!  Assume -R? [n] \n...`; `-R` of
  the deletion on a non-empty `f` -> `The next patch, when reversed, would
  create the file f,\nwhich already exists!  Ignore -R? [n] \nApply anyway?
  [n] \nSkipping patch.\n1 out of 1 hunk ignored\n`, 1; `-R` of the
  creation on a missing `f` -> `..., when reversed, would delete the file
  f,\nwhich does not exist!  Ignore -R? [n] \n...`.
- `PatchEpochWindow`: a `-0,0 +1` creation on an existing `new` = `old\n`,
  the old time varied: each counting time of "Recognising patches" gives
  `The next patch would create the file new,...`; `1970-01-02 02:00:00
  +0000`, `1969-12-30 23:00:00 +0000` and `2024-01-01 00:00:00 +0000` give
  `patching file new\n`, 0, `new` = `new\nold\n`.
- `PatchGitDiff`: `diff --git a/keep b/keep\nindex 814f4a4..1c7f1bd
  100644\n--- a/keep\n+++ b/keep\n@@ -1,2 +1,2 @@\n one\n-two\n+TWO\ndiff
  --git a/old b/old\ndeleted file mode 100644\nindex 1fd0b46..0000000\n---
  a/old\n+++ /dev/null\n@@ -1 +0,0 @@\n-gone\ndiff --git a/new b/new\nnew
  file mode 100644\nindex 0000000..3e75765\n--- /dev/null\n+++ b/new\n@@
  -0,0 +1 @@\n+new\n` with `-p1` -> `patching file keep\npatching file
  old\npatching file new\n`, 0; then `-R -p1` -> the same three lines, 0,
  the tree back. Without `-p` -> the same as `-p1`. With `-p0` in an empty
  directory -> `can't find file to patch at input line 5\nPerhaps you used
  the wrong -p or --strip option?\nThe text leading up to this
  was:\n--------------------------\n|diff --git a/keep b/keep\n|index
  814f4a4..1c7f1bd 100644\n|--- a/keep\n|+++
  b/keep\n--------------------------\nFile to patch: \nSkip this patch? [y]
  \nSkipping patch.\n1 out of 1 hunk ignored\nThe next patch would delete
  the file a/old,\nwhich does not exist!  Assume -R? [n] \nApply anyway?
  [n] \nSkipping patch.\n1 out of 1 hunk ignored\npatching file b/new\n`,
  1, `b/new` created; with `-t` the first block ends `No file to patch.
  Skipping patch.\n1 out of 1 hunk ignored\n`, then `The next patch would
  delete the file a/old,\nwhich does not exist!  Assuming -R.\npatching file
  a/old\npatching file b/new\n`, 1, `a/old` = `gone\n`.
- `PatchGitWithoutHunks`: `diff --git a/e b/e\ndeleted file mode
  100644\nindex e69de29..0000000\n` on an empty `e` -> `patching file e\n`,
  0, `e` gone; `new file mode` for `n` -> `patching file n\n`, `n` empty;
  again on the now empty `n` -> `The next patch would empty out the file
  n,\nwhich is already empty!  Assume -R? [n] \nApply anyway? [n]
  \nSkipping patch.\n`, 1 (no summary: no hunks). A rename only (`rename
  from k`/`rename to k2`) -> `patching file k2 (renamed from k)\n`; with
  `--dry-run` when `k2` already exists and `k` does not -> `checking file
  k2 (already renamed from k)\n`. A copy -> `patching file k3 (copied from
  k)\n`, both there; a copy from a missing `k` -> `Cannot copy file without
  two valid file names\n`, 1. A mode change only on an existing `k` ->
  `patching file k\n`, 0, `k` unchanged.
- `PatchRenameFromGit`: `diff --git a/keep b/kept\nsimilarity index
  50%\nrename from keep\nrename to kept\n--- a/keep\n+++ b/kept\n@@ -1,2
  +1,2 @@\n one\n-two\n+TWO\n` with `-p1` -> `patching file kept (renamed
  from keep)\n`, 0; `keep` gone, `kept` = `one\nTWO\n`.
- `PatchNames`: with `--dry-run`, `d/e/f`, `d/f` and `f` all `a\n`, the
  patch `--- d/e/f\n+++ d/f\n@@ -1 +1 @@\n-a\n+A\n` -> no -p `checking file
  f`, `-p0` `checking file d/f`, `-p1` and `-p2` `checking file f`, `-p3`
  the `can't find file` block; `--- /x//d/e/f` / `+++ /x/d/f` with `-p2` ->
  `checking file d/f`; `--- sp ace\t<time>` -> `checking file 'sp ace'`,
  bare `--- sp ace` -> the `can't find file ... Perhaps you should have
  used the -p or --strip option?` block; `--- "sp ace"` -> `checking file
  'sp ace'`; `"q\"t"` -> `checking file 'q"t'`; `Index: d/f` with
  `nosuch1`/`nosuch2` -> `can't find file to patch at input line 4`, with
  `/dev/null` on both sides -> `checking file d/f`. Leading text: `header
  text\n` + a patch of `f` + `trailing words\nmore words\n` + `--- nosuch\n+++
  nosuch\n@@ ...` with `-t` -> `patching file f\ncan't find file to patch
  at input line 11\n...|trailing words\n|more words\n|--- nosuch\n|+++
  nosuch\n...No file to patch.  Skipping patch.\n1 out of 1 hunk ignored\n`.
- `PatchOptions`: `-o out.txt -i PATCH -p1` (the `-ruN` patch) ->
  `patching file out.txt (read from keep)\npatching file out.txt (read from
  new)\npatching file out.txt (read from old)\n`, `out.txt` =
  `one\nTWO\nnew\n`, originals untouched; `-o out` with a failing hunk ->
  `patching file out (read from f)\nHunk #1 FAILED at 1.\n1 out of 1 hunk
  FAILED -- saving rejects to file out.rej\n`, `out` = `f`'s content; with
  an offset, no `.orig` anywhere. `-E` on `--- old\n+++ old\n@@ -1 +0,0
  @@\n-gone\n` removes `old` (without `-E`: `old` stays, empty). `Not
  deleting`: `g` = `gone\nextra\n`, `--- g\t2024-...` / `+++
  g\t1970-01-01 00:00:00 +0000` / `@@ -1 +0,0 @@ -gone` -> `patching file
  g\nNot deleting file g as content differs from patch\n`, 1, `g` =
  `extra\n`. `-d sub` applies in `sub`; ORIGFILE: `patch x PATCH` patches
  `x` whatever the headers say; `w~` failing -> `... saving rejects to file
  w~.rej`.
- `PatchSilent`: `-s` prints nothing on success, on an offset, or for
  `Not deleting` (exit 1); on a reversed patch `Reversed (or previously
  applied) patch detected!  Assume -R? [n] \nApply anyway? [n] \n1 out of 1
  hunk ignored -- saving rejects to file f.rej\n`; with `-N` `Reversed (or
  previously applied) patch detected!  Skipping patch.\n1 out of 1 hunk
  ignored -- saving rejects to file f.rej\n`; `-t` `Reversed ...  Assuming
  -R.\n`; a failed hunk `1 out of 2 hunks FAILED -- saving rejects to file
  x.rej\n`; no file `The text leading up to this
  was:\n--------------------------\n|--- f\n|+++
  g\n--------------------------\nFile to patch: \nSkip this patch? [y] \n1
  out of 1 hunk ignored\n` (with -t, `No file to patch.  Skipping patch.\n`
  in place of the two questions); the create/delete conflicts keep their
  two lines and questions but lose `Skipping patch.`.
- `PatchLineEndings`: `f` = `a\r\nb\r\nc\r\n`, an LF patch -> `patching
  file f\nHunk #1 FAILED at 1 (different line endings).\n...`; an all-CRLF
  patch on `a\nb\nc\n` -> `(Stripping trailing CRs from patch; use --binary
  to disable.)\npatching file f\n`, `f` = `a\nB\nc\n`; the same on the CRLF
  file -> the stripping line, then `Hunk #1 FAILED at 1 (different line
  endings).`; with `--binary` -> `patching file f\n`, `f` =
  `a\r\nB\r\nc\r\n`. No newline: `n` = `a\nb` (no final newline), `@@ -1,2
  +1,2 @@\n a\n-b\n\\ No newline at end of file\n+B\n` -> `n` = `a\nB\n`;
  and `-R` of `+B\n\\ No newline...` restores `a\nb\n`.
- `PatchMalformed`: the three cases of "Recognising patches" (`xx` after an
  offset hunk: stdout `patching file t\nHunk #1 succeeded at 2 (offset 1
  line).\n`, stderr `patch: **** malformed patch at line 10: xx\n\n`, 2,
  `t` unchanged; a second file's malformed hunk after a first file patched:
  the first stays patched; the end-of-input cases with `line 4:  \n\n` and
  the completed `-1,3 +1,3 -q +Q` on `q\n\n\n` -> `Q\n\n\n`, 0). A trailing
  `zz\n` after a complete patch is ignored (0).
- `PatchErrors`: `--bogus` -> err `patch: unrecognized option
  '--bogus'\npatch: Try 'patch --help' for more information.\n`, 2; `-U`
  -> `patch: invalid option -- 'U'` + Try, 2; `-p x` -> `patch: **** strip
  count x is not a number\n`, 2; `-p -1` -> `patch: **** strip count -1 is
  negative\n`, 2; `a b c` -> `patch: c: extra operand\n` + Try, 2; `-i
  nosuch` -> `patch: **** Can't open patch file nosuch : No such file or
  directory\n`, 2; `-d nodir` -> `patch: **** Can't change to directory
  nodir : No such file or directory\n`, 2; input `garbage\n` -> `patch:
  **** Only garbage was found in the patch input.\n`, 2; empty input ->
  nothing, 0; `-v` -> `patch (HaisosOS builtin) 1.0.0\n`; `-m`, `-x 1`,
  `--verbose` reported as not treated.
- `PatchHunkAtStartNeedsStart`: `x` = `z\na\nb\n`, `--- x\n+++ x\n@@ -1,2
  +1,2 @@\n-a\n+A\n b\n` -> `patching file x\nHunk #1 FAILED at 1.\n1 out
  of 1 hunk FAILED -- saving rejects to file x.rej\n`, 1 (GNU's `-F0`
  output; GNU's default prints `Hunk #1 succeeded at 2 with fuzz 1 (offset
  1 line).` -- diff--patch-fuzz-rej changes this test).
- `PatchRoundTripWithDiff` (acceptance scenario 2), through `hsh -c` with
  absolute `/bin/` paths, in `/p` holding `a.cpp` (a 30-line file) and
  `b.cpp` (the same with two lines changed far apart and one added):
  `/bin/diff -u a.cpp b.cpp > p.diff; /bin/patch -p0 < p.diff; /bin/diff
  a.cpp b.cpp; echo $?` -> out `patching file a.cpp\n0\n` (both header names
  exist and are as good: the first, `a.cpp`, is patched).

`DiffTest.cpp`:
- `DiffEdIdenticalMissingNewline` rewritten to the verified cases of "diff
  fixes" (`n1 n2` identical: stderr both warnings, 2; `-s`: the same, no
  identical line; `n1 m`: one warning, 2; `- n2` with `n1` as input: both;
  `n1 n1`, `n1 ./n1`, `n1 d/../n1`, `- -`: silent 0; `-s n1 n1`: `Files n1
  and n1 are identical\n`, 0; `-q n1 n2`: silent 0; `-q n1 m`: `Files n1
  and m differ\n`, 1). `DiffEdScript` keeps its expectations.
- `DiffFindLongOptionExactFirst`: on a table `ab-x`, `ab-y`, `ab` (in that
  order) `ab` finds `ab`; `ab-` is ambiguous (null); `ab-x` finds `ab-x`.

`BuiltinCommandsTest.cpp`: `"patch"` in the expected list of
`ListsEveryBuiltinSortedWithAVersion` (after `"nl"`).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Patch*:BuiltinCommandsTest.Diff*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `patch` in the opening list
  and the `commands/` list (`commands/patch/`: `PatchParse` the reader,
  `PatchApply` the hunk placement and `.rej` text, `Patch.cpp` the
  command); a row (1.0.0; treated as above; exceptions: questions answered
  as without a terminal; unified diffs only and exact matching -- GNU's
  `-F 0` -- for now; no permissions, so git modes are ignored; a header time
  stamp `ParseDateString` does not understand never counts as the epoch;
  indented patches not recognised). The `diff` row: 1.1.1, the `-e`
  missing-newline exception removed (two distinct files warn and exit 2; the
  same resolved path is silent), `-q` comparing the bytes as read.
- `BuiltinText.h`'s entry: `ReadWholeInput`.
- Root `CLAUDE.md`: `patch` in the Builtin Commands sentence, the
  directory tree's BuiltinCommands list and the table (`Applies unified and
  git diffs (-p -R -N -i -o -d -E -s -t -f --dry-run --binary), offsets,
  .orig and .rej files`).

## Acceptance

- [ ] `patch` registered; standard `--help`; `-v`/`--version`; `man patch`.
- [ ] Every GNU patch 2.7.6 option in `Options()`; untreated ones reported.
- [ ] File choice, -p stripping, `/dev/null` and epoch rules, git headers
      (with and without hunks), creation and deletion, reversed detection
      with the no-terminal answers (`Assume -R?` / `Ignore -R?`), `-N -f -t
      -R -E -o -d -s --dry-run --binary`, exactly as specified.
- [ ] `LocateHunk` places hunks as the verified examples (nearest first,
      forward first, start/end anchoring, consumed lines; fuzz a
      parameter, 0 here); offset and failure messages byte for byte;
      `.rej` in unified form; `.orig` kept on a mismatch.
- [ ] Malformed and truncated hunks, garbage, CRs as specified.
- [ ] Every result written to a temporary file then `IO().Rename`d; nothing
      reached except through `context.IO()`.
- [ ] Exit 0/1/2; the round trip with `diff -u` works.
- [ ] The two diff fixes, with their tests; `ReadWholeInput` shared.
- [ ] No identifier, comment or test name taken from another program's
      source; CLAUDE.md rows; `ListsEveryBuiltinSortedWithAVersion`; all
      tests pass.

## Out of scope

- Fuzz, `-F`, `-l`, backups beyond the mismatch `.orig` (`-b -z -V
  --[no-]backup-if-mismatch`), `-r`, `--reject-format`, context and normal
  diff input (diff--patch-fuzz-rej).
- Ed scripts, `--merge`, `-D`, `-g`, `-T`/`-Z`, `-B`/`-Y`: not treated.
