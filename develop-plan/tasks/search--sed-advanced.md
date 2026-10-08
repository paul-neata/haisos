# Task search--sed-advanced: sed's remaining commands, -i in place, -z, -u, -l

- Rock: search
- Depends on: search--sed-core (`commands/sed/`), base--fs-rename-times (`IFileIO::Rename`)
- Size: ~800 changed lines in ~6 files
- Plan checked against: develop @ ccb9dbe
- PR title: Complete sed: hold space, branches, text and file commands, -i

## Goal

`sed` has every command of GNU sed 4.9 but `e`: `N D P h H g G x`, `b t
T`, `a i c` (GNU one-liners and the classic `\`-newline form), `r R w W`
(with `/dev/stdout`, `/dev/stderr`, and `r /dev/stdin`), `s///w FILE`, `y`,
`l [N]`, `z`, `F`, `v [VERSION]`; and the options `-i[SUFFIX]` (in place,
through a temporary file renamed over the original), `--follow-symlinks`,
`-z`, `-u`, `-l N`, `--sandbox`. `sed -i -E 's/(Create)\(\)/\1Instance()/g'
src/a.cpp` (acceptance scenario 2) edits the file as GNU sed does, and
`sed ':a;N;$!ba;s/\n/,/g'` joins lines.

## Context

Read first: the plan `develop-plan/tasks/search--sed-core.md` and the code
it produced in `src/components/BuiltinCommands/commands/sed/`
(`SedScript.h` -- `Command`, `Script`; `SedParser.cpp` -- `ParseScript`, the
error-location rules; `SedExecutor.cpp` -- `RunScript`, `SedInput` with its
one-line lookahead and missing-newline rule), root `CLAUDE.md` ("Security",
"Builtin Commands"), `src/components/BuiltinCommands/CLAUDE.md`.

What earlier tasks provide, as if on develop:
- `IFileIO::Rename(oldPath, newPath)` (base--fs-rename-times): as rename(),
  replaces an existing newPath; 0, `kFileSystemError`, or
  `kFileSystemCrossDevice` across mounts (cannot happen here: the
  temporary file is created in the same directory).
- `BuiltinText.h` (coreutils--sort): `WriteFully(descriptor, bytes)`,
  `OpenInputOperand`, `BuiltinLineReader`.

Rules that bite: files only through `context.IO()` (no `IFileSystem`);
nothing in Haisos creates a link (no `--follow-symlinks` work is needed: no
links are visible); GNU's output, messages and exit statuses byte for byte
(`LC_ALL=C sed` 4.9 in the container); option table and `--help` as before
(the options this task treats get ids and descriptions, which the generic
help test checks); portable C++17.

## Changes

### `commands/sed/SedParser.cpp`

New commands (GNU's address rules: `a i c r R w W l F = z y` and the rest
take two addresses in GNU 4.9; `}` and `:` none):
- `N D P h H g G x z F`: no argument.
- `b`, `t`, `T [LABEL]`: label to `;` or newline (whitespace before it
  skipped); none = to the end of the script. Resolve labels after the whole
  script is parsed: unknown -> ``sed: can't find label for jump to `foo'``,
  exit **4**.
- `a`, `i`, `c` text: after the command, whitespace skipped; then
  - `\` followed by a newline: the classic form -- the text is the
    following lines, each ending in `\` continuing onto the next;
  - `\` followed by other text: one-liner keeping that text's leading
    whitespace (`2i\  hello` -> `  hello`);
  - otherwise the one-liner GNU form: the rest of the line (`2i hello`).
  In the text, `\` followed by a character is that character, `\\` a
  backslash; it ends at an unescaped newline (`;` is part of it). Nothing at
  all after the command: ``expected \ after `a', `c' or `i'`` (char of the
  command). A `\` at the very end of a piece continues into the next `-e`
  (`-e 'a\' -e 'foo'` appends `foo`). `a\` alone at the end of the script
  is an empty text that adds nothing (GNU: `sed '2a\'` prints the input
  unchanged -- verify).
- `r R w W FILE`: the file name is the rest of the line (to newline, not
  `;`). `w`/`W` open their file at parse time (created/truncated), one
  descriptor per distinct name shared by every `w`, `W` and `s///w` naming
  it; `/dev/stdout` and `/dev/stderr` are sed's own streams (whatever the
  filesystem holds there). Unopenable: `sed: couldn't open file
  /nonexist/x: No such file or directory`, exit 4.
