# Task diff--diff-recursive: diff on directories -- -r, -N, -x, -S, a directory and a file

- Rock: diff
- Depends on: diff--diff-core (`DiffTwoFiles`, `DiffSettings`), search--grep-recursive (`FnMatch`, contract 6 -- earlier in the playbook)
- Size: ~550 changed lines in ~4 files
- Plan checked against: develop @ ccb9dbe
- PR title: diff: directories, -r, -N, --unidirectional-new-file, -x/-X, -S

## Goal

`diff` compares directories as GNU diffutils 3.10 does (C locale): `diff
dir1 dir2` lists `Only in DIR: NAME`, `Common subdirectories: A and B`,
`File A is a T1 while file B is a T2`, `Binary files A and B differ`, and
before each pair of differing files a `diff [OPTIONS] A B` line; `-r`
descends; `-N`/`--new-file` and `--unidirectional-new-file` compare a
missing file as empty; `-x PAT`/`--exclude`, `-X FILE`/`--exclude-from`
skip names; `-S FILE`/`--starting-file` starts the top level at FILE;
`--from-file`/`--to-file` compare one file with many; a directory and a file
operand compare the file with the one of the same name in the directory.
`diff -ruN a b` then gives exactly the patch GNU gives (what diff--patch-core
reads).

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md`, the plan
`develop-plan/tasks/diff--diff-core.md` and its code in
`src/components/BuiltinCommands/commands/diff/` (`Diff.h`: `DiffSettings`,
`DiffTwoFiles(context, settings, name0, name1, header)`; `Diff.cpp`'s option
table and `Run`), `commands/ls/Ls.cpp` (walking a directory with
`IO().ReadDirectory` and `IO().Stat`).

What earlier tasks provide, as if on develop:
- diff--diff-core: everything in `commands/diff/`; the options of this task
  are already in `Options()` as `kBuiltinNotTreated` -- give them ids.
  `DiffTwoFiles` currently refuses a directory operand with `diff: NAME: Is a
  directory`; this task takes directories before it is called.
- search--grep-recursive, `src/components/BuiltinCommands/BuiltinFnmatch.h`:
  `bool FnMatch(std::string_view pattern, std::string_view text, int flags = 0);`
  with `kFnmPathname`, `kFnmPeriod`, `kFnmCaseFold`, `kFnmLeadingDir` (glibc
  fnmatch).

Reference: GNU diffutils 3.10 `src/diff.c` (`compare_files`) and `src/dir.c`
(`diff_dirs`). The container has GNU diff 3.10: check anything not spelled
out here with `LC_ALL=C TZ=UTC diff ...`.

## Changes

Rules that bite: `ICurrentProcess` is the only door out (directories listed
with `context.IO().ReadDirectory`, entries stat-ed with `IO().Stat`; nothing
else); GNU's messages byte for byte; untreated options stay reported; stops
promptly on `TriggerStop()` (check `context.StopRequested()` before each
entry); portable C++17, no POSIX headers. There are no symbolic links in
Haisos, so no loop detection is needed and `--no-dereference` has no effect.

### New `src/components/BuiltinCommands/commands/diff/DiffDirectories.h` / `.cpp`

```cpp
struct DiffTreeSettings {
    bool recursive = false;              // -r
    bool newFile = false;                // -N
    bool unidirectionalNewFile = false;  // --unidirectional-new-file
    std::vector<std::string> excludes;   // -x patterns and -X file lines
    std::optional<std::string> startingFile; // -S
    std::string switchString;            // " -r -x 'o*'" -- see below; empty without options
};

// GNU's compare_files for two operands as given on the command line (no
// parent): files, directories, "-", a missing one (with -N/-P). Prints
// everything; returns the exit status 0/1/2.
int DiffOperands(BuiltinContext& context, const DiffSettings& settings,
                 const DiffTreeSettings& tree, const std::string& name0, const std::string& name1);
