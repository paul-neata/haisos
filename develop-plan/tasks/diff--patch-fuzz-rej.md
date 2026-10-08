# Task diff--patch-fuzz-rej: patch -- fuzz, backups, reject files, context and normal diffs

- Rock: diff
- Depends on: diff--patch-core (`PatchReader`, `FilePatch`, `PatchHunk`, `LocateHunk`, the `.rej` writer), coreutils--cp (`BackupPathFor`, `BackupMode` in `BuiltinCopy.h`)
- Size: ~750 changed lines in ~5 files
- Plan checked against: develop @ ccb9dbe
- PR title: patch: fuzz, backups, -r, context and normal diffs

## Goal

`patch` finishes matching GNU patch 2.7.6 (C locale, no terminal): a hunk
whose context no longer matches exactly is applied with fuzz (`-F NUM`,
default 2: `Hunk #1 succeeded at 2 with fuzz 2.`); `-l` matches loosely on
whitespace; backups follow `-b`, `-z SUFFIX`, `-V METHOD`, `VERSION_CONTROL`
/ `PATCH_VERSION_CONTROL` / `SIMPLE_BACKUP_SUFFIX`, `--backup-if-mismatch`
and `--no-backup-if-mismatch`; rejects go to `-r FILE` (or nowhere with `-r
-`) in `--reject-format=context|unified` or the input's own format;
context diffs (`diff -c`, `-c`) and normal diffs (`diff`, `-n`, with
ORIGFILE) are read and applied. Ed scripts (`-e`) and `--merge` stay not
treated.

## Context

Read first: the plan `develop-plan/tasks/diff--patch-core.md` and its code in
`src/components/BuiltinCommands/commands/patch/` (`PatchParse.h`,
`PatchApply.h`, `Patch.cpp`), `develop-plan/tasks/coreutils--cp.md`
("Backups"), the root `CLAUDE.md` ("Builtin Commands", "Security") and
`src/components/BuiltinCommands/CLAUDE.md`.

What earlier tasks provide, as if on develop:
- diff--patch-core: `PatchReader::Next`, `FilePatch` (with `PatchFormat
  { Unified }`, `oldSaysNonexistent`/`newSaysNonexistent`, time texts),
  `PatchHunk` (`oldFirst/oldCount/newFirst/newCount`, `lines`,
  `prefixContext`, `suffixContext`, `function`), `SwapFilePatch`,
  `LocateHunk(target, hunk, fuzz, lastFrozenLine, inOffset)` -- already
  implementing fuzz as a parameter, always called with 0 -- the unified
  `.rej` writer, the mismatch `.orig` backup, and every message of the main
  loop. The options of this task are in `Options()` as `kBuiltinNotTreated`.
- coreutils--cp, `src/components/BuiltinCommands/BuiltinCopy.h`:
  `enum class BackupMode { None, Simple, Numbered, Existing };`
  `std::string BackupPathFor(BuiltinContext&, const std::string& dest, BackupMode mode, const std::string& suffix);`
  (`Numbered`: `dest.~N~` one above the highest; `Existing`: numbered if a
  numbered backup exists, else simple).

Reference: GNU patch 2.7.6 (`patch.c` main loop and `locate_hunk`, `pch.c`
`another_hunk` for context and normal diffs, `abort_hunk_context`). The
container has no `patch`: the outputs below are GNU's with `LC_ALL=C`.

## Changes

Rules that bite: every file through `context.IO()` (backups are
`IO().Rename`s, `-r FILE` written through `IO().OpenFile`); GNU's messages
byte for byte; untreated options stay reported; portable C++17; nothing
creates a link.

### `commands/patch/Patch.cpp`

- Ids for `-F/--fuzz NUM`, `-l/--ignore-whitespace`, `-b/--backup`,
  `-z/--suffix SUFFIX`, `-V/--version-control METHOD`,
  `--backup-if-mismatch`, `--no-backup-if-mismatch`, `-r/--reject-file
  FILE`, `--reject-format FORMAT`, `-c/--context`, `-n/--normal`. Version
  `1.1.0`.
