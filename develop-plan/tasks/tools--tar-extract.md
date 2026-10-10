# Task tools--tar-extract: tar extract, compare, append, update, delete

- Rock: tools
- Depends on: tools--tar-create-list (the `tar` builtin, `commands/tar/`: `TarSettings`, `TarReader`, `TarMemberSelector`, `TarMember`, `EncodeMemberHeader`, the create walk), base--fs-rename-times (`IFileIO::SetTimes`, `IFileIO::Rename`)
- Size: ~900 changed lines in ~8 files
- Plan checked against: develop @ ccb9dbe
- PR title: tar: extract, compare, append, update and delete

## Goal

`tar` gains every remaining operation mode of GNU tar 1.35, with GNU's
messages and exit statuses:

- `-x` (`--extract`, `--get`): creates directories and regular files under
  the current directory (or `-C DIR`), creating missing parents, restoring
  modification times (directories' after their contents) unless `-m`;
  `-k`/`--keep-old-files`, `--skip-old-files`, `--keep-newer-files`,
  `--overwrite`, `-U`/`--unlink-first`, `-O`/`--to-stdout`,
  `--strip-components=N`, member operands with or without `--wildcards`,
  `--exclude`; `..` and absolute names made safe as GNU does. Symbolic and
  hard link members (and devices, FIFOs) are refused with GNU's own
  "Operation not permitted" wording: no link is ever created.
- `-d` (`--diff`, `--compare`): `NAME: Mod time differs`, `Size differs`,
  `Contents differ`, ... exit 1 when something differs.
- `-r` (`--append`), `-u` (`--update`), `-A` (`--catenate`), `--delete`:
  the archive rewritten with the members added or removed.

So `tar -xf s.tar`, `tar -xzf` (failing as without gzip), `tar -C out -xvf
s.tar --strip-components=1 'src/*' --wildcards`, `tar -xOf s.tar
src/a.txt | head`, `tar -df s.tar` and `tar -rf s.tar new.txt` behave as
on Linux.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security" -- in
particular: no builtin may create a link), `src/components/BuiltinCommands/CLAUDE.md`
(the `tar` row and notes added by tools--tar-create-list), the
tools--tar-create-list plan (`develop-plan/tasks/tools--tar-create-list.md`)
and the code it left in `src/components/BuiltinCommands/commands/tar/`,
`interfaces/IFileIO.h` (`SetTimes`, `Rename`, `kFileSystemError`).

What earlier tasks provide, as if already on develop:

