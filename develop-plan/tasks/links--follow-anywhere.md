# Task links--follow-anywhere: Follow symbolic links anywhere

- Rock: links
- Depends on: none
- Size: ~350-450 changed lines (mostly removals) in ~16 files
- Plan checked against: develop @ 1bfc5be
- PR title: Follow symbolic links on physical filesystems wherever they lead

## Goal

Symbolic links -- and, on Windows, junctions and WSL symlinks -- inside a
disk-backed filesystem (`PhysicalFileSystem`, and `WindowsFullPhysicalFileSystem`
which derives from it) are followed wherever they lead, exactly as the host's
own calls follow them, including out of the filesystem's root directory.
Haisos keeps no link-specific code at all:

- no check that a resolved path stays within the root (`CanonicalWithin`,
  `IsWithin`), no `weakly_canonical` per call;
- no refusal of a path ending in a dangling link: opened with `O_CREAT`, the
  link's target is created wherever it points, as `open()` does on Linux;
- no `O_NOFOLLOW`;
- no `Follow`/`Keep` resolution modes: every path is handed to the host as
  the root joined with its segments, and `unlink()`, `rmdir()`, `mkdir()`,
  `open()`, `stat()` do with a link whatever the host does;
- no `FileStatus::symbolicLink`, no `FileSystem::IsLink`;
- `DELETE` treats a link like whatever it points at: a link to a file is
  removed itself (the host's `unlink()` never removes a target), a link to a
  directory is descended into -- the target's contents are removed, wherever
  it lies -- and then the link itself is removed.

What stays, because it is about paths **as written**, not links:

- `SplitVirtualPath` refusing a `..` that climbs above the top;
- `IsPlainHostName` for every segment; `FileSystem::IsDevicePath` on Windows;
- the top of the filesystem (and, in `WindowsFullPhysicalFileSystem`, a
  drive's root) can be neither removed (`RemoveFile`, `RemoveDirectory`) nor
  created (`CreateDirectory`) -- this keeps `DELETE` of a physical mount point
  from deleting the host directory itself;
- the root path's canonicalization in the `PhysicalFileSystem(const std::string&)`
  constructor (it is done once, harmless, and keeps the logs readable).

The user decided this (see `develop-plan/goal.md`, Clarifications): whoever
declares a physical filesystem vouches for the links in it.

## Context

Read first: the root `CLAUDE.md` (sections "The `haisosfile` DSL", the
`PHYSICAL` paragraph, and "Security: `ICurrentProcess` is the only door out of
a process"), `src/components/Filesystem/CLAUDE.md`, and then:

- `src/components/Filesystem/PhysicalFileSystem.h` / `.cpp` -- `ResolveOnHost`
  (the one place a path becomes a host path), `LastComponent`,
  `CanonicalWithin`, `IsWithin`, `IsSymbolicLink`, `kOpenNoFollow`, and the
  `Local*` operations that call them.
- `src/components/Filesystem/windows/WindowsFullPhysicalFileSystem.h` / `.cpp`
  -- overrides `HostPathOf` and `IsWithin`.
- `src/components/Filesystem/Filesystem.h`, `linux/PosixFilesystem.cpp`,
  `windows/WindowsFilesystem.cpp` -- `FileSystem::IsLink` (and, in the Windows
  file, the `IO_REPARSE_TAG_LX_SYMLINK` define used only by it).
- `interfaces/IFileSystemService.h` -- `FileStatus::symbolicLink` and the
  comments on `CreateDirectory`, `RemoveDirectory`, `RemoveFile`,
  `ReadDirectory`, `Stat` that talk about links.
- `src/haisos/HaisosFileOperations.cpp` -- `RemoveTree` (the `DELETE`
  directive).
- Tests: `tests/unit/components/Filesystem.unittests/PhysicalFileSystemTest.cpp`,
  `.../WindowsFullPhysicalFileSystemTest.cpp`,
  `tests/unit/haisos/haisos.unittests/HaisosFileOperationsTest.cpp`,
  `.../HaisosFileSystemBuilderTest.cpp`.

No other code reads `symbolicLink` or calls `IsLink` (check with
`grep -rn "symbolicLink\|IsLink\|IsSymbolicLink\|IsWithin\|CanonicalWithin\|LastComponent\|kOpenNoFollow\|LX_SYMLINK" src interfaces tests`
-- after the task it must find nothing). The composing filesystems
(read-only, sub-path, composed, mounted) copy `FileStatus` whole and never name
the field.

## Changes

### `interfaces/IFileSystemService.h`

- Remove the field `bool symbolicLink = false;` from `FileStatus`, with its
  comment.
- Rewrite the link sentences of the method comments so they describe the
  host's behaviour, not Haisos's:
  - `CreateDirectory`: "the counterpart of `mkdir()`" -- drop the sentence
    about not following a link (the host's `mkdir()` already fails on any
    existing path, a link included).
  - `RemoveDirectory`: counterpart of `rmdir()`; drop "never removes a
    directory through a symbolic link ..." (the host decides: POSIX `rmdir()`
    fails on a link, Windows removes a directory link or junction itself).
  - `RemoveFile`: keep "removes a file, never a directory"; replace the link
    sentence with: on a disk, a symbolic link is removed itself, as `unlink()`
    does.
  - `ReadDirectory`: keep that links are followed (a link to a directory lists
    as a directory); drop "Stat tells the two apart (FileStatus::symbolicLink)".
  - `Stat`: "Symbolic links on a real disk are followed, as by `stat()`"; drop
    the `symbolicLink` sentence.
  - Add, near the top of the `IFileSystem` comment or on `Stat`, one sentence:
    on a disk-backed filesystem symbolic links are followed wherever they
    lead, out of the filesystem's directory included -- whoever creates the
    filesystem vouches for the links in it.

### `src/components/Filesystem/PhysicalFileSystem.h`

- Class comment: replace the paragraph "Symbolic links already on the disk are
  followed only while they stay within the root ..." with: symbolic links (on
  Windows also junctions) already on the disk are followed wherever they lead,
  as the host follows them -- out of the root included; whoever creates the
  filesystem vouches for them. Paths **as written** stay confined: a `..`
  climbing above the top is refused, each segment must be a plain host name,
  and the top itself can be neither removed nor created. Nothing done through
  here creates a link. Fix the first paragraph too ("every path passed in is
  resolved and validated to stay within rootPath" -> placed under rootPath, a
  `..` climbing above it refused).
- Protected default constructor comment: "it overrides HostPathOf" (no more
  `IsWithin`).
- Remove `virtual bool IsWithin(const std::string& canonical) const;`.
- Remove `enum class LastComponent`, `CanonicalWithin`, `IsSymbolicLink`.
- `ResolveOnHost` becomes:
  ```cpp
  // What ResolveOnHost does with the top of the filesystem (no segments, or a
  // drive's root): reading and opening it is fine, removing or creating it
  // is not.
  enum class Top { Allowed, Refused };
  // The one place a path handed to this filesystem becomes a real one: splits
  // |pathname|, checks every segment, and places it on the host (HostPathOf).
  // Nothing on the disk is consulted: links on the way, and at the end, are
  // left to the host call that uses |resolved|. Returns false (leaving
  // |resolved| untouched) if the path is invalid, climbs above the top, lies
  // nowhere on the host, names a device, or is the top while |top| is Refused.
  bool ResolveOnHost(const std::string& pathname, Top top, std::string& resolved) const;
  ```

### `src/components/Filesystem/PhysicalFileSystem.cpp`

- Remove `kOpenNoFollow` and the `#else #include <fcntl.h>` branch that exists
  only for it (keep the `#ifdef _WIN32` include of
  `windows/WindowsFullPhysicalFileSystem.h`). Check nothing else in the file
  needs `<fcntl.h>`.
- Remove `IsWithin`, `CanonicalWithin`, `IsSymbolicLink`.
- `ResolveOnHost(pathname, top, resolved)`, step by step:
  1. `NoCriticalErrorDialogs noDialogs;` (keep).
  2. `SplitVirtualPath(pathname, segments)`, refusing as today (log line
     "path escapes root" may stay -- it still means a `..` above the top).
  3. `IsPlainHostName` on every segment, as today.
  4. In the existing `try`: `HostPathOf(segments, hostPath)` (false -> as
     today), then the `FileSystem::IsDevicePath` refusal, as today.
  5. If `top == Top::Refused` and (`segments.empty()` or
     `hostPath.filename().empty()`): log at debug "refusing to act on the root
     itself", return false (this is today's check in the `Keep` branch,
     unchanged).
  6. `resolved = Utf8FromPath(hostPath); return true;`
  Delete the check-then-use (TOCTOU) comment block: there is nothing left to
  race.
