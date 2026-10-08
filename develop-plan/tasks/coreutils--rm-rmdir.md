# Task coreutils--rm-rmdir: the rm and rmdir builtins

- Rock: coreutils
- Depends on: none
- Size: ~750 changed lines in ~10 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the rm and rmdir builtins

## Goal

`rm` and `rmdir` are builtin commands, placed with `BUILTIN rootfs rm /bin/rm`
(and `rmdir`), behaving as GNU coreutils 9.4's (Ubuntu 24.04, the task
container's): the same options, the same messages byte for byte, the same
prompts (read from the process's standard input), the same exit codes. So
`rm -rv out2` prints `removed 'out2/a/y'`, `removed directory 'out2/a'`,
`removed directory 'out2'`, and `rmdir -p a/b` removes both.

Two pieces are written once here for later tasks:
- `BuiltinPrompt` -- GNU's yes/no prompt (cp -i and mv -i use it);
- `RemoveOperand` -- removing one operand as rm does, recursively when asked
  (mv uses it to remove a source it copied across filesystems).

## Context

Read first: the root `CLAUDE.md` (sections "Security", "Builtin Commands",
"Automatic Development Rules" rule 9), `src/components/BuiltinCommands/CLAUDE.md`,
`src/components/Filesystem/CLAUDE.md`.

What exists:
- `src/components/BuiltinCommands/BuiltinCommand.h`: `IBuiltinCommand`,
  `BuiltinContext` (`Out`, `Error` = `"<name>: " + message + "\n"` on stderr,
  `ErrorText` = raw stderr bytes, `TryHelp`, `StopRequested`, `IO()`,
  `Process()`), `BeginBuiltin`, `ParseBuiltinArgs`, `ShellEscapeQuoted`,
  `BuiltinHelpText`, `kBuiltinNotTreated`. `Error`/`ErrorText` flush the
  buffered stdout first, so prompts and messages come out in order.
- `commands/mkdir/Mkdir.cpp` is the model for a small builtin (option table,
  `Help()`, `Run()` using `BeginBuiltin`, messages built from `Stat`).
- `src/components/Filesystem/FilesystemUtils.h`: `EntryTypeOf(io, path)`,
  `VirtualParentOf`, `VirtualLastSegment` (from `VirtualPath.h`).
- `src/components/libheaders/DescriptorLineReader.h`: reads lines from a
  descriptor (`nullopt` at end of input or on a failed read, `kIOInterrupted`
  included -- so a stop while a prompt waits ends the read).
- `IFileIO` (`interfaces/IFileIO.h`): `RemoveFile`, `RemoveDirectory`,
  `ReadDirectory` (`.` and `..` first, then the rest in no particular order),
  `Stat`, `ResolvePath`, `GetDescriptor(IFileIO::kStdIn)`. Failures return -1
  (`kFileSystemError`) with no reason: the reason for a message is found with
  `Stat`/`ReadDirectory`, as `mkdir` does.
- A builtin's path cannot be removed with `RemoveFile`; `RemoveDirectory`
  refuses the directory holding a builtin and every directory above it, a
  mount point (it routes to the mounted filesystem's root), and the root.
- Tests: `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h`
  (`BuiltinCommandsTest`: an in-memory root with every builtin in `/bin`,
  `/notes.txt`, `/.hidden`, `/docs/a.md`, `/docs/sub/b.md`; `Run` (console,
  empty stdin) and `RunCaptured(command, args, input, workingDirectory,
  environment)` giving stdout, stderr and status byte for byte).

Rules that bite (root `CLAUDE.md`):
- `ICurrentProcess` is the only door out of a process: every file operation
  goes through `context.IO()`; nothing holds an `IFileSystem` or `IHaisosOS`.
- Builtins: GNU's output byte for byte; every option of the real command in
  `Options()` (untreated ones `kBuiltinNotTreated`, reported by
  `BeginBuiltin`); `--help` built by `BuiltinHelpText` (never by hand) with
  the documented exceptions in `Help().notes`; `--version`; registered in
  `CreateStandardBuiltinCommands()`, which also puts `# BUILTIN rootfs rm
  /bin/rm` in the `haisos --init` template (never hand-write it).
- Portable C++17 (Linux, Windows/MSVC, WASM): no POSIX headers, no `<regex>`
  in production code.

## Changes

### `src/components/BuiltinCommands/BuiltinPrompt.h` / `.cpp` (new)

A plain helper class (it implements no interface from `interfaces/`, so the
private-constructor rule does not apply; it lives on the stack of a `Run`):

```cpp
namespace Haisos {
// GNU's yesno(): writes |question| to standard error exactly as given (no
// newline added), reads one line from the process's standard input, and
// answers true only when that line's first byte is 'y' or 'Y' (" y" and an
// empty line are no). End of input, a failed read, and a stop while waiting
// are all "no". One instance per run of a command, so lines read ahead are
// kept for the next question.
class BuiltinPrompt {
public:
    explicit BuiltinPrompt(BuiltinContext& context);
    bool Ask(const std::string& question);
private:
    BuiltinContext& m_context;
    std::optional<DescriptorLineReader> m_reader;  // made on the first Ask, over IO().GetDescriptor(IFileIO::kStdIn)
};
}
```

`Ask` writes with `context.ErrorText(question)` (which flushes stdout first).
At end of input GNU prints nothing more -- the next output simply follows the
prompt on the same line (`rm: remove regular file 'a'? removed 'b'`).

### `src/components/BuiltinCommands/BuiltinRemove.h` / `.cpp` (new)

```cpp
namespace Haisos {
struct RemoveOptions {
    bool recursive = false;        // -r/-R
    bool emptyDirectories = false; // -d
    bool ignoreMissing = false;    // -f: a missing operand is no error and no message
    bool interactive = false;      // -i: ask before each removal (needs a prompt)
    bool verbose = false;          // -v: "removed 'x'", "removed directory 'x'" on stdout
    bool preserveRoot = true;      // refuse "/" with -r
};
// Removes one operand as GNU rm does, |path| as the user wrote it (relative
// paths go through context.IO(), messages show |path| and the paths built
// from it). |prompt| may be null when options.interactive is false. Messages
// start with context.Name() ("rm: ..." in rm, "mv: ..." when mv calls it).
// Returns false if anything failed (the exit status becomes 1); a declined
// prompt is not a failure.
bool RemoveOperand(BuiltinContext& context, BuiltinPrompt* prompt,
                   const std::string& path, const RemoveOptions& options);
}
```

Behaviour of `RemoveOperand`, in order (`q(x)` below is
`ShellEscapeQuoted(x, /*always=*/true)`, GNU's quoteaf, used in every rm
message):

1. Status: `IO().Stat(path)`. Missing: with `ignoreMissing` return true
   silently; otherwise `cannot remove q(path): No such file or directory`,
   false. When `path` ends in `/` and what is there is not a directory:
   `cannot remove q(path): Not a directory`, false.
2. The last segment of `path` with trailing slashes stripped is `.` or `..`
   and (`recursive` or `emptyDirectories`): `refusing to remove '.' or '..'
   directory: skipping q(path)`, false.
3. A directory with `recursive` and `preserveRoot` whose
   `IO().ResolvePath(path) == "/"`: two lines, `it is dangerous to operate
   recursively on '/'` and `use --no-preserve-root to override this failsafe`,
   false.
4. A directory without `recursive`:
   - without `emptyDirectories`: `cannot remove q(path): Is a directory`,
     false (with `-i` too: no prompt first);
   - with `emptyDirectories`: prompt (if interactive) `remove directory
     q(path)? `, then `RemoveDirectory`; on failure the reason (step 7).
5. A file (or character device), any mode: if interactive, prompt
   `<name>: remove regular file q(path)? ` -- `regular empty file` when its
   size is 0, `character special file` for a device; declined: return true.
   Then `RemoveFile`; on success with `verbose`, `removed q(path)` on stdout.
6. A directory with `recursive`, post-order:
   - interactive and the directory has entries (besides `.`/`..`): prompt
     `descend into directory q(path)? `; declined: return true, nothing
     under it is touched and no further prompt for it.
   - Each entry, **sorted by name (byte order)** -- a Haisos choice: GNU
     follows the disk's order, which no listing guarantees -- is removed by
     the same steps with the child path `path + "/" + name` (no doubled `/`
     when `path` already ends with one). `.` and `..` are skipped.
   - Stop early (return false) if `context.StopRequested()`.
   - If any child **failed** (not merely declined), the directory itself is
     skipped silently: no prompt, no message (GNU marks the ancestors of a
     failure). Return false.
   - Otherwise: interactive -> prompt `remove directory q(path)? ` (declined:
     true). Then `RemoveDirectory`; with `verbose`, `removed directory q(path)`.
7. Reasons, since `IFileIO` gives none:
   - `RemoveFile` failed: `cannot remove q(path): Permission denied` (a
     builtin's path, a read-only filesystem).
   - `RemoveDirectory` failed: `Directory not empty` when `ReadDirectory`
     still lists anything besides `.`/`..`, else `Device or resource busy` (a
     mount point, the root of a filesystem). Document in the help notes and
     the CLAUDE.md table that a read-only filesystem is reported this way too.

Exact prompt texts and messages (verified against GNU rm 9.4, `LC_ALL=C`):

```
rm: remove regular file 'a'?            (each prompt ends in "? ", no newline)
rm: remove regular empty file 'empty'?
rm: remove character special file '/dev/null'?
rm: descend into directory 'd'?
rm: remove directory 'd/e'?
rm: cannot remove 'd': Is a directory
rm: cannot remove 'nothing': No such file or directory
rm: cannot remove 'd': Directory not empty
rm: cannot remove 'tf/': Not a directory
rm: refusing to remove '.' or '..' directory: skipping 'z/w/..'
removed 'd/e/f'                          (stdout, -v)
removed directory 'd/e'                  (stdout, -v)
```

### `src/components/BuiltinCommands/commands/rm/Rm.cpp` (new)

`CreateRmCommand()`; `Name()` `rm`, `Version()` `1.0.0`. Options (every
option of GNU rm 9.4):

| short | long | argument | treated | description |
|---|---|---|---|---|
| `f` | `force` | none | yes | ignore nonexistent files, never prompt |
| `i` | | none | yes | prompt before every removal |
| `I` | | none | yes | prompt once before removing more than three files, or recursively |
| | `interactive` | Optional `WHEN` | yes | never, once (-I), or always (-i) |
| | `one-file-system` | none | no | |
| | `no-preserve-root` | none | yes | do not treat '/' specially |
| | `preserve-root` | Optional `all` | yes | do not remove '/' (default) |
| `r` | `recursive` | none | yes | remove directories and their contents |
| `R` | | none | yes (same id as `-r`) | |
| `d` | `dir` | none | yes | remove empty directories |
| `v` | `verbose` | none | yes | explain what is being done |

`-R` needs a description too (the generic help test requires one for every
treated option): "same as -r".

Help: summary `remove files or directories`, usage `rm [OPTION]... [FILE]...`,
notes: entries are removed in name order; a failed directory removal whose
directory is empty is reported as `Device or resource busy` (no reason is
known; a read-only filesystem included); `--preserve-root=all` is taken as
`--preserve-root`.

`Run`:
1. `BeginBuiltin(context, *this, 1, status)`.
2. Walk `parsed->options` in order, as GNU's switch does: `-f` sets
   interactive = never, ignoreMissing = true, promptOnce = false; `-i` sets
   interactive = always, promptOnce = false (ignoreMissing stays as `-f` left
   it); `-I` sets interactive = never, promptOnce = true;
   `--interactive[=WHEN]`: no argument or `always`/`yes` -> as `-i`;
   `once` -> as `-I`; `never`/`no`/`none` -> interactive never, promptOnce
   false; anything else -> exactly (then return 1):
   ```
   rm: invalid argument 'bogus' for '--interactive'
   Valid arguments are:
     - 'never', 'no', 'none'
     - 'once'
     - 'always', 'yes'
   Try 'rm --help' for more information.
   ```
   (`Error` for the first line, `ErrorText` for the list, then `TryHelp`).
   Accept any unambiguous prefix of a WHEN word only if it is easy; exact
   words are enough. `--preserve-root=all`: `context.NotTreated("--preserve-root=all")`,
   then as `--preserve-root`; any other value of `--preserve-root` is the
   same invalid-argument block with the list `  - 'all'`.
3. No operands: with `-f` (ignoreMissing) exit 0 silently; else `missing
   operand` + `TryHelp`, exit 1.
4. promptOnce: ask once, before anything is removed, when there are more than
   three operands or `-r` was given: `rm: remove N argument(s)? `, or with
   `-r` `rm: remove N argument(s) recursively? ` -- `argument` when N is 1,
   `arguments` otherwise. Declined: exit 0, nothing removed.
5. Each operand through `RemoveOperand`; exit 1 if any returned false, else 0.

### `src/components/BuiltinCommands/commands/rmdir/Rmdir.cpp` (new)

`CreateRmdirCommand()`; `rmdir`, `1.0.0`. Options: `--ignore-fail-on-non-empty`
(treated: "ignore each failure to remove a non-empty directory"), `-p,
--parents` (treated: "remove DIRECTORY and its ancestors"), `-v, --verbose`
(treated: "output a diagnostic for every directory processed"). Help: summary
`remove empty directories`, usage `rmdir [OPTION]... DIRECTORY...`.

Behaviour (verified against GNU rmdir 9.4):
- No operand: `rmdir: missing operand` + Try line, exit 1.
- With `-v`, before each attempt, on **stdout**: `rmdir: removing directory, 'a/b'`
  (`"rmdir: removing directory, " + q(path) + "\n"` via `Out`).
- One removal: `RemoveDirectory(path)`; on failure the message is
  `rmdir: failed to remove q(path): <reason>` with reason: nothing there ->
  `No such file or directory`; not a directory -> `Not a directory`; last
  segment `.` -> `Invalid argument`; still has entries -> `Directory not
  empty`; otherwise `Device or resource busy`.
- `--ignore-fail-on-non-empty`: a failure whose reason is `Directory not
  empty` is silent and not an error.
- `-p`: after removing `a/b/c`, the operand is shortened at its last `/`
  (trailing slashes stripped first: `s/t/` -> `s`), and each prefix is removed
  in turn (`a/b`, then `a`), stopping at the first failure. A failure of a
  parent (not the operand itself) is worded `rmdir: failed to remove directory
  q(prefix): <reason>`, and with `--ignore-fail-on-non-empty` a non-empty
  parent stops the walk silently with success.
- Exit 1 if any operand failed.

Exact transcript to match (GNU 9.4):
```
$ rmdir -pv q/r         # q also holds a file
rmdir: removing directory, 'q/r'
rmdir: removing directory, 'q'
rmdir: failed to remove directory 'q': Directory not empty     (stderr; exit 1)
```

### `src/components/BuiltinCommands/BuiltinCommandList.h`

Declare `CreateRmCommand()` and `CreateRmdirCommand()`; add both to
`CreateStandardBuiltinCommands()` (keep the list alphabetical).

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `BuiltinPrompt.cpp`, `BuiltinRemove.cpp`, `commands/rm/Rm.cpp`,
`commands/rmdir/Rmdir.cpp`. The `BuiltinCommands` target already reaches
`src/components/libheaders/` through `${CMAKE_SOURCE_DIR}` includes as the
other sources do (`#include "src/components/libheaders/DescriptorLineReader.h"`);
check that the include root is available, as `Ls.cpp` includes
`src/components/libheaders/CrtInvalidParameterAsError.h`.

## Tests

New file `tests/unit/components/BuiltinCommands.unittests/RmTest.cpp` (add it
to that directory's `CMakeLists.txt` `add_executable` list), all
`TEST_F(BuiltinCommandsTest, ...)` on the fixture, using `RunCaptured` and
asserting stdout, stderr and status exactly:

- `RmRemovesAFile`: `rm /notes.txt` -> status 0, no output, `Stat` fails afterwards.
- `RmVerbose`: `rm -v /notes.txt` -> out `removed '/notes.txt'\n`.
- `RmMissingOperand`: `rm` -> err `rm: missing operand\nTry 'rm --help' for more information.\n`, 1; `rm -f` -> 0, nothing.
- `RmMissingFile`: `rm /nothing` -> `rm: cannot remove '/nothing': No such file or directory\n`, 1; `rm -f /nothing` -> 0, nothing.
- `RmDirectoryWithoutR`: `rm /docs` -> `rm: cannot remove '/docs': Is a directory\n`, 1, `/docs` still there.
- `RmDirEmptyAndNot`: make `/e`; `rm -dv /e` -> out `removed directory '/e'\n`; `rm -d /docs` -> `rm: cannot remove '/docs': Directory not empty\n`.
- `RmRecursiveVerboseInNameOrder`: add `/docs/c.md`; `rm -rv /docs` -> out exactly `removed '/docs/a.md'\nremoved '/docs/c.md'\nremoved '/docs/sub/b.md'\nremoved directory '/docs/sub'\nremoved directory '/docs'\n`.
- `RmRelativeFromWorkingDirectory`: `RunCaptured("rm", {"-v", "a.md"}, nullopt, "/docs")` -> `removed 'a.md'\n`.
- `RmInteractiveDeclinedAndAccepted`: `rm -i /notes.txt` with input `n\n` -> err `rm: remove regular file '/notes.txt'? `, 0, file kept; input `y\n` with `-iv` -> err the prompt, out `removed '/notes.txt'\n`; with no input (end of input) -> the prompt only, file kept.
- `RmInteractiveEmptyFile`: an empty `/empty` -> prompt `rm: remove regular empty file '/empty'? `.
- `RmInteractiveRecursiveTranscript`: `/d/e/f` with content; `rm -ri /d` with input `y\ny\nn\n` -> err `rm: descend into directory '/d'? rm: descend into directory '/d/e'? rm: remove regular file '/d/e/f'? rm: remove directory '/d/e'? rm: remove directory '/d'? `, status 0, everything kept; with input `y\ny\nn\ny\ny\n` -> err ends `rm: remove directory '/d/e'? rm: cannot remove '/d/e': Directory not empty\n` and no prompt for `/d`, status 1.
- `RmPromptOnce`: four files, `rm -I` with `n\n` -> err `rm: remove 4 arguments? `, all kept; three files -> no prompt, all removed; `rm -rI /docs` with `n\n` -> `rm: remove 1 argument recursively? `.
- `RmLastOfForceAndInteractiveWins`: `rm -fi /notes.txt` with `n\n` prompts; `rm -if /notes.txt` removes without a prompt.
- `RmInteractiveBadWhen`: `rm --interactive=bogus /notes.txt` -> the exact six-line block above, status 1.
- `RmRefusesDotAndDotDot`: `rm -r /docs/sub/..` -> `rm: refusing to remove '.' or '..' directory: skipping '/docs/sub/..'\n`, 1, nothing removed.
- `RmPreservesRoot`: `rm -r /` -> err `rm: it is dangerous to operate recursively on '/'\nrm: use --no-preserve-root to override this failsafe\n`, 1, `/docs` still there. (Do NOT test `--no-preserve-root /`: it would empty the fixture's root.)
- `RmTrailingSlashOnAFile`: `rm /notes.txt/` -> `rm: cannot remove '/notes.txt/': Not a directory\n`.
- `RmRefusesABuiltin`: `rm /bin/ls` -> `rm: cannot remove '/bin/ls': Permission denied\n`, 1, `/bin/ls` still a builtin (`root->IsBuiltinCommand`).
- `RmRecursiveKeepsADirectoryHoldingBuiltins`: `rm -r /bin` -> status 1, err holds `rm: cannot remove '/bin/cat': Permission denied`, no line about `/bin` itself (`Contains` false for `'/bin':`), `/bin` still a directory.
- `RmBusyMountPoint`: mount an empty in-memory filesystem (`factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem()`) at `/mnt` with `root->Mount`; `rm -r /mnt` -> `rm: cannot remove '/mnt': Device or resource busy\n`, 1.
- `RmdirRemovesEmptyDirectories`, `RmdirMessages` (missing operand; `/notes.txt` -> `rmdir: failed to remove '/notes.txt': Not a directory`; `/nothing` -> `No such file or directory`; `/docs` -> `Directory not empty`; `--ignore-fail-on-non-empty /docs` -> 0, silent).
- `RmdirParentsVerbose`: `/a/b/c` made; `RunCaptured("rmdir", {"-pv", "a/b/c"})` (working directory `/`) -> out `rmdir: removing directory, 'a/b/c'\nrmdir: removing directory, 'a/b'\nrmdir: removing directory, 'a'\n`, 0.
- `RmdirParentFailure`: `/q/r` and file `/q/keep`; `rmdir -pv q/r` -> out the two `removing` lines, err `rmdir: failed to remove directory 'q': Directory not empty\n`, 1; again (recreate `/q/r`) with `--ignore-fail-on-non-empty` -> 0, no err.
- `RmdirBusyMountPoint`: as above, `rmdir /mnt` -> `rmdir: failed to remove '/mnt': Device or resource busy\n`.

`BuiltinPrompt` is covered through rm. Update the existing generic tests in
`BuiltinCommandsTest.cpp`: `ListsEveryBuiltinSortedWithAVersion` -- add
`"rm", "rmdir"` in sorted position to the expected list. The generic tests
`EveryBuiltinsHelpHasTheSameShape`, `EveryUntreatedOptionIsAcceptedAndReported`
(it runs `rm --one-file-system /docs`: must report and must not remove
anything -- it does not, without `-r`) and the `--init` template test
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` then cover the rest.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Rm*'
bash ./scripts/test_linux.sh L U
```
(The script's filter must match the test executable's name, so
`BuiltinCommands` is the narrowest it takes; the direct run narrows further.)

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the opening list of commands;
  rows for `rm` (1.0.0) and `rmdir` (1.0.0) in "The commands" table with the
  documented exceptions above; a line under "Key Classes" for `BuiltinPrompt`
  and `RemoveOperand` (shared: cp/mv use them).
- Root `CLAUDE.md`: rows for `rm` and `rmdir` in the "Builtin Commands" table;
  add `rm`, `rmdir` to the list in its first paragraph and in the directory
  tree comment for `BuiltinCommands/`.

## Acceptance

- [ ] `rm` and `rmdir` run from `/bin` in a haisosfile and with `RunCaptured`, messages byte-identical to GNU 9.4 for every case in Tests.
- [ ] Every GNU rm/rmdir option is in `Options()`; `--one-file-system` reported as not treated.
- [ ] Prompts go to stderr without a newline, answers come from descriptor 0, end of input means no.
- [ ] A builtin's path, a directory holding one, a mount point and `/` are never removed.
- [ ] All file access through `context.IO()`; no `IFileSystem`/`IHaisosOS` held.
- [ ] `BuiltinPrompt` and `RemoveOperand` exist with the exact signatures above.
- [ ] `--help` shape test, untreated-options test, `--init` template test pass; all unit tests green.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- `cp`, `mv` (coreutils--cp, coreutils--mv-touch: they reuse `BuiltinPrompt`
  and `RemoveOperand`), `find -delete` (search rock).
- Write-protection prompts (`remove write-protected regular file`): Haisos has
  no permissions.
- `--one-file-system` (not treated).
