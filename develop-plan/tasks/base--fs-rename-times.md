# Task base--fs-rename-times: Rename and SetTimes on every filesystem and IFileIO

- Rock: base
- Depends on: none
- Size: ~720 changed lines in ~22 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add Rename and SetTimes to IFileSystem and IFileIO

## Goal

`IFileSystem` and `IFileIO` gain two operations every later file command needs:

- `Rename` -- as POSIX `rename()`: moves a file or directory to a new path in
  one step, replacing a file already at the new path. Across two different
  filesystems (a mount boundary) it fails with a distinct result,
  `kFileSystemCrossDevice` (EXDEV), which `mv` answers by copy-then-remove.
- `SetTimes` -- as `utimensat()`: sets a path's access and/or modification
  time; a `nullopt` leaves that time as it is.

Every filesystem implements both (physical on Linux and Windows, the Windows
full filesystem, in-memory, read-only, sub-path, composed/mounted, device),
`ProcessFileIO` resolves both against the process's working directory, and
the test double `GatedFileSystem` passes them through. No builtin uses them
yet: `cp`, `mv`, `touch`, `sed -i`, `patch` and `tar -x` (later tasks) will.

## Context

Read first: the root `CLAUDE.md` (sections "Security: `ICurrentProcess` is the
only door out of a process" and "Creating things"),
`src/components/Filesystem/CLAUDE.md`, `src/components/HaisosOS/CLAUDE.md`
(the `ProcessFileIO` bullets), `interfaces/IFileSystemService.h`,
`interfaces/IFileIO.h`, `interfaces/IFileDescriptor.h` (the `kIO*` constants
are the model for the new result constants).

How things are today:

- Every `IFileSystem` implementation derives from `MountableFileSystem`
  (`src/components/Filesystem/MountableFileSystem.h/.cpp`). Its public
  methods are `final`: they check the filesystem's own builtins, route a path
  under a mount point to the mounted filesystem (`MountPoints::Resolve`,
  which returns the mounted `IFileSystem*` and the path inside it), and
  otherwise call a protected pure virtual `Local*` method that each subclass
  implements. Follow that pattern exactly: add `Rename`/`SetTimes` as `final`
  public methods there and `LocalRename`/`LocalSetTimes` as pure virtuals.
- Failures are reported by returning -1, with no `errno` (see
  `InMemoryFileSystem::LocalRemoveDirectory`, `PhysicalFileSystem::LocalStat`).
  Callers such as `mkdir` find out why with `Stat`. Keep that: -1 for every
  failure, except the cross-filesystem one, which gets its own constant.
- `FileSystem` (`Filesystem.h`, backends `linux/PosixFilesystem.cpp` for
  Linux and WASM, `windows/WindowsFilesystem.cpp`) is the unrooted host
  passthrough; `PhysicalFileSystem` resolves a path onto the host with
  `ResolveOnHost(path, Top::Allowed|Top::Refused, resolved)` and calls its
  `m_inner` `FileSystem`'s `Local*` method with the host path.
  `WindowsFullPhysicalFileSystem` derives from `PhysicalFileSystem` and only
  overrides `HostPathOf`, `LocalReadDirectory` and `LocalStat`, so it inherits
  the new methods with no change (a drive root is refused by `Top::Refused`).
- `InMemoryFileSystem` keeps nodes in `std::unordered_map<std::string, Node>`
  keyed by normalized path; its descriptors (`InMemoryFileDescriptor`) refer to
  the node by path, not by identity.
- The only other implementations of the interfaces: `ProcessFileIO`
  (`src/components/HaisosOS/ProcessFileIO.h/.cpp`, the one `IFileIO`) and the
  test double `GatedFileSystem` in
  `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`. There is no
  mock of either interface in `tests/mocks/`.

## Changes

### `interfaces/IFileSystemService.h`

Next to `FileDateTime`, add the result constants (the model is `kIOError` /
`kIOBrokenPipe` in `IFileDescriptor.h`):

```cpp
// What IFileSystem's (and IFileIO's) int-returning operations return on
// failure. Every failure is kFileSystemError unless telling it apart matters.
constexpr int kFileSystemError = -1;        // any failure (ENOENT, EEXIST, EACCES, ...)
constexpr int kFileSystemCrossDevice = -2;  // Rename between two different filesystems (EXDEV)
```

Add to `IFileSystem`, after `RemoveFile` (keep the doc comments; they are the
contract later tasks read):

```cpp
// Rename is the IFileSystem counterpart of the C rename() function: moves
// what is at |oldPath| to |newPath| in one step and returns 0. A file at
// newPath is replaced; a directory at newPath is replaced only by a directory,
// and only when empty. Renaming a path to itself does nothing and succeeds.
// Fails with kFileSystemError if oldPath does not exist, newPath's directory
// does not, a directory would be moved into itself or below itself, a file
// would replace a directory or a directory a file, either path is the root,
// a builtin command, a mount point or a directory holding one, or a
// directory a builtin pins (see the builtin rules below). Fails with
// kFileSystemCrossDevice -- as EXDEV, the way a caller tells it apart --
// when oldPath exists but the two paths are served by different filesystems
// (on either side of a mount point, or on two host devices); nothing is
// moved, and a caller wanting the move anyway copies and removes (as mv does).
virtual int Rename(const std::string& oldPath, const std::string& newPath) = 0;

// SetTimes is the IFileSystem counterpart of the C utimensat() function:
// sets the access time and the modification time of what is at |path|
// (following symbolic links), each left as it is when nullopt, and returns
// 0; the change time becomes now, as on POSIX, unless both are nullopt.
// Fails with kFileSystemError if nothing is at |path|, the filesystem is
// read-only, or |path| is a builtin command (its times are those of its
// placing). A device keeps its times: SetTimes on one succeeds and changes
// nothing.
virtual int SetTimes(const std::string& path,
                     const std::optional<FileDateTime>& accessTime,
                     const std::optional<FileDateTime>& modificationTime) = 0;
```

`FileDateTime` is declared above `IFileSystem` already; `<optional>` is
already included.

### `interfaces/IFileIO.h`

Add, after `RemoveFile` in "The IFileSystem operations, on resolved paths":

```cpp
// As rename(): both paths resolved against the working directory; see
// IFileSystem::Rename, including kFileSystemCrossDevice across filesystems.
virtual int Rename(const std::string& oldPath, const std::string& newPath) = 0;
// As utimensat(): see IFileSystem::SetTimes.
virtual int SetTimes(const std::string& path,
                     const std::optional<FileDateTime>& accessTime,
                     const std::optional<FileDateTime>& modificationTime) = 0;
```

The existing "A failure to reach the OS at all is reported the same way as any
other failure" sentence applies: no OS gives `kFileSystemError`.

### `src/components/Filesystem/MountPoints.h/.cpp`

Add `bool HasMountAtOrBelow(const std::string& path) const;` -- true if a
mount point is `path` itself or lies strictly below it (same
`IsAtOrUnder`-style prefix test the file already uses; `"/"` has every mount
below it). Used to refuse renaming a mount point or a directory holding one
(Linux says EBUSY).

### `src/components/Filesystem/MountableFileSystem.h/.cpp`

Public, `final`:
`int Rename(const std::string& oldPath, const std::string& newPath) final;`
`int SetTimes(const std::string& path, const std::optional<FileDateTime>& accessTime, const std::optional<FileDateTime>& modificationTime) final;`

Protected, pure virtual:
`virtual int LocalRename(const std::string& oldPath, const std::string& newPath) = 0;`
`virtual int LocalSetTimes(const std::string& path, const std::optional<FileDateTime>& accessTime, const std::optional<FileDateTime>& modificationTime) = 0;`

`Rename` logic, in order:
1. `absOld = AbsolutePathFor(oldPath)`, `absNew = AbsolutePathFor(newPath)`.
2. If `absOld == "/"` or `absNew == "/"`: `kFileSystemError`.
3. Builtins of this filesystem's own list: if `OwnBuiltinAt(absOld)` or
   `OwnBuiltinAt(absNew)` or `HasOwnBuiltinAtOrUnder(absOld)` (a pinned
   directory) or `HasOwnBuiltinAtOrUnder(absNew)`: `kFileSystemError`. A
   builtin moves only by `RemoveBuiltinCommand` + `AddBuiltinCommand`.
4. If `m_mounts.HasMountAtOrBelow(absOld)` or `m_mounts.HasMountAtOrBelow(absNew)`
   (a mount point itself, or a directory with a mount inside):
   `kFileSystemError`. Careful: a path strictly *inside* a mount is not "at or
   below a mount point" in that sense -- `HasMountAtOrBelow` asks whether a
   mount point is at the path or under it, not whether the path is under one.
5. `routeOld = m_mounts.Resolve(absOld)`, `routeNew = m_mounts.Resolve(absNew)`.
6. If `routeOld.filesystem != routeNew.filesystem` (including one null and
   the other not): `Stat(oldPath, status)`; if it fails, `kFileSystemError`
   (as rename() reports ENOENT before EXDEV); else `kFileSystemCrossDevice`.
7. Both routed to the same mounted filesystem:
   `return routeOld.filesystem->Rename(routeOld.innerPath, routeNew.innerPath);`
   (pass its result through unchanged -- a nested mount may answer
   `kFileSystemCrossDevice` itself).
8. Neither routed: `return LocalRename(oldPath, newPath);`

`SetTimes` logic: own builtin at the path -> `kFileSystemError`; routed ->
the mounted filesystem's `SetTimes(innerPath, ...)`; otherwise
`LocalSetTimes(path, ...)`. A directory that exists only as the way down to a
mount (no real directory underneath) fails with `kFileSystemError` -- it has
no times of its own to set.

### `src/components/Filesystem/Filesystem.h`, `linux/PosixFilesystem.cpp`, `windows/WindowsFilesystem.cpp`

`FileSystem` (the unrooted host passthrough) overrides both, on host paths.

POSIX (Linux and WASM):
- `LocalRename`: `::rename(old, new)`; 0 on success; on failure
  `errno == EXDEV` -> `kFileSystemCrossDevice`, anything else ->
  `kFileSystemError`. (Read errno immediately after the call.)
- `LocalSetTimes`: `::utimensat(AT_FDCWD, path, times, 0)` with
  `struct timespec times[2]`: index 0 access, 1 modification; a `nullopt`
  is `tv_nsec = UTIME_OMIT`; otherwise `tv_sec = seconds`,
  `tv_nsec = nanoseconds`. Needs `<sys/stat.h>` and `<fcntl.h>` (already
  included). If the WASM build (emscripten) lacks `utimensat`, use it under
  `#ifndef __EMSCRIPTEN__` and `::utimes` with the current times filled in
  from `stat()` for the omitted one under `#else`; do not drop WASM support.

Windows (wide-character calls, `NoCriticalErrorDialogs` and
`CrtInvalidParameterAsError` in scope like the other methods, paths through
`ToWidePath`):
- `LocalRename`: POSIX semantics on top of `MoveFileExW`:
  1. `GetFileAttributesW` on both. Old missing -> `kFileSystemError`.
  2. Same path (compare the wide strings case-insensitively with
     `CompareStringOrdinal(..., TRUE)`) -> 0 if it exists. (A case-only rename
     such as `a` -> `A` is a real rename on Windows: only an *identical* path
     returns early; compare exactly first, and when they differ only by case
     call `MoveFileExW` without the replace steps below.)
  3. New exists: if old is a directory and new is a file, or old a file and
     new a directory -> `kFileSystemError`; if both are directories, remove the
     new one with `RemoveDirectoryW` first (it fails, and so does the rename,
     when it is not empty -> `kFileSystemError`).
  4. `MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING)` (never
     `MOVEFILE_COPY_ALLOWED`: copying across volumes is the caller's
     decision). On failure `GetLastError() == ERROR_NOT_SAME_DEVICE` ->
     `kFileSystemCrossDevice`; otherwise `kFileSystemError`.
  Note `windows.h` macros: `MoveFileEx` is a macro too, so call the `W` names
  explicitly; the file already `#undef`s `CreateDirectory`/`RemoveDirectory`
  -- call `::RemoveDirectoryW` by its `W` name.
- `LocalSetTimes`: `CreateFileW(path, FILE_WRITE_ATTRIBUTES,
  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS /* directories too */, nullptr)`;
  then `SetFileTime(handle, nullptr, accessOrNull, modificationOrNull)` (a null
  pointer leaves that time alone); `CloseHandle`. Conversion to `FILETIME`
  (100 ns ticks since 1601-01-01 UTC):
  `ticks = (seconds + 11644473600) * 10000000 + nanoseconds / 100`; a time
  before 1601 -> `kFileSystemError`. Split `ticks` into
  `dwLowDateTime`/`dwHighDateTime`.

### `src/components/Filesystem/PhysicalFileSystem.h/.cpp`

`LocalRename`: resolve both with `ResolveOnHost(path, Top::Refused, ...)`
(neither the top of the filesystem nor a drive root is renamed, nor renamed
over); either failing -> `kFileSystemError`; else
`m_inner->LocalRename(hostOld, hostNew)` passed through (so a host EXDEV
inside one physical directory -- a host mount below it -- becomes
`kFileSystemCrossDevice`). `LocalSetTimes`: `ResolveOnHost(path,
Top::Allowed, ...)` then `m_inner->LocalSetTimes`. `WindowsFullPhysicalFileSystem`
needs no change; check it still compiles (it is Windows only -- read
`windows/WindowsFullPhysicalFileSystem.h` to be sure no override is missing).

### `src/components/Filesystem/InMemoryFileSystem.h/.cpp`

Both under `m_mutex`, on `NormalizeVirtualPath`d paths.

`LocalRename(old, new)`:
1. Old missing, or either is `"/"` -> `kFileSystemError`.
2. `old == new` -> 0.
3. `new` equal to `old + "/..."` (inside the old directory) -> `kFileSystemError`.
4. Parent of `new` missing or not a directory -> `kFileSystemError`.
5. New exists: file over file -> replaced; directory over directory -> only
   if the new one has no entries (no key starts with `new + "/"`), else
   `kFileSystemError`; any type mismatch -> `kFileSystemError`.
6. Move: erase the replaced node (if any); re-key the old node to `new`, and
   for a directory every node whose key starts with `old + "/"` to
   `new + key.substr(old.size())`. Re-key by collecting the keys first, then
   extracting and re-inserting (`std::unordered_map::extract` + `key()` is
   fine in C++17) -- never insert while iterating.
7. The moved node's `changeTime` = now (rename updates ctime on Linux);
   `TouchDirectory` both parents (one call if they are the same).

Descriptors follow their path, not their file (see "Context"): after a
rename a descriptor open on the old path finds nothing (`kIOError`), as one
on a removed file does already, and one open on the replaced target reads the
moved file. Document this in a comment on `LocalRename` and in
`src/components/Filesystem/CLAUDE.md`; do not redesign the descriptors.

`LocalSetTimes`: node missing -> `kFileSystemError`; set `accessTime` /
`modificationTime` from what is given; `changeTime` = now unless both are
nullopt. 0.

### `src/components/Filesystem/ReadOnlyFileSystem.h/.cpp`

Both `Local*` return `kFileSystemError`.

### `src/components/Filesystem/SubFileSystem.h/.cpp`

`LocalRename` -> `m_root->Rename(ResolveInRoot(old), ResolveInRoot(new))`;
`LocalSetTimes` -> `m_root->SetTimes(ResolveInRoot(path), ...)`. Results
passed through unchanged.

### `src/components/Filesystem/ComposedFileSystem.h/.cpp`

Same, delegating to `m_main` with `AbsolutePathFor(...)` (protected, as its
other `Local*` are). Its mounted overlay is handled by `MountableFileSystem`
before these are reached, so renaming from `main` into the overlay is
`kFileSystemCrossDevice` by step 6 above.

### `src/components/Filesystem/DeviceFileSystem.h/.cpp`

`LocalRename` -> `kFileSystemError` (nothing moves in `/dev`).
`LocalSetTimes` -> 0 if `LocalStat` finds the path (the root or a device),
changing nothing (devices keep their times, as the doc comment says);
`kFileSystemError` otherwise.

### `src/components/HaisosOS/ProcessFileIO.h/.cpp`

Override both, as `RemoveFile` is: `auto fs = RootFileSystem();` null ->
`kFileSystemError`; else `fs->Rename(ResolvePath(oldPath), ResolvePath(newPath))`
/ `fs->SetTimes(ResolvePath(path), accessTime, modificationTime)`.
This is the only way a process reaches these operations -- through
`ICurrentProcess::IO()` -- and the root `CLAUDE.md` rule stands: nothing in a
process gets an `IFileSystem`; `IFileIO` still has no `Mount`/`Unmount` and
must not gain them.

### `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`

`GatedFileSystem` passes both through to `m_inner` (it implements
`IFileSystem` directly, so it will not compile otherwise).

Grep the whole tree (`grep -rn "public IFileSystem\|public IFileIO" src tests`)
after the change: every implementation must override both.

## Tests

New file `tests/unit/components/Filesystem.unittests/FilesystemRenameTest.cpp`,
added to `add_executable(Filesystem.unittests ...)` in that directory's
`CMakeLists.txt`. Suite names must contain `Filesystem` (the runner's filter
matches both the executable name and the gtest name). Use
`InMemoryFileSystem::Create()` and the helpers style of `MountTest.cpp`
(write a file, `ReadWholeFile`), and for physical cases a temporary directory
the way `PhysicalFileSystemTest.cpp`'s fixture makes one (copy its
`SetUp`/`TearDown` pattern into a fixture `FilesystemPhysicalRenameTest`).