- `s` flag `w FILE` (the rest of the line), the same registry.
- `y/src/dst/`: any delimiter; in both strings `\\` is a backslash, `\n`
  newline, `\delim` the delimiter, other escapes as in s; lengths must
  match: ``strings for `y' command are different lengths``; unterminated
  ``unterminated `y' command`` (char = the end).
- `l [N]`: optional decimal line length (`l 0` no wrapping).
- `v [VERSION]`: nothing at run time; a VERSION above 4.9 -> ``expected
  newer version of sed`` (at the char after it).
- `e [COMMAND]`: parsed (rest of line), `context.NotTreated("e")`, no
  effect at run time (out of the develop's scope); the `s///e` flag stays
  as sed-core left it.
- `--sandbox`: `e`, `r`, `R`, `w`, `W` and `s///w` are refused at parse:
  `sed: -e expression #1, char 1: e/r/w commands disabled in sandbox mode`,
  exit 1.

### `commands/sed/SedExecutor.cpp`

State added: the hold space (initially empty), the "substituted since the
last input line or the last `t`/`T`" flag, an append queue (text, `r` file
contents, `R` lines) flushed at the end of each cycle and before `n`/`N`
read their line, and per-`R`-file read positions (an open descriptor and a
`BuiltinLineReader` kept for the run; at its end, nothing more is queued).
The record delimiter `d` is `'\n'`, or `'\0'` with `-z`.

- `N`: if there is a next line: flush the append queue, append `d` + the
  line to the pattern space (GNU joins with the record delimiter: with
  `-z`, `sed -z 'N;l;d'` on `a\0b\0` prints `a\000b$`), line number + 1;
  if not: print the pattern space (unless `-n`) and end without running the
  rest (GNU's behaviour without POSIXLY_CORRECT: `sed 'N;N;N'` on three
  lines prints all three).
- `D`: no newline in the pattern space -> as `d`; else delete through the
  first newline and start the next cycle **without** reading a line.
- `P`: print through the first newline (and a newline).
- `h H g G x`: copy / append (`\n` + ...) / copy back / append back / swap;
  `H` and `G` join with the record delimiter (`sed -z G` on `a\0b\0` gives
  `a\0\0b\0\0`).
- `b`: jump; `t`: jump if the flag is set, clearing it; `T`: jump if it is
  not set (clearing it otherwise -- verify with `sed 's/o/0/;T;s/$/!/'`:
  `0ne!`, `tw0!`, `three`).
- `a`: queue the text + `\n`. `i`: print the text + `\n` now. `c`: delete
  the pattern space and start the next cycle; print the text + `\n` when
  there is no range, or at the range's last line (with `!`, for every
  line); GNU: `sed '2,3c X'` on three lines prints `one`, `X`.
- `r FILE`: queue the whole file (missing: nothing, silently); `r
  /dev/stdin` reads the rest of sed's stdin. `R FILE`: queue its next line.
- `w FILE`: write the pattern space + `\n`; `W FILE`: its first line + `\n`.
  Writes to a file go through `WriteFully`; to `/dev/stdout` through the
  same output as the pattern space (so they interleave in order).
