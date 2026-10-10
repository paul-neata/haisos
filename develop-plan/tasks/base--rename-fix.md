# Task base--rename-fix: a failed Windows rename leaves the target in place

- Rock: base
- Depends on: base--fs-rename-times
- Size: ~150-200 changed lines in ~4 files
- Plan checked against: develop @ 4321c31
- PR title: Keep the target directory when a Windows rename fails

## Goal

`IFileSystem::Rename` on a physical filesystem behaves as rename() on
Windows too: **when it fails, nothing has changed.** Today
`FileSystem::LocalRename` in `src/components/Filesystem/windows/WindowsFilesystem.cpp`
removes an existing empty target directory (`RemoveDirectoryW`) before
`MoveFileExW`; if the move then fails, the target is gone. Two ways it fails:

1. **A directory moved below itself**: `/d` -> `/d/sub`, with `/d/sub` an
   empty directory. rename() refuses it (EINVAL) and changes nothing; Windows
   today removes `/d/sub`, then `MoveFileExW` fails.
2. **Any other `MoveFileExW` failure** after the target was removed: a file
   open inside the source directory (a sharing violation / access denied), a
   cross-volume move (`ERROR_NOT_SAME_DEVICE`), a permission error.

`coreutils--mv-touch` (`mv`) relies on this: a failed `mv` must not lose the
user's directory.

The task also clears two low findings of the base--fs-rename-times review
(`develop-plan/reviews/base--fs-rename-times.md`): `ToFileTime`'s int64
overflow, and a test comment left above the wrong test.

## Context

Read first: the root `CLAUDE.md` (Security: "A physical filesystem is only as
narrow as the links inside it"), `src/components/Filesystem/CLAUDE.md` (the
`Rename` and `SetTimes` bullets).