- tools--tar-create-list, `commands/tar/`: `TarSettings` (mode enum with
  `Extract, Diff, Append, Update, Catenate, Delete` already parsed, the
  name list with `-C` directories, exclusion matching, `absoluteNames`,
  `verbose`, `utc`, ...), `ParseTarArgs`, `TarMember`,
  `EncodeMemberHeader`, `DecodeHeader`, `TarModeString`, `TarReader`
  (`NextMember`, `ReadData`, `SkipData`, `KeepRawBytes`/`RawBytes`,
  `HadError`), `TarMemberSelector` (`Selects`, `ReportUnmatched`), the
  `safer_name_suffix` stripping with its "Removing leading" warning, the
  listing line (`-tv`), archive opening for reading and writing with GNU's
  messages, and the compression failures. `Tar.cpp`'s `Run` prints `tar:
  <mode> is not available yet` for the modes this task implements.
- base--fs-rename-times: `int IFileIO::SetTimes(const std::string& path,
  const std::optional<FileDateTime>& accessTime, const
  std::optional<FileDateTime>& modificationTime)` and `int
  IFileIO::Rename(const std::string& oldPath, const std::string& newPath)`
  (0, `kFileSystemError`, or `kFileSystemCrossDevice`).

GNU tar 1.35 is in the task container: verify any wording you doubt by
running it there.

## Changes

### Rules that bite (root CLAUDE.md, restated)

- `ICurrentProcess` is the only door out: files only through
  `context.IO()` (`OpenFile`, `CreateDirectory`, `RemoveFile`,
  `RemoveDirectory`, `Stat`, `ReadDirectory`, `SetTimes`, `Rename`);
  stdin/stdout through the descriptor table and `context.Out`. Never
  `ChangeDirectory`: paths are composed with the `-C` directory.
- **No link of any kind is created**, whatever the archive says.
- GNU's messages and exit codes byte for byte; options moved from
  `kBuiltinNotTreated` to treated keep their place in `Options()`;
  bump `tar`'s `Version()` to `1.1.0` (and the version in the CLAUDE.md
  row); `--help` is still `BuiltinHelpText`.
- Stop promptly: `StopRequested()` between members and data chunks,
  `kIOInterrupted` ends the run; output through `context.Out` (141 on a
  broken pipe).

### `commands/tar/TarOptions.cpp` / `.h`

Give ids (and `TarSettings` fields) to: `-O --to-stdout`,
`-k --keep-old-files`, `--skip-old-files`, `--keep-newer-files`,
`--overwrite`, `--overwrite-dir` (the default: a no-op), `-U
--unlink-first` (the same as the default here: an existing file is
replaced), `--strip-components=NUMBER`, `-m --touch`,
`-i --ignore-zeros` (the reader goes on past zero blocks to end of file).
Still `kBuiltinNotTreated`: `-p --preserve-permissions --same-permissions`,
`--no-same-permissions`, `--same-owner`, `--no-same-owner`,
`--recursive-unlink`, `--no-overwrite-dir`, `--keep-directory-symlink`,
`--to-command`, `--occurrence`, `--backup`, `--suffix`, `--delay-directory-restore`,
`-W --verify`, `--remove-files`. (`tar -xpf` thus prints `Parameter -p is
not treated by HaisosOS tar v. 1.1.0` and extracts.)

Validation (GNU's, verified): `-r`, `-u` or `-A` with the archive `-` ->
`tar: Options '-Aru' are incompatible with '-f -'` + `Try 'tar --help' or
'tar --usage' for more information.`, exit 2. `--strip-components=X` not a
non-negative number -> `tar: X: Invalid number of elements` + the Try line,
exit 2 (verified for `x` and `-1`).

### New `commands/tar/TarExtract.cpp` -- `-x`

`int TarExtract(BuiltinContext& context, TarSettings& settings);`

The base directory: the `-C` in force for the first operand, else the last
`-C` given, else the current directory (a missing one: `tar: DIR: Cannot
open: No such file or directory` + `tar: Error is not recoverable: exiting
now`, exit 2). Open the archive as `-t` does (same refusals, compression
failures). For each member from `TarReader` (long names joined):

1. Not selected by `TarMemberSelector` (operands; `--wildcards` etc.), or
   excluded by `--exclude`/`-X` (the create-side matching, on the member
   name) -> skip its data.
2. Name safety, unless `-P`: print the "Removing leading `PREFIX' from
   member names" warning (once per prefix, as `-t` does). If the original
   name has a `..` component anywhere -> `tar: NAME: Member name contains
   '..'` (NAME as in the archive), skip, failure status (verified: GNU
   refuses `a/../../x`, `../y`, `ok/../z` this way, each after its
   "Removing leading" line). Otherwise the stripped name is used.
3. `--strip-components=N`: drop the first N `/`-separated components; a
   member left with nothing is skipped silently. (`-t` is unaffected.)
4. `-v`: the name (after stripping) and `\n` before acting -- to standard
   output, or to standard error with `-O` (verified); `-vv`: the `-tv` line.