- `s///w FILE`: after a successful substitution, write the pattern space.
- `y`: map bytes.
- `l [N]` (N from the command, else `-l`, else 70): the pattern space with
  `\\`, `\a \b \f \n \r \t \v` escaped, other bytes below 32 or above 126 as
  `\ooo`, then `$`; lines are cut so each output line has at most N-1
  characters followed by `\` (an escape is never split); `l 0` (or `-l 0`)
  never cuts; `l 1` is GNU's oddity (verify: `echo abc | sed -n 'l 1'`
  prints `\`, `a\`, `b\`, `c$`). Verified: 80 `a`s with the default give 69
  `a`s and `\`, then 11 `a`s and `$`; `sed -l 4 -n l` on `abcdefghij` gives
  `abc\`, `def\`, `ghi\`, `j$`.
- `z`: empty the pattern space. `F`: the input's name (`-` for stdin) and a
  newline.
- In-place mode: every output command (`p = l i a c r R F`, the autoprint)
  writes to the file being built; `w /dev/stdout` still goes to the real
  stdout.
- `-u`: `context.Flush()` after each line's output (and after every `w`).

### `commands/sed/Sed.cpp`

The options sed-core listed as `kBuiltinNotTreated` become treated, with
descriptions: `-i, --in-place[=SUFFIX]`, `-l, --line-length=N`, `-z,
--null-data` / `--zero-terminated`, `-u, --unbuffered`, `--follow-symlinks`
(accepted: there are no links -- documented), `--sandbox`. `--debug` and
`--posix` stay not treated. `-l` is read as GNU reads it, with
`atoi` (`-l x` is 0: no wrapping, no error -- verified). Version `1.1.0`.

**`-i[SUFFIX]`** (implies `-s`):
- No input files: `sed: no input files`, exit **4**.
- Per input file (in order): `Stat` it; missing -> `sed: can't read nofile:
  No such file or directory`, status 2, next; not a regular file (a
  directory, a device) -> `sed: couldn't edit dd: not a regular file`, exit
  4 at once (verified).