Suite `FilesystemRenameTest` (in-memory unless said):
- `RenamesAFileAndItsContentFollows` -- `/a.txt` "x" -> `/b.txt`: 0; `/b.txt`
  reads "x"; `Stat("/a.txt")` fails.
- `ReplacesAnExistingFile` -- `/a` "1", `/b` "2"; rename a -> b: 0; `/b` reads "1".
- `RenamesADirectoryWithEverythingInIt` -- `/d/e/f.txt`; rename `/d` -> `/g`:
  `/g/e/f.txt` readable; `ReadDirectory("/")` lists `g`, not `d`.
- `ReplacesOnlyAnEmptyDirectory` -- dir over empty dir: 0; dir over non-empty
  dir: -1 (`kFileSystemError`), both unchanged.
- `RefusesTypeMismatches` -- file over dir and dir over file: -1.
- `RefusesMovingADirectoryIntoItself` -- `/d` -> `/d/sub`: -1.
- `FailsForAMissingSourceOrParent` -- missing old: -1; new under a missing
  directory: -1.
- `RenamingToItselfSucceeds` -- 0, content unchanged.
- `MovesTheChangeTimeAndBothDirectoriesTimes` -- after rename, the moved
  file's `changeTime` and both parents' `modificationTime` are >= a time
  taken before the call.
