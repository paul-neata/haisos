# Task diff--patch-fuzz-rej: patch -- fuzz, backups, reject files, context and normal diffs

- Rock: diff
- Depends on (all done): diff--patch-core (#74: the `patch` builtin -- `PatchReader`, `FilePatch`, `PatchHunk`, `SideAbsence`, `LocateHunk`, `RejText`, `ApplyFilePatch`), coreutils--cp (`BackupPathFor`, `BackupMode`, `ParseBackupControlNamed` in `BuiltinCopy.h`)
- Size: ~900 changed lines of code (~750 for the task, ~150 for #74's follow-ups) plus ~500 of tests, in ~16 files (most of them small edits)
- Plan checked against: develop @ 1cbb5da
- PR title: patch: fuzz, backups, -r, context and normal diffs

## Goal

`patch` finishes matching GNU patch 2.7.6 (C locale, no terminal): a hunk
whose context no longer matches exactly is applied with fuzz (`-F NUM`,
default 2: `Hunk #1 succeeded at 2 with fuzz 2.`); `-l` matches loosely on
blanks; backups follow `-b`, `-z SUFFIX`, `-V METHOD`,
`PATCH_VERSION_CONTROL` / `VERSION_CONTROL` / `SIMPLE_BACKUP_SUFFIX`,
`--backup-if-mismatch` and `--no-backup-if-mismatch`; rejects go to `-r
FILE` (or nowhere with `-r -`) in `--reject-format=context|unified` or the
input's own format; context diffs (`diff -c`, old and new style) and normal
diffs (`diff`, with ORIGFILE or `-n`) are read and applied; `-c`/`-n`/`-u`
force the format. Ed scripts (`-e`) and `--merge` stay not treated. This
removes patch-core's "exact matching (GNU -F 0)" exception. #74's open
review findings ride along (see "Follow-ups of #74").

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
names here (`PatchReader`, `LocateHunk`, `lineShift`, ...) are Haisos's own.

**Write in pieces:** never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step (earlier
runs died on "response exceeded the 32000 output token maximum").

Read first: the plan `develop-plan/tasks/diff--patch-core.md` (its rules
all still hold unless changed here) and its code in
`src/components/BuiltinCommands/commands/patch/` -- `PatchParse.h/.cpp`
(`PatchReader::Next`, `ParseHunkHeader`, `ReadHunk`, `StripFileName`,
`SwapFilePatch`), `PatchApply.h/.cpp` (`LocateHunk`, `RejText`), `Patch.cpp`
(`PatchRun`, `ApplyFilePatch`, `PatchCommand::Run`) -- and
`tests/unit/components/BuiltinCommands.unittests/PatchTest.cpp`; the root
`CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCopy.h` (backups) and
`BuiltinText.h` (`ArgMatch`).

What is on develop to use:
- `PatchHunk`: `oldStart/oldCount/newStart/newCount` (a start is the first
  line the side covers: A, or A + 1 for an empty range `A,0`), `lines`
  (`PatchLine { kind Context|Delete|Insert, text, noNewline }`),
  `function`, `leadingContext`/`trailingContext` (context lines before the
  first / after the last change), `headerLine`.
- `FilePatch`: `format` (`PatchFormat { Unified }`), `oldName/newName/
  indexName`, `oldTimeText/newTimeText`, `oldAbsence/newAbsence`
  (`SideAbsence { Present, Maybe, Surely }`), `gitDiff/gitRename/gitCopy`,
  `stripTrailingCr`, `reportLine`, `leadingText`, `hunks`, `malformed`.
- `LocateHunk(target, hunk, fuzz, consumedLines, runningOffset)` -- already
  takes a fuzz (always 0 today) and drops up to that many context lines at
  each end; candidates `L > consumedLines`; the start/end anchoring of
  unbalanced context. `RejText(patch, hunkNumbers)` -- the unified `.rej`.
- `ApplyFilePatch`: the hunk loop (reversal probe on hunk #1 at fuzz 0),
  `written`/`consumedLines`, the `.orig` copy (`origMade`), `.rej` writing
  (`WriteRejFile`, `rejWritten`), the temporary file renamed into place.
- `BuiltinCopy.h`: `BackupMode { None, Simple, Numbered, Existing }`,
  `BackupPathFor(context, dest, mode, suffix)` (`Numbered`: `dest.~N~` one
  above the highest; `Existing`: numbered when a numbered backup exists,
  else simple), `ParseBackupControlNamed(context, reportedName, word, mode)`
  over `ArgMatch`. `ArgMatch` ends its block with a `Try 'CMD --help'` line,
  which patch's block does not have (see below).
- The fixture's `RunCaptured(command, args, input, workingDirectory,
  environment)`: an `IEnvironment` with `VERSION_CONTROL` etc. set reaches
  the command through `context.Process().GetEnvironment()`, as cp reads it.

## Changes

Rules that bite: every file through `context.IO()` (backups are
`IO().Rename`s; `-r FILE` opened through `IO().OpenFile`); GNU's messages
byte for byte; untreated options stay reported; portable C++17, no `<regex>`;
nothing creates a link.

### How a hunk is located (`PatchApply.h/.cpp`, `LocateHunk`)

Keep `LocateHunk`'s shape; add a `bool looseWhitespace` parameter (or an
options struct with `fuzz` and `looseWhitespace`) and change three rules.
All verified on the host (seq files, a change line with A leading and B
trailing context lines, at its stated place unless said):

1. **Which context lines fuzz F ignores.** Let a = leadingContext, b =
   trailingContext. The side with more context loses lines first: with
   a >= b, d = a - b, the leading side ignores min(F, a) lines and the
   trailing side min(max(0, F - d), b); mirrored when b > a. The anchoring
   rule of patch-core (fewer leading than trailing: only at line 1 when
   oldStart <= 1; fewer trailing than leading: only at the file's end) is
   then applied to what is left. So an unbalanced hunk needs fuzz d before
   it can match anywhere but at its anchor: (3,1) mid-file `FAILED` with
   `-F1`, `succeeded at 12 with fuzz 2.` by default; (4,1) needs `-F3`,
   (5,2) `-F3`, (2,0) fuzz 2, (1,0) fuzz 1. With (3,1) and the trailing
   line changed in the file: default `FAILED`, `-F3` `with fuzz 3`; (3,1)
   with the second leading line changed: `with fuzz 2`; (3,2) with the last
   trailing line changed: `with fuzz 2`, with the first trailing line
   changed: `with fuzz 3`; (1,3) mid-file with the last trailing line
   changed: `with fuzz 1`.
2. **Where a candidate may lie.** L (the hunk's first old line, ignored ones
   included) is >= 1: ignored leading lines may not fall before the file
   (`x y -a +A b c d` stated at 1 on `a b c d` fails even with `-F5`); the
   compared lines must lie inside the file, ignored trailing lines may run
   past its end (`@@ -1,4 +1,4 @@ 1 -2 +TWO 3 4` on `1 2 3`: `succeeded at
   1 with fuzz 1.`); and the first *compared* line (L + ignored leading
   lines) is >= the last input line the previous hunks of this file used
   up (`consumedLines`, as today: L + oldCount - trailingContext - 1) --
   patch-core's `L > consumedLines` becomes `>=`: a hunk may start on the
   last old line before the previous hunk's trailing context (after ` b -c
   +C d` at 2, ` c -d +D e` stated at 3 applies: `a b C D e ...`; ` b -c
   +CC d` at 2 fails). When fuzz leaves nothing to compare, every candidate
   matches and the one nearest the expected place wins.
3. **`-l`** (`looseWhitespace`): two lines compare equal when they are equal
   after these rules -- a run of blanks (space, tab) in one matches a run of
   blanks in the other, whatever their lengths and kinds; blanks at the end
   of a line, and the line's newline, are ignored (so a missing final
   newline matches); a run of blanks matches nothing at the start or in the
   middle of a line (`xy` vs `x y`, `  b` vs `b`: no match; `  b` vs ` b`:
   match); `\r`, `\v`, `\f` are not blanks; case matters. The file's lines
   are what is kept as context: `a  ` matched by ` a` stays `a  `.

The search order, the outward search from the expected place, the
unbalanced-context anchors and the placed (never searched) insertion with no
old lines stay as they are. The reported fuzz is the F the hunk matched at
(the smallest), not the number of lines actually ignored.

### `Patch.cpp` -- the hunk loop, numbers and messages

- **The fuzz loop.** For each hunk: maxFuzz = min(`-F` (default 2),
  max(a, b)); for F = 0, 1, ... maxFuzz: try the hunk as it is; if not
  found and the reversal probe applies (hunk #1, not `-f`, not already
  decided -- as patch-core), try the swapped hunk at the same F; the first
  that matches wins. So a reversal found at F beats a forward match at a
  higher F, and a forward match at F beats a reversal at a higher F
  (verified: forward at fuzz 2 elsewhere vs reversed exact -> the `Reversed
  (or previously applied) patch detected!` question; forward at fuzz 1 vs
  reversed at fuzz 2 -> `Hunk #1 succeeded at 11 with fuzz 1 (offset 9
  lines).`). After `-t` takes the reversal at F > 0 the hunk's line follows:
  `Reversed (or previously applied) patch detected!  Assuming -R.\nHunk #1
  succeeded at 2 with fuzz 2.\n`.
- **Line numbers are the output's** (fixes #74 too, which printed input
  numbers). Keep `lineShift` per file patch: after each applied hunk, add its
  Insert lines and subtract its Delete lines. Then:
  - success: `Hunk #K succeeded at P` with P = L + lineShift, then ` with
    fuzz F` when F > 0, then ` (offset N line[s])` when N = L - oldStart is
    not 0, then `.` (printed when F > 0 or N != 0; a placed insertion with
    no old lines never shows an offset, as now). Verified: after a hunk
    deleting one line, a hunk at offset 1 prints `succeeded at 10 (offset
    1 line)` for input line 11; after one adding two, `succeeded at 13
    (offset 3 lines)` for input line 11; `succeeded at 3 with fuzz 2 (offset
    1 line).`; a hunk whose ignored leading lines start at input line 1
    after a hunk deleting one line: `succeeded at 0 with fuzz 3.`.
  - failure: `Hunk #K FAILED at E` with E = oldStart + lineShift -- not
    plus runningOffset (verified: a first hunk at offset 2 changing no line
    count, then a failing hunk stated at 10: `FAILED at 10.`; after a hunk
    deleting one line: `FAILED at 9.`). ` (different line endings)` as now.
  - `.rej` ranges: every start (both sides, both formats) is shifted by the
    lineShift when the hunk was rejected (`@@ -10,3 +9,3 @@` after a hunk
    deleting one line is saved as `@@ -9,3 +8,3 @@`; `@@ -8 +8,0 @@` after one
    adding a line, `@@ -9 +9,0 @@`). A skipped (ignored) file patch's
    shifts are all 0.
- **Applying over a shared line**: a hunk's lines that fall on input lines
  the previous hunk already handled (index < `written`) are skipped -- not
  written again; a Delete there is a no-op (after ` b +X c d` at 2, ` b -c +C
  d` at 2 gives `a b X C d ...`, `b` once).
- **`-F NUM`, `--fuzz=NUM`**: through the same parse as `-p` (see "Counts"
  below): `patch: **** fuzz factor X is not a number` / `... is negative`,
  2; digits beyond int64 saturate (`-F 99999999999999999999` is accepted).
- **`-l`, `--ignore-whitespace`**: `looseWhitespace` for every comparison,
  the reversal probe included.
- **Messages under `-s`**: the fuzz lines are `Hunk #K ...` lines, dropped
  like the others.

### `Patch.cpp` -- backups

- Options: `-b/--backup`, `-z/--suffix SUFFIX`, `-V/--version-control
  METHOD`, `--backup-if-mismatch`, `--no-backup-if-mismatch` (the later of
  the two wins; mismatch backups are on by default).
- `-z ''`: `patch: **** backup suffix is empty`, 2, checked where it is met
  (so `-z '' -z .x` fails, and `-z '' -F x` reports the suffix, `-F x -z
  ''` the fuzz factor: `-p`, `-F`, `-z` and `--reject-format` are checked in
  command-line order, the first bad one wins).
- The suffix: the last `-z`, else `SIMPLE_BACKUP_SUFFIX` when set and not
  empty, else `.orig` (an empty `SIMPLE_BACKUP_SUFFIX` gives `.orig`).
- The method, decided after all options are parsed: the last `-V` (`-V bogus
  -V simple` is fine; `-V none -V bogus` fails), else `PATCH_VERSION_CONTROL`
  when it is set at all, else `VERSION_CONTROL` when set, else `existing`;
  an empty value means `existing`. The words: `none`/`off`, `simple`/`never`,
  `existing`/`nil`, `numbered`/`t`, or an unambiguous prefix. **GNU 2.7.6
  makes numbered backups for `none` and `off`** (verified: `-V none -b`,
  `-V off -b`, `VERSION_CONTROL=none -b` all leave `f.~1~`) -- map them to
  `BackupMode::Numbered`. A bad word, checked even when no backup will be
  made, is exit 2 with exactly (no Try line):
  ```
  patch: invalid argument 'bogus' for '--version-control or -V option'
  Valid arguments are:
    - 'none', 'off'
    - 'simple', 'never'
    - 'existing', 'nil'
    - 'numbered', 't'
  ```
  (`ambiguous argument 'n'` for a prefix of several; `'$PATCH_VERSION_CONTROL'`
  / `'$VERSION_CONTROL'` in place of `'--version-control or -V option'` for
  the environment's word, which is only read when no `-V` is given). Reuse
  `ParseBackupControlNamed`: give `ArgMatch` (and it) a trailing `bool
  tryHelp = true` parameter, patch passing false; cp/mv/others unchanged.
- **When a backup is made** (replaces patch-core's fixed `.orig` rule): with
  `-b`; or, mismatch backups on, when some hunk of the file patch was
  applied with an offset or fuzz, or failed. Never with `--dry-run`, never
  with `-o` (`-b -o out` leaves neither `f` nor `out` backed up), never for a
  file patch skipped at its question. Once per file per run, as now: the
  first content wins (`-b -V numbered` with two file patches of `f` in one
  run leaves only `f.~1~`, holding the original).
- **How**: the name is `BackupPathFor(context, target, mode, suffix)`; the
  original is moved there with `IO().Rename(target, backup)` just before the
  temporary file is renamed into place (a file that did not exist gets an
  empty backup file: `-b` on a creation leaves an empty `new.orig`).
  Verified: `-b` -> `f.orig`; `-b -z .bak` -> `f.bak`; `-z .bak` alone ->
  no backup; `-V numbered -b`, then `-V numbered -b -R` -> `f.~1~`, `f.~2~`;
  `-V existing -b` with `f.~1~ f.~2~` present -> `f.~3~`, with none ->
  `f.orig`; `VERSION_CONTROL=numbered -b` -> `f.~1~`;
  `PATCH_VERSION_CONTROL=numbered VERSION_CONTROL=simple -b` -> `f.~1~`;
  `PATCH_VERSION_CONTROL= VERSION_CONTROL=numbered -b` -> `f.orig`;
  `SIMPLE_BACKUP_SUFFIX=.s -b` -> `f.s`; an offset hunk with
  `--no-backup-if-mismatch` -> no backup, with `-z .bak` -> `f.bak`, with
  `-V numbered` -> `f.~1~`; `-b` on a deletion -> `old.orig` holding the
  deleted content.
- A failed backup rename is fatal, the target left as it was: `patch: ****
  Can't rename file f to f.orig : Is a directory`, 2 (reason: the backup
  path is a directory -> `Is a directory`, else `Permission denied`).

### `Patch.cpp` -- rejects

- `-r FILE`, `--reject-file=FILE`: every reject of the run goes to FILE
  (replaced by the run's first rejects, appended to after, as `.rej` files
  are today); the summary says `-- saving rejects to file FILE`. `-r -`:
  rejects are discarded and the summary ends after `FAILED`/`ignored` with
  `\n` (`1 out of 1 hunk FAILED\n`; `-s` keeps the summary). `--dry-run`
  writes no reject file and names none, as now.
- `--reject-format=FORMAT`: `context` or `unified`, exactly (no prefixes);
  anything else is `patch: Try 'patch --help' for more information.` alone
  on stderr, 2. Without it the input's own: unified -> unified, context /
  old-style context / normal -> context.
- A reject file that cannot be opened is fatal after its summary line:
  `patch: **** Can't create file NAME : REASON`, 2 (verified for `-r
  nodir/my.rej`: `No such file or directory`). REASON from what is there:
  NAME a directory -> `Is a directory`; its parent missing -> `No such file
  or directory`; else `Permission denied` (one helper, also used for `-o`,
  the temporary file and the empty backup). A failed write: `patch: ****
  write error : Input/output error`, 2.
- The order of the end-of-file lines: `Not deleting file X as content
  differs from patch` comes before the `N out of M` summary (verified on a
  `-f` deletion whose hunk failed: `patching file old\nHunk #1 FAILED at
  1.\nNot deleting file old as content differs from patch\n1 out of 1 hunk
  FAILED -- saving rejects to file old.rej\n`).

### `PatchApply.h/.cpp` -- the reject texts

Replace `RejText(patch, hunkNumbers)` by one entry point taking the rejected
hunks with their shifts and the format, e.g.
`struct RejectedHunk { size_t number; int64_t lineShift; };`
`enum class RejFormat { Unified, Context };`
`std::string RejText(const FilePatch&, const std::vector<RejectedHunk>&, RejFormat);`

- Header, both formats: a side with no name is written `/dev/null` with no
  time text (verified: `--- /dev/null\t2026-10-08 ...` in a `diff -uN`
  creation is saved as `--- /dev/null`; #74 kept the time); otherwise the
  name, then `\t` + time text when there is one, as now.
- Unified: as now, each start + lineShift.
- Context (the input's own for context and normal diffs, or
  `--reject-format=context`): `*** OLD[\tTIME]\n--- NEW[\tTIME]\n` (normal
  diffs: `*** /dev/null\n--- /dev/null\n`), then per hunk:
  `***************` + (` ` + function when there is one) + `\n`; `*** R0` +
  ` ****` + `\n`; every old line (Context and Delete) with its mark; `---
  R1` + ` ----` + `\n`; every new line (Context and Insert) with its mark.
  For an old-style context or a normal input the old header has no ` ****`
  and the new one ends ` -----` instead of ` ----`.
  - R: `0` for a side with no lines; `S` for one line; `S,E` (E = S + count
    - 1) otherwise; S = the side's start + lineShift (verified: `*** 12,14
    ****` / `--- 102,104 ----` for `@@ -10,3 +100,3 @@` after a hunk adding
    two lines; `--- 0 ----` for the empty side of a deletion, whatever its
    stated start).
  - Marks: `  ` context; a run of Delete lines directly followed by a run of
    Insert lines is a change: both runs `! `; any other Delete `- `, Insert
    `+ `. A normal input never uses `! `: `- ` and `+ ` only.
  - Both parts are written in full, even when the input listed only one
    (`diff -c`'s pure insertion lists no old lines: its `.rej` repeats the
    context lines under `*** 3,8 ****`).
  - A noNewline line is written without its newline, nothing after it, as
    the unified writer does (`! b` then `--- 1,2 ----` ... `! b` with no
    final newline).
  Verified examples (each hunk failing on a file of ten `X` lines):
  `diff -c a mix` (`a` = seq 10, `mix` = `3` -> `three`, `6` deleted,
  `seven2` added after `7`) is saved as `*** a\n--- mix\n***************\n***
  1,10 ****\n  1\n  2\n! 3\n  4\n  5\n- 6\n  7\n  8\n  9\n  10\n--- 1,10
  ----\n  1\n  2\n! three\n  4\n  5\n  7\n+ seven2\n  8\n  9\n  10\n`
  (the same text comes from `diff -u a mix` with `--reject-format=context`,
  its header then carrying the unified times). The normal diff of the same
  pair, `3c3\n< 3\n---\n> three\n6d5\n< 6\n7a7\n> seven2\n` with ORIGFILE
  `a` (its third hunk placed, two rejected), is saved as `*** /dev/null\n---
  /dev/null\n***************\n*** 3\n- 3\n--- 3 -----\n+
  three\n***************\n*** 6\n- 6\n--- 0 -----\n`.
  `--reject-format=unified` on the context input gives `--- a\n+++
  mix\n@@ -1,10 +1,10 @@\n 1\n 2\n-3\n+three\n 4\n 5\n-6\n 7\n+seven2\n 8\n
  9\n 10\n`; on the normal one `--- /dev/null\n+++ /dev/null\n@@ -3 +3
  @@\n-3\n+three\n@@ -6 +5,0 @@\n-6\n`. An old-style context hunk saved:
  `*** f\n--- f\n***************\n*** 4,6\n  4\n! 5\n  6\n---
  4,6 -----\n  4\n! FIVE\n  6\n`.

### `PatchParse.h/.cpp` -- context and normal diffs

- `enum class PatchFormat { Unified, Context, OldContext, Normal };` and the
  reader told what it may recognise: `-u` unified only, `-c` context only
  (both styles), `-n` normal only, none of them: any -- but a normal diff
  only when an ORIGFILE operand is given or `-n` is (it names no file).
  Verified: `-c` on a unified diff, `-u` on a context one, `-n a` on a
  context or unified one, and a normal diff with no ORIGFILE and no `-n` are
  all `patch: **** Only garbage was found in the patch input.`, 2.
- Every hunk is converted to the unified `PatchHunk` (lines in order,
  `oldStart/oldCount/newStart/newCount`, leading/trailing context), so the
  locator, `SwapFilePatch` and the unified writer need nothing new; only
  `format` remembers where it came from (for the `.rej` default and its
  ` ****`/` -----` forms).
- **Context diffs.** File headers `*** NAME[\tTIME]` (old) then `---
  NAME[\tTIME]` (new): names, quoting, `/dev/null`, `-p`, the epoch rule
  and the time texts exactly as for `---`/`+++` (a `diff -c` C-locale time
  `Tue Jan  2 03:04:05 2024` is the form `ParseWordedStamp` already reads).
  A hunk starts with a line of 8 or more `*` (fewer: not a hunk); what
  follows the stars, leading blanks dropped, is the function. Then `*** A[,B]
  ****` (new style; `*** A[,B]` alone: old style, the file patch's format
  `OldContext`), the old part (`  `, `- `, `! ` + text, each two-character
  mark), `--- C[,D] ----` (old style `--- C[,D]`), the new part (`  `, `+ `,
  `! `). `\ No newline at end of file` after a line marks it, as in unified
  hunks. A range `X,Y` is the lines X..Y; `X` alone one line.
  - A part listing no lines is the other part's context lines (`diff -c`
    writes a pure insertion's old part and a pure deletion's new part that
    way). A side whose lines (listed or filled) are none is the empty range
    after its stated line: start X + 1, count 0 (`*** 0 ****` of a creation:
    like `-0,0`, the old side maybe absent, surely with `/dev/null` or an
    epoch time; `--- 0 ----` of a deletion likewise for the new side).
  - Walking both parts together gives the unified order: a context line
    meets a context line; `- ` lines are Deletes, `+ ` lines Inserts; a run
    of `! ` lines in the old part and the next run of `! ` lines in the new
    part are Deletes then Inserts.
  - Malformations (verified; stored in `malformed` and reported when the
    hunk is reached, as unified ones are -- except that **a context file
    patch whose first hunk is malformed reports it before anything else of
    that file patch**, no `patching file` line):
    - a line in either part without a valid mark: `malformed patch at line
      N: LINE` (LINE with its newline, so the message ends in a blank line);
    - the `---` line before the old part has its range's lines: `Premature
      '---' at line N; check line numbers at line M` (N the `---` line, M
      the `*** A,B` line); after more than that: `Overdue '---' at line N;
      check line numbers at line M`;
    - the new part cut short by the next `***************` line: `unexpected
      end of hunk at line N` (N that line); by other text: `malformed patch
      at line N: LINE`; by the input's end: completed with the old part's
      remaining context lines (accepted, as GNU does);
    - a `! ` line meeting a context line of the other part:
      `Out-of-sync patch, lines X,Y -- mangled text or line numbers,
      maybe?` (X and Y the two input lines) -- this one comes after the
      `patching file` line even in the first hunk (verified).
  - `reportLine` (for `can't find file to patch at input line N`) is the
    first hunk's star line; `leadingText` runs to the `--- ` header line.
- **Normal diffs.** A hunk is `A[,B]{a|c|d}C[,D]`: `c` and `d` read B-A+1
  (or 1) lines `< TEXT`; `c` then a line `---`; `a` and `c` read D-C+1 (or
  1) lines `> TEXT` (`<`/`>` followed by a space or a tab).
  `\ No newline at end of file` as usual. Old lines are Deletes, new lines
  Inserts, no context (so no fuzz). `a`: oldStart A + 1, oldCount 0 (placed,
  not searched, as unified insertions with no old lines); `d`: newStart
  C + 1, newCount 0. Consecutive normal hunks are one file patch; it has no
  names (`patching file` names the ORIGFILE); `reportLine` is its first
  hunk's line. Malformations (verified, fatal when reached): `'---'
  expected at line N of patch`, `'<' followed by space or tab expected at
  line N of patch` (`'>' ...` likewise), `unexpected end of file in patch
  at line N` (N the last line; no completion for normal diffs). An `a` line
  with nothing after it at the input's end is not a hunk (garbage).
  Verified: `patch f < n.diff` (`5c5\n< 5\n---\n> five\n`) on seq 10 ->
  `patching file f\n`, 0; on `0 0` + seq 10 -> `Hunk #1 succeeded at 7
  (offset 2 lines).`; applied twice -> the reversed question.
- **No file for a normal diff** (`-n` without ORIGFILE): the block has no
  `Perhaps you ...` line, and no `The text leading up to this was:` block
  when there is no leading text: `can't find file to patch at input line
  1\nFile to patch: \nSkip this patch? [y] \nSkipping patch.\n1 out of 1
  hunk ignored\n`, 1 (`3 out of 3 hunks ignored` for three hunks; with
  leading text `some text\nmore\n` the block shows it, still with no
  `Perhaps`; `-t`: `can't find file to patch at input line 1\nNo file to
  patch.  Skipping patch.\n1 out of 1 hunk ignored\n`).
- Ed scripts are not recognised (a `5c` line is not a normal hunk); `-e`
  stays not treated.

### Follow-ups of #74 (small, each with a test)

- **Counts** (`Patch.cpp` ~line 860, `PatchParse.h/.cpp`): `PatchReader::Next`
  and `StripFileName` take `int64_t strip` (today `int`: `Patch.cpp` ~954
  narrows the parsed int64). One parse for `-p` and `-F`: optional `-`, then
  one or more digits, else `<what> X is not a number`; a `-` with digits
  `<what> X is negative`; digits beyond int64 saturate to INT64_MAX (GNU
  never says "not a number" for digits: `-p 99999999999999999999` simply
  finds no file). `-p 4294967296` then strips 4294967296 components -- no
  name survives (`can't find file ...`, `Perhaps you used the wrong -p or
  --strip option?`). GNU 2.7.6 wraps a count to 32 bits (`-p 4294967296`
  strips 0, `-p 4294967297` 1, `-p 2147483648` is `strip count 2147483648
  is too large`, `-p 3000000000` `... is negative`): a documented exception.
- **Hunk numbers** (`PatchParse.cpp` ~385, `ParseHunkHeader`): no signed
  overflow. A start or count with more digits than int64 holds is fatal
  (when reached): `line number 99999999999999999999 is too large at line N:
  LINE`; a start whose empty range would overflow (`@@
  -9223372036854775807,0 +1 @@`, `+9223372036854775807,0`) is `malformed
  patch at line N: LINE`. Today such a line is not a hunk at all. Both
  verified after `patching file f`, exit 2, the message ending in a blank
  line. (Large valid numbers keep working: `@@ -1000000000000,3 ...`.)
- **Open/write failures reported** (`Patch.cpp` ~326 `.rej`, ~675 `.orig`,
  ~681 `-o`): see "rejects" and "backups" above; `-o DIR` is `patch: ****
  Can't create file o : Is a directory`, `-o nodir/out` `... : No such file
  or directory`, both 2 before anything is printed (today the reason is
  always the latter); a failed write to the `-o` file is `write error :
  Input/output error`, 2.
- **ORIGFILE with a git rename or copy** (`Patch.cpp` ~400): the operand
  replaces the OLD side -- read from ORIGFILE, written to NEW, named in the
  line: `patching file new (renamed from other)` / `(copied from other)`; a
  rename removes ORIGFILE (not oldName), a copy keeps it; a rename without
  hunks moves ORIGFILE's content to NEW; with `-o out`: `patching file out
  (renamed from other)`, ORIGFILE kept. A missing ORIGFILE: `patching file
  new (renamed from nosuch)\nHunk #1 FAILED at 1.\n1 out of 1 hunk FAILED --
  saving rejects to file new.rej\n`, 1, NEW written empty with an empty
  `new.orig` (GNU also leaves an empty `nosuch.orig`: documented exception).
- **`PatchParse.cpp` hygiene** (~27, ~281): drop the local `strlen` that
  shadows libc's (`std::strlen` from `<cstring>`, or compare with the
  prefix's own length); move `HeaderName`, `ParseHeaderName`, `ParseGitName`,
  `GitHeader` and `ClassifyGitHeader` (after line 260) into the anonymous
  namespace.
- **`Diff.cpp` ~268, `DiffFindLongOption`**: the exact-match pass skips
  entries with an empty `longName` (`--=x` must not find a short-only
  option). No user-visible change, no version bump.

### Options, help, version

- `Options()`: `-F/--fuzz NUM`, `-l/--ignore-whitespace`, `-b/--backup`,
  `-z/--suffix SUFFIX`, `-V/--version-control METHOD`,
  `--backup-if-mismatch`, `--no-backup-if-mismatch`, `-r/--reject-file
  FILE`, `--reject-format FORMAT`, `-c/--context`, `-n/--normal` become
  treated (ids and short descriptions); `-u` now forces unified. Still not
  treated: `-B -Y -D -e -g -m -T -Z -x --verbose --posix --quoting-style
  --read-only --follow-symlinks`. Version `1.1.0`.
- Help notes (two or three lines): the no-terminal answers; ed scripts not
  read; the exceptions listed under "Docs".

### Registration and build

No new sources: `src/components/BuiltinCommands/CMakeLists.txt` already lists
`commands/patch/Patch.cpp`, `PatchApply.cpp`, `PatchParse.cpp` (lines
47-49). New test file
`tests/unit/components/BuiltinCommands.unittests/PatchFuzzRejTest.cpp`,
added to that directory's `CMakeLists.txt` right after `PatchTest.cpp`; the
helpers `PatchTest.cpp` keeps in its anonymous namespace (`kP1`, `Seq`,
`MakePatchDir`, `WritePatchFile`, `ReadPatchFile`, `PatchFileExists`) move
to a new `PatchTestHelpers.h` beside it (inline, in `Haisos::`), included by
both.

## Tests

Expected outputs are GNU patch 2.7.6's (`LC_ALL=C`, no terminal) unless
marked "Haisos". `TEST_F(BuiltinCommandsTest, Patch...)`, working in `/p`
(`MakePatchDir`), the patch on standard input unless an operand says
otherwise. Names: `S10` = seq 10 (`1\n`..`10\n`), `kP1` = patch-core's
`--- f\n+++ g\n@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n`, `kC` =
`*** f\tTue Jan  2 03:04:05 2024\n--- g\tTue Jan  2 03:04:05
2024\n***************\n*** 2,8 ****\n  2\n  3\n  4\n! 5\n  6\n  7\n  8\n---
2,8 ----\n  2\n  3\n  4\n! five\n  6\n  7\n  8\n` (what `diff -c f g`
writes), `kN` = `5c5\n< 5\n---\n> five\n`, "FAILED(L)" =
`patching file f\nHunk #1 FAILED at L.\n1 out of 1 hunk FAILED -- saving
rejects to file f.rej\n`, status 1.

### Changes to `PatchTest.cpp`

- Helpers moved to `PatchTestHelpers.h` (see "Registration and build").
- `PatchSearchOrder`, the end-anchored case (`Seq(9) + "a\n"`, `@@ -7,3
  +7,3 @@ 7 8 -9 +NINE`): run it with `-F0` (the expectation stays
  `FAILED at 7`), and add the default: `patching file x\nHunk #1 succeeded
  at 7 with fuzz 2.\n`, 0, x = `1..8 NINE a`.
- `PatchHunkAtStartNeedsStart` (`z\na\nb\n`, `@@ -1,2 +1,2 @@ -a +A b`):
  default -> `patching file x\nHunk #1 succeeded at 2 with fuzz 1 (offset 1
  line).\n`, 0, x = `z\nA\nb\n`, x.orig = `z\na\nb\n`; the current
  expectation (FAILED at 1, the .rej, the .orig) moves under `-F0`.
- Every other test keeps its expectation (re-verified with the default
  fuzz: their failing hunks fail at every fuzz).

### New `PatchFuzzRejTest.cpp`

- `PatchFuzzLevels` (kP1 on S10 with lines changed): line 2 `TWO` ->
  `patching file f\nHunk #1 succeeded at 2 with fuzz 1.\n`, 0; with `-F0` ->
  FAILED(2); line 3 `three` -> `... with fuzz 2.`, f = `1 2 three 4 five 6
  .. 10`, f.orig the input; lines 2 and 8 -> fuzz 1; 3 and 8 -> fuzz 2; line
  4 `four` -> FAILED(2), with `-F3` -> `with fuzz 3.`; `0\n` + S10 with
  `three` -> `Hunk #1 succeeded at 3 with fuzz 2 (offset 1 line).`.
- `PatchFuzzUnbalancedContext` (seq 30; hunk around line 15 with a leading
  and b trailing context lines, `@@ -S,N +S,N @@`, S = 15 - a, N = a + b +
  1, ` S`..` 14`, `-15`, `+X`, ` 16`..): (3,1) `-F1` -> FAILED(12), default
  and `-F6` -> `Hunk #1 succeeded at 12 with fuzz 2.`; (4,1) default
  FAILED(11), `-F3` -> `succeeded at 11 with fuzz 3.`; (3,1) with line 16
  `Z` -> default FAILED(12), `-F3` fuzz 3; (3,1) with 13 `Z` -> fuzz 2; (3,2)
  with 17 `Z` -> fuzz 2, with 16 `Z` -> default FAILED(12), `-F3` fuzz 3;
  (1,3) with 18 `Z` -> `succeeded at 14 with fuzz 1.`; `@@ -1,6 +1,6 @@ x y
  -a +A b c d` on `a b c d` with `-F5` -> FAILED(1); `@@ -1,4 +1,4 @@ 1 -2
  +TWO 3 4` on `1 2 3` -> `succeeded at 1 with fuzz 1.`, f = `1 TWO 3`.
- `PatchFuzzAndReversal`: f = `1 2 three 4 five 6 7 8 9 10`, kP1 ->
  `patching file f\nReversed (or previously applied) patch detected!  Assume
  -R? [n] \nApply anyway? [n] \nSkipping patch.\n1 out of 1 hunk ignored --
  saving rejects to file f.rej\n`, 1; `-t` -> `patching file f\nReversed (or
  previously applied) patch detected!  Assuming -R.\nHunk #1 succeeded at 2
  with fuzz 2.\n`, 0, f = `1 2 three 4 5 6 .. 10`; f = `1 2 THREE 4 five 6 7
  8 9 10 a 3 4 5 6 7 d` -> `patching file f\nHunk #1 succeeded at 11 with
  fuzz 1 (offset 9 lines).\n`, 0; f = `1 2 3 4 five 6 7 8 9 10 a b 4 5 6 c d`
  -> the reversed question block above, 1.
- `PatchOutputLineNumbers` (`-F0`; f = `0\n` + seq 20): `@@ -1,4 +1,3 @@ 1
  -2 -3 +TWO 4` then `@@ -10,3 +9,3 @@ 10 -11 +ELEVEN 12` -> `patching file
  f\nHunk #1 succeeded at 2 (offset 1 line).\nHunk #2 succeeded at 10
  (offset 1 line).\n`; with `-Q` for `-11` -> `...\nHunk #2 FAILED at 9.\n1
  out of 2 hunks FAILED -- saving rejects to file f.rej\n`, f.rej = `---
  f\n+++ f\n@@ -9,3 +8,3 @@\n 10\n-Q\n+ELEVEN\n 12\n`, with
  `--reject-format=context` `*** f\n--- f\n***************\n***
  9,11 ****\n  10\n! Q\n  12\n--- 8,10 ----\n  10\n! ELEVEN\n  12\n`; `@@ -3,3 +3,5 @@ 2
  +a +b 3 4` then `@@ -8,3 +100,3 @@ 10 -11 +ELEVEN 12` -> `patching file
  f\nHunk #2 succeeded at 13 (offset 3 lines).\n`; the same first hunk then
  `@@ -10,3 +100,3 @@ 10 -Q +ELEVEN 12` -> `Hunk #2 FAILED at 12.`, f.rej
  `@@ -12,3 +102,3 @@`, context form `*** 12,14 ****` / `--- 102,104 ----`;
  f = `0\n0\n` + seq 20, `@@ -1,3 +1,3 @@ 1 -2 +TWO 3` then `@@ -10,3 +10,3
  @@ 10 -Q +ELEVEN 12` -> `Hunk #1 succeeded at 3 (offset 2 lines).\nHunk #2
  FAILED at 10.\n`; seq 12 (default fuzz), `@@ -1,4 +1,3 @@ 1 -2 -3 +TWO 4`
  then `@@ -2,5 +1,5 @@ 2 3 4 -5 +FIVE 6` -> `patching file f\nHunk #2
  succeeded at 1 with fuzz 2.\n`, f = `1 TWO 4 FIVE 6 .. 12`; with `@@ -1,6
  +1,6 @@ 1 2 3 4 -5 +FIVE 6` and `-F3` -> `Hunk #2 succeeded at 0 with fuzz
  3.`, the same f.
- `PatchOverlappingHunks` (`-F0`, f = `a`..`h`): ` b -c +C d` at 2 then ` c
  -d +D e` at 3 -> `patching file f\n`, 0, f = `a b C D e f g h`; then ` b
  -c +CC d` at 2 instead -> Haisos `patching file f\nHunk #2 FAILED at
  2.\n1 out of 2 hunks FAILED -- saving rejects to file f.rej\n` (GNU prints
  `misordered hunks! output would be garbled\n` before the `Hunk #2` line),
  f = `a b C d e f g h`; `@@ -2,3 +2,4 @@ b +X c d` then `@@ -2,3 +3,3 @@ b
  -c +C d` -> `patching file f\n`, f = `a b X C d e f g h`.
- `PatchLooseWhitespace`: with `-l -F0`, the patch
  `"--- f\n+++ f\n@@ -1,3 +1,3 @@\n a\n-" OLD "\n+NEW\n c\n"` on the
  file `"a\n" FILE "\nc\n"`, as C strings (OLD, FILE):
  - applies (`patching file f\n`, 0) for ("b  ", "b"), ("b", "b  "),
    ("x y", "x  y"), ("x y", "x\ty"), (" b", "  b"), ("b", "b\t"),
    ("", "   "), and ("b", "b") on the file `"a\nb\nc"` (no final newline);
  - fails (FAILED(1)) for ("x y", "xy"), ("xy", "x y"), ("b", "  b"),
    ("  b", "b"), ("b", "B"), ("b", "b\r"), ("x y", "x\vy"), ("x y", "x\fy");
  - without `-l`, ("b", "b  ") fails;
  - OLD "b" on `"a  \nb\nc\n"` gives `"a  \nNEW\nc\n"` (the file's
    context kept);
  - S10 with `"--- f\n+++ f\n@@ -1,3 +1,3 @@\n 1\n-2  \n+two\n 3\n"` and
    `-l` -> `patching file f\n`, 0, line 2 `two`.
- `PatchContextDiff`: kC on S10 -> `patching file f\n`, 0, line 5 `five`;
  `-R` on the result -> S10; line 3 `three` -> `Hunk #1 succeeded at 2 with
  fuzz 2.`, with `-F0` FAILED(2) and f.rej = kC; lines 3 and 4 changed,
  `-F3` -> fuzz 3; on `a` = S10: `*** a\n--- del\n***************\n*** 2,8
  ****\n  2\n  3\n  4\n- 5\n  6\n  7\n  8\n--- 2,7 ----\n` -> `5` gone;
  `*** a\n--- ins\n***************\n*** 3,8 ****\n---
  3,9 ----\n  3\n  4\n  5\n+ new\n  6\n  7\n  8\n` -> `new` after `5`; old style `*** f\n---
  f\n***************\n*** 4,6\n  4\n! 5\n  6\n--- 4,6\n  4\n! FIVE\n  6\n` ->
  applied; `*** /dev/null\tThu Oct  8 21:24:34 2026\n--- new\tTue Jan  2
  03:04:05 2024\n***************\n*** 0 ****\n--- 1,3 ----\n+ a\n+ B\n+ c\n`
  with no `new` -> `patching file new\n`, new = `a\nB\nc\n`; `*** old\n---
  nn\n***************\n*** 1,2 ****\n  a\n! b\n--- 1,2 ----\n  a\n! b\n\\ No
  newline at end of file\n` on `a\nb\n` -> `a\nb`; a star line of 8 `*`
  works, of 7 is `Only garbage ...`, 2; `*** nosuch\n--- nosuch2\n` + a hunk
  -> `can't find file to patch at input line 3\nPerhaps you should have used
  the -p or --strip option?\nThe text leading up to this
  was:\n--------------------------\n|*** nosuch\n|---
  nosuch2\n--------------------------\nFile to patch: \nSkip this patch? [y]
  \nSkipping patch.\n1 out of 1 hunk ignored\n`, 1.
- `PatchContextMalformed` (S10; hunk `*** 4,6 ****  4 ! 5  6 --- 4,6 ----  4
  ! FIVE  6` under `*** f\n--- f\n***************\n`, broken as said):
  `X 5` for `! 5` -> out empty, err `patch: **** malformed patch at line 6:
  X 5\n\n`, 2; the old `  6` left out -> err `patch: **** Premature '---' at
  line 7; check line numbers at line 4\n`; `*** 4,5 ****` -> `Overdue '---'
  at line 8; check line numbers at line 4`; `X FIVE` in the new part ->
  `malformed patch at line 10: X FIVE\n\n`; new part `---
  4,5 ----\n  4\n  6\n` -> out `patching file f\n`, err `patch: **** Out-of-sync patch, lines
  6,10 -- mangled text or line numbers, maybe?\n`; the new `  6` left out
  and a second hunk following -> `unexpected end of hunk at line 11`, out
  empty; left out at the input's end -> `patching file f\n`, 0, applied; on
  `0\n` + S10, a good first hunk `*** 1,2 ****\n! 1\n  2\n--- 1,2 ----\n!
  ONE\n  2\n` then the `X 5` hunk -> out `patching file f\nHunk #1 succeeded
  at 2 with fuzz 1 (offset 1 line).\n`, err `... malformed patch at line 13:
  X 5\n\n`, 2, f unchanged. Every case leaves f unchanged.
- `PatchNormalDiff`: `patch f` with kN on S10 -> `patching file f\n`, 0;
  without the operand -> `patch: **** Only garbage was found in the patch
  input.\n`, 2; `-n` -> `can't find file to patch at input line 1\nFile to
  patch: \nSkip this patch? [y] \nSkipping patch.\n1 out of 1 hunk
  ignored\n`, 1; `-n -s` -> `File to patch: \nSkip this patch? [y] \n1 out
  of 1 hunk ignored\n`; `-n -t` -> `can't find file to patch at input line
  1\nNo file to patch.  Skipping patch.\n1 out of 1 hunk ignored\n`;
  `some text\nmore\n` + kN with `-n` -> `can't find file to patch at input
  line 3\nThe text leading up to this
  was:\n--------------------------\n|some text\n|more\n--------------------------\nFile
  to patch: \nSkip this patch? [y] \nSkipping patch.\n1 out of 1 hunk
  ignored\n`, and with `f` -> `patching file f\n`, 0; on `0\n0\n` + S10 ->
  `Hunk #1 succeeded at 7 (offset 2 lines).`; on the patched file -> the
  reversed question; `-o out2 f` -> `patching file out2 (read from f)\n`;
  `3c3\n< 3\n---\n> three\n6d5\n< 6\n7a7\n> seven2\n` with `a` on `1 2 3 4
  X 6 7 8 9 10` -> a = `1 2 three 4 X 7 seven2 8 9 10`, 0; on ten `X` lines
  -> `patching file a\nHunk #1 FAILED at 3.\nHunk #2 FAILED at 6.\n2 out of 3
  hunks FAILED -- saving rejects to file a.rej\n`, 1, a = seven `X`,
  `seven2`, three `X`, a.rej as in "rejects"; forcing: kP1 with `-c`, kC
  with `-u`, kC with `-n f`, kP1 with `-n f` -> `Only garbage ...`, 2.
- `PatchNormalMalformed` (`f`, S10): `5c5\n< 5\nX\n> five\n` -> out
  `patching file f\n`, err `patch: **** '---' expected at line 3 of
  patch\n`, 2; `5,6c5\n< 5\n---\n> five\n` -> `'<' followed by space or tab
  expected at line 3 of patch`; `5c5\n< 5\n---\nX\n` -> `'>' followed by
  space or tab expected at line 4 of patch`; `5c5,6\n< 5\n---\n> five\n` ->
  `unexpected end of file in patch at line 4`; `2c2\n< 2\n---\n> two\n` +
  the first case's hunk -> `'---' expected at line 7 of patch`, f unchanged;
  `5a6\n` alone -> `Only garbage ...`.
- `PatchRejectOptions` (kC, f = S10 with line 5 `X`): `-r my.rej` ->
  `patching file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED -- saving
  rejects to file my.rej\n`, my.rej = kC, no f.rej; `-r -` -> `patching file
  f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED\n`, no reject file; `-r -
  -s` -> `1 out of 1 hunk FAILED\n`; `-r my.rej --dry-run` -> `checking file
  f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED\n`; `-r -` on the patched
  file -> the question, then `1 out of 1 hunk ignored\n`; x = `a\n`, y =
  `b\n`, my.rej = `STALE\n`, `--- x\n+++ x\n@@ -1 +1 @@\n-q\n+Q\n--- y\n+++
  y\n@@ -1 +1 @@\n-r\n+R\n` with `-r my.rej` -> both summaries name my.rej,
  1, my.rej = the two file patches' unified texts in order; `-r
  nodir/my.rej` -> the summary naming it, then err `patch: **** Can't create
  file nodir/my.rej : No such file or directory\n`, 2; f.rej a directory ->
  Haisos err `patch: **** Can't create file f.rej : Is a directory\n`, 2;
  `--reject-format=context` with kP1 -> f.rej = `*** f\n--- g\n` + kC's
  hunk; `--reject-format=unified` with kC -> `--- f\tTue Jan  2 03:04:05
  2024\n+++ g\tTue Jan  2 03:04:05 2024\n@@ -2,7 +2,7 @@\n 2\n 3\n
  4\n-5\n+five\n 6\n 7\n 8\n`; `--reject-format=foo` and `=c` -> err `patch: Try
  'patch --help' for more information.\n`, 2.
- `PatchContextRejects` (each on lines of `X`): the `diff -c a mix`, normal
  and old-style `.rej` texts of "rejects"; `-F0 --reject-format=context k`
  on `k` = `a\nb\nc\n` with `--- k\n+++ k\n@@ -1,2 +1,3 @@\n Q\n+new\n b\n` ->
  k.rej = `*** k\n--- k\n***************\n*** 1,2 ****\n  Q\n  b\n--- 1,3
  ----\n  Q\n+ new\n  b\n` (without `-F0`: `Hunk #1 succeeded at 1 with fuzz
  1.`, k = `a new b c`); `--- f\n+++ f\n@@ -5,0 +6 @@\n+new\n@@ -8 +8,0
  @@\n-8\n@@ -10,2 +10,2 @@\n-x\n-y\n+X\n+Y\n` on `Q\n` with
  `--reject-format=context` -> `patching file f\nHunk #2 FAILED at 9.\nHunk
  #3 FAILED at 11.\n2 out of 3 hunks FAILED -- saving rejects to file
  f.rej\n`, f.rej = `*** f\n--- f\n***************\n*** 9 ****\n- 8\n--- 0
  ----\n***************\n*** 11,12 ****\n! x\n! y\n--- 11,12 ----\n! X\n!
  Y\n`, unified: `--- f\n+++ f\n@@ -9 +9,0 @@\n-8\n@@ -11,2 +11,2
  @@\n-x\n-y\n+X\n+Y\n`; `*************** fn()` saved as such, and a unified
  `@@ -4,3 +4,3 @@ fn()` with `--reject-format=context` too; `--- /dev/null\t2026-10-08
  21:24:34.863626600 +0300\n+++ new\t2024-01-02 03:04:05.000000000 +0200\n@@
  -0,0 +1,3 @@\n+a\n+B\n+c\n` with `-f` on `new` = `x\ny\n` -> `The next
  patch would create the file new,\nwhich already exists!  Applying it
  anyway.\npatching file new\nHunk #1 FAILED at 1.\n1 out of 1 hunk FAILED
  -- saving rejects to file new.rej\n`, new.rej = `--- /dev/null\n+++
  new\t2024-01-02 03:04:05.000000000 +0200\n@@ -0,0 +1,3 @@\n+a\n+B\n+c\n`,
  context form `*** /dev/null\n--- new\t2024-...\n***************\n*** 0
  ****\n--- 1,3 ----\n+ a\n+ B\n+ c\n`.
- `PatchBackups` (kC on S10 unless said; environments through
  `RunCaptured`'s last parameter): every case of "backups -> How", plus `-b
  -V none` and `-b -V off` -> `f.~1~`, `-b -V never`/`nil` -> `f.orig`, `-b
  --dry-run` -> none, `-b -o out` -> none, the patched file with `-b` (the
  question, skipped) -> none, `-b` on the creation above -> empty
  `new.orig`, `-b -V numbered` with kC followed by `*** f\n---
  f\n***************\n*** 9,10 ****\n  9\n! 10\n--- 9,10 ----\n  9\n! TEN\n`
  in one run -> only `f.~1~`, holding S10.
- `PatchBackupOptionErrors`: `-F x` -> err `patch: **** fuzz factor x is not
  a number\n`, 2; `-F -1` -> `... -1 is negative`; `-F 2x` not a number;
  `-F ''` -> `patch: **** fuzz factor '' is not a number\n`; `-F
  99999999999999999999` -> applies, 0; `-z ''` -> `patch: **** backup suffix
  is empty\n`, 2; `-b -V bogus` and `-V bogus` -> the block of "backups", 2;
  `-b -V n` -> the same with `ambiguous argument 'n'`; `-V bogus -V simple
  -b` -> 0, `f.orig`; `-V none -V bogus -b` -> the block; `-F x -z ''` ->
  the fuzz message; `-z '' -F x` -> the suffix message; `--reject-format=foo
  -F x` -> the Try line; `VERSION_CONTROL=bogus` (with or without `-b`) ->
  the block with `'$VERSION_CONTROL'`, 2, f unchanged;
  `PATCH_VERSION_CONTROL=bogus -b` -> with `'$PATCH_VERSION_CONTROL'`;
  `VERSION_CONTROL=bogus -V simple -b` -> 0, `f.orig`;
  `PATCH_VERSION_CONTROL=numbered VERSION_CONTROL=bogus -b` -> 0, `f.~1~`.
- `PatchFileFailures` (#74; kC): f.orig a directory with `-b` -> out
  `patching file f\n`, err `patch: **** Can't rename file f to f.orig : Is a
  directory\n`, 2, f = S10; the same with an offset hunk (no `-b`) -> out
  `patching file f\nHunk #1 succeeded at 3 (offset 1 line).\n`, the same
  err; `-o o` with `o` a directory -> out empty, err `patch: **** Can't
  create file o : Is a directory\n`, 2; `-o nodir/out` -> `... Can't create
  file nodir/out : No such file or directory\n`, 2.
- `PatchHugeNumbers` (#74; S10 in f, `--- f\n+++ f\n` + header + `+x\n`):
  `@@ -9223372036854775807,0 +1 @@` -> out `patching file f\n`, err `patch:
  **** malformed patch at line 3: @@ -9223372036854775807,0 +1 @@\n\n`, 2;
  `@@ -1 +9223372036854775807,0 @@` -> the same message for that line;
  `@@ -99999999999999999999 +1 @@` -> err `patch: **** line number
  99999999999999999999 is too large at line 3: @@ -99999999999999999999 +1
  @@\n\n`, 2; `-p 99999999999999999999 --dry-run` on `--- a/d/f\n+++
  a/d/f\n@@ -1 +1 @@\n-1\n+one\n` with `d/f` = `1\n` -> `can't find file to
  patch at input line 3\nPerhaps you used the wrong -p or --strip
  option?\n...`, 1; Haisos: `-p 4294967296` the same (GNU strips 0 there).
- `PatchGitRenameWithOrigFile` (#74; `old` = `1\n2\n3\n`, `other` = the
  same, `diff --git a/old b/new\nsimilarity index 80%\nrename from
  old\nrename to new\n--- a/old\n+++ b/new\n@@ -1,3 +1,3 @@\n 1\n-2\n+TWO\n
  3\n`, `-p1 other`): `patching file new (renamed from other)\n`, 0, new =
  `1 TWO 3`, other gone, old kept; `copy from/copy to` -> `(copied from
  other)`, other kept; `similarity index 100%`, no hunks, `other` =
  `other\n` -> new = `other\n`, other gone; `-o out` -> `patching file out
  (renamed from other)\n`, other kept; ORIGFILE `nosuch` -> `patching file
  new (renamed from nosuch)\nHunk #1 FAILED at 1.\n1 out of 1 hunk FAILED --
  saving rejects to file new.rej\n`, 1.
- `PatchNotDeletingBeforeSummary`: `old` = `x\ny\n`, `*** old\n---
  /dev/null\n***************\n*** 1,2 ****\n- a\n- b\n--- 0 ----\n` with
  `-f` -> `patching file old\nHunk #1 FAILED at 1.\nNot deleting file old as
  content differs from patch\n1 out of 1 hunk FAILED -- saving rejects to
  file old.rej\n`, 1, old.rej = `*** old\n--- /dev/null\n` + the hunk.
- `DiffTest.cpp`, `DiffFindLongOptionExactFirst`: add a short-only entry
  `{'q', "", 4, BuiltinArgument::None}` first in the table and
  `EXPECT_EQ(DiffFindLongOption("", table), nullptr)`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Patch*:BuiltinCommandsTest.Diff*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`, the `patch` row: version
  1.1.0; treated adds `-F --fuzz -l --ignore-whitespace -b --backup -z
  --suffix -V --version-control --backup-if-mismatch
  --no-backup-if-mismatch -r --reject-file --reject-format -c --context -n
  --normal`; fuzz (the larger context side ignored first, the reported
  fuzz the smallest that matched), the output's line numbers in the hunk
  messages and `.rej` ranges, backups (`BackupPathFor`, `none`/`off`
  numbered as GNU 2.7.6), context (old and new style) and normal diffs, the
  context `.rej`. Exceptions, replacing "only unified diffs ... (no fuzz
  factor)", "the backup options ... not treated" and "`SIMPLE_BACKUP_SUFFIX`
  and `VERSION_CONTROL` are not read": no terminal (as now); ed scripts not
  recognised (`-e` not treated); a hunk that matches only before the end of
  the previous hunk fails without GNU's `misordered hunks! output would be
  garbled` line; a `-p` count of 2^31 or more is taken as written, never
  wrapped to 32 bits; a `.rej` that cannot be written is reported as `Can't
  create file NAME : REASON` (GNU names its temporary file in a `Can't
  rename` message), and a failed write as `write error : Input/output
  error`, the reasons inferred because `IFileIO` gives none; a `diff -C0`
  deletion (an empty `--- C ----` part) is applied, where GNU refuses it
  (`replacement text or line numbers mangled`); a git rename from a missing
  ORIGFILE leaves no `ORIGFILE.orig`. The `commands/patch/` line: the fuzz,
  `-l`, the context and normal readers and the context `.rej` writer.
- Root `CLAUDE.md`, the `patch` row: fuzz, backups, `-r`, context and normal
  diffs.
- Help notes, as listed above.

## Acceptance

- [ ] Fuzz: levels 0..min(-F, max context) tried in turn, the forward hunk
      then (hunk #1) the reversed one at each; the larger context side
      ignored first; `with fuzz F` before the offset; `-F0` is patch-core's
      exact matching; `PatchHunkAtStartNeedsStart` and the end-anchored case
      updated.
- [ ] Hunk messages and `.rej` ranges in the output's line numbers; a hunk
      may start on the previous hunk's last old line, written once.
- [ ] `-l` as specified.
- [ ] Backups: `-b`, `-z`, `-V` (last wins, checked after parsing,
      `none`/`off` numbered), `PATCH_VERSION_CONTROL` before
      `VERSION_CONTROL`, `SIMPLE_BACKUP_SUFFIX`, mismatch backups on by
      default, by rename, once per file per run, none with `-o`/`--dry-run`.
- [ ] Rejects: `-r FILE`, `-r -`, `--reject-format`, the context form byte
      for byte for context, old-style, normal and unified inputs;
      `/dev/null` sides without times.
- [ ] Context (old and new style) and normal diffs parsed, applied and
      refused with GNU's messages; `-c`/`-n`/`-u` force the format; normal
      diffs need ORIGFILE or `-n`.
- [ ] #74 follow-ups: int64 strip count, no overflow in hunk headers,
      failures of `.rej`/`.orig`/`-o` reported, ORIGFILE for git
      renames/copies, `PatchParse.cpp` hygiene, `DiffFindLongOption`.
- [ ] Every file through `context.IO()`; version 1.1.0; docs; all unit tests
      pass.

## Out of scope

- Ed scripts (`-e` and ed input), `--merge`, `-D/--ifdef`, `-g/--get`,
  `-T`/`-Z`, `-B/--prefix`, `-Y/--basename-prefix`, `--quoting-style`,
  `--read-only`, `--follow-symlinks`, `--posix`, `--verbose`, `-x/--debug`;
  RFC 934 encapsulation and indented patches.