- `LocalOpenFile` (both overloads), `LocalReadDirectory`, `LocalStat`:
  `ResolveOnHost(path, Top::Allowed, resolved)`; pass `flags` unchanged (no
  `| kOpenNoFollow`). `LocalStat` no longer sets anything after
  `m_inner->LocalStat`.
- `LocalCreateDirectory`, `LocalRemoveDirectory`, `LocalRemoveFile`:
  `ResolveOnHost(path, Top::Refused, resolved)`. Shorten their comments to the
  host call each one is (`mkdir()`, `rmdir()`, `unlink()`), without the link
  discussion.
- Remove the comment above `LocalOpenFile` about `O_NOFOLLOW`.

### `src/components/Filesystem/windows/WindowsFullPhysicalFileSystem.h` / `.cpp`

- Remove the `IsWithin` override (declaration and definition, with its
  comment). Update the class comment if it mentions `IsWithin` or links
  leading between drives.

### `src/components/Filesystem/Filesystem.h`, `linux/PosixFilesystem.cpp`, `windows/WindowsFilesystem.cpp`

- Remove `static bool IsLink(const std::string& hostPath);` with its comment,
  and both definitions. In `WindowsFilesystem.cpp` also remove the
  `IO_REPARSE_TAG_LX_SYMLINK` `#ifndef`/`#define` block and its comment (used
  only by `IsLink`), and any include or helper that becomes unused. Keep
  `IsDevicePath` and `NoCriticalErrorDialogs` exactly as they are.