- `AcrossAMountIsCrossDevice` -- host with `/m` mounted (another in-memory
  fs): rename `/a` -> `/m/a`: `kFileSystemCrossDevice`, `/a` still there;
  `/m/x` -> `/y`: `kFileSystemCrossDevice`; a missing source across the mount:
  `kFileSystemError`.
- `WithinAMountIsRoutedThere` -- `/m/x` -> `/m/y`: 0, and the mounted
  filesystem itself now has `/y`.
- `AcrossAComposedOverlayIsCrossDevice` -- through
  `ComposedFileSystem::Create(main, "/m", overlay)`: main -> overlay is
  `kFileSystemCrossDevice`; main -> main is 0 and lands on `main`.
- `MountPointsAndTheirParentsDoNotMove` -- renaming `/m` (a mount point) or
  `/p` with a mount at `/p/m`: -1.
- `BuiltinsAndPinnedDirectoriesDoNotMove` -- `AddBuiltinCommand("/bin/ls",
  "ls")` (after `CreateDirectory("/bin")`): rename `/bin/ls` -> `/x`, `/bin`
  -> `/b`, and `/x` (a file) -> `/bin/ls`: all -1.
- `ReadOnlyAndDeviceRefuse` -- `ReadOnlyFileSystem::Create(inner)` rename: -1,
  inner unchanged; `DeviceFileSystem` (create it as `DeviceFileSystemTest.cpp`
  does) `null` -> `x`: -1.
