# diff--patch-fuzz-rej — done

Finished the `patch` builtin against GNU patch 2.7.6 (C locale, no
terminal): the fuzz factor, backups, rejects, context/normal diffs, and
the #74 follow-ups. All 36 `Patch*` unit tests pass; the full unit
suite is green (22 test binaries, 0 failures).

## What landed (commit 642aa776, on top of 431c0504)

- Fuzz (`-F`, default 2): each level tried in turn, the side with more
  context losing lines first, an unbalanced hunk anchored at its light
  side, an insertion with no old lines placed not searched, reversal
  probed on the first hunk only, forward beating reversed at the same
  level; the reported fuzz is the smallest that matched. The candidate
  bound cannot reach back into earlier hunks' ground.
- `-l` loose whitespace: blank runs compared loosely, blanks and the
  newline ignored at a line's end.
- Backups: `-b` always, or a mismatch (offset/fuzz/failure) unless
  `--no-backup-if-mismatch`; `-z`/`SIMPLE_BACKUP_SUFFIX` the suffix,
  `-V`/`PATCH_VERSION_CONTROL`/`VERSION_CONTROL` the method (GNU's
  `none`/`off` numbered quirk kept), once per file per run, never with
  `-o`/`--dry-run`; `BackupPathFor` writes the method's messages.
- Rejects: `-r` file (or `-` to discard), `--reject-format=unified|
  context`; ranges written in the output file's line numbers; the
  reject file reuses the input's own grammar without the option;
  old-style and new-style context forms and a normal diff's marks.
- Context (old and new style) and normal diffs are now read; `-c`/`-n`
  force their format on the reject side.
- Hunk messages and `.rej` ranges carry the output's line numbers.
- #74 follow-ups: int64 `-p` strip (never wrapped to 32 bits), a hunk
  start/count beyond int64 fatal at the hunk reader, `--reject-format`
  errors carry the `patch: ` prefix, ORIGFILE with a git rename/copy
  (a missing source reads empty, hunks fail, backup under the written
  name), `DiffFindLongOption` short-only entry, version 1.1.0, help
  notes, docs updated in both CLAUDE.md files.

## Tests

`tests/unit/components/BuiltinCommands.unittests/PatchFuzzRejTest.cpp`
(new, ~1700 lines, 18 tests over the pieces of the plan): fuzz levels
and anchoring, reversal, `-l`, every backup mode and the env chain,
option errors (`-F`, `-z`, `-V` argmatch blocks, option order, env
wins), reject files (both formats, `-r -`, `-r` with `--dry-run`, a
directory in the way, creation/deletion rejects), context and normal
inputs, huge line numbers, git renames with an ORIGFILE operand, and
the not-deleting-before-summary path.

## Notes

- Four test-run failures turned out to be code bugs, all fixed on the
  side the plan indicates: the `--reject-format` Try line missing its
  prefix, huge hunk numbers treated as garbage (not fatal), `-p`
  wrapping at 32 bits, and a git rename from a missing ORIGFILE dying
  fatal (and double-numbering backups in one run).
- Documented exceptions (component CLAUDE.md patch row): no terminal;
  ed scripts not recognised; no `misordered hunks!` line; `.rej` write
  failures as `Can't create file NAME : REASON`; `diff -C0` deletions
  applied where GNU refuses; a git rename from a missing ORIGFILE
  leaves no `ORIGFILE.orig`; a `-p` count of 2^31+ taken as written.