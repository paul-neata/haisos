# Task diff--patch-core: the patch builtin -- unified diffs, offsets, reversed patches, rejects

- Rock: diff
- Depends on: base--fs-rename-times (`IFileIO::Rename`, contract 1), coreutils--sort (`OpenInputOperand` in `BuiltinText.h`), diff--diff-core (only for the round-trip test)
- Size: ~1000 changed lines in ~7 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the patch builtin: unified and git diffs, offsets, rejects

## Goal

`patch` applies unified diffs as GNU patch 2.7.6 does (C locale, no
terminal), printing exactly its messages: one or many files per patch,
`diff --git` headers with git's `a/` `b/` prefixes (`-p1`), `/dev/null` and
epoch time stamps for created and deleted files, `-p NUM`, `-R`,
`-N/--forward`, `-i FILE` (standard input by default), the `[ORIGFILE
[PATCHFILE]]` operands, `-o FILE`, `-d DIR`, `--dry-run`, `-s/--silent`,
`-f/--force`, `-t/--batch`, `-E/--remove-empty-files`, hunks found at an
offset (`Hunk #1 succeeded at 12 (offset 2 lines).`, the original kept as
`FILE.orig`), reversed patches detected (`Reversed (or previously applied)
patch detected!  Assume -R? [n] ` answered as without a terminal), failed
hunks reported and saved to `FILE.rej` in unified form, exit status 0/1/2.
Each patched file is written to a temporary file and moved into place with
`Rename`. This is the `patch -p0 < p` half of acceptance scenario 2
(goal.md).

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`,
`commands/cat/Cat.cpp` (reading standard input with `StopRequested()` and
`kIOInterrupted`), `src/components/Filesystem/FilesystemUtils.h`
(`kFileOpenWriteCreateTruncate`, `kFileCreateMode`, `ReadWholeFile`), the
plans `develop-plan/tasks/base--fs-rename-times.md` and
`develop-plan/tasks/coreutils--du-cmp.md` (cmp: how a diffutils-style
command handles `-v` and its Try line).

What earlier tasks provide, as if on develop:
- base--fs-rename-times: `int IFileIO::Rename(const std::string& oldPath, const std::string& newPath)`
  (0, `kFileSystemError`, or `kFileSystemCrossDevice`; replaces an existing
  file), constants `kFileSystemError`/`kFileSystemCrossDevice` in
  `interfaces/IFileSystemService.h`.
- coreutils--sort, `BuiltinText.h`: `OpenInputOperand(context, name, failure)`
  (`-` is descriptor 0; `InputOpenFailure { None, Missing, Directory, Denied, BadDescriptor }`).
- diff--diff-core: the `diff` builtin (used by the round-trip test).

Reference: GNU patch 2.7.6 (`src/patch.c`, `src/pch.c`, `src/util.c`); this
plan describes its behaviour -- implement from the description, do not copy
GPL code. The task container has no `patch` binary: the expected outputs
below were taken from GNU patch 2.7.6 with `LC_ALL=C`; for a case not listed,
follow the rules here.

## Changes

Rules that bite: `ICurrentProcess` is the only door out -- every file is
read, written, renamed and removed through `context.IO()` (and `-d` moves
the process's own working directory with `IO().ChangeDirectory`); no
`IFileSystem`/`IHaisosOS` held. GNU's messages byte for byte; every GNU
patch option in `Options()` (untreated ones reported); `--help` from
`BuiltinHelpText`; registered in `CreateStandardBuiltinCommands()` (and so in
the `haisos --init` template); sources in the CMakeLists; portable C++17, no
POSIX headers, no `<regex>`; reading stops on `TriggerStop()`.
Nothing here creates a link.

Where GNU asks a question (`ask()`), Haisos always behaves as GNU does with
no terminal: the question is printed to stdout followed by `\n`, and the
default answer is taken. A documented exception (GNU reads `/dev/tty` when
stdout is a terminal).

All of patch's own messages (`say`) go to **stdout**; fatal errors go to
stderr as `patch: **** MESSAGE\n` and exit 2 (no Try line); `pfatal`-style
ones add ` : <reason>` (note the space before the colon): `patch: **** Can't
open patch file nosuch : No such file or directory`. Names in messages are
`ShellEscapeQuoted(name)` (quoted only when needed).

### New `src/components/BuiltinCommands/commands/patch/PatchParse.h` / `.cpp`

```cpp
enum class PatchLineKind : char { Context = ' ', Delete = '-', Insert = '+' };
struct PatchLine {
    PatchLineKind kind;
    std::string text;      // the line, '\n' included unless noNewline
    bool noNewline = false; // followed by "\ No newline at end of file"
};
struct PatchHunk {
    int64_t oldFirst = 0, oldCount = 0;  // GNU's pch_first: when oldCount is 0, the number after "-" plus 1
    int64_t newFirst = 0, newCount = 0;  // likewise
    std::vector<PatchLine> lines;        // in the patch's order
    std::string function;                // what follows the second "@@" (for the .rej)
    int64_t prefixContext = 0, suffixContext = 0; // leading / trailing context lines
};
enum class PatchFormat { Unified };      // diff--patch-fuzz-rej adds Context and Normal
struct FilePatch {
    PatchFormat format = PatchFormat::Unified;
    std::optional<std::string> oldName, newName, indexName; // after -p; nullopt: /dev/null or too few components
    std::string oldTimeText, newTimeText;   // the rest of the ---/+++ line after the name, '\n' and '\r' removed
    int oldSaysNonexistent = 0, newSaysNonexistent = 0; // 0 no, 1 maybe, 2 surely (GNU's p_says_nonexistent)
    bool gitDiff = false, gitRename = false, gitCopy = false;
    bool stripTrailingCr = false;
    int64_t firstLine = 0;                  // patch input line where this patch's hunks start (for messages)
    std::string leadingText;                // the input lines from the end of the previous patch up to the hunks
    std::vector<PatchHunk> hunks;
};

