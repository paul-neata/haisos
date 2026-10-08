# Task coreutils--chmod-paths: chmod, basename, dirname and realpath

- Rock: coreutils
- Depends on: none
- Size: ~800 changed lines in ~9 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add chmod, basename, dirname and realpath builtins

Split off coreutils--names-env (which came to ~1500 lines with these four):
the commands about names and modes, none of which runs a program.

## Goal

`chmod`, `basename`, `dirname` and `realpath` are builtin commands (placed
with `BUILTIN rootfs <name> /bin/<name>`) behaving as GNU coreutils 9.4's
(Ubuntu 24.04): the same options, messages byte for byte, exit codes.
`chmod` parses and validates its mode exactly as GNU does, octal and
symbolic, and requires the files to exist -- but changes nothing, since
Haisos has no permissions (a documented exception, consistent with `ls -l`
showing `rwxrwxrwx`). `basename`, `dirname` are pure string functions;
`realpath` resolves against the process's working directory, checking what
exists (there are no links, so it is lexical plus existence checks).

## Context

Read first: the root `CLAUDE.md` ("Security", "Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `src/components/Filesystem/CLAUDE.md`
(paths, `VirtualPath.h`).

What exists: `BuiltinCommand.h` (`BuiltinContext`, `BeginBuiltin`,
`ParseBuiltinArgs`, `ShellEscapeQuoted`), `commands/mkdir/Mkdir.cpp` (the
model), `FilesystemUtils.h` (`EntryTypeOf`), `IFileIO::Stat`,
`ReadDirectory`, `ResolvePath`, `GetCurrentDirectory`. Haisos files have no
mode: `ls -l` prints `rwxrwxrwx` for everything, i.e. mode 0777.

Rules that bite: all file access through `context.IO()` (`ICurrentProcess`
is the only door out); GNU's output byte for byte; every GNU option in
`Options()`; `--help` from `BuiltinHelpText` with exceptions in
`Help().notes`; registration in `CreateStandardBuiltinCommands()` (feeds the
`haisos --init` template); portable C++17.

Below, `q(x)` is `ShellEscapeQuoted(x, /*always=*/true)` and `qf(x)` is
`ShellEscapeQuoted(x)` (quoted only when needed: realpath's style).

## Changes

### `commands/chmod/Chmod.cpp` (new)

`CreateChmodCommand()`; `chmod`, `1.0.0`; summary `change file mode bits`;
usage `chmod [OPTION]... MODE[,MODE]... FILE...`, `chmod [OPTION]...
OCTAL-MODE FILE...`, `chmod [OPTION]... --reference=RFILE FILE...`.

Options (GNU 9.4, all treated): `-c, --changes`; `-f, --silent, --quiet`
(three spellings, one id: `-f`/`--silent` and a second row `--quiet`, both
with descriptions); `-v, --verbose`; `--no-preserve-root`;
`--preserve-root`; `--reference=RFILE` (Required); `-R, --recursive`.

Notes (documented exception): `Haisos has no permissions: the mode is parsed
and validated, the files must exist, and nothing changes. Every file's mode
is taken as 0777 (as ls -l shows rwxrwxrwx), and the umask as 0.`

**Mode words that look like options.** Before parsing, scan the arguments up
to a `--`: the first one that starts with `-`, is longer than 1, and whose
other characters are all in `rwxXstugoa,+-=01234567` is the MODE (`chmod -w
f`, `chmod -x,+r f`); take it out of the list given to `BeginBuiltin`.
(`R`, `c`, `f`, `v` are not in that set, so `-R -c -f -v` stay options.)

**Mode parsing**, against an old mode (always 0777) and a "directory" flag:
- Octal: only digits 0-7, value at most 07777 (`00755`, `000755` are fine;
  `77777`, `8`, `9z` are invalid). The new mode is the value.
- Symbolic: comma-separated clauses, none empty (`''` and `,u+x` invalid);
  each `[ugoa]*` then one or more actions `[-+=]` followed by either `[rwxXst]*`,
  or one of `u g o` (copy that class's bits of the current mode), or octal
  digits (`=644`, valid in 9.4). Bits: r 4, w 2, x 1 per class; `X` is x when
  the file is a directory or the current mode has any x bit (with 0777,
  always); `s` is setuid for `u` and setgid for `g`; `t` is the sticky bit
  (`01000`) for `o`/`a`/no who. No who means `a` (umask 0) -- for `s` that is
  `u` and `g`. `+` adds, `-` removes, `=` sets the classes' bits (and clears
  their special bits) to exactly the given ones. Clauses apply in order, each
  on the result of the previous.
- Invalid: `chmod: invalid mode: q(mode)` + Try, exit 1.

**Behaviour** (GNU 9.4):
- No operand: `chmod: missing operand` + Try, 1. Mode only: `chmod: missing
  operand after q(mode)` + Try, 1. With `--reference`, every operand is a file.
- `--reference=RFILE`: `Stat` must find it, else `chmod: failed to get
  attributes of q(RFILE): No such file or directory`, 1; its mode is 0777.
- Each file: `Stat`; missing -> `chmod: cannot access q(f): No such file or
  directory` (silent with `-f`), status 1. Compute the new mode from 0777.
  `-v`: `mode of q(f) changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)`, or
  when equal `mode of q(f) retained as 0777 (rwxrwxrwx)`; `-c`: only the
  `changed` lines. On stdout.
- `-R`: after a directory, its entries (skip `.`/`..`) sorted by name, paths
  `dir + "/" + name`. With `--preserve-root` and a directory resolving to
  `/`: `chmod: it is dangerous to operate recursively on '/'` and `chmod: use
  --no-preserve-root to override this failsafe`, status 1.
- Mode text: `%04o` of the mode's low 12 bits, then in parentheses nine
  characters `rwxrwxrwx` with `-` for a missing bit; the user x position is
  `s` (setuid and x) or `S` (setuid, no x), the group's likewise with setgid,
  the others' `t`/`T` with the sticky bit. E.g. `7750 (rwsr-s--T)`, `4755
  (rwsr-xr-x)`, `0000 (---------)`.

### `commands/basename/Basename.cpp` (new)

`CreateBasenameCommand()`; `basename`, `1.0.0`; summary `strip directory and
suffix from filenames`; usage `basename NAME [SUFFIX]`, `basename OPTION...
NAME...`. Options: `-a, --multiple`; `-s, --suffix=SUFFIX` (implies `-a`);
`-z, --zero`. All treated.
- No operand: `basename: missing operand` + Try, 1. Without `-a`/`-s`: one
  operand, or two (the second the suffix); a third: `basename: extra operand
  q(third)` + Try, 1.
- The base name: an empty string stays empty; a string of only `/` is `/`;
  otherwise trailing `/` stripped and the part after the last `/`. The suffix
  is removed when the name ends with it and is not equal to it (`.txt` with
  suffix `.txt` stays `.txt`).
- Each result followed by `\n`, or `\0` with `-z`.

### `commands/dirname/Dirname.cpp` (new)

`CreateDirnameCommand()`; `dirname`, `1.0.0`; summary `strip last component
from file name`; usage `dirname [OPTION] NAME...`. Option `-z, --zero`.
- No operand: `dirname: missing operand` + Try, 1. Each operand, one line
  (`\0` with `-z`).
- GNU's `dir_len`, byte for byte (no `//` root on Linux):
  1. `prefix` = 1 if the name starts with `/`, else 0.
  2. `last` = the start of the last component: skip leading `/`s; then
     scanning on, a component starts at each non-`/` byte that follows a `/`
     (trailing `/`s do not start one). For a name of only `/`s it is the end.
  3. `length = last`; while `length > prefix` and the byte at `length - 1`
     is `/`, decrement.
  4. `length == 0` -> `.`; otherwise the first `length` bytes.
  Results to match: `/a/b/c` -> `/a/b`; `a` -> `.`; `/` -> `/`; `//a` -> `/`;
  `a/b/` -> `a`; `a//b//` -> `a`; `''` -> `.`; `///a//b///` -> `///a`.

### `commands/realpath/Realpath.cpp` (new)

`CreateRealpathCommand()`; `realpath`, `1.0.0`; summary `print the resolved
path`; usage `realpath [OPTION]... FILE...`. Options (GNU 9.4, all treated):
`-e, --canonicalize-existing`; `-m, --canonicalize-missing`; `-L, --logical`
and `-P, --physical` (no links: the same); `-q, --quiet`; `--relative-to=DIR`
and `--relative-base=DIR` (Required); `-s, --strip, --no-symlinks` (no links:
the same; `--strip` a second row); `-z, --zero`. Notes: `There are no
symbolic links, so -L, -P and -s change nothing.`

Resolution of one path, in a mode (default, `-e`, `-m`; the last given wins):
1. Empty: error `No such file or directory` (any mode).
2. Absolute: the path itself; relative: `GetCurrentDirectory()` + `/` + it.
3. Walk the segments: empty and `.` skipped; `..` drops the last kept
   segment (at `/` stays). Other segments are appended, and outside `-m`
   checked with `Stat` on the path so far: for a segment that is **not the
   last**, missing -> `No such file or directory`, a non-directory -> `Not a
   directory`; for the last, with `-e` missing -> `No such file or
   directory`; in the default mode a missing last segment is fine.
4. A trailing `/` on the operand (outside `-m`): if what is there exists and
   is not a directory -> `Not a directory`.
5. Errors: `realpath: qf(path): <reason>` (`realpath: nothing: No such file
   or directory`, `realpath: '': No such file or directory`), none with `-q`;
   status 1, nothing printed for that operand.

`--relative-to`/`--relative-base`: each DIR resolved by the same rules and
mode (an error there is reported as for an operand, status 1, exit). If both
are given and the relative-to directory is not the base or under it, both are
dropped (paths print absolute). Then for each resolved path: with a base and
the path not the base or under it -> absolute; else with relative-to -> the
path relative to it (common leading segments removed, one `..` per remaining
segment of the directory, then the rest, joined by `/`; equal -> `.`); else
(base only, path under it) relative to the base.

Each result followed by `\n`, `\0` with `-z`.

GNU 9.4 results to match (working directory `/w`, a file `f`, directories
`d/e`, file `d/e/g`): `realpath f d/e/../e nothing` -> `/w/f`, `/w/d/e`,
`/w/nothing`, 0; `-e nothing` -> `realpath: nothing: No such file or
directory`, 1; `-m nothing/x/../y` -> `/w/nothing/y`; `f/x` -> `realpath:
f/x: Not a directory`; `nothing/x` -> `realpath: nothing/x: No such file or
directory`; `f/` -> `realpath: f/: Not a directory`; `--relative-to=d f
d/e/g` -> `../f`, `e/g`; `--relative-to=d/e d` -> `..`; `--relative-base=d f
d/e/g` -> `/w/f`, `e/g`; `--relative-to=/w --relative-base=d f d/e` ->
`/w/f`, `/w/d/e`; `--relative-to=nothing f` -> `../f`.

### Registration and build

`BuiltinCommandList.h`: declare and register `CreateBasenameCommand`,
`CreateChmodCommand`, `CreateDirnameCommand`, `CreateRealpathCommand`
(alphabetical). `CMakeLists.txt`: the four `commands/<name>/<Name>.cpp`.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/ChmodPathsTest.cpp`
(added to its `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, ...)` on
`RunCaptured`, exact stdout, stderr and status:

- `ChmodValidatesAndChangesNothing`: `chmod 755 /notes.txt` -> 0, nothing;
  `ls -l /notes.txt` still `rwxrwxrwx` (check `Stat` is unchanged instead if
  simpler).
- `ChmodVerboseAndChanges`: `chmod -v 755 /notes.txt` -> `mode of '/notes.txt'
  changed from 0777 (rwxrwxrwx) to 0755 (rwxr-xr-x)\n`; `-v 777` -> `mode of
  '/notes.txt' retained as 0777 (rwxrwxrwx)\n`; `-c 777` -> nothing; `-v
  a=rwx,g-w,o=` -> `... to 0750 (rwxr-x---)`; `-v ug+s,+t` -> `... to 7777
  (rwsrwsrwt)`; `-v =644` -> `0644 (rw-r--r--)`; `-v 0` -> `0000
  (---------)`; `-v 4755` -> `4755 (rwsr-xr-x)`; `-v u=g,go-w` -> `0755 (rwxr-xr-x)`;
  `-v o-x,+t` -> `1776 (rwxrwxrwT)`; `-v ug+s` -> `6777 (rwsrwsrwx)` (all
  verified with GNU chmod 9.4 from 0777 under umask 0).
- `ChmodOptionLikeMode`: `chmod -v -w /notes.txt` -> `... to 0555
  (r-xr-xr-x)`; `chmod -v -x,+r /notes.txt` -> `... to 0666 (rw-rw-rw-)`.
- `ChmodInvalidModes`: `9z`, `8`, `u+q`, `77777`, `''`, `,u+x` -> each
  `chmod: invalid mode: '<m>'\nTry 'chmod --help' for more information.\n`, 1
  (the empty one prints `''`).
- `ChmodOperandErrors`: `chmod` -> missing operand; `chmod 755` -> `chmod:
  missing operand after '755'\n` + Try; `chmod 755 /nothing` -> `chmod:
  cannot access '/nothing': No such file or directory\n`, 1; `-f` -> silent, 1;
  `--reference=/nothing /notes.txt` -> `chmod: failed to get attributes of
  '/nothing': No such file or directory\n`, 1; `--reference=/docs -v
  /notes.txt` -> `retained as 0777 (rwxrwxrwx)`.
- `ChmodRecursive`: `chmod -Rv 755 /docs` -> lines for `/docs`, `/docs/a.md`,
  `/docs/sub`, `/docs/sub/b.md` in that order, each `changed from 0777
  (rwxrwxrwx) to 0755 (rwxr-xr-x)`; `chmod -R --preserve-root 755 /` -> the
  two failsafe lines, 1.
- `BasenameCases`: `/a/b/c.txt .txt` -> `c\n`; `a/b/` -> `b\n`; `/` and `//`
  -> `/\n`; `''` -> `\n`; `-a x/y z/w` -> `y\nw\n`; `-s .c a.c b/b.c` ->
  `a\nb\n`; `a b c` -> `basename: extra operand 'c'\n` + Try, 1; `-z x/y` ->
  `y\0`; `.txt .txt` -> `.txt\n`; no operand -> missing operand, 1.
- `DirnameCases`: each pair from the list under Dirname, in one run with all
  operands (one line each); `-z a/b` -> `a\0`; no operand -> missing operand, 1.
- `RealpathCases`: build `/w/f`, `/w/d/e/g` and run from working directory
  `/w` every case listed under Realpath; plus `realpath -q /nothing/x /w/f`
  -> out `/w/f\n`, no err, 1; `-z /w/f` -> `/w/f\0`; `realpath ''` ->
  `realpath: '': No such file or directory\n`; `realpath` -> missing operand.

Update `ListsEveryBuiltinSortedWithAVersion` in `BuiltinCommandsTest.cpp`
(add `"basename"`, `"chmod"`, `"dirname"`, `"realpath"` in sorted position).
None of the four has an untreated option.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Chmod*:BuiltinCommandsTest.Basename*:BuiltinCommandsTest.Dirname*:BuiltinCommandsTest.Realpath*'
bash ./scripts/test_linux.sh L U
```

Any further expected output: verify against GNU in the container (`touch f; chmod 777 f;
chmod -v <mode> f` -- run `chmod 777 f` first so the old mode is 0777, and
`umask 0` so symbolic modes without a who match Haisos's umask 0).

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list; rows for
  `basename`, `chmod`, `dirname`, `realpath` (1.0.0) -- chmod's exception
  (validated, nothing changes, 0777, umask 0), realpath's (no links).
- Root `CLAUDE.md`: rows for the four; the command lists.

## Acceptance

- [ ] chmod accepts exactly the modes GNU accepts and rejects the rest with GNU's message; nothing is ever changed.
- [ ] dirname follows the `dir_len` steps; every listed result matches.
- [ ] realpath resolves through `context.IO()` from the working directory; every listed result matches.
- [ ] Every GNU option of the four in `Options()`; generic builtin tests and the `--init` template test pass; all unit tests green.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- env, which, sleep, true, false and running programs (coreutils--names-env).
- Real permissions, `chown`/`chgrp` (develop out of scope).