What exists (merged in base--fs-rename-times, #43):
- `interfaces/IFileSystemService.h`: `IFileSystem::Rename(oldPath, newPath)`,
  `kFileSystemError` (-1), `kFileSystemCrossDevice` (-2).
- `MountableFileSystem::Rename` (`src/components/Filesystem/MountableFileSystem.cpp`)
  makes the generic refusals (root, builtins, mount points, cross-filesystem)
  and calls `LocalRename`. It does **not** check "a directory into itself":
  `InMemoryFileSystem::LocalRename` does that itself, and on Linux rename()
  does it (`src/components/Filesystem/linux/PosixFilesystem.cpp`).
- `PhysicalFileSystem::LocalRename` resolves both paths to host paths and
  calls `FileSystem::LocalRename` (the Windows or POSIX one) with them.
- Windows `FileSystem::LocalRename`: identical path -> no-op; case-only
  rename -> `MoveFileExW(..., 0)`; otherwise type checks, `RemoveDirectoryW`
  of an empty target directory, then
  `MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING)`, mapping
  `ERROR_NOT_SAME_DEVICE` to `kFileSystemCrossDevice`.
- `ToFileTime` (same file, anonymous namespace) converts a `FileDateTime` to a
  `FILETIME` and is used by `FileSystem::LocalSetTimes`.
- Tests: `tests/unit/components/Filesystem.unittests/FilesystemRenameTest.cpp`,
  fixture `FilesystemPhysicalRenameTest` (a scratch directory
  `kPhysicalRoot` under the temp directory; helpers `WriteFile`,
  `ReadHostFile`).

**The container builds Linux only.** `WindowsFilesystem.cpp` is not compiled
there: write it carefully against the Win32 API (headers already included:
`<windows.h>`), keep to calls the file already uses or that are plain Win32
(`GetFullPathNameW`, `GetFileAttributesExW`, `CreateDirectoryW`,
`CreateFileW`, `SetFileTime`, `CloseHandle`, `CompareStringOrdinal`).
Windows CI checks it; Windows fixes happen on the host after review.

## Changes

### `src/components/Filesystem/windows/WindowsFilesystem.cpp`

In `FileSystem::LocalRename`, after the `oldAttrs` check and before any
replace step:

1. **Refuse a directory moved to itself or below itself.** When the source is
   a directory: take the full paths of both (`GetFullPathNameW`; on failure
   return `kFileSystemError`), turn every `/` into `\`, drop a trailing `\`
   (except a drive root's), and return `kFileSystemError` -- having touched
   nothing -- when the new path, compared case-insensitively
   (`CompareStringOrdinal(..., TRUE)` on the prefix), equals the old one or
   starts with the old one followed by `\`. (The identical and case-only
   cases above return before this, so "equals" here only catches a different
   spelling of the same path, e.g. `C:\x\d` vs `C:/x/d/`.) A small helper in
   the anonymous namespace, e.g.
   `bool IsSameOrBelow(const std::wstring& path, const std::wstring& base)`.

2. **Keep the target if the move fails.** When the target is an empty
   directory that is removed to make room:
   - before `RemoveDirectoryW`, read its times with
     `GetFileAttributesExW(newWide, GetFileExInfoStandard, &data)`
     (`ftCreationTime`, `ftLastAccessTime`, `ftLastWriteTime`) and its
     attributes (`data.dwFileAttributes`);
   - if `MoveFileExW` then fails: save `GetLastError()` first, then
     recreate the directory (`CreateDirectoryW(newWide, nullptr)`), put back
     its three times (open it with `CreateFileW(newWide,
     FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE |
     FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
     nullptr)`, `SetFileTime`, `CloseHandle`) and its attributes
     (`SetFileAttributesW`, when they differ from `FILE_ATTRIBUTE_DIRECTORY`
     alone -- hidden, read-only, ...). Restoring is best effort: a failure
     there is logged (`Logger`, warning) and does not change the result.
   - return what the move's error says, as today (`ERROR_NOT_SAME_DEVICE` ->
     `kFileSystemCrossDevice`, else `kFileSystemError`), using the saved
     error, not `GetLastError()` after the restore calls.
   A comment says why: rename() changes nothing when it fails, and
   `MoveFileExW` cannot replace a directory, so the empty target is taken
   away first and put back if the move does not happen.

3. **`ToFileTime` overflow (review, low).** Besides the lower bound, return
   false when `time.seconds` is above the largest value whose tick count fits
   an `int64_t`: `(INT64_MAX - 9999999) / 10000000 - kSecondsTo1601` (or an
   equivalent check made before the multiplication). No signed overflow may
   be evaluated. `LocalSetTimes` already fails when `ToFileTime` returns
   false; keep that.

Nothing changes on Linux: rename() already refuses (1) and changes nothing on
failure (2). Do not add a generic "into itself" check to
`MountableFileSystem`: the filesystems each answer it already.

### `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp` (review, low)

The test `FileIORenamesAndSetsTimesFromTheWorkingDirectory` was inserted
between the comment `// OpenFile hands back the open file itself ...` and the
test that comment describes. Move the new test (whole) below that described
test, so the comment sits directly above its own test again. No behaviour
change.

### `src/components/Filesystem/CLAUDE.md`

In the `Rename` bullet, one sentence: on Windows, where `MoveFileExW` cannot
replace a directory, an empty target directory is removed first and put back
-- times and attributes included -- if the move fails, so a failed rename
changes nothing; a directory is refused a move below itself before anything
is touched.

## Tests

In `tests/unit/components/Filesystem.unittests/FilesystemRenameTest.cpp`,
fixture `FilesystemPhysicalRenameTest`:

- `RefusesMovingADirectoryBelowItselfAndKeepsTheTarget` (all platforms):
  host directories `d` and `d/sub` (empty), a file `d/f.txt`;
  `PhysicalFileSystem::Create(kPhysicalRoot)->Rename("/d", "/d/sub")` is
  `kFileSystemError`; `d/sub` still exists and is a directory; `d/f.txt` reads
  as before. Also `Rename("/d", "/d/sub/deeper")` (missing parent) fails and
  nothing changes. On Linux this documents rename()'s EINVAL; on Windows it
  checks the new refusal.
- `AFailedMoveKeepsTheEmptyTarget` (`#ifdef _WIN32` only -- the failure is
  forced through a Windows sharing rule): host directories `src` with a file
  `src/held.txt`, and an empty `dst`; set `dst`'s times with
  `fs->SetTimes("/dst", FileDateTime{1600000000, 0}, FileDateTime{1600000000, 0})`;
  open `src/held.txt` with `CreateFileW(..., GENERIC_READ, FILE_SHARE_READ |
  FILE_SHARE_WRITE /* no FILE_SHARE_DELETE */, nullptr, OPEN_EXISTING, 0,
  nullptr)` and keep the handle; `fs->Rename("/src", "/dst")` is not 0;
  `dst` still exists as a directory, `fs->Stat("/dst")` reports modification
  time `{1600000000, 0}`, `src/held.txt` still exists. Close the handle
  (also on failure: a small RAII guard, or close before the `EXPECT`s).
  The test file includes `<windows.h>` under `#ifdef _WIN32` (with
  `NOMINMAX`/`WIN32_LEAN_AND_MEAN` defined before it, and `#undef
  CreateDirectory`, `RemoveDirectory`, `GetCurrentDirectory` after it, as
  `WindowsFilesystem.cpp` does). If the rename unexpectedly succeeds on
  some Windows version, the test must still pass only if `dst` holds
  `held.txt` -- i.e. assert "either the rename failed and `dst` is intact
  and empty, or it succeeded and `dst/held.txt` exists".
- `ToFileTime`'s bound (all platforms, through the public API):
  `SetTimesRefusesATimeBeyondFiletime` -- on a physical file,
  `fs->SetTimes("/t.txt", FileDateTime{INT64_MAX / 2, 0}, std::nullopt)` is
  not 0 on Windows and the file's times are unchanged; on Linux, whatever
  utimensat answers (EOVERFLOW on most filesystems) -- assert only that the
  call returns without crashing and, when it returned non-zero, that the
  modification time is unchanged. Keep it under `#ifdef _WIN32` if that is
  simpler and clearer.

Commands:

```bash
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U FilesystemPhysicalRenameTest
bash ./scripts/test_linux.sh L U HaisosOSTest
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/Filesystem/CLAUDE.md`: the sentence above.

## Acceptance

- [ ] On Windows, `Rename` of a directory to itself (another spelling) or
      below itself returns `kFileSystemError` before anything is removed.
- [ ] On Windows, when `MoveFileExW` fails after an empty target directory
      was removed, the directory is recreated with its times and attributes,
      and the result reflects the move's own error.
- [ ] `ToFileTime` evaluates no signed overflow; an out-of-range time fails
      `SetTimes` on Windows.
- [ ] The `HaisosOSTest` comment sits above the test it describes.
- [ ] The new tests exist; Linux build and unit tests are green in the
      container; nothing outside the files named here changes.

## Out of scope

- An atomic replace (renaming the target aside first): best-effort
  recreation is what this task does.
- Any change to Linux, in-memory or the generic `MountableFileSystem` rename.
- `mv` itself (coreutils--mv-touch).