5. By typeflag:
   - `5` directory: create it and every missing parent
     (`CreateDirectory`, mode as mkdir uses). An existing directory is fine.
     An existing non-directory there is removed (`RemoveFile`) and the
     directory created (verified: GNU replaces a file with the directory).
     Remember `(path, mtime)` for step 6.
   - `0`, `\0`, `7` regular file: create missing parent directories
     silently (their times are not touched). Then, if something exists at
     the path:
     - `-k`: `tar: NAME: Cannot open: File exists`, failure status, skip;
     - `--skip-old-files`: skip silently;
     - `--keep-newer-files`, existing mtime >= the member's: `tar: Current
       'NAME' is newer or same age` (ASCII quotes, the C locale's), status
       unchanged, skip;
     - an empty directory: removed, then the file written (verified);
     - a non-empty directory: `tar: NAME: Cannot open: File exists`,
       failure status, skip (verified);
     - a file: replaced (opened with truncation).
     Write the data in 64 KiB chunks (`OpenFile(path,
     kFileOpenWriteCreateTruncate, kFileCreateMode)`; null -> `tar: NAME:
     Cannot open: Permission denied` -- or `No such file or directory` /
     `Not a directory` when the parent is missing / not a directory --
     failure status; a write failure -> `tar: NAME: Cannot write:
     Input/output error`). Then, unless `-m`, `SetTimes(path, nullopt,
     FileDateTime{mtime, 0})`; a failure -> `tar: NAME: Cannot utime:
     Operation not permitted`, failure status.
     With `-O`: the data goes to `context.Out` instead and nothing is
     created; non-file members are skipped silently.
   - `2` symbolic link: `tar: NAME: Cannot create symlink to 'TARGET':
     Operation not permitted`, failure status. `1` hard link: `tar: NAME:
     Cannot hard link to 'TARGET': Operation not permitted`, failure
     status. `3`/`4` devices: `tar: NAME: Cannot mknod: Operation not
     permitted`; `6` FIFO: `tar: NAME: Cannot mkfifo: Operation not
     permitted`; each a failure. (GNU's own wording for an `EPERM` from
     symlink/link/mknod/mkfifo; documented: Haisos never creates links or
     special files.)
   - `x`/`g` (pax extended headers): skipped, data and all; the member that
     follows is taken from its ustar fields (documented: pax records not
     treated).
   - any other typeflag: `tar: NAME: Unknown file type 'C', extracted as
     normal file` and extracted as a file.
6. After the archive: unless `-m`, set each remembered directory's
   modification time, deepest (last extracted) first, with `SetTimes`.
7. Unmatched operands: `TarMemberSelector::ReportUnmatched` (`tar: X: Not
   found in archive`). Exit 2 with `tar: Exiting with failure status due to
   previous errors` after any failure (reader errors included), else 0.

The archive path itself is never extracted over: a member resolving to the
archive being read -> skip it (Haisos choice, no message).

### New `commands/tar/TarCompare.cpp` -- `-d`

`int TarCompare(BuiltinContext& context, TarSettings& settings);`
Each selected member (safety and strip as for `-x`, no warnings repeated
beyond `-x`'s), `-v` prints its name first (stdout). Then, against the
file at the composed path (GNU 1.35's wording, verified for the first
three; differences go to **standard output** with no `tar:` prefix,
warnings to stderr):

- nothing there -> stderr `tar: NAME: Warning: Cannot stat: No such file
  or directory`, a difference;
- file member: not a file there -> `NAME: File type differs`; else
  `NAME: Mod time differs` when the seconds differ, then `NAME: Size
  differs` when sizes differ; only when the sizes match, the contents are
  compared chunk by chunk -> `NAME: Contents differ`;
- directory member: not a directory there -> `NAME: File type differs`;
  nothing else is compared;
- symbolic link member -> stderr `tar: NAME: Warning: Cannot readlink:
  Invalid argument` when something is there (Haisos has no links), the
  Cannot stat warning when nothing is; a difference;
- hard link member -> `NAME: Not linked to TARGET`; a difference;
- devices/FIFOs -> `NAME: File type differs`.

Mode, uid/gid and owner names are never compared (no permissions or
owners: documented). Exit: 2 (with the `Exiting with failure status` line)
when an error occurred (reader errors, unmatched operands), else 1 when
something differed, else 0 -- and GNU prints no tail line for status 1
(verified).

### New `commands/tar/TarModify.cpp` -- `-r`, `-u`, `-A`, `--delete`

`int TarModify(BuiltinContext& context, TarSettings& settings);`

The archive is rewritten whole:

1. Read the existing archive with `TarReader` and `KeepRawBytes(true)`,
   collecting each member's raw bytes (headers, long-name entries, padded
   data) up to the end-of-archive marker (the zero blocks and the record
   padding are dropped). `-r`/`-u` on an archive that does not exist start
   from an empty one (GNU creates it: verified); `-A`/`--delete` on a
   missing archive -> the usual `Cannot open` + not recoverable, exit 2.
2. Mode:
   - `-r`: append the operands' members exactly as `-c` would write them
     (refactor `TarCreate.cpp` so its walk writes members to a sink --
     `std::function<bool(const std::string&)>` or a small `TarWriter`
     class -- usable for the descriptor, `context.Out`, or a string;
     verbose, excludes, `-C`, `-T`, long names and every message as for
     `-c`).
   - `-u`: as `-r`, but a file or directory is appended only when its
     mtime is newer than that of the last member of the same name already
     in the archive (or when no member has that name). Verified: with
     nothing newer, `-uvf` prints nothing.
   - `-A`: each operand is an archive; its members' raw bytes (up to its
     end marker) are appended. A missing one -> `tar: X: Cannot open: No
     such file or directory`, failure status, go on (verified).
   - `--delete`: members selected by the operands are dropped (no operand:
     nothing is); unmatched operands reported as `Not found in archive`,
     exit 2 (verified). With the archive `-`, read from standard input and
     written to standard output (verified to work in GNU).
3. Write: the kept bytes + new bytes + two zero blocks, padded to the
   record size. For a named archive, write to a sibling temporary file
   `NAME.haisos-tmp-<pid>` and `IO().Rename` it over the archive (so a
   failure never leaves half an archive; on failure remove the temporary
   and report `tar: NAME: Cannot write: Input/output error` + not
   recoverable, exit 2). The whole archive is held in memory (documented).
4. Exit status as for `-c`.

### `commands/tar/Tar.cpp`

Dispatch `Extract` -> `TarExtract`, `Diff` -> `TarCompare`, `Append`,
`Update`, `Catenate`, `Delete` -> `TarModify`; remove the "not available
yet" branch. Version `1.1.0`. Help notes: add that link and special-file
members are refused (no links are created), modes and owners are neither
restored nor compared, `-r/-u/-A/--delete` rewrite the archive in memory.

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `commands/tar/TarExtract.cpp`, `TarCompare.cpp`, `TarModify.cpp`.

## Tests

Extend `tests/unit/components/BuiltinCommands.unittests/TarTest.cpp`
(fixture `BuiltinCommandsTest`, `RunCaptured`; `/docs/a.md` `alpha`,
`/docs/sub/b.md` `bravo!`). Build archives with this builtin's own `-c`
(`--mtime=@1704164645`), or with the test file's raw-header helper from
tools--tar-create-list for link members and `..` names. Check files with
`ReadWholeFile(*root, ...)` and `root->Stat`.

- `TarExtractRoundTrip`: `tar -cf /d.tar docs`, then `mkdir /out` and
  `tar -xvf /d.tar` with cwd `/out` -> out `docs/\ndocs/a.md\ndocs/sub/\ndocs/sub/b.md\n`,
  status 0; `/out/docs/sub/b.md` is `bravo!`; every file's and directory's
  modification time is 1704164645 (directories too: set after their
  contents); `-m` leaves them recent instead.
- `TarExtractDirectoryOptionAndStrip`: `tar -C /out -xf /d.tar
  --strip-components=1` -> `/out/a.md`, `/out/sub/b.md`, no `/out/docs`;
  `tar -C /missing -xf /d.tar` -> `tar: /missing: Cannot open: No such file
  or directory\ntar: Error is not recoverable: exiting now\n`, 2.
- `TarExtractExistingFiles`: after one extraction, `-xkf` -> err `tar:
  docs/a.md: Cannot open: File exists\ntar: docs/sub/b.md: Cannot open:
  File exists\ntar: Exiting with failure status due to previous errors\n`,
  2; `--skip-old-files` -> silent, 0; a changed `docs/a.md` is restored by
  plain `-xf`; `--keep-newer-files` with a newer `docs/a.md` -> err `tar:
  Current 'docs/a.md' is newer or same age\n`, 0, file kept; a non-empty
  directory where a file member goes -> `Cannot open: File exists`, 2; an
  empty one -> replaced; a file where a directory member goes -> replaced.
- `TarExtractToStdout`: `-xOf /d.tar docs/a.md` -> out `alpha`, nothing
  created; `-xvOf` -> the name on stderr, `alpha` on stdout.
- `TarExtractMembersAndWildcards`: `-xf /d.tar docs/sub` -> only
  `docs/sub/b.md` (and its directory); `--wildcards -xf /d.tar '*/a.md'`
  -> only `docs/a.md`; `-xf /d.tar nope` -> `tar: nope: Not found in
  archive\ntar: Exiting with failure status due to previous errors\n`, 2;
  `--exclude=sub` -> no `docs/sub`.
- `TarExtractUnsafeNames`: a hand-built archive with `../y` and `/abs.txt`
  -> err ``tar: Removing leading `../' from member names\ntar: ../y: Member
  name contains '..'\ntar: Removing leading `/' from member names\n`` then
  the tail, 2; `abs.txt` extracted under the cwd; nothing written outside it.
- `TarExtractRefusesLinks`: hand-built `ln` (symlink to `target`) and `hl`
  (hard link to `ok`) and a `3` device -> err `tar: ln: Cannot create
  symlink to 'target': Operation not permitted\ntar: hl: Cannot hard link
  to 'ok': Operation not permitted\ntar: dev: Cannot mknod: Operation not
  permitted\ntar: Exiting with failure status due to previous errors\n`, 2;
  nothing at those paths.
- `TarCompare`: right after extracting, `-df /d.tar` -> nothing, 0; change
  `docs/a.md` to `alphabet` -> out `docs/a.md: Mod time differs\ndocs/a.md:
  Size differs\n`, 1; same size, different bytes, same mtime (SetTimes) ->
  `docs/a.md: Contents differ\n`, 1; removed -> err `tar: docs/a.md:
  Warning: Cannot stat: No such file or directory\n`, 1; `-dvf` prints
  names first.
- `TarAppendUpdateCatenateDelete`: `-rvf /d.tar notes.txt` -> out
  `notes.txt\n`, `-tf` lists it last, the file is a multiple of 10240;
  `-rf /new.tar notes.txt` creates `/new.tar`; `-uvf /d.tar docs/a.md` with
  nothing newer -> no output, archive unchanged; after touching it newer
  (SetTimes) -> `docs/a.md\n` and the member appears twice; `-Af /d.tar
  /new.tar` -> `/new.tar`'s members at the end; `-Af /d.tar /nosuch.tar`
  -> `tar: /nosuch.tar: Cannot open: No such file or directory` + tail, 2;
  `--delete -f /d.tar docs/sub` -> no `docs/sub/` nor `docs/sub/b.md`;
  `--delete -f /d.tar nope` -> not found, 2; `-rf - x` -> `tar: Options
  '-Aru' are incompatible with '-f -'\nTry 'tar --help' or 'tar --usage'
  for more information.\n`, 2; `--delete -f - docs/a.md` with the archive
  as stdin -> stdout an archive without it.
- `TarExtractCompressedFails`: `-xzf /d.tar` -> the four read-side lines
  with `gzip`, 2, nothing created.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Tar*'
bash ./scripts/test_linux.sh L U
```