class PatchReader {   // a plain class over the whole patch text
public:
    explicit PatchReader(std::string text);
    // The next file patch, or nullopt at the end. |strip| is -p (-1: not given).
    // On a malformed hunk returns nullopt with |error| set to GNU's message.
    std::optional<FilePatch> Next(int strip, std::string& error);
    bool SawAnyPatch() const;
};

// -R and GNU's "dwim" retry: swaps old and new (names, time texts, says-
// nonexistent, each hunk's ranges, Delete <-> Insert; within each run of
// changed lines the deletions come first).
void SwapFilePatch(FilePatch& patch);
```

Parsing, GNU's `intuit_diff_type` restricted to unified diffs:
- Scan lines. `Index: NAME` sets indexName. `diff --git A B` starts a git
  patch: oldName/newName from A and B (each a word, or a C-quoted `"..."`
  string), stripped by -p; then `index X..Y [mode]` (an all-zero X or Y says
  that side surely does not exist: 2), `new file mode M` (oldSays = 2),
  `deleted file mode M` (newSays = 2), `old mode`/`new mode` (ignored: no
  permissions), `rename from`/`rename to` (gitRename), `copy from`/`copy to`
  (gitCopy). A second `diff --git` before any hunk ends a hunk-less patch
  (a pure rename/copy/mode change).
- `--- NAME[\tTIME]` and `+++ NAME[\tTIME]`: the name runs to the first
  whitespace, except that a name may hold spaces when a tab follows (the
  time stamp separator); a `"..."` name is C-unquoted. `/dev/null` gives no
  name and counts as a time stamp at the epoch. Strip with -p: remove that
  many leading components (runs of `/` count as one separator); without -p
  keep only the last component; a name with fewer components than -p asks
  for is dropped (no name). The time text is kept for the `.rej` header. A
  time stamp `YYYY-MM-DD HH:MM:SS[.fraction] +HHMM` (or `-HHMM`) within 25
  hours before / 26 hours after the epoch counts as the epoch (GNU's rule
  for "this file does not exist").
- `@@ -A[,B] +C[,D] @@[ FUNC]` starts the hunks (B, D default 1). With
  `-0,0` (`A` 0 and B 0) oldSays = 2 if the old header time is the epoch (or
  `/dev/null`), else 1 (unless git already set it); likewise `+0,0` for
  newSays. If a header line ended in `\r\n`, every patch line has its `\r`
  stripped and `(Stripping trailing CRs from patch; use --binary to
  disable.)\n` is printed once for that file.
- Hunk body: lines starting with ' ', '-', '+' until B old and D new lines
  are read; an empty line counts as an empty context line; `\ ` after a
  line marks it noNewline. Anything else before the counts are met:
  `patch: **** malformed patch at line N: LINE` (LINE as read, its newline
  included), exit 2. More `@@` lines continue the same file patch.
- The first line that starts nothing (no header, no hunk) when no patch was
  found yet, with input that is not empty: `patch: **** Only garbage was
  found in the patch input.`, 2. Empty input: nothing, 0. Trailing garbage
  after the last patch is ignored.

### New `src/components/BuiltinCommands/commands/patch/PatchApply.h` / `.cpp`

```cpp
struct PatchTarget {           // the input file as lines
    std::vector<std::string> lines;   // each with its '\n' (the last may lack it)
};
// GNU's locate_hunk: the 1-based input line where |hunk| applies with |fuzz|
// context lines ignored at each end (diff--patch-core always passes 0),
// searching from hunk.oldFirst + inOffset; updates inOffset; 0 if nowhere.
// lastFrozenLine: the input lines already consumed by earlier hunks.
int64_t LocateHunk(const PatchTarget&, const PatchHunk&, int64_t fuzz, int64_t lastFrozenLine, int64_t& inOffset);
```

`LocateHunk`, exactly (GNU's rules; `pat` = the hunk's old lines -- Context
and Delete -- count; `context = max(prefixContext, suffixContext)`;
`prefixFuzz = fuzz + prefixContext - context`, `suffixFuzz = fuzz +
suffixContext - context`; `firstGuess = oldFirst + inOffset`; `maxWhere =
inputLines - (pat - suffixFuzz) + 1`, `minWhere = lastFrozenLine + 1`,
`maxPos = maxWhere - firstGuess`, `maxNeg = firstGuess - minWhere`, `maxOffset =
max(maxPos, maxNeg)`):
- `pat == 0`: return firstGuess.
- if `firstGuess <= maxNeg`: `maxNeg = firstGuess - 1`.
- `prefixFuzz < 0 && oldFirst <= 1` (the hunk is at the start of the file):
  if also `suffixFuzz < 0` it must match the whole file (`pat ==
  inputLines`, else 0); try only offset `1 - firstGuess` (if
  `lastFrozenLine <= prefixContext` and that offset `<= maxPos`), matching
  with suffix fuzz `suffixFuzz` and prefix fuzz 0.
- else if `prefixFuzz < 0`: `prefixFuzz = 0`.
- `suffixFuzz < 0` (it must match at the end of the file): try only
  `offset = firstGuess - (inputLines - pat + 1)`, as a backward offset, if
  `offset <= maxNeg`.
- otherwise for `offset` from `minOffset` (`maxPos < 0 ? firstGuess -
  maxWhere : maxNeg < 0 ? firstGuess - minWhere : 0`) to `maxOffset`: try
  `+offset` (if `<= maxPos`), then `-offset` (if `<= maxNeg`); the first
  match wins and `inOffset` moves by it.
- A match at line L compares the hunk's old lines `1 + prefixFuzz .. pat -
  suffixFuzz` with input lines from `L + prefixFuzz`, byte for byte (the
  line's newline status included).

Applying a hunk at line `where`: copy the input lines not yet copied up to
`where - 1`, then walk the hunk: Context -> copy the input line, Delete ->
skip the input line, Insert -> write the patch line; `lastFrozenLine` moves
past the hunk's old lines. After the last hunk copy the rest. A hunk whose
`where` would go back before `lastFrozenLine` fails.

`.rej` text for a failed hunk (unified, GNU's `abort_hunk_unified`): for the
first failed hunk of a file the header `--- OLDNAME` + oldTimeText and `+++
NEWNAME` + newTimeText (`/dev/null` for no name; names as stripped; after
-R or an accepted reversal, the swapped ones); each hunk `@@ -R0 +R1
@@FUNC` with R = `start` if count is 1, `start-1,0` if 0 (start = the
hunk's first), else `start,count`; then its lines with their marks ('
'/'-'/'+'), and `\ No newline at end of file` after a noNewline line.

### New `src/components/BuiltinCommands/commands/patch/Patch.cpp`

`CreatePatchCommand()`, name `patch`, version `1.0.0`; help summary "apply a
diff file to an original", usage `patch [OPTION]... [ORIGFILE [PATCHFILE]]`;
notes: the no-terminal answers, unified diffs only (until the next task).

`Options()` -- every GNU patch 2.7.6 option:
- treated here: `-d/--directory DIR`, `-E/--remove-empty-files`,
  `-f/--force`, `-i/--input FILE`, `-N/--forward`, `-o/--output FILE`,
  `-p/--strip NUM`, `-R/--reverse`, `-s/--silent` and `--quiet` (same id),
  `-t/--batch`, `-u/--unified` (the only format here), `--dry-run`,
  `--binary` (no effect on POSIX, as GNU), `-v` (`kBuiltinOptionVersion`).
- not treated here, treated by diff--patch-fuzz-rej: `-F/--fuzz NUM`,
  `-r/--reject-file FILE`, `--reject-format FORMAT`, `-b/--backup`,
  `-z/--suffix SUFFIX`, `-V/--version-control METHOD`,
  `--backup-if-mismatch`, `--no-backup-if-mismatch`, `-c/--context`,
  `-n/--normal`, `-l/--ignore-whitespace`.
- not treated: `-B/--prefix PREFIX`, `-Y/--basename-prefix PREFIX`,
  `-D/--ifdef NAME`, `-e/--ed`, `-g/--get NUM`, `-m/--merge[=STYLE]` (`{'m', "merge", Optional, "STYLE"}`), `-T/--set-time`, `-Z/--set-utc`,
  `-x/--debug NUM`, `--verbose`, `--posix`, `--quoting-style WORD`,
  `--read-only BEHAVIOR`, `--follow-symlinks`.

`Run`:
1. Not `BeginBuiltin` (GNU prefixes the Try line): `ParseBuiltinArgs`; on an
   error `patch: <error>` + `patch: Try 'patch --help' for more
   information.`, exit 2; then help/version/`-v`, then `ReportNotTreated`.
   `-p X` not a number: `patch: **** strip count X is not a number` (quoted
   as `ShellEscapeQuoted` would), 2; negative: `... is negative`. A third
   operand: `patch: X: extra operand` + Try, 2.
2. `-d DIR` first: `IO().ChangeDirectory(DIR)`; failing -> `patch: **** Can't
   change to directory DIR : No such file or directory`, 2.
3. The patch text: PATCHFILE operand or `-i FILE` (the later wins), else
   standard input; read whole (64 KiB reads, stop-aware). Unopenable: `patch:
   **** Can't open patch file NAME : No such file or directory`, 2.