### `src/haisos/HaisosFileOperations.cpp` -- `RemoveTree`

New behaviour, with no knowledge of links (only `Stat`, `ReadDirectory`,
`RemoveFile`, `RemoveDirectory`):

1. `isDirectory = fs.Stat(path, status) == 0 && status.type == DirectoryEntryType::Dir`
   (`Stat` follows links, so a link to a directory counts as a directory).
2. Not a directory (a file, a link to a file, or something that cannot be
   stat'ed, e.g. a dangling link): `RemoveFile`; if that fails,
   `RemoveDirectory` (on Windows a dangling directory link is removed by
   `rmdir`); if both fail, `outReason = "cannot remove file " + path`.
3. A directory: `RemoveTree` every entry except `.` and `..` (as today), then
   `RemoveDirectory(path)`; if that fails, `RemoveFile(path)` (on POSIX a link
   to a directory is not removed by `rmdir()` but by `unlink()`); if both
   fail, `outReason = "cannot remove directory " + path`.
Rewrite its comment: removes what is there the way `rm -rf` would if it
followed links to directories -- a link to a directory is descended into, so
whatever it points at is emptied, wherever it lies, and then the link itself is
removed; a link to a file is removed itself.

### `src/haisos/HaisosFileSystemBuilder.cpp`

- `TakePhysicalDirectory`'s comment: keep that the directory becomes a
  filesystem of its own (`CreatePhysicalFileSystem`) rather than a
  `SubFileSystem` of the full one, but give the reason that remains: on
  Windows a UNC directory (`\\server\share\x`, or a relative path under a
  `\\wsl.localhost\...` current directory) is on no drive, so the full
  filesystem does not hold it. Drop the sentences about links staying inside.
  No code change.

### `interfaces/IFactory.h`

- `CreatePhysicalFileSystem` comment: replace "A symbolic link inside the
  directory is followed only while it stays inside -- which a SubFileSystem of
  the full filesystem would not ensure." with: symbolic links inside it are
  followed wherever they lead; the caller vouches for them. "jailed to" in the
  first line becomes "rooted at" (a `..` above the root is still refused).

## Tests

All POSIX link tests sit inside the existing `#ifndef _WIN32` block; the
Windows one inside its `#ifdef _WIN32` block. Keep the fixtures.

### `tests/unit/components/Filesystem.unittests/PhysicalFileSystemTest.cpp`

- Rewrite the comment block above `PhysicalFileSystemLinkTest`: links already
  on the disk are followed wherever they lead, as the host follows them;
  nothing done through a `PhysicalFileSystem` creates one.
- `NothingIsCreatedThroughADanglingLinkToOutsideTheRoot` -> rename to
  `ADanglingLinkIsFollowedWhenCreating`: link `Root()/notes.txt ->
  Outside()/created.txt`; `OpenFile("/notes.txt", O_WRONLY|O_CREAT|O_TRUNC,
  S_IRUSR|S_IWUSR)` returns `>= 0`; write `"hi"`, close; `Outside()/created.txt`
  exists and holds `hi`; `Root()/notes.txt` is still a symlink.
- Delete `ADanglingLinkPointingInsideIsRefusedToo`.
- Delete `NoDirectoryIsCreatedThroughADanglingLink`.
- `ALinkToOutsideTheRootCannotBeReadEvenBehindDotDot` -> rename to
  `ALinkToOutsideTheRootIsFollowed`: `Outside()/secret.txt` = `"secret"`, link
  `Root()/secret_link -> Outside()/secret.txt`: reading `secret_link` gives
  `secret`. Also a directory link `Root()/out -> Outside()`:
  `ReadDirectory("/out")` lists `secret.txt`; writing `/out/new.txt` (`O_CREAT`)
  creates `Outside()/new.txt`; `Stat("/out", ...)` returns 0 with type `Dir`.
- Keep `ALinkWithinTheRootIsFollowed` as is.
- Keep `RemoveFileRemovesALinkNotWhatItPointsAt` and
  `RemoveDirectoryNeverRemovesThroughALink` as they are (they now document the
  host's `unlink()`/`rmdir()`, and must still pass).
- Delete `StatSaysWhetherAPathIsALink`.
- Keep `TheRootItselfCanBeNeitherRemovedNorCreated` as is.
- Keep `TraversalOutsideRootIsRejected` (in `PhysicalFileSystemTest`) as is.
- Windows, `AJunctionIsALink` -> rename to `AJunctionIsFollowed`: same setup
  (`mklink /J`, skip when it fails); `Opens(*fs, "/out/secret.txt")` is now
  **true** for the filesystem rooted at `jail`; drop the `symbolicLink`
  expectations; keep `whole->RemoveDirectory("/jail/out") == 0` removing the
  junction only (`outside/secret.txt` still exists). Update its comment.

### `tests/unit/components/Filesystem.unittests/WindowsFullPhysicalFileSystemTest.cpp`

- Remove the two `EXPECT_FALSE(status.symbolicLink);` lines.

### `tests/unit/haisos/haisos.unittests/HaisosFileOperationsTest.cpp`

- `DeleteRemovesALinkNotWhatItPointsAt` -> rename to
  `DeleteFollowsALinkToADirectory`: same setup but without `file_link` (keep
  `work/link -> ../important`, `important/keep.txt`, `work/sub/file.txt`,
  `work/dangling`); `DELETE /work` succeeds; `work` no longer exists (check
  with `symlink_status`); `important` still exists as a directory and is
  empty (`keep.txt` gone). Comment: the accepted consequence -- a link to a
  directory is followed and its target emptied.
- `DeleteOfALinkRemovesTheLinkOnly` -> rename to
  `DeleteOfALinkToADirectoryEmptiesItsTarget`: `shortcut -> important`;
  `DELETE /shortcut` succeeds; `shortcut` is gone; `important` exists and is
  empty.
- Add `DeleteOfALinkToAFileRemovesTheLinkOnly`: `keep.txt` = `precious`,
  `file_link -> keep.txt`; `DELETE /file_link` succeeds; the link is gone,
  `keep.txt` still reads `precious`.

### `tests/unit/haisos/haisos.unittests/HaisosFileSystemBuilderTest.cpp`

- `ALinkInsideAPhysicalDirectoryCannotLeadOutOfIt` -> rename to
  `ALinkInsideAPhysicalDirectoryIsFollowed`: same `sub/up -> ..` link and
  `FS data PHYSICAL ./sub`; `ReadAll(*fs, "/marker.txt") == "sub"` and
  `ReadAll(*fs, "/up/marker.txt") == "root"` (the fixture writes `root` to
  `kTestDir/marker.txt`). Drop the `SubFileSystem` comparison and rewrite the
  comment above it.

### Commands

```bash
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Filesystem
bash ./scripts/test_linux.sh L U haisos.unittests
bash ./scripts/test_linux.sh L U
```

(The filter matches unit-test executable names: `Filesystem` selects
`Filesystem.unittests` and `FileSystemService.unittests`; `haisos.unittests`
selects the haisos executable.) The Windows tests are compiled and run by CI.

## Docs

- Root `CLAUDE.md`:
  - The `DELETE` line of the DSL example: `# remove a file, or a directory and
    everything in it (a link to a directory is followed: its target is emptied, the link removed)`.
  - The `PHYSICAL` paragraph: replace "jailed there -- not as a `SubFileSystem`
    of the full filesystem, which confines paths only as written, so a
    symbolic link inside would lead anywhere on the disk." with: rooted there
    (a `..` cannot climb above it), and symbolic links inside it are followed
    wherever they lead, as the host follows them -- whoever declares the
    filesystem vouches for the links in it.
  - "Security: `ICurrentProcess` is the only door out of a process": add a
    short paragraph -- a physical filesystem is only as narrow as the links
    inside it, since they are followed wherever they lead; nothing in Haisos
    creates a link, and no builtin, tool or directive may create one on a
    physical filesystem unless a confinement of links comes back with it.
  - The Components table row of **Filesystem**: "a `PhysicalFileSystem`
    rooted at a real disk path" (instead of "jailed to").
- `src/components/Filesystem/CLAUDE.md`:
  - The `Stat` bullet (around "A symbolic link is followed, as by `stat()`,
    and `symbolicLink` says ..."): keep "followed, as by `stat()`"; drop the
    rest.
  - The `FileSystem::IsLink` sentence in the platform bullet: remove.
  - The `PhysicalFileSystem` bullet: rewrite the part from "canonicalized, and
    checked to lie within (`IsWithin`)" to the end -- the segments are placed
    on the host (`HostPathOf`) and handed to it as they are; links on the disk
    are followed wherever they lead (whoever creates the filesystem vouches for
    them); the top can be neither removed nor created; it is a filesystem of
    its own rather than a `SubFileSystem` of the full one because of UNC paths
    on Windows. Drop the sentences about `weakly_canonical`, dangling links,
    `O_NOFOLLOW`, the check-then-use window, `Keep`, and `FileStatus::symbolicLink`.
  - The `WindowsFullPhysicalFileSystem` bullet: drop "and `IsWithin` (anywhere
    on a drive, so a link may lead from one drive to another, but not to a
    share)".
  - The `SubFileSystem` bullet: keep that it is purely lexical; replace "--
    which is why a directory of the disk is jailed with a `PhysicalFileSystem`
    of its own instead" with nothing (or: links under it are followed as the
    filesystem beneath follows them).
- Do not touch `notes/` (the plan session updates
  `notes/note-physical-directory-jail.md`).

## Acceptance

- [ ] `grep -rn "symbolicLink\|IsLink\|IsSymbolicLink\|IsWithin\|CanonicalWithin\|LastComponent\|kOpenNoFollow\|O_NOFOLLOW\|LX_SYMLINK" src interfaces tests` finds nothing.
- [ ] `PhysicalFileSystem::ResolveOnHost` consults nothing on the disk (no
      `weakly_canonical`, `exists`, `symlink_status` or `lstat` in it); the
      constructor's canonicalization of the root is the only one left.
- [ ] `..` above the top is still refused (`TraversalOutsideRootIsRejected`
      passes), and the top can be neither removed nor created
      (`TheRootItselfCanBeNeitherRemovedNorCreated` passes).
- [ ] A link to outside the root is read, listed and written through; a
      dangling link is created through (the renamed tests).
- [ ] `DELETE` of a directory holding a link to a directory empties the target
      and removes the link, and succeeds; `DELETE` of a link to a file removes
      only the link.
- [ ] No new interface, class or file; `FileStatus` lost exactly one field.
- [ ] Linux build green; `bash ./scripts/test_linux.sh L U` all green.
- [ ] Root `CLAUDE.md`, `src/components/Filesystem/CLAUDE.md`,
      `interfaces/IFactory.h` and `interfaces/IFileSystemService.h` no longer
      say links are confined, and the root `CLAUDE.md` Security section has
      the "no link may be created" rule.

## Out of scope

- Creating links (an `ln` builtin, a tool, a directive).
- `ls`: showing links as links (`l`, `-> target`), or stopping `ls -R` from
  descending into links.
- `pwd -P` / `-L`, and the comment in
  `src/components/BuiltinCommands/commands/Pwd.cpp`.
- `SubFileSystem`, `ReadOnlyFileSystem`, in-memory, `/dev`, mounts,
  `PhysicalPath.h`, `TakePhysicalDirectory`'s code.
- `notes/`, `HAISOS_VERSION`, `.claude/`, `scripts/`.