- `ASubFileSystemRenamesWithinItsBase` -- `SubFileSystem::Create(root,
  "/base")`: rename `a` -> `b` inside it moves `/base/a` to `/base/b` of root;
  `../a` stays confined (resolves to `/a` of the sub, i.e. `/base/a`).

Suite `FilesystemSetTimesTest`:
- `SetsBothTimes` -- in-memory file; SetTimes with access {100, 5} and
  modification {200, 6}: 0; `Stat` reports exactly those; `changeTime` >= a
  time taken before the call.
- `NulloptLeavesATimeAlone` -- set only modification; access unchanged.
- `FailsForAMissingPathABuiltinAndReadOnly` -- each -1.
- `ADeviceKeepsItsTimes` -- `SetTimes("/null", ...)`: 0, `Stat` unchanged;
  `/nope`: -1.
- `IsRoutedThroughMountsAndSubFileSystems` -- set through a mount and through
  a `SubFileSystem`; the underlying filesystem's `Stat` shows the times.

Fixture `FilesystemPhysicalRenameTest` (temp directory, `PhysicalFileSystem::Create(dir)`):
- `RenamesAndReplacesOnDisk` -- file rename, replace, directory rename;
  check with `std::filesystem::exists` and by reading back.
- `ReplacesAnEmptyDirectoryOnly` -- as on POSIX (this is the Windows
  emulation's test too).
- `RefusesTheRootAndClimbingOut` -- rename `/` -> `/x`, `/a` -> `../a`: -1.
- `SetsTimesToTheNanosecondOnLinux` -- SetTimes {1700000000, 123456789} for
  both; `Stat` gives exactly that on Linux; on Windows (`#ifdef _WIN32`)
  whole seconds (`_stat64` has no more).
- `AcrossTwoPhysicalFileSystemsMountedTogetherIsCrossDevice` -- two
  subdirectories of the temp dir, one mounted into the other's
  `PhysicalFileSystem` at `/m`: `kFileSystemCrossDevice`.

In `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`, next to
`FileIOReadsAndWritesThroughTheWorkingDirectory`:
- `TEST_F(HaisosOSTest, FileIORenamesAndSetsTimesFromTheWorkingDirectory)` --
  process started in `sub` (as that test does); write `a.txt` through `IO()`;
  `IO()->Rename("a.txt", "b.txt")` is 0 and `/sub/b.txt` exists on disk;
  `IO()->SetTimes("b.txt", FileDateTime{1000000000, 0}, std::nullopt)` is 0
  and `IO()->Stat("b.txt")` reports access 1000000000; clean up the file.

In `tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp`:
- `TEST(HaisosOSDescriptorTableTest, RenameAndSetTimesFailWithoutAnOS)` -- as
  `OpenFileFailsWithoutAnOS`: both return `kFileSystemError`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Filesystem
bash ./scripts/test_linux.sh L U HaisosOS
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/Filesystem/CLAUDE.md`: a "Responsibilities" bullet for
  `Rename` (rename() semantics, the refusals, `kFileSystemCrossDevice` across
  mounts and how `MountableFileSystem` decides it, in-memory descriptors
  following their path) and `SetTimes` (utimensat() semantics, builtins
  refused, devices keep their times, Windows `SetFileTime` with whole-second
  `Stat`); mention `MountPoints::HasMountAtOrBelow`.
- `src/components/HaisosOS/CLAUDE.md`: the `ProcessFileIO` bullet lists
  `Rename`/`SetTimes` among the operations resolved against the working
  directory.
- Root `CLAUDE.md`: in the "Architecture" Filesystem row, add "rename and
  set-times" to what it provides; nothing else changes.

## Acceptance

- [ ] `IFileSystem` and `IFileIO` declare `Rename` and `SetTimes` with the
      exact signatures above; `kFileSystemError` and `kFileSystemCrossDevice`
      exist in `interfaces/IFileSystemService.h`.
- [ ] Every implementation overrides both (`MountableFileSystem` final +
      `Local*` in `FileSystem`, `PhysicalFileSystem`, `InMemoryFileSystem`,
      `ReadOnlyFileSystem`, `SubFileSystem`, `ComposedFileSystem`,
      `DeviceFileSystem`; `ProcessFileIO`; `GatedFileSystem`).
- [ ] Rename across a mount or composed overlay returns
      `kFileSystemCrossDevice` and moves nothing; a missing source returns
      `kFileSystemError` first.
- [ ] Builtins, pinned directories, mount points, the root: never renamed.
- [ ] No `errno` contract introduced; no recursion added; no new class
      without `Create()` (none is needed).
- [ ] Windows code compiles in principle: only `W` calls, macros respected,
      `FILETIME` conversion as specified (the container cannot build it; CI
      will).
- [ ] All the listed tests exist and pass; the full unit suite passes.

## Out of scope

- Any builtin using these (`cp`, `mv`, `touch`, `sed -i`, `patch`, `tar`):
  their own tasks.
- Making in-memory descriptors follow their file across rename/unlink.
- Symbolic links, permissions, `renameat2` flags (`RENAME_NOREPLACE`): GNU
  `mv -n` is done by the caller with `Stat`.
- An `os_*` tool or Lua global for rename.