- `-F X`: GNU's `numeric_string`: not a number -> `patch: **** fuzz factor X
  is not a number`, negative -> `... is negative`, 2.
- `-z ''` -> `patch: **** backup suffix is empty`, 2.
- `-V WORD`: `none`/`off`, `simple`/`never`, `existing`/`nil`,
  `numbered`/`t`, or an unambiguous prefix of them; otherwise stderr gets
  exactly (no Try line) `patch: invalid argument 'WORD' for '--version-control
  or -V option'\nValid arguments are:\n  - 'none', 'off'\n  - 'simple',
  'never'\n  - 'existing', 'nil'\n  - 'numbered', 't'\n`, exit 2 (an
  ambiguous prefix: `ambiguous argument`).
- `--reject-format` other than `context`/`unified`: `patch: Try 'patch
  --help' for more information.`, 2.
- Backup settings: suffix = `-z`, else the process environment's
  `SIMPLE_BACKUP_SUFFIX` (non-empty), else `.orig`; method = `-V`, else
  `PATCH_VERSION_CONTROL`, else `VERSION_CONTROL`, else `Existing` (an
  invalid environment value: the same block with `$VERSION_CONTROL` /
  `$PATCH_VERSION_CONTROL` in place of `--version-control or -V option`).
  Environment through `context.Process()`'s environment, as cp reads it.