- Create the temporary file in the same directory: `<dir>/sed` + 6 random
  characters from `[A-Za-z0-9]` (as GNU's `sedXXXXXX`), retrying while
  `Stat` finds one; opened with `kFileOpenWriteCreateTruncate`. Run the
  script with that file as the output; flush and release it.
- With a SUFFIX: the backup name is the suffix appended to the file's base
  name, or, when the suffix holds `*`, the suffix with every `*` replaced
  by the base name (`--in-place='old_*'` makes `old_f`); a suffix holding
  `/` names a path relative to the working directory (`-i'bak/*.o'` on `g`
  makes `bak/g.o`, verified). `Rename(file, backup)`, then
  `Rename(temporary, file)`; without a suffix just the second. A failed
  Rename: `sed: cannot rename g: No such file or directory` (the file as
  given; GNU 4.9's text for a missing backup directory, verified), remove
  the temporary file, exit 4.
- `q`/`Q` in place: the rest of the file is not copied -- the file is cut
  there (`sed -i 2q f` on `1 2 3` leaves `1 2`, verified).
- `-n -i p f`: the file keeps only what was printed (verified: `sed -i -n
  '$p' f g` leaves each with its last line).

Note GNU's surprising parse of `sed -i in.txt`: with no `-e`, `in.txt` is
the script (the command `i` with text `n.txt`), so the message is `sed: no
input files`, exit 4 -- the same rule as sed-core's.

### Help

Notes updated: `-i` writes `sedXXXXXX` in the file's directory and renames
it over the file; `--follow-symlinks` has nothing to follow; `e` and
`s///e` not treated; `--posix`, `--debug` not treated.

## Tests

Extend `tests/unit/components/BuiltinCommands.unittests/SedTest.cpp` (or a
new `SedAdvancedTest.cpp`, listed in the CMakeLists). Each expected output
verified with `LC_ALL=C sed` in the container.

- `SedNextAppendAndDelete`: `$!N;P;D` on three lines (unchanged); `N;N;N` (all three printed); `:a;N;$!ba;s/\n/,/g` -> `one,two,three`; `N;s/^b/X/M` on `a\nb` -> `a\nX`; `N;s/a$/X/M` -> `X\nb`.
- `SedHoldSpace`: `-n 'x;p'` -> empty line, `one`, `two`; `G` on `a\nb` -> `a\n\nb\n\n`; `1!G;h;$!d` (tac) -> reversed; `H;$!d;x` style.
- `SedBranches`: `b end; s/o/0/; :end` (unchanged); `s/o/0/;t;s/$/!/` -> `0ne`, `tw0`, `three!`; `s/o/0/;T;s/$/!/` -> `0ne!`, `tw0!`, `three`; `b foo` -> the label error, exit 4.
- `SedTextCommands`: `2i\  hello`, `2i hello`, `2a\` + newline + `X\` + newline + `Y`, `2c\` forms, `2,3c X` -> `one\nX\n`; `a foo` on input without a final newline -> `line\nfoo\n`; `-e 'a\' -e 'foo'`; `a` alone -> ``expected \ after `a', `c' or `i'``, 1.
- `SedFiles`: `R in.txt` interleaves; `2r nosuch` silent; `r /dev/stdin` with stdin `x\n` and a file operand; `w out` then read `out` back; `W /dev/stdout` duplicates the first line; `s/e/E/w /dev/stdout` -> `onE\nonE\ntwo\nthrEe\nthrEe\n`; `w /nonexist/x` -> exit 4 message; `--sandbox 'w x'` -> exit 1 message.
- `SedTransliterate`: `y/abc/xyz/`; `y/o\n/0_/`; `y/abc/\n\t\\/` -> bytes `\n\t\\`; `y/abc/de/` and `y/abc/xyz` errors.
- `SedList`: `l` on `a\tb\001c` -> `a\tb\001c$` then the line; `-n 'l 5'` on `abcdefghij` -> `abcd\`, `efgh\`, `ij$`; `-l 4 -n l`; default wrapping of 80 `a`s; `-l 0`; `l 1`.
- `SedZapFileVersion`: `z;s/^$/E/` -> three `E`; `F` on a file and on stdin (`-`); `v`; `v 9.0` -> ``expected newer version of sed``, 1; `e echo` -> reported not treated, no effect.
- `SedNullData`: `-z 's/\n/,/g'` on `one\ntwo\nthree\n` -> `one,two,three,` with no NUL (the input held none, so its one record had no delimiter: the missing-newline rule); `-z 'N;l;d'`; `-z G`.
- `SedInPlace`: `-i.bak 's/a/A/' f` -> f changed, f.bak the original; `--in-place='old_*'`; `-i s/x/y/ /docs` -> `sed: couldn't edit /docs: not a regular file`, 4; `-i -n '$p' f g` (each its own last line, `-s` implied); `-i '1F;1='` writes the name and number into the file; `-i 'w /dev/stdout'` prints to stdout and leaves the file as is; `sed -i` with no files -> `sed: no input files`, 4; `-i 2q f` on `1\n2\n3\n` leaves `1\n2\n`; `-i'nodir/*' p f` -> `sed: cannot rename f: No such file or directory`, 4, f unchanged; no `sed??????` file is left in the directory after any of these (list it).
- `SedUnbuffered`: `-u p` with stdout a pipe read while sed still waits for more input: the first line arrives before stdin is closed (or simply: output is complete and identical with and without `-u`).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Sed*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `sed` row to 1.1.0 -- every
  command but `e`; `-i` through a temporary file and `Rename`;
  `--follow-symlinks` a no-op (no links); `e`, `s///e`, `--posix`,
  `--debug` not treated.
- Root `CLAUDE.md`: the `sed` row.

## Acceptance

- [ ] Every command and option of GNU sed 4.9 is parsed; all but `e`/`--posix`/`--debug` act as GNU's.
- [ ] `-i` never leaves a temporary file behind and never writes the original until the new content is complete; it uses only `context.IO()` (`OpenFile`, `Rename`, `RemoveFile`).
- [ ] Every message, output and exit status in the tests matches `LC_ALL=C sed` 4.9.
- [ ] Generic builtin tests green (the newly treated options have descriptions).
- [ ] Both CLAUDE.md files updated.

## Out of scope

- The `e` command and `s///e` (the develop's out of scope); `--posix`
  strictness; `--debug`.