```

`DiffOperands(name0, name1)` (top level):
1. Stat both (`-` is standard input, never a directory). Missing -> with
   `-N`, or `--unidirectional-new-file` for name0 only, and the other one
   existing (or `-`): treat it as *nonexistent* (an empty file, header time
   the epoch: `FileDateTime{0, 0}`, printed through `FormatDateTime` in
   local time as every header time is); otherwise `diff: NAME: No such file
   or directory`, 2 (both reported).
2. Exactly one is a directory: `-` with a directory -> `diff: cannot compare
   '-' to a directory` (no Try), 2. Else replace the directory operand by
   `DIR/BASENAME` (BASENAME = the other operand's last component; join
   without doubling a `/`: `rb/` + `x` is `rb/x`) and stat it again (missing:
   `diff: rb/x: No such file or directory`, 2). Then compare the two files
   with no header line.
3. Both directories: compare the directories (below), even without `-r` (one
   level).
4. Otherwise `DiffTwoFiles(..., "")`, with a nonexistent side read as empty.

Comparing directories D0 and D1 (`diffDirs`, called with whether this is the
top level):
1. List both (`ReadDirectory`), drop `.`/`..`, and drop every name that
   matches any exclude pattern: `FnMatch(pattern, name, 0)` against the
   entry's own name (a nonexistent side has an empty listing).
2. Sort each by bytes (`strcmp`; the C locale's `strcoll`). At the top level
   only, with `-S FILE`, skip the names that sort before FILE in both lists.
3. Merge the two lists in order; for each name, with `P0 = D0 + "/" + name`
   and `P1 = D1 + "/" + name` (no doubled `/`):
   - in one list only, and neither `-N` nor (`--unidirectional-new-file` and
     it is missing from D0): `Only in D: NAME\n` (D the directory that has
     it, as joined), status 1;
   - otherwise compare the entry pair (a missing side is nonexistent):
     - both directories: without `-r`, `Common subdirectories: P0 and P1\n`
       (status unchanged); with `-r`, recurse;
     - one is a directory and the other is missing: with `-r` and `-N` (or
       `-P` with D0's side missing) recurse into it against an empty one;
       otherwise `Only in D: NAME\n`, 1;
     - a directory and a non-directory, or anything but two regular files
       (a device): `File P0 is a T0 while file P1 is a T1\n`, 1, where T is
       `regular file`, `regular empty file` (size 0), `directory` or
       `character special file`; labels replace the paths when given;
     - two files: `DiffTwoFiles(context, settings, P0, P1, header)` with
       `header = "diff" + switchString + " " + P0 + " " + P1` (each path as
       diff--diff-core escapes header names, GNU's `c_escape`; with
       `--label`, the label in place of the path, escaped the same way --
       GNU prints `diff -r -L 'x y' "x y" rb/s/y`). The header is printed
       only when the pair produces output (not with `-q`, not for `Binary
       files ... differ`, not when identical).
4. The status of a directory comparison is the maximum of its entries'.

**The switch string** (GNU's `option_list`): every command-line word that
is an option or an option's separate argument, in the order given, each
preceded by a space and written with `ShellEscapeQuoted(word)` (quoted only
when needed: `-x 'o*'`, `'--exclude=a b'`); operands and a `--` are left
out. Classify the words with the same rules `ParseBuiltinArgs` uses (a
cluster ending in an option taking an argument takes the next word; a long
option with a Required argument and no `=` takes the next word) -- write a
small `std::vector<std::string> DiffOptionWords(const std::vector<std::string>& args, const std::vector<BuiltinOption>& options)`
in `Diff.cpp` for it. Example: `diff ra -r -x 'o*' rb` -> `diff -r -x 'o*'
ra/x rb/x`.

### Changes to `src/components/BuiltinCommands/commands/diff/Diff.cpp`

- Give ids to `-r/--recursive`, `-N/--new-file`, `P`/`--unidirectional-new-file`,
  `-x/--exclude PAT`, `-X/--exclude-from FILE`, `-S/--starting-file FILE`,
  `--from-file FILE`, `--to-file FILE`, `--no-dereference` (accepted: no
  links); `--ignore-file-name-case`/`--no-ignore-file-name-case` stay not
  treated.
- `-X FILE`: read the file through `IO()` (`-` is standard input); each line
  (without its `'\n'`, empty lines skipped) is a pattern; unreadable ->
  `diff: FILE: No such file or directory` (no Try), 2.
- `-S` given twice with different values: `diff: conflicting -S option value
  'B'` (no Try), 2 (GNU's `specify_value`; the same rule for `--from-file`
  and `--to-file`: `conflicting --from-file option value 'B'`).
- `--from-file F` and `--to-file T` both: `diff: --from-file and --to-file
  both specified` (no Try), 2. With `--from-file F`: `DiffOperands(F, op)`
  for each operand in order (none: nothing, 0); with `--to-file T`:
  `DiffOperands(op, T)`; the status is the maximum. Otherwise the two-operand
  rule of diff--diff-core, then `DiffOperands(op0, op1)` instead of
  `DiffTwoFiles`.
- Remove diff--diff-core's interim `Is a directory` refusal from
  `DiffTwoFiles` (a directory never reaches it now).
- Bump the version to `1.1.0`.

### CMake

Add `commands/diff/DiffDirectories.cpp` to
`src/components/BuiltinCommands/CMakeLists.txt`.

## Tests

In `tests/unit/components/BuiltinCommands.unittests/DiffTest.cpp` (exists),
`TEST_F(BuiltinCommandsTest, DiffDir...)`. A helper builds the tree used by
most tests under `/t` (run in `/t`): `ra/x` = `1\n`, `rb/x` = `2\n`, `ra/s/y`
= `a\n`, `rb/s/y` = `b\n`, `ra/only` = `q\n`, `ra/onlydir/` (empty dir),
`rb/t/` (empty dir), `ra/kind` = `k\n`, `rb/kind/` (dir), `ra/bin` = `x\0`,
`rb/bin` = `y\0`. Expected outputs are GNU diff 3.10's (`LC_ALL=C`).

- `DiffDirOneLevel`: `diff ra rb` -> out
  `Binary files ra/bin and rb/bin differ\nFile ra/kind is a regular file while file rb/kind is a directory\nOnly in ra: only\nOnly in ra: onlydir\nCommon subdirectories: ra/s and rb/s\nOnly in rb: t\ndiff ra/x rb/x\n1c1\n< 1\n---\n> 2\n`, status 1.
- `DiffDirRecursive`: `diff -r ra rb` -> the same, but in place of the
  `Common subdirectories` line `diff -r ra/s/y rb/s/y\n1c1\n< a\n---\n> b\n`,
  and `diff -r ra/x rb/x` before the last hunk; status 1.
- `DiffDirBriefAndExclude`: `diff -rq -x 's*' ra rb` ->
  `Files ra/bin and rb/bin differ\nFile ra/kind is a regular file while file rb/kind is a directory\nOnly in ra: only\nOnly in ra: onlydir\nOnly in rb: t\nFiles ra/x and rb/x differ\n`;
  `-r -x '*' ra rb` -> nothing, 0; `-rq -X exl ra rb` with `exl` =
  `only\ns*\n` -> the lines above without `Only in ra: only`.
- `DiffDirSwitchString`: `diff ra -r -x 'o*' rb` -> header lines `diff -r -x
  'o*' ra/s/y rb/s/y` and `diff -r -x 'o*' ra/x rb/x`; `diff --recursive
  '--exclude=a b' ra rb` -> `diff --recursive '--exclude=a b' ra/x rb/x`.
- `DiffDirNewFile`: `diff -rN ra rb` -> after the `Binary` and `File ...`
  lines, `diff -rN ra/only rb/only\n1d0\n< q\n`, then the `s/y` and `x`
  diffs with `diff -rN` headers; no `Only in` lines; status 1. `diff -ruN`
  of a tree with a file only in `b` -> its hunk `@@ -0,0 +1 @@` under `---
  a/new\t` + the epoch formatted by `FormatDateTime("%Y-%m-%d %H:%M:%S.%N
  %z", {0,0}, false)`.
- `DiffDirNewFileTopLevel`: `diff -N nofile x` -> `0a1\n> 1\n`, 1; `diff
  --unidirectional-new-file x nofile` -> err `diff: nofile: No such file or
  directory\n`, 2; `diff -P nofile x` -> `0a1\n> 1\n`, 1.
- `DiffDirAndFile`: file `x` = `1\n` in `/t`: `diff x rb` -> `1c1\n< 1\n---\n> 2\n`,
  1 (no header line); `diff rb x` -> `1c1\n< 2\n---\n> 1\n`; `diff xf ra`
  (no `ra/xf`) -> err `diff: ra/xf: No such file or directory\n`, 2; `diff -
  ra` -> err `diff: cannot compare '-' to a directory\n`, 2; `diff -u x rb`
  -> header `+++ rb/x\t...`.
- `DiffDirStartingFile`: `diff -r -S s ra rb` -> only `diff -r -S s ra/s/y
  rb/s/y` (its hunk), `Only in rb: t`, `diff -r -S s ra/x rb/x` (its hunk).
- `DiffDirTypes`: `ta/e` empty file vs `tb/e` dir -> `File ta/e is a regular
  empty file while file tb/e is a directory\n`.
- `DiffFromToFile`: `diff --from-file=x ra/x rb/x` -> `1c1\n< 1\n---\n> 2\n`
  (x vs ra/x identical, then x vs rb/x), 1; `diff -q --to-file=rb ra/x x` ->
  `Files ra/x and rb/x differ\nFiles x and rb/x differ\n`, 1;
  `--from-file=a --to-file=b x` -> err `diff: --from-file and --to-file both
  specified\n`, 2.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Diff*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the `diff` row -- version
  1.1.0, `-r -N --unidirectional-new-file -x -X -S --from-file --to-file`
  treated, directories and a directory/file pair compared; exceptions: no
  links (so `--no-dereference` changes nothing), `--ignore-file-name-case`
  not treated.
- Root `CLAUDE.md`: the `diff` row mentions `-r`, `-N`, `-x`.

## Acceptance

- [ ] Directory comparison output, order and status match GNU (the tests
      above byte for byte); `-r`, `-N`, `--unidirectional-new-file`, `-x`,
      `-X`, `-S`, `--from-file`, `--to-file` behave as specified.
- [ ] The `diff [OPTIONS] A B` lines carry the option words as given,
      shell-quoted, and appear only before real output.
- [ ] Directory and file operands, `-` against a directory, missing files.
- [ ] Every path reached through `context.IO()`; stops on `TriggerStop()`.
- [ ] Version 1.1.0; CLAUDE.md rows updated; all unit tests pass.

## Out of scope

- Symbolic links and loop detection (Haisos has no links).
- `--ignore-file-name-case`; output formats left untreated by diff--diff-core.
- patch (diff--patch-core).