- When to back up (replaces diff--patch-core's fixed rule): `-b`, or
  backup-if-mismatch (on by default; `--no-backup-if-mismatch` turns it off,
  `--backup-if-mismatch` on) and some hunk was applied with an offset or
  fuzz, or failed. The backup name is `BackupPathFor(context, target,
  method, suffix)`; the original is renamed to it once per file per run.
  A file that did not exist gets no backup.
- The fuzz loop (GNU's): for each hunk, `maxFuzz = min(-F (default 2),
  max(prefixContext, suffixContext))`; try `LocateHunk` with fuzz 0, 1, ...
  up to maxFuzz, the first success wins; the reversed-patch check of
  diff--patch-core runs inside the fuzz-0 try of hunk #1 only, as now.
  Success with fuzz: `Hunk #K succeeded at L with fuzz F.` or `... with
  fuzz F (offset N lines).` (fuzz before offset), silenced by -s.
- `-l`: in `LocateHunk`'s line comparison, two lines match when equal after
  every run of whitespace (space, tab, `\r`, `\n` handling as GNU's
  `similar`: runs of blanks equal any non-empty run, and trailing blanks
  are ignored) -- add a `bool looseWhitespace` parameter to `LocateHunk`.
- Rejects: `-r FILE`: all rejects of the run go to FILE (written on the
  first failure, appended after), and the summary says `-- saving rejects
  to file FILE`; `-r -`: rejects are discarded and the summary ends after
  `FAILED` (or `ignored`) with `\n`. Format: `--reject-format`, else the
  input's own: unified input -> unified, context or normal input -> context.

### `commands/patch/PatchParse.cpp` / `.h`

- `PatchFormat` gains `Context`, `NewContext` (GNU's new-style, `*** a,b
  ****` headers, what `diff -c` writes) and `Normal`. `-c`/`-n`/`-u` force
  how the input is read (GNU's `diff_type`).
- **Context diffs**: file headers `*** NAME[\tTIME]` (old) and `---
  NAME[\tTIME]` (new), names and times as for unified headers (the time
  texts kept for the `.rej`; a `diff -c` C-locale time `Tue Jan  2 03:04:05
  2024` and an ISO one both count for the epoch rule); a hunk starts with a
  line of 15 or more `*`s (anything after them is the function), then
  `*** A[,B] ****` (new style) or `*** A[,B]` (old style), its old lines
  (`  `, `- `, `! ` followed by the text), `--- C[,D] ----` (or ` -----`
  / nothing), its new lines (`  `, `+ `, `! `). Either part may list no
  lines: it is then the other part's context lines. Convert to the unified
  `PatchHunk` order by walking both parts together: context lines pair up;
  `- ` lines are deletions; `+ ` insertions; a run of `! ` in the old part
  and the next run of `! ` in the new part are deletions followed by
  insertions. A first range of `0` (or `A` 0 with no lines) says, with an
  epoch time, the old file does not exist; a hunk with no new lines whose
  new range starts at 1 says the new one does not (GNU). Counts: B - A + 1
  old lines (one when B is absent), likewise new. Malformed -> `patch:
  **** malformed patch at line N: LINE`, 2.
- **Normal diffs**: a hunk is `A[,B]{a|c|d}C[,D]`, then `< ` lines (old),
  `---` (for `c`), `> ` lines (new), `\ No newline at end of file` after a
  line as usual. A normal diff has no file names: it is only recognised
  when a file to patch is given (the ORIGFILE operand) -- otherwise input
  starting with one is `patch: **** Only garbage was found in the patch
  input.`, 2 (GNU). Hunks have no context, so `LocateHunk` only finds
  `pat == 0` (an insertion) at its stated place, and a deletion or change
  where its old lines are, searching offsets as usual. For `a`, oldFirst is
  A + 1 with no old lines; for `d`, newFirst is C + 1 with no new lines.
- Ed scripts (a hunk line like `5c` followed by text and `.`): reported with
  `-e` as not treated; met in the input, they are not recognised (GNU would
  run `ed`).

### `commands/patch/PatchApply.cpp` / `.h`

- The context-format `.rej` writer (GNU's `abort_hunk_context`): header
  (first failed hunk of a file) `*** OLDNAME` + oldTimeText, `--- NEWNAME` +
  newTimeText (`/dev/null` for an absent name -- always for normal diffs);
  per hunk `***************` + function; `*** R0 ****` (old/new-style and
  unified inputs; ` ****` left out for normal inputs) with R0 = `0` when
  the old part is empty, `A` for one line, else `A,B`; every old line with
  its mark: `  ` context, `- ` deleted, `! ` deleted in a change (a
  deletion run followed by an insertion run; for a normal input always `-
  `); `--- R1 ----` (` -----` for normal inputs); every new line: `  `, `+
  `, `! ` (normal: `+ `). Both parts are always written in full, even when
  one has no changes.

## Tests

In `tests/unit/components/BuiltinCommands.unittests/PatchTest.cpp` (exists),
`TEST_F(BuiltinCommandsTest, Patch...)`. Setup for most: `/q/f` = `seq 1
10` (`1\n` .. `10\n`); `c.diff` = `*** f\tTue Jan  2 03:04:05 2024\n--- g\tTue
Jan  2 03:04:05 2024\n***************\n*** 2,8 ****\n  2\n  3\n  4\n! 5\n
6\n  7\n  8\n--- 2,8 ----\n  2\n  3\n  4\n! five\n  6\n  7\n  8\n` (what
`diff -c f g` writes, g having `five` for line 5); `n.diff` = `5c5\n< 5\n---\n>
five\n`. Expected outputs are GNU patch 2.7.6's (`LC_ALL=C`).

- `PatchContextDiff`: `patch < c.diff` -> `patching file f\n`, 0, line 5 is
  `five`.
- `PatchNormalDiff`: `patch f < n.diff` -> `patching file f\n`, 0; `patch <
  n.diff` -> err `patch: **** Only garbage was found in the patch input.\n`,
  2.
- `PatchFuzz`: `f` with line 3 changed to `three`: `patch < c.diff` ->
  `patching file f\nHunk #1 succeeded at 2 with fuzz 2.\n`, 0; with `-F 0`
  -> `patching file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED --
  saving rejects to file f.rej\n`, 1. Lines 3 and 4 changed: default ->
  FAILED, and `f.rej` is `*** f\tTue Jan  2 03:04:05 2024\n--- g\tTue Jan  2
  03:04:05 2024\n***************\n*** 2,8 ****\n  2\n  3\n  4\n! 5\n  6\n
  7\n  8\n--- 2,8 ----\n  2\n  3\n  4\n! five\n  6\n  7\n  8\n`; `-F 3` ->
  `Hunk #1 succeeded at 2 with fuzz 3.\n`, 0.
- `PatchFuzzAtFileStart`: diff--patch-core's `PatchHunkAtStartNeedsStart`
  case (`z\na\nb\n`, hunk `@@ -1,2 +1,2 @@\n-a\n+A\n b\n`) -> `patching file
  x\nHunk #1 succeeded at 2 with fuzz 1 (offset 1 line).\n`, 0, `x` =
  `z\nA\nb\n` -- update that test's expectation.
- `PatchRejectOptions`: line 5 changed to `X`: `-r my.rej` -> `... --
  saving rejects to file my.rej\n`, `my.rej` as above (context form); `-r
  -` -> `patching file f\nHunk #1 FAILED at 2.\n1 out of 1 hunk FAILED\n`,
  no file; `--reject-format=context` on a unified input -> a context `.rej`
  with `*** 2,8 ****` and `! ` marks; a normal-diff failure (`patch f <
  n.diff` on that file) -> `f.rej` = `*** /dev/null\n--- /dev/null\n***************\n***
  5\n- 5\n--- 5 -----\n+ five\n`.
- `PatchContextRejectPureInsertion`: `k` = `a\nb\nc\n`, `-F0
  --reject-format=context k` with `--- k\n+++ k\n@@ -1,2 +1,3 @@\n Q\n+new\n
  b\n` -> `k.rej` = `*** k\n--- k\n***************\n*** 1,2 ****\n  Q\n
  b\n--- 1,3 ----\n  Q\n+ new\n  b\n`; without `-F0` -> `Hunk #1 succeeded
  at 1 with fuzz 1.\n`.
- `PatchBackups`: `-b < c.diff` -> `f.orig` holds the old content; `-b -z
  .bak` -> `f.bak`; `-V numbered -b` twice (the second with `-R`) ->
  `f.~1~` then `f.~2~`; environment `VERSION_CONTROL=numbered` with `-b`
  -> `f.~1~` (pass an environment with that variable to `RunCaptured`);
  `--no-backup-if-mismatch` with an offset hunk -> no `.orig`.
- `PatchOptionErrors`: `-F x` -> `patch: **** fuzz factor x is not a
  number\n`, 2; `-z ''` -> `patch: **** backup suffix is empty\n`, 2; `-b -V
  bogus` -> the exact block above, 2; `--reject-format=foo` -> `patch: Try
  'patch --help' for more information.\n`, 2.
- `PatchLooseWhitespace`: `f` = seq 1 10, `-l` with `--- f\n+++ f\n@@ -1,3
  +1,3 @@\n 1\n-2  \n+two\n 3\n` -> `patching file f\n`, 0, line 2 `two`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Patch*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the `patch` row -- version
  1.1.0; `-F -l -b -z -V --[no-]backup-if-mismatch -r --reject-format -c
  -n` treated, context and normal diffs read; exceptions: ed scripts,
  `--merge`, `-D`, `-g`, `-T`/`-Z`, `-B`/`-Y` not treated.
- Root `CLAUDE.md`: the `patch` row mentions fuzz, backups and context /
  normal diffs.

## Acceptance

- [ ] Fuzz loop and messages as GNU (`with fuzz F`, before the offset);
      `-F` default 2, bounded by the hunk's context.
- [ ] Backups: names from `BackupPathFor`, method/suffix from options, then
      environment; on mismatch unless `--no-backup-if-mismatch`; once per
      file per run.
- [ ] Rejects: `-r FILE`, `-r -`, `--reject-format`, context form byte for
      byte for context, unified-as-context and normal inputs.
- [ ] Context (old and new style) and normal diffs parsed and applied;
      normal diffs need ORIGFILE.
- [ ] Every file through `context.IO()`; version 1.1.0; docs; tests pass.

## Out of scope

- Ed scripts, `--merge`, `-D/--ifdef`, `-g/--get`, `-T`/`-Z`,
  `-B/--prefix`, `-Y/--basename-prefix`, `--quoting-style`, `--read-only`,
  `--follow-symlinks`, `--posix`, `--verbose`, `-x/--debug`.