Manual cross-check in the container: an archive written by GNU tar (`tar
-cf g.tar src`) extracted by Haisos's tar (a haisosfile with `FS rootfs
PHYSICAL <dir>`, `RUN /bin/tar -C /out -xf /g.tar`) gives the same files
and mtimes; `tar -df g.tar` (GNU) run in `out` reports nothing but mode
differences, if any.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`, the `tar` row: version
  1.1.0; treated adds `-x -d -r -u -A --delete -O -k --skip-old-files
  --keep-newer-files --overwrite -U --strip-components -m -i`; exceptions
  add: link, device and FIFO members refused with "Operation not
  permitted" (no links are created), modes and owners not restored or
  compared, `-r/-u/-A/--delete` rewrite the archive through a temporary
  file, holding it in memory; pax extended headers skipped.
- Root `CLAUDE.md`: the `tar` row's description (create, list, extract,
  compare, append, update, delete; no compression).

## Acceptance

- [ ] `-x` creates directories and files with their mtimes (directories last), honours `-C`, `-k`, `--skip-old-files`, `--keep-newer-files`, `-O`, `--strip-components`, operands, `--wildcards`, `--exclude`, `-m`.
- [ ] Unsafe names handled as GNU does; nothing is ever written outside the base directory without `-P`.
- [ ] No link, device or FIFO is created; the refusals use GNU's wording.
- [ ] `-d` prints GNU's difference lines on stdout and exits 0/1/2 as GNU.
- [ ] `-r -u -A --delete` give archives GNU tar lists correctly; `-f -` restrictions as GNU; the rewrite goes through a temporary file and `Rename`.
- [ ] Only `context.IO()`/`context.Out` for I/O; stop and broken pipe honoured.
- [ ] Version 1.1.0; CMakeLists; docs; tests green on Linux.

## Out of scope

- Compression, pax extended headers' contents, sparse members, multi-volume
  archives, incremental archives, `--to-command`, `--transform`, restoring
  permissions or owners, creating links (forbidden), `--backup`.
- Anything of `-c`/`-t` beyond the refactor of the create walk into a sink
  (tools--tar-create-list).