4. For each `FilePatch` (in order), with `reverse = -R`:
   1. If -R: `SwapFilePatch`.
   2. **Which file** (GNU's rules; ORIGFILE operand, if given, is the file
      for every patch): among oldName, newName, indexName (indexName only
      when both others are absent), the existing ones (`IO().Stat`); pick
      the best: fewest path components, then shortest last component, then
      shortest name, then the first in that order. None exists: if the
      patch says the old side does not exist (oldSays != 0 after a swap),
      pick the best name among those needing the fewest new directories;
      else no file.
   3. **Create/delete sanity** (GNU's `maybe_reverse`), on the chosen name
      (or the first name when none exists): `isEmpty` = missing or size 0;
      in the current orientation (after any swap), it looks reversed when
      `(isEmpty ? 0 : 1) < (isEmpty ? newSays : oldSays)` -- an existing
      non-empty file the patch surely creates, or a missing/empty file it
      deletes -- except when `isEmpty` and `(missing ? newSays : oldSays) ==
      1` and `(missing ? oldSays : newSays) == 2` (an empty file git knows to
      be created/deleted). If it looks reversed: print `The next patch would create the file X,\nwhich
      already exists!` / `... would delete the file X,\nwhich does not
      exist!` / `... would empty out the file X,\nwhich is already empty!`
      (with `, when reversed,` after `patch` under -R) and answer as for a
      reversed patch (step 5).
   4. No file: `can't find file to patch at input line N\n` (N = firstLine),
      then `Perhaps you should have used the -p or --strip option?\n`
      (without -p) or `Perhaps you used the wrong -p or --strip option?\n`,
      then `The text leading up to this was:\n--------------------------\n`,
      each line of leadingText prefixed with `|`,
      `--------------------------\n`; with -f or -t: `No file to patch.
      Skipping patch.\n`; else `File to patch: \n`, `Skip this patch? [y]
      \n`, `Skipping patch.\n`. Then the hunks count as ignored.
   5. Unless skipping: print `patching file NAME\n` (`checking file` with
      --dry-run; with -o, `patching file OUT (read from NAME)\n`; a git
      rename/copy `patching file NEW (renamed from OLD)\n` / `(copied from
      OLD)` -- the output name is then newName, the input oldName). -s
      silences these.
   6. Hunks in order, `inOffset = 0`, `lastFrozen = 0`: `where =
      LocateHunk(..., 0, ...)`. For hunk #1 only, when not found and
      neither -f nor an earlier decision: swap and locate again; found ->
      `Reversed (or previously applied) patch detected!` (`Unreversed patch
      detected!` under -R) then: -N -> `  Skipping patch.\n` (skip the
      rest); -f -> `  Applying it anyway.\n`; -t -> `  Assuming -R.\n` (or
      `  Ignoring -R.\n`) and keep the swap (the whole file patch is now
      reversed); otherwise `  Assume -R? [n] \n`, `Apply anyway? [n] \n`,
      `Skipping patch.\n` (skip the rest). If not accepted, swap back.
      A creation (`oldSays == 2`) found at line 1 of a non-empty existing
      file fails.
   7. A hunk not applied: `Hunk #K FAILED at L.\n` (L = where it was
      expected: oldFirst + inOffset; ` (different line endings)` before the
      `.` when exactly one of the patch's first line and that input line
      ends in `\r\n`); when the file patch is being skipped, its hunks are
      `ignored` and print nothing. Applied at an offset: `Hunk #K succeeded
      at L (offset N lines).\n` (`line` for 1; N signed); -s silences both.
      Failed and ignored hunks go to the `.rej` text.
   8. **Output** (not with --dry-run): the new content goes to a temporary
      file next to the target (`.NAME.patchtmp` in the same directory,
      created with `kFileOpenWriteCreateTruncate`) and is moved into place
      with `IO().Rename(tmp, target)`; on any failure remove the temporary
      file. Missing directories of a created file are created. If any hunk
      was found at an offset or failed (GNU's `backup_if_mismatch`) and the
      original existed, it is first renamed to `NAME.orig` -- once per file
      per run (a second patch of the same file in one run keeps the first
      `.orig`). With `-o FILE`, every patched content is appended to FILE
      instead (FILE truncated once at the start) and nothing else changes.
      A result that is empty is removed when newSays == 2 (a deletion),
      or with -E; `Not deleting file X as content differs from patch\n`
      when newSays == 2 but the result is not empty. With a git rename, the
      old file is removed after the new one is written.
   9. Failed or ignored hunks: `N out of M hunk FAILED` (`hunks` when M !=
      1; `ignored` when the file patch was skipped), then, when there is an
      output name and not --dry-run, ` -- saving rejects to file NAME.rej\n`
      and the `.rej` written (a `.rej` already written in this run is
      appended to, else replaced; a name ending in `~` gets `#` in place of
      the `~`); otherwise just `\n`. Printed even with -s.
5. Exit 1 if any hunk failed or was ignored, or any file patch was skipped;
   else 0. Fatal errors 2.

### Registration and build

- `BuiltinCommandList.h`: `CreatePatchCommand()`, in
  `CreateStandardBuiltinCommands()` alphabetically.
- `src/components/BuiltinCommands/CMakeLists.txt`: `commands/patch/Patch.cpp`,
  `commands/patch/PatchParse.cpp`, `commands/patch/PatchApply.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/PatchTest.cpp` (in the
CMakeLists), `TEST_F(BuiltinCommandsTest, Patch...)`, files under `/p`, the
patch given as `RunCaptured`'s input (`patch` reads stdin) unless stated.
Expected outputs are GNU patch 2.7.6's (`LC_ALL=C`); `seq 1 10` means the
lines `1\n` .. `10\n`.

- `PatchAppliesUnified`: `f` = seq 1 10, patch `--- f\n+++ g\n@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n`
  with `-p0` -> out `patching file f\n`, 0; `f` has `five` in line 5; no
  `f.orig`, no temporary file left in `/p`.
- `PatchOffsetKeepsOrig`: `f` = `x\ny\n` + seq 1 10, same patch -> out
  `patching file f\nHunk #1 succeeded at 4 (offset 2 lines).\n`, 0; `f.orig`
  holds the old content. Two hunks both at +2: `Hunk #1 succeeded at 3
  (offset 2 lines).\nHunk #2 succeeded at 12 (offset 2 lines).\n` (patch
  `--- x\n+++ x\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n@@ -10,3 +10,3 @@\n j\n-k\n+K\n l\n`
  on `z\nz\n` + `a`..`m`).
- `PatchReversedWithoutTerminal`: `f` already patched -> out `patching file
  f\nReversed (or previously applied) patch detected!  Assume -R? [n]
  \nApply anyway? [n] \nSkipping patch.\n1 out of 1 hunk ignored -- saving
  rejects to file f.rej\n`, 1; `f.rej` exists. `-N` -> `patching file
  f\nReversed (or previously applied) patch detected!  Skipping patch.\n1 out
  of 1 hunk ignored -- saving rejects to file f.rej\n`, 1. `-t` -> `patching
  file f\nReversed (or previously applied) patch detected!  Assuming -R.\n`,
  0, `f` back to seq 1 10. `-R` on the patched file -> `patching file f\n`, 0.
- `PatchFailedHunkWritesRej`: `x` = `a`..`m`, patch with header times
  `--- x\t2024-01-02 03:04:05.000000000 +0000` / `+++ x\t2024-01-03 ...`,
  hunk 1 `-1,3 +1,3` (a, -b +B, c) and hunk 2 `-10,3 +10,3` (j, -Q +K, l)
  -> out `patching file x\nHunk #2 FAILED at 10.\n1 out of 2 hunks FAILED
  -- saving rejects to file x.rej\n`, 1; `x.rej` = `--- x\t2024-01-02
  03:04:05.000000000 +0000\n+++ x\t2024-01-03 03:04:05.000000000 +0000\n@@
  -10,3 +10,3 @@\n j\n-Q\n+K\n l\n`; `x.orig` exists; `x` has `B`. With
  `--dry-run -R` on the result -> `checking file x\nHunk #2 FAILED at 10.\n1
  out of 2 hunks FAILED\n`, 1, nothing written.
- `PatchCreatesAndDeletes`: the `diff -ruN a b` output of a tree with `keep`
  changed, `new` added, `old` removed (headers with `1970-01-01
  00:00:00.000000000 +0000` on the missing sides), applied with `-p1` in a
  directory holding `keep` and `old` -> out `patching file keep\npatching
  file new\npatching file old\n`, 0; `old` is gone, `new` = `new\n`.
  Again -> `patching file keep\nReversed (or previously applied) patch
  detected!  Assume -R? [n] \nApply anyway? [n] \nSkipping patch.\n1 out of
  1 hunk ignored -- saving rejects to file keep.rej\nThe next patch would
  create the file new,\nwhich already exists!  Assume -R? [n] \nApply
  anyway? [n] \nSkipping patch.\n1 out of 1 hunk ignored\nThe next patch
  would delete the file old,\nwhich does not exist!  Assume -R? [n] \nApply
  anyway? [n] \nSkipping patch.\n1 out of 1 hunk ignored\n`, 1.
- `PatchGitDiff`: `diff --git a/keep b/keep\nindex 814f4a4..1c7f1bd
  100644\n--- a/keep\n+++ b/keep\n@@ -1,2 +1,2 @@\n one\n-two\n+TWO\ndiff
  --git a/old b/old\ndeleted file mode 100644\nindex 1fd0b46..0000000\n---
  a/old\n+++ /dev/null\n@@ -1 +0,0 @@\n-gone\ndiff --git a/new b/new\nnew
  file mode 100644\nindex 0000000..3e75765\n--- /dev/null\n+++ b/new\n@@
  -0,0 +1 @@\n+new\n` with `-p1` -> `patching file keep\npatching file
  old\npatching file new\n`, 0; then `-R -p1` -> the same three lines, 0,
  and the tree is back. Without `-p` (basenames) -> the same as `-p1`.
  With `-p0` in an empty directory -> the `can't find file to patch at input
  line 5` block (`Perhaps you used the wrong -p or --strip option?`, the
  four `|` lines from `diff --git` to `+++ b/keep`), `File to patch: \nSkip
  this patch? [y] \nSkipping patch.\n1 out of 1 hunk ignored\n`, then `The
  next patch would delete the file a/old,\nwhich does not exist!  Assume -R?
  [n] \nApply anyway? [n] \nSkipping patch.\n1 out of 1 hunk
  ignored\npatching file b/new\n` (and `b/new` created), 1; with `-t`, the
  first block ends `No file to patch.  Skipping patch.\n1 out of 1 hunk
  ignored\n`.
- `PatchRenameFromGit`: `diff --git a/keep b/kept\nsimilarity index
  50%\nrename from keep\nrename to kept\n--- a/keep\n+++ b/kept\n@@ -1,2
  +1,2 @@\n one\n-two\n+TWO\n` with `-p1` -> `patching file kept (renamed
  from keep)\n`, 0; `keep` gone, `kept` = `one\nTWO\n`.
- `PatchOptions`: `-o out.txt -i PATCH` (the `-ruN` patch) -> `patching file
  out.txt (read from keep)\npatching file out.txt (read from new)\npatching
  file out.txt (read from old)\n`, originals untouched; `-E` on a deletion
  without epoch times (`--- old\n+++ old\n@@ -1 +0,0 @@\n-gone\n`) removes
  `old` (without `-E`: `old` stays, empty); `-s` prints nothing on success;
  `-d sub` applies in `sub`; ORIGFILE operand: `patch x PATCH` patches `x`
  whatever the headers say.
- `PatchErrors`: `--bogus` -> err `patch: unrecognized option
  '--bogus'\npatch: Try 'patch --help' for more information.\n`, 2; `-p x` ->
  `patch: **** strip count x is not a number\n`, 2; `a b c` -> `patch: c:
  extra operand\n` + Try, 2; `-i nosuch` -> `patch: **** Can't open patch
  file nosuch : No such file or directory\n`, 2; `-d nodir` -> `patch: ****
  Can't change to directory nodir : No such file or directory\n`, 2; input
  `garbage\n` -> `patch: **** Only garbage was found in the patch input.\n`,
  2; empty input -> nothing, 0; `-v` -> `patch (HaisosOS builtin) 1.0.0\n`.
- `PatchHunkAtStartNeedsStart`: `x` = `z\na\nb\n`, patch `--- x\n+++ x\n@@
  -1,2 +1,2 @@\n-a\n+A\n b\n` -> `patching file x\nHunk #1 FAILED at 1.\n1
  out of 1 hunk FAILED -- saving rejects to file x.rej\n`, 1 (no leading
  context: it may only match at line 1; GNU applies it with fuzz 1, which
  arrives with diff--patch-fuzz-rej).
- `PatchRoundTripWithDiff` (acceptance scenario 2), through `hsh -c` with
  absolute `/bin/` paths, in `/p` holding `a.cpp` (a 30-line file) and
  `b.cpp` (the same with two lines changed far apart and one added):
  `/bin/diff -u a.cpp b.cpp > p.diff; /bin/patch -p0 < p.diff; /bin/diff
  a.cpp b.cpp; echo $?` -> out `patching file a.cpp\n0\n` (both header names
  exist and are as good: the first, `a.cpp`, is patched).

Update `BuiltinCommandsTest.cpp`: `"patch"` in the expected list of
`ListsEveryBuiltinSortedWithAVersion`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Patch*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `patch` in the opening list; a
  row (1.0.0; treated as above; exceptions: questions answered as without a
  terminal; unified diffs only for now; git patches applied one by one as
  read (GNU defers the outputs of a series of git diffs); no permissions, so
  git modes are ignored; indented patches not recognised).
- Root `CLAUDE.md`: `patch` in the Builtin Commands sentence and table
  (`Applies unified and git diffs (-p -R -N -i -o -d --dry-run), offsets,
  .rej files`) and the directory tree line.

## Acceptance

- [ ] `patch` registered; standard `--help`; `-v`/`--version`; `man patch`.
- [ ] Every GNU patch 2.7.6 option in `Options()`; untreated ones reported.
- [ ] File choice, -p stripping, `/dev/null` and epoch rules, git headers,
      creation and deletion, reversed detection with the no-terminal
      answers, `-N -f -t -R -E -o -d -s --dry-run`, exactly as specified.
- [ ] `LocateHunk` implements GNU's search order and boundary rules (fuzz as
      a parameter, 0 here); messages for offsets and failures byte for
      byte; `.rej` in unified form; `.orig` kept on a mismatch.
- [ ] Every result written to a temporary file then `IO().Rename`d; nothing
      reached except through `context.IO()`.
- [ ] Exit 0/1/2; the round trip with `diff -u` works.
- [ ] CLAUDE.md rows; `ListsEveryBuiltinSortedWithAVersion`; all tests pass.

## Out of scope

- Fuzz, `-F`, `-l`, backups beyond the mismatch `.orig` (`-b -z -V
  --[no-]backup-if-mismatch`), `-r`, `--reject-format`, context and normal
  diff input (diff--patch-fuzz-rej).
- Ed scripts, `--merge`, `-D`, `-g`, `-T`/`-Z`, `-B`/`-Y`: not treated.
