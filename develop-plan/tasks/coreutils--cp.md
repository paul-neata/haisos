# Task coreutils--cp: the cp builtin and the shared copy routine

- Rock: coreutils
- Depends on: base--fs-rename-times, coreutils--rm-rmdir
- Size: ~900 changed lines in ~8 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the cp builtin and a shared copy routine

## Goal

`cp` is a builtin command (`BUILTIN rootfs cp /bin/cp`) behaving as GNU
coreutils 9.4's cp (Ubuntu 24.04): files and directory trees (`-r`, `-a`),
times kept with `-p`/`-a`, `-i -n -u -f -b -v -t -T --parents ...`, the same
messages byte for byte and the same exit codes. `cp -a out out2` keeps the
modification times, so `stat` later shows them.

The copying itself is written once, in `BuiltinCopy.h/.cpp`, for `mv`
(copy-then-remove across filesystems, coreutils--mv-touch) and any later
command that copies files (tar, patch, ...).

## Context

Read first: the root `CLAUDE.md` ("Security", "Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `src/components/Filesystem/CLAUDE.md`.

What earlier tasks provide, as if on develop:
- base--fs-rename-times: in `interfaces/IFileSystemService.h`
  `constexpr int kFileSystemError = -1; constexpr int kFileSystemCrossDevice = -2;`,
  and on `IFileSystem` and `IFileIO`:
  `int Rename(const std::string& oldPath, const std::string& newPath)` and
  `int SetTimes(const std::string& path, const std::optional<FileDateTime>& accessTime, const std::optional<FileDateTime>& modificationTime)`
  (nullopt leaves that time; fails on a builtin's path).
- coreutils--rm-rmdir: `BuiltinPrompt` (`BuiltinPrompt.h`: `explicit
  BuiltinPrompt(BuiltinContext&)`, `bool Ask(const std::string& question)` --
  GNU's yesno on stderr/stdin) and `RemoveOperand` (`BuiltinRemove.h`); the
  `rm`/`rmdir` builtins, whose `Rm.cpp` is the nearest model.

What exists: `BuiltinCommand.h` (`BuiltinContext`, `BeginBuiltin`,
`ParseBuiltinArgs`, `ShellEscapeQuoted`, `kBuiltinNotTreated`),
`FilesystemUtils.h` (`kFileOpenReadOnly`, `kFileOpenWriteCreateTruncate`,
`kFileOpenWriteCreateAppend`, `kFileCreateMode`, `EntryTypeOf`),
`commands/mkdir/Mkdir.cpp` (`kDirectoryMode` per platform). `IFileIO`
operations fail with -1 and no reason: reasons for messages come from `Stat`.

Rules that bite: `ICurrentProcess` is the only door out (all file access via
`context.IO()`); builtins print GNU's output byte for byte, list every GNU
option in `Options()` (untreated ones `kBuiltinNotTreated`), `--help` from
`BuiltinHelpText` with exceptions in `Help().notes`, registered in
`CreateStandardBuiltinCommands()` (which feeds the `haisos --init` template);
no symbolic or hard link may ever be created; portable C++17 (no POSIX
headers, no `<regex>`).

## Changes

### `src/components/BuiltinCommands/BuiltinCopy.h` / `.cpp` (new)

```cpp
namespace Haisos {

enum class BackupMode { None, Simple, Numbered, Existing };
enum class UpdateMode { All, None, Older };        // --update=all|none|older
enum class CopyVerbose { None, Cp, Mv };           // see "Verbose lines"

struct CopyOptions {
    bool recursive = false;          // -r/-R/-a: copy directories
    bool preserveTimes = false;      // -p, -a, --preserve=timestamps/all
    bool attributesOnly = false;     // --attributes-only: no data
    bool removeDestination = false;  // --remove-destination
    bool force = false;              // -f: unwritable destination removed and retried
    bool interactive = false;        // -i: ask before overwriting an existing file
    UpdateMode update = UpdateMode::All;  // -n is UpdateMode::None, -u UpdateMode::Older
    BackupMode backup = BackupMode::None;
    std::string backupSuffix = "~";
    CopyVerbose verbose = CopyVerbose::None;
};

// One source and the exact path its copy gets (never "the directory to copy
// into"), both as the user is shown them.
struct CopyTarget {
    std::string source;
    std::string dest;
};

// cp's and mv's operand rules (see "Operands"). Reports GNU's messages with
// context.Name() and returns nullopt on an error, the exit status then 1.
std::optional<std::vector<CopyTarget>> ResolveCopyTargets(
    BuiltinContext& context, std::vector<std::string> operands,
    const std::optional<std::string>& targetDirectory, bool noTargetDirectory,
    bool parents, bool stripTrailingSlashes);

// Copies |source| to |dest| -- a file, or with options.recursive a directory
// and its whole tree -- applying options at every level as cp does. |prompt|
// may be null when options.interactive is false. Messages begin with
// context.Name(). Returns false if anything was not copied, a declined
// overwrite included (GNU 9.4 then exits 1); a file skipped by -n/-u is not a
// failure.
bool CopyPath(BuiltinContext& context, BuiltinPrompt* prompt,
              const std::string& source, const std::string& dest,
              const CopyOptions& options);

// The name a backup of |dest| gets (see "Backups"); |dest| exists.
std::string BackupPathFor(BuiltinContext& context, const std::string& dest,
                          BackupMode mode, const std::string& suffix);

// Parses a --backup CONTROL word: none/off, simple/never, existing/nil,
// numbered/t. On a bad word prints GNU's block (below) without the Try line
// and returns false.
bool ParseBackupControl(BuiltinContext& context, const std::string& word, BackupMode& out);
}
```

Below, `q(x)` is `ShellEscapeQuoted(x, /*always=*/true)` (GNU's quoteaf).

#### Operands (`ResolveCopyTargets`)

Verified against GNU cp/mv 9.4 (`<n>` is `context.Name()`):
1. With `stripTrailingSlashes`, trailing `/`s are removed from each source
   (not from a lone `/`).
2. `-t DIR` and `-T` together: `<n>: cannot combine --target-directory (-t)
   and --no-target-directory (-T)` (no Try line).
3. No operands: `<n>: missing file operand` + Try. With `-t`: every operand is
   a source (none: `missing file operand`).
4. Without `-t`, one operand: `<n>: missing destination file operand after
   q(op)` + Try.
5. `-T`: exactly two operands (more: `<n>: extra operand q(third)` + Try); the
   target is the second as is.
6. `-t DIR`: DIR must be a directory: missing -> `<n>: target directory
   q(DIR): No such file or directory`; not a directory -> `...: Not a
   directory`.
7. Otherwise the last operand is the target; with more than two operands it
   must be a directory (`<n>: target q(t): No such file or directory` /
   `...: Not a directory`); with two, it is used as a directory only when it
   is an existing directory.
8. `parents`: the target must be a directory, else `<n>: with --parents, the
   destination must be a directory` + Try.
9. Into a directory: `dest = DIR + "/" + name`, no doubled `/` when DIR ends
   in one; name is the source's last segment after trailing slashes are
   stripped (`s/` -> `s`); with `parents` the whole source path instead, a
   leading `/` dropped (`cp --parents d/e/f p` -> `p/d/e/f`).

#### Copying (`CopyPath`)

For one (source, dest), in order:
1. `Stat(source)` fails: `cannot stat q(source): No such file or directory`
   (or `Not a directory` when a parent segment is a file). False.
2. Source a directory without `recursive`: `-r not specified; omitting
   directory q(source)`. False.
3. Same file: `IO().ResolvePath(source) == IO().ResolvePath(dest)` ->
   `q(source) and q(dest) are the same file` (cp `a ./` gives `'a' and './a'
   are the same file`). False.
4. Directory into itself: the resolved dest is the resolved source plus `/`
   and more -> `cannot copy a directory, q(source), into itself, q(dest)`.
   False, nothing copied. (GNU copies one level before noticing; Haisos
   refuses at once -- document it.)
5. Existing dest and types differ: source directory onto a non-directory ->
   `cannot overwrite non-directory q(dest) with directory q(source)`; a
   non-directory onto a directory -> `cannot overwrite directory q(dest) with
   non-directory`. False.
6. **Directory**: if dest does not exist, `CreateDirectory(dest,
   kDirectoryMode)` (failure: `cannot create directory q(dest): <reason>`,
   reason `No such file or directory` when the parent is missing, `Not a
   directory` when it is a file, else `Permission denied`) and print the
   verbose line; an existing directory is merged into, no line. Then every
   entry (skip `.`/`..`), **sorted by name** (byte order; GNU follows the
   disk's order), by `CopyPath(source + "/" + name, dest + "/" + name)` (no
   doubled `/`: `s/` + `x` is `s/x`), stopping early when
   `context.StopRequested()`. Then, with `preserveTimes`, `SetTimes(dest,
   srcStatus.accessTime, srcStatus.modificationTime)` -- after the contents,
   or copying them would change the directory's times again.
7. **File** (or device), dest existing: in this order --
   `update == None`: skip (true, no line). `update == Older` and
   dest's modification time >= source's: skip (true). `interactive`:
   `prompt->Ask(<n> + ": overwrite " + q(dest) + "? ")`, declined -> false.
   `backup != None`: `Rename(dest, BackupPathFor(...))` (any result but 0:
   `cannot backup q(dest): Permission denied`, false), remembering the backup
   name for the verbose line.
   `removeDestination`: `RemoveFile(dest)` and, with verbose Cp, print
   `removed q(dest)` before the copy line.
8. Copy the bytes: open source `kFileOpenReadOnly` (failure: `cannot open
   q(source) for reading: Permission denied`); open dest
   `kFileOpenWriteCreateTruncate, kFileCreateMode` -- with `attributesOnly`
   `kFileOpenWriteCreateAppend` and nothing written. If opening dest fails
   and it exists and `force`: `RemoveFile(dest)` and open again. Failure:
   `cannot create regular file q(dest): <reason>` (parent missing -> `No such
   file or directory`, parent a file or dest ending in `/` -> `Not a
   directory`, else `Permission denied`). Read/write in 64 KiB chunks,
   checking `context.StopRequested()` between chunks; a read or write error:
   `error reading q(source)` / `error writing q(dest)`, false. A character
   device as a **recursive** tree member is not copied: `cannot create special
   file q(dest): Operation not permitted` (Haisos cannot create devices); as
   a top-level operand its contents are copied, as GNU does (`cp /dev/null x`
   makes an empty file).
9. Release both descriptors, then with `preserveTimes` `SetTimes(dest,
   src.accessTime, src.modificationTime)`; failure: `preserving times for
   q(dest): Permission denied`, false.
10. Print the verbose line.

#### Verbose lines (stdout)

- `CopyVerbose::Cp` (cp -v), files and created directories alike:
  `'a' -> 'b'`, with a backup `'z' -> 'a' (backup: 'a~')`.
- `CopyVerbose::Mv` (mv across filesystems, as GNU mv 9.4 prints it):
  a created directory `created directory '/mnt/d'`, a file
  `copied 'd/e/f' -> '/mnt/d/e/f'`.
- cp `--parents -v` first prints each directory it creates on the way, **unquoted**:
  `d -> p/d`, `d/e -> p/d/e`, then `'d/e/f' -> 'p/d/e/f'` (exactly GNU's bytes).
  That walk (create the missing parent directories of `dest` that lie under
  the target directory, printing those lines) lives in `Cp.cpp`, before
  `CopyPath`.

#### Backups

`Simple`: `dest + suffix`. `Numbered`: `dest + ".~N~"`, N one more than the
largest N of the `<name>.~N~` entries in dest's directory (1 if none).
`Existing`: numbered if any `<name>.~N~` exists, else simple. Invalid
control word -- exactly, then `TryHelp`, exit 1:
```
cp: invalid argument 'bogus' for 'backup type'
Valid arguments are:
  - 'none', 'off'
  - 'simple', 'never'
  - 'existing', 'nil'
  - 'numbered', 't'
```
`-b` and `--backup` without a word take the mode from the process
environment's `VERSION_CONTROL` (`context.Process().GetEnvironment()`), else
`Existing`; the suffix is `-S`'s, else the environment's
`SIMPLE_BACKUP_SUFFIX`, else `~`. `-S SUFFIX` alone also turns backups on
(GNU 9.4: `cp -v -S .bak a b` -> `'a' -> 'b' (backup: 'b.bak')`).

### `src/components/BuiltinCommands/commands/cp/Cp.cpp` (new)

`CreateCpCommand()`; `cp`, `1.0.0`; help summary `copy files and
directories`, usage `cp [OPTION]... [-T] SOURCE DEST`, `cp [OPTION]...
SOURCE... DIRECTORY`, `cp [OPTION]... -t DIRECTORY SOURCE...`.

Options -- every option of GNU cp 9.4:

| option | argument | treated | effect |
|---|---|---|---|
| `-a, --archive` | | yes | recursive + preserveTimes |
| `--attributes-only` | | yes | |
| `--backup` | Optional `CONTROL` | yes | |
| `-b` | | yes | backup, mode from VERSION_CONTROL |
| `--copy-contents` | | no | |
| `-d` | | yes | no-op: there are no links |
| `--debug` | | no | |
| `-f, --force` | | yes | force |
| `-i, --interactive` | | yes | interactive on, update back to All if -n set it (last of -i/-n wins) |
| `-H` | | yes | no-op (no links) |
| `-l, --link` | | yes | fails, see below |
| `-L, --dereference` | | yes | no-op (no links) |
| `-n, --no-clobber` | | yes | update None, interactive off; prints the 9.4 warning |
| `-P, --no-dereference` | | yes | no-op (no links) |
| `-p` | | yes | preserveTimes |
| `--preserve` | Optional `ATTR_LIST` | yes | see below |
| `--no-preserve` | Required `ATTR_LIST` | yes | `timestamps`/`all` clear preserveTimes |
| `--parents` | | yes | |
| `-R, -r, --recursive` | | yes | (`-R` and `-r` share an id; give `-R` the description "same as -r") |
| `--reflink` | Optional `WHEN` | no | |
| `--remove-destination` | | yes | |
| `--sparse` | Required `WHEN` | no | |
| `--strip-trailing-slashes` | | yes | |
| `-s, --symbolic-link` | | yes | fails, see below |
| `-S, --suffix` | Required `SUFFIX` | yes | |
| `-t, --target-directory` | Required `DIRECTORY` | yes | |
| `-T, --no-target-directory` | | yes | |
| `--update` | Optional `UPDATE` | yes | all / none / older (no word: older) |
| `-u` | | yes | update Older |
| `-v, --verbose` | | yes | CopyVerbose::Cp |
| `-x, --one-file-system` | | no | |
| `-Z` | | no | |
| `--context` | Optional `CTX` | no | |

Details:
- `-n` (9.4): before anything else, `cp: warning: behavior of -n is
  non-portable and may change in future; use --update=none instead` on stderr;
  exit stays 0 when files are skipped.
- `--update` with a bad word: the GNU block `invalid argument 'bogus' for
  '--update'` / `Valid arguments are:` / `  - 'all'` / `  - 'none'` /
  `  - 'older'` + Try, exit 1.
- `--preserve[=LIST]` (default `mode,ownership,timestamps`), comma-separated:
  `timestamps` and `all` set preserveTimes; `mode`, `ownership`, `links` are
  accepted and need nothing (no permissions, owners or links in Haisos);
  `context`, `xattr` are reported with `context.NotTreated("--preserve=context")`.
  Any other word: the block `invalid argument 'bogus' for '--preserve'` /
  `Valid arguments are:` / `  - 'mode'` / `  - 'timestamps'` /
  `  - 'ownership'` / `  - 'links'` / `  - 'context'` / `  - 'xattr'` /
  `  - 'all'` + Try, exit 1.
- `-l` / `-s`: no link may be created (root `CLAUDE.md`). For each target:
  `cp: cannot create hard link q(dest) to q(source): Operation not permitted`
  (`-s`: `cannot create symbolic link`), status 1, nothing created.
- Run: `BeginBuiltin(..., 1, ...)`; options; `ResolveCopyTargets`; one
  `BuiltinPrompt` for the whole run; `--parents` directory walk; `CopyPath`
  per target; exit 1 if any failed.

Help notes (documented exceptions): `-l`/`-s` fail (no links may be
created); `-d -H -L -P` change nothing (no links); `--preserve` keeps only
timestamps (no modes, owners, links); entries are copied in name order; a
directory copied into itself is refused before anything is copied.

### `src/components/BuiltinCommands/BuiltinCommandList.h`, `CMakeLists.txt`

Declare and register `CreateCpCommand()` (alphabetical); add
`BuiltinCopy.cpp` and `commands/cp/Cp.cpp` to the library.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/CpTest.cpp` (listed in
that directory's `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, Cp...)` on
`RunCaptured`, asserting stdout, stderr and status exactly. Expected outputs
are GNU 9.4's (all verified on Ubuntu 24.04).

- `CpCopiesAFile`: `cp /notes.txt /n2` -> 0; `/n2` has the same bytes.
- `CpVerboseIntoADirectory`: `cp -v /notes.txt /docs/a.md /docs/sub` -> out `'/notes.txt' -> '/docs/sub/notes.txt'\n'/docs/a.md' -> '/docs/sub/a.md'\n`.
- `CpOperandErrors`: `cp` -> `cp: missing file operand\nTry 'cp --help' for more information.\n`; `cp /notes.txt` -> `cp: missing destination file operand after '/notes.txt'\n` + Try; `cp /notes.txt /.hidden /x` -> `cp: target '/x': No such file or directory\n`; with `/x` a file -> `...: Not a directory`; `cp -t /x /notes.txt` (no /x) -> `cp: target directory '/x': No such file or directory\n`; `cp -t /docs -T /a /b` -> the cannot-combine line; `cp -T /a /b /c` -> `cp: extra operand '/c'\n` + Try. Status 1 each.
- `CpMissingSource`: `cp /nothing /x` -> `cp: cannot stat '/nothing': No such file or directory\n`, 1.
- `CpDirectoryWithoutR`: `cp /docs /d2` -> `cp: -r not specified; omitting directory '/docs'\n`, 1.
- `CpRecursiveVerbose`: `cp -rv /docs /d2` -> out exactly `'/docs' -> '/d2'\n'/docs/a.md' -> '/d2/a.md'\n'/docs/sub' -> '/d2/sub'\n'/docs/sub/b.md' -> '/d2/sub/b.md'\n`; again `cp -rv /docs /d2` -> copies into `/d2/docs`, lines with `/d2/docs/...`.
- `CpTrailingSlashSource`: `RunCaptured("cp", {"-rv", "docs/", "u"})` -> out `'docs/' -> 'u'\n'docs/a.md' -> 'u/a.md'\n'docs/sub' -> 'u/sub'\n'docs/sub/b.md' -> 'u/sub/b.md'\n`.
- `CpSameFile`: `RunCaptured("cp", {"notes.txt", "./"})` -> `cp: 'notes.txt' and './notes.txt' are the same file\n`, 1.
- `CpIntoItself`: `cp -r /docs /docs/sub` -> `cp: cannot copy a directory, '/docs', into itself, '/docs/sub/docs'\n`, 1, `/docs/sub/docs` absent.
- `CpTypeMismatch`: `cp -r /docs /notes.txt` -> `cp: cannot overwrite non-directory '/notes.txt' with directory '/docs'\n`; `cp -T /notes.txt /docs` -> `cp: cannot overwrite directory '/docs' with non-directory\n`.
- `CpCreateFailures`: `cp /notes.txt /missing/x` -> `cp: cannot create regular file '/missing/x': No such file or directory\n`; `cp /notes.txt /nodir/` -> `cp: cannot create regular file '/nodir/': Not a directory\n`.
- `CpPreservesTimes`: set `/notes.txt`'s times with `root->SetTimes` to a fixed `FileDateTime{1704164645, 0}` (both); `cp -p /notes.txt /p` and `cp -a /docs /a2` (after setting `/docs/a.md`'s and `/docs`'s times) -> `Stat` of the copies shows the same access and modification times; plain `cp` does not.
- `CpNoClobberWarns`: `cp -n /.hidden /notes.txt` -> err `cp: warning: behavior of -n is non-portable and may change in future; use --update=none instead\n`, 0, content unchanged; `cp --update=none -v` -> nothing, 0.
- `CpInteractive`: `cp -i /.hidden /notes.txt` input `n\n` -> err `cp: overwrite '/notes.txt'? `, status 1, unchanged; input `y\n` with `-iv` -> err the prompt, out `'/.hidden' -> '/notes.txt'\n`, 0; to a new file -> no prompt.
- `CpUpdateOlder`: dest newer (SetTimes) -> `cp -uv` copies nothing, 0; dest older -> copies, line printed.
- `CpBackups`: `cp -bv /.hidden /notes.txt` -> out `'/.hidden' -> '/notes.txt' (backup: '/notes.txt~')\n` and `/notes.txt~` holds the old content; `--backup=numbered` twice -> `.~1~` then `.~2~`; `-S .bak` alone with no numbered backups present -> `(backup: '/x.bak')`; `--backup=bogus` -> the exact block + Try, 1.
- `CpRemoveDestination`: `cp -v --remove-destination /.hidden /notes.txt` -> out `removed '/notes.txt'\n'/.hidden' -> '/notes.txt'\n`.
- `CpAttributesOnly`: `cp --attributes-only /notes.txt /ao` -> `/ao` exists, size 0.
- `CpParentsVerbose`: make `/p`; `RunCaptured("cp", {"-v", "--parents", "docs/sub/b.md", "p"})` -> out `docs -> p/docs\ndocs/sub -> p/docs/sub\n'docs/sub/b.md' -> 'p/docs/sub/b.md'\n`; `cp --parents /a /b` with `/b` missing -> `cp: with --parents, the destination must be a directory\n` + Try.
- `CpLinksFail`: `cp -l /notes.txt /lnk` -> `cp: cannot create hard link '/lnk' to '/notes.txt': Operation not permitted\n`, 1, `/lnk` absent; `-s` the symbolic-link wording.
- `CpPreserveBadAttribute`: `cp --preserve=bogus /notes.txt /x` -> the exact block + Try, 1.
- `CpCopiesABuiltinsNote`: `cp /bin/ls /ls.txt` -> 0, `/ls.txt` holds what `cat /bin/ls` prints.
- `CpAcrossAMount`: mount an empty in-memory filesystem at `/mnt` (`root->Mount`); `cp -r /docs /mnt/d` -> the tree is there.

Update `ListsEveryBuiltinSortedWithAVersion` in `BuiltinCommandsTest.cpp`
(add `"cp"` in sorted position). The generic tests check `--help`, untreated
options (`cp --copy-contents /docs` etc. stop at "missing destination" and
must not copy anything) and the `--init` template.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Cp*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list; a `cp` row
  (1.0.0) with the exceptions above; under "Key Classes" `BuiltinCopy`
  (`CopyPath`, `ResolveCopyTargets`, `BackupPathFor`, `ParseBackupControl`:
  shared by cp and mv, for any command that copies files).
- Root `CLAUDE.md`: a `cp` row in "Builtin Commands"; the command lists.

## Acceptance

- [ ] Every case in Tests prints GNU 9.4's bytes and status.
- [ ] Every GNU cp 9.4 option is in `Options()`; untreated: `--copy-contents --debug --reflink --sparse -x -Z --context`.
- [ ] No link is ever created; `-l`/`-s` fail as described.
- [ ] Times come from `Stat` and go to `SetTimes`; all access through `context.IO()`.
- [ ] `BuiltinCopy.h` has exactly the declarations above, usable by mv.
- [ ] Generic builtin tests and the `--init` template test pass; all unit tests green.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- `mv` (coreutils--mv-touch calls `ResolveCopyTargets`, `CopyPath` with
  `CopyVerbose::Mv`, `BackupPathFor`, `ParseBackupControl`).
- Sparse files, reflinks, SELinux contexts, `--debug` (not treated).
