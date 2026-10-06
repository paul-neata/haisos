# Task fd--descriptor-objects: Open files as IFileDescriptor objects in every filesystem

- Rock: fd
- Depends on: none
- Size: ~1100 changed lines in ~40 files (about 500 of them mechanical test edits: `int fd` -> descriptor object)
- Plan checked against: develop @ 0d92271
- PR title: Return IFileDescriptor objects from IFileSystem::OpenFile

## Goal

An open file is an object, not a number. `IFileSystem::OpenFile` (both
overloads) returns a `std::shared_ptr<IFileDescriptor>` -- null on failure --
and the file is read and written through that object. It is closed when its
last `shared_ptr` is released. `IFileSystem::ReadFile`, `WriteFile` and
`CloseFile` are gone.

Every filesystem returns a descriptor class of its own:
- a disk file wraps the host's descriptor;
- an in-memory file reads and writes the filesystem's node;
- `/dev/null` and `/dev/zero` behave as on Linux;
- a builtin command's file reads as its note.

The composing filesystems (read-only, sub-path, composed, and every mount)
pass the inner filesystem's descriptor up as it is. So `MountPoints` loses its
synthetic descriptor numbers and its translation table, and
`AllocateSyntheticFd` goes.

Behaviour fixes that come with it:
- An in-memory descriptor opened with `O_APPEND` writes at the current end of
  the file **before every write**, as POSIX says, not only at open. Two
  `>> log` writers no longer overwrite each other.
- Every leaf descriptor enforces the access mode it was opened with:
  - `Write` on a descriptor opened read-only fails with `kIOError`.
  - `Read` on one opened write-only fails with `kIOError`, as `EBADF` would.

  This is what makes passing the inner descriptor up through
  `ReadOnlyFileSystem` safe.
- A descriptor keeps working after its file's filesystem is unmounted, or after
  the last outside reference to that filesystem is gone, until the descriptor
  itself is released.

`IFileIO` (what a process uses) **keeps its `int` API in this task**.
`ProcessFileIO` gives out numbers of its own (3 and up), mapped to descriptor
objects. `fd--process-table` replaces this with the real descriptor table.
Nothing a user or an agent sees changes.

## Context

Read first:
- the root `CLAUDE.md`: "Security: `ICurrentProcess` is the only door out of a
  process" and "Creating things: private constructors and `Create()`";
- `src/components/Filesystem/CLAUDE.md`;
- `src/components/HaisosOS/CLAUDE.md` (the `ProcessFileIO` paragraph);
- `develop-plan/goal.md` "## Contracts" (the first two bullets are this task's).
  They are binding: the interface below is copied from them.

The code this task changes:
- `interfaces/IFileSystemService.h`: `IFileSystem`. The `ssize_t` alias for
  Windows lives here today.
- `interfaces/IFileIO.h`: comments only, in this task.
- `src/components/Filesystem/`:
  - `MountableFileSystem.h/.cpp`: the base of every filesystem. It routes
    `OpenFile` to its own builtins, then to its mounts, then to `LocalOpenFile`,
    and today translates descriptors.
  - `MountPoints.h/.cpp`: the mount table, plus today's synthetic descriptor
    range (`kSyntheticFdBase`, `IsSynthetic`, `AllocateSyntheticFd`,
    `RegisterFd`, `LookupFd`, `ReleaseFd`).
  - `Filesystem.h`, `linux/PosixFilesystem.cpp`, `windows/WindowsFilesystem.cpp`:
    the host calls.
  - `PhysicalFileSystem.h/.cpp`, `InMemoryFileSystem.h/.cpp`,
    `DeviceFileSystem.h/.cpp`, `ReadOnlyFileSystem.h/.cpp`,
    `SubFileSystem.h/.cpp`, `ComposedFileSystem.h/.cpp`.
  - `BuiltinCommandFile.h`: the note text of a builtin.
  - `FilesystemUtils.h`: `ReadWholeFile`, a template today, used on both an
    `IFileSystem` and an `IFileIO`.
  - `windows/WindowsFullPhysicalFileSystem.*` derives from
    `PhysicalFileSystem`, overrides only `HostPathOf`, and needs no change.
- `src/components/HaisosOS/ProcessFileIO.h/.cpp`: passes `int` descriptors
  straight to the root filesystem today.
- `src/haisos/HaisosFileOperations.cpp`: `WriteAll`, `WriteContent`,
  `CopyFileBetween`.
- `ReadWholeFile` callers. They need no change if the overloads below are
  kept:
  - `src/components/HaisosOS/HaisosOS.cpp` (on `IFileSystem`);
  - `src/haisos/main.cpp`;
  - `src/tools/os_read_file/OSReadFileTool.cpp` (on `IFileIO`).
- `cat` (`src/components/BuiltinCommands/commands/Cat.cpp`) and
  `os_write_file` use `IFileIO`'s `int` API. They are untouched in this task.

Earlier tasks provide nothing: this is the rock's first task.

How the tests are selected: `scripts/test_linux.sh`'s name filter must be a
substring of the test executable's name (case-insensitive) **and** is passed
to gtest as `--gtest_filter=*<filter>*` (case-sensitive). `Filesystem`, for
example, selects `Filesystem.unittests` and, in it, every test whose
`Suite.Name` contains `Filesystem`.

## Changes

### `interfaces/IFileDescriptor.h` (new)

Exactly this interface (comments in your own words, saying what is below):

```cpp
#pragma once
#include <cstddef>
#ifdef _WIN32
using ssize_t = std::ptrdiff_t;
#else
#include <sys/types.h>
#endif

namespace Haisos {

// IOResult: the negative values IFileDescriptor::Read and ::Write return on failure.
constexpr ssize_t kIOError = -1;        // any failure (EBADF, EIO, EISDIR, ...)
constexpr ssize_t kIOBrokenPipe = -2;   // a write to a pipe no one can read any more
constexpr ssize_t kIOInterrupted = -3;  // the calling process was asked to stop while blocked

class IFileDescriptor {
public:
    virtual ~IFileDescriptor() = default;
    // As read(): the bytes read (at most count), 0 at end of file, or a negative IOResult.
    virtual ssize_t Read(void* buf, size_t count) = 0;
    // As write(): the bytes written (at most count), or a negative IOResult.
    virtual ssize_t Write(const void* buf, size_t count) = 0;
    // As isatty(): true only for a console descriptor.
    virtual bool IsTerminal() const = 0;
};

}
```

The class comment must say:
- the object is POSIX's "open file description": every holder (several
  descriptor slots, several processes) shares one position;
- releasing the last `shared_ptr` closes it;
- only a pipe, console input or console output may block (none exist yet);
  nothing in this task blocks;
- `kIOBrokenPipe` and `kIOInterrupted` are reserved for pipes
  (`pipes--pipe-service`). No filesystem descriptor returns them.

### `interfaces/IFileSystemService.h`

- `#include "IFileDescriptor.h"`. Remove the `#ifdef _WIN32 using ssize_t`
  block: it now lives in `IFileDescriptor.h`.
- In `IFileSystem`:
  ```cpp
  // OpenFile is the IFileSystem counterpart of the C open() function: the open
  // file, or null on failure. Closed when its last holder releases it.
  virtual std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) = 0;
  virtual std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) = 0;
  ```
- Delete `CloseFile`, `ReadFile` and `WriteFile`, with their comments.
- `Unmount`'s comment: "Files still open on the unmounted filesystem keep
  working until their descriptors are released."
- The builtin paragraph ("reads as a short text naming the builtin, but can
  never be opened for writing") stays true; keep it.

### `interfaces/IFileIO.h`

The signatures do not change in this task. Edit the comment above the
operations: `OpenFile`/`ReadFile`/`WriteFile`/`CloseFile` here use numbers of
the process's own (never 0, 1 or 2), each standing for an `IFileDescriptor`
the process holds -- unlike `IFileSystem`, which returns the descriptor
itself.

### `src/components/Filesystem/MountableFileSystem.h/.cpp`

- The public overrides become:
  ```cpp
  std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags) final;
  std::shared_ptr<IFileDescriptor> OpenFile(const std::string& pathname, int flags, int mode) final;
  ```
  Remove `CloseFile`, `ReadFile` and `WriteFile`.
- The protected pure virtuals become:
  ```cpp
  virtual std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags) = 0;
  virtual std::shared_ptr<IFileDescriptor> LocalOpenFile(const std::string& pathname, int flags, int mode) = 0;
  ```
  Remove `LocalCloseFile`, `LocalReadFile` and `LocalWriteFile`.
- `OpenFile` (both overloads) works in this order:
  1. If one of this filesystem's own builtins is at the path: return null when
     `RequestsWriteAccess(flags)`, else a new builtin-note descriptor (below).
  2. Else, if a mount serves the path: return
     `route.filesystem->OpenFile(route.innerPath, flags[, mode])` **untouched**.
  3. Else: `LocalOpenFile(...)`.
- `OpenOwnBuiltin` becomes
  `bool OpenOwnBuiltin(const std::string& absolute, int flags, std::shared_ptr<IFileDescriptor>& outFile)`
  (or fold it into `OpenFile`; your choice).
- Remove `BuiltinFileHandle`, `m_builtinFiles`, and every use of
  `MountPoints::IsSynthetic`/`AllocateSyntheticFd`/`LookupFd`/`ReleaseFd`/`RegisterFd`.
- The builtin-note descriptor goes in the anonymous namespace of
  `MountableFileSystem.cpp`:
  - `class BuiltinCommandFileDescriptor final : public IFileDescriptor`;
  - a private constructor and
    `static std::shared_ptr<BuiltinCommandFileDescriptor> Create(std::string content)`;
  - it holds the text and a read position, both guarded by its own
    `std::mutex`;
  - `Read` copies from the position (0 at the end);
  - `Write` always returns `kIOError`;
  - `IsTerminal` is false.
  - A builtin removed while its file is open keeps reading its text, as
    today.
- The class comment drops "and each re-deriving the file-descriptor
  translation a mount needs".

### `src/components/Filesystem/MountPoints.h/.cpp`

Remove all of these:
- `kSyntheticFdBase`, `IsSynthetic`, `AllocateSyntheticFd`;
- `RegisterFd`, `LookupFd`, `ReleaseFd`;
- `struct Handle` and `m_openFds`;
- the includes only they needed (`<atomic>`, `<unordered_map>`);
- the comments about descriptors.

What remains is the mount table: `Mount`, `Unmount`, `Empty`, `Resolve`,
`ChildSegments`, `LatestMountTimeBelow`.

### `src/components/Filesystem/Filesystem.h`, `linux/PosixFilesystem.cpp`, `windows/WindowsFilesystem.cpp`

- In `Filesystem.h`, declare
  `class HostFileDescriptor final : public IFileDescriptor`:
  - `static std::shared_ptr<HostFileDescriptor> Create(int hostFd)`;
  - a private constructor;
  - `~HostFileDescriptor() override`, which closes `hostFd`;
  - `Read`, `Write`, and `IsTerminal() const` (always false: only console
    descriptors are terminals, whatever the host fd is -- goal.md, `ls` into
    a pipe);
  - `const int m_hostFd`.

  Its methods are defined per backend, next to today's `Local*` functions,
  which they replace.
- `FileSystem::LocalOpenFile` (both overloads) returns
  `std::shared_ptr<IFileDescriptor>`. It calls the host's open as today, and
  returns null when that fails, else `HostFileDescriptor::Create(fd)`.
  Remove `FileSystem::LocalCloseFile/LocalReadFile/LocalWriteFile`.
- POSIX (`linux/PosixFilesystem.cpp`; also the WASM build):
  - `Read`/`Write` are `::read`/`::write` on `m_hostFd`. They return the
    result, or `kIOError` when it is negative.
  - The destructor calls `::close`, and logs a `LogWarning` naming the fd if
    that fails.
- Windows (`windows/WindowsFilesystem.cpp`): the same with `_read`/`_write`/
  `_close`.
  - Each runs with `NoCriticalErrorDialogs` (read/write) and
    `CrtInvalidParameterAsError` in scope, as today's functions do.
  - Cap `count` at `INT_MAX` before the cast to `unsigned int`: a short read
    or write is allowed, a silently truncated count is not.
  - Update the comment at the top of the file: a descriptor is no longer an
    int "any caller may hand in", but `CrtInvalidParameterAsError` still
    guards the calls.
  - You cannot build Windows in the container. Keep this file's edit
    mechanical and parallel to the POSIX one; the host fixes Windows after
    review.

### `src/components/Filesystem/PhysicalFileSystem.h/.cpp`

- `LocalOpenFile` (both overloads) returns `std::shared_ptr<IFileDescriptor>`.
  It returns null where it returned -1 (`ResolveOnHost` failing), else
  `m_inner->LocalOpenFile(resolved, ...)`.
- Remove `LocalCloseFile/LocalReadFile/LocalWriteFile`.

### `src/components/Filesystem/InMemoryFileSystem.h/.cpp`

- `class InMemoryFileSystem : public MountableFileSystem, public std::enable_shared_from_this<InMemoryFileSystem>`.
  It is only ever made by `Create()`, so `shared_from_this()` is safe.
- Remove `OpenHandle`, `m_openHandles` and `m_nextFd`. Also remove the three
  `Local*` fd methods.
- Add the descriptor class, `class InMemoryFileDescriptor;`, declared in the
  header in namespace `Haisos` and made a friend
  (`friend class InMemoryFileDescriptor;`). Define it in the .cpp:
  - `final : public IFileDescriptor`;
  - a private constructor and a static `Create(...)` returning
    `std::shared_ptr<InMemoryFileDescriptor>`;
  - it holds `std::shared_ptr<InMemoryFileSystem>`. It keeps the filesystem
    alive while open, so an unmounted or otherwise dropped in-memory
    filesystem still serves its open files. There is no cycle: the filesystem
    holds no descriptors.
  - It also holds the normalized path, `size_t position`, and
    `bool readable, writable, append`, all taken from the flags at open:
    - `writable = (flags & (kFileWriteOnlyBit | kFileReadWriteBit)) != 0`;
    - `readable = (flags & kFileWriteOnlyBit) == 0`;
    - `append = (flags & kFileAppendBit) != 0`.
- Add two private methods to `InMemoryFileSystem`, both taking `m_mutex`.
  The descriptor's position is only ever touched inside them, under that
  mutex.
  ```cpp
  ssize_t ReadAt(const std::string& normalizedPath, size_t& position, void* buf, size_t count);
  ssize_t WriteAt(const std::string& normalizedPath, size_t& position, bool append, const void* buf, size_t count);
  ```
  - Their bodies are today's `LocalReadFile`/`LocalWriteFile` bodies, keyed
    by the path instead of an fd. A node that is gone (removed) gives
    `kIOError`, as today.
  - **The fix:** `WriteAt` sets `position = data.size()` before writing when
    `append` is set.
- `Read` returns `kIOError` if not `readable`, else `ReadAt`. `Write` returns
  `kIOError` if not `writable`, else `WriteAt`. `IsTerminal` is false.
- `LocalOpenFile` keeps every check it has today (directory, `O_CREAT`,
  parent, `O_TRUNC`) and returns null where it returned -1. It returns
  `InMemoryFileDescriptor::Create(shared_from_this(), normalized, flags)`.
  - The start position is 0.
  - With `append`, every write moves to the end anyway. Reads of an
    `O_RDWR|O_APPEND` descriptor start at 0, as on Linux.

### `src/components/Filesystem/DeviceFileSystem.h/.cpp`

- Remove `m_mutex`, `m_openHandles` and `m_nextFd`. Also remove the three fd
  methods.
- `LocalOpenFile` returns null for anything but a device, else a
  `DeviceFileDescriptor`. Declare that class in the .cpp's anonymous
  namespace:
  - `final : public IFileDescriptor`, with a private constructor and a static
    `Create`;
  - it holds `bool readsZeros` (zero) and `readable`/`writable`, from the
    flags as above;
  - `Read`: `kIOError` if not readable; else 0 for null, or `count`
    (capped as today) zero bytes for zero;
  - `Write`: `kIOError` if not writable; else `count` (capped as today);
  - `IsTerminal`: false;
  - it is immutable, so it needs no mutex.

### `src/components/Filesystem/ReadOnlyFileSystem.*`, `SubFileSystem.*`, `ComposedFileSystem.*`

- `LocalOpenFile` (both overloads) returns `std::shared_ptr<IFileDescriptor>`:
  `m_inner->OpenFile(...)` / `m_root->OpenFile(ResolveInRoot(...))` /
  `m_main->OpenFile(...)`, passed up untouched.
  - `ReadOnlyFileSystem` keeps returning null when `RequestsWriteAccess(flags)`.
  - The leaf descriptor then refuses writes by itself, since it was opened
    read-only.
- Remove `LocalCloseFile/LocalReadFile/LocalWriteFile` from all three.
- In `ComposedFileSystem.h`'s class comment, drop "file-descriptor
  translation".

### `src/components/Filesystem/FilesystemUtils.h`

- `#include "interfaces/IFileIO.h"` and `"interfaces/IFileDescriptor.h"`.
- New helper:
  ```cpp
  // Reads |file| to its end, up to the same 10 MB cap. False on a read error.
  inline bool ReadWholeDescriptor(IFileDescriptor& file, std::string& outContent);
  ```
  Its body is today's read loop, with the 10 MB cap and the 64 KB chunk.
- Replace the `ReadWholeFile` template with two non-template overloads.
  Callers do not change, and an `InMemoryFileSystem&` or `DeviceFileSystem&`
  argument converts unambiguously to `IFileSystem&`:
  ```cpp
  inline bool ReadWholeFile(IFileSystem& fs, const std::string& path, std::string& outContent);
  inline bool ReadWholeFile(IFileIO& io, const std::string& path, std::string& outContent);
  ```
  - The `IFileSystem` one opens with `kFileOpenReadOnly`, returns false on
    null, then calls `ReadWholeDescriptor`.
  - The `IFileIO` one keeps today's `int` body (open, read loop, close). Its
    comment says `fd--process-table` turns it into the same two lines as the
    other.
- `EntryTypeOf` stays a template.

### `src/components/HaisosOS/ProcessFileIO.h/.cpp` (interim number map)

`IFileIO`'s `int` methods keep their signatures. Their meaning moves inside
`ProcessFileIO`. Add these private members:

```cpp
// Interim, until fd--process-table: the process's open files by number.
// Numbers start at 3, so 0, 1 and 2 never name a file; the lowest free one is used.
mutable std::mutex m_openFilesMutex;
std::map<int, std::shared_ptr<IFileDescriptor>> m_openFiles;
```

- `OpenFile` (both overloads):
  1. `fs->OpenFile(ResolvePath(pathname), ...)`.
  2. On null (or no OS), return -1.
  3. Otherwise, under the mutex, insert the descriptor at the lowest number
     >= 3 not in the map, and return that number.
- `ReadFile`/`WriteFile`: under the mutex, copy the `shared_ptr` out (-1 if
  the number is not in the map), then call `Read`/`Write` on it **after
  unlocking**. Return its result.
- `CloseFile`: under the mutex, move the entry out of the map (-1 if absent);
  release it after unlocking; return 0.
- The OS is no longer needed for read/write/close: a descriptor works on its
  own.
- Comment this as interim. Nothing else in the class changes.

### `src/haisos/HaisosFileOperations.cpp`

- `WriteAll(IFileDescriptor& file, const char* data, size_t size)`.
- `WriteContent` and `CopyFileBetween` use `auto file = fs.OpenFile(...)`,
  test for null, call `Read`/`Write` on the objects, and release them with
  `reset()` (or end of scope).
- The `closed` checks go: closing reports nothing any more. A host close
  failure is logged by `HostFileDescriptor`.
- Keep the same `outReason` strings for open failures. Keep "writing to it
  failed" / "copying the data failed" for read/write failures.

### Tests and mocks implementing `IFileSystem`

- `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp`, `GatedFileSystem`:
  - `OpenFile` overloads return `std::shared_ptr<IFileDescriptor>` (still
    `HoldIfGated` first);
  - delete its `CloseFile/ReadFile/WriteFile`.
- `tests/mocks/MockFilesystem.h`: **delete the file**. Nothing includes it:
  `grep -rn MockFilesystem tests src` finds only itself.

### Root rules that bite

- Every new class implementing `IFileDescriptor` (an interface in
  `interfaces/`) has private constructors and a public static `Create(...)`
  returning `std::shared_ptr`. It is never on the stack, by value, or in a
  `unique_ptr`. This holds for test fakes too.
- Nothing here creates a link on a physical filesystem.
- `IFileIO` still has no `Mount`/`Unmount`.
- `ICurrentProcess::IO()` stays the only way a process reaches files.
- `interfaces/` headers are not listed in CMake (they are found through the
  include path), so `IFileDescriptor.h` needs no CMake entry.
- A new test `.cpp` **must** be added to its test executable's
  `add_executable` line.

## Tests

Adapt every existing use. The pattern:
- `int fd = fs->OpenFile(...)` becomes `auto file = fs->OpenFile(...)`;
- `ASSERT_GE(fd, 0)` becomes `ASSERT_NE(file, nullptr)`;
- `EXPECT_LT(fs->OpenFile(...), 0)` becomes `EXPECT_EQ(fs->OpenFile(...), nullptr)`;
- `fs->ReadFile(fd, ...)` / `fs->WriteFile(fd, ...)` become
  `file->Read(...)` / `file->Write(...)`;
- `fs->CloseFile(fd)` becomes `file.reset()`, or goes; drop the
  `EXPECT_EQ(..., 0)` around it.

The files to adapt:
- in `tests/unit/components/Filesystem.unittests/`: `FilesystemTest.cpp`,
  `PhysicalFileSystemTest.cpp`, `DeviceFileSystemTest.cpp`, `MountTest.cpp`,
  `BuiltinFileTest.cpp`, `StatTest.cpp`, and
  `WindowsFullPhysicalFileSystemTest.cpp` (Windows-only: edit carefully, it
  is not compiled on Linux);
- `tests/unit/components/ServicesCreator.unittests/ServicesCreatorTest.cpp`;
- `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`
  (the `WriteFile` helper of the fixture, which uses `root->...`);
- `tests/unit/haisos/haisos.unittests/HaisosFileSystemBuilderTest.cpp`;
- `HaisosOSTest.cpp` (only `GatedFileSystem`: its `io->` calls stay `int` in
  this task).

If an existing test opened a file read-only and then wrote to it (or the
reverse), fix the test's flags: that is now refused, as on POSIX.

Delete or rewrite the tests whose subject is gone:
- `FilesystemTest`: delete `CloseFileInvalidFdReturnsError`,
  `ReadFileReturnsErrorOnInvalidFd`, `ADescriptorThatIsNotOpenIsRefused` and
  `WriteFileReturnsErrorOnInvalidFd`. There is no invalid descriptor any
  more; `OpenFileNonExistentReturnsError` covers the null.
- `DeviceFileSystemTest.ZeroDiscardsWritesAndReadsEndlessZeroBytes`: drop the
  "Closed, it reads and writes no more" tail.
- `MountTest.HostAndMountedDescriptorsDoNotCollide`: rename it
  `HostAndMountedFilesReadThroughTheirOwnDescriptors`. Keep its point: each
  descriptor reads its own file.
- `BuiltinFileTest.DescriptorsOfStackedFilesystemsNeverCollide`: rename it
  `DescriptorsOfStackedFilesystemsReadTheirOwnFiles`. Keep the setup and the
  two reads; drop `EXPECT_NE(innerFd, outerFd)`.
- `BuiltinFileTest.AReadDescriptorOfABuiltinCannotBeWrittenThrough`: now
  `Write` on the descriptor returns `kIOError`.

New file `tests/unit/components/Filesystem.unittests/FileDescriptorTest.cpp`,
added to `Filesystem.unittests` in that folder's `CMakeLists.txt`. Its suite
name is **`FilesystemDescriptorTest`**, so the `Filesystem` filter selects it.

| Test | Sets up | Asserts |
|------|---------|---------|
| `IOResultValues` | -- | `kIOError == -1`, `kIOBrokenPipe == -2`, `kIOInterrupted == -3` |
| `InMemoryAppendWritesAtTheEndBeforeEveryWrite` | in-memory fs; two descriptors opened on `/log` with `kFileOpenWriteCreateAppend`, both before any write | A writes `a`, B `b`, A `c` -> `ReadWholeFile` gives `abc` (it was `cb` / overwritten before the fix) |
| `InMemoryWritesWithoutAppendStayAtTheirPosition` | file `12345`; open `kFileWriteOnlyBit` (no trunc) | `Write("ab")` -> file `ab345` |
| `AReadOnlyDescriptorRefusesWritesAndAWriteOnlyOneRefusesReads` | in-memory file; `/dev/null` and `/dev/zero` of a `DeviceFileSystem` | read-only: `Write` == `kIOError`, file unchanged; write-only: `Read` == `kIOError` |
| `ADescriptorWorksUntilItsLastHolderReleasesIt` | in-memory; open for write, copy the `shared_ptr`, reset the first | write through the copy succeeds and is read back |
| `ADescriptorOutlivesItsFilesystem` | in-memory fs; open `/f` twice, write-only (with create) and then read-only; then `fs.reset()` | `Write("hi")` on the first returns 2; `Read` on the second returns `hi` |
| `AMountedFilesDescriptorKeepsWorkingAfterUnmount` | in-memory root, in-memory `m` mounted at `/m`; open `/m/x` for write through the root; `Unmount("/m")` | write succeeds; `ReadWholeFile(*m, "/x")` has the bytes |
| `WrappersPassTheInnerDescriptorUp` | in-memory inner with `/d/f`; `SubFileSystem` at `/d`, `ComposedFileSystem` mounting another in-memory fs at `/c`, `ReadOnlyFileSystem` over inner | writes through sub and composed land in the inner/mounted fs; read-only opens for reading and returns null for write flags |
| `NoFilesystemDescriptorIsATerminal` | in-memory file, `/dev/null`, a builtin's file, a `PhysicalFileSystem` file in a temp dir | `IsTerminal()` is false for each |
| `ReleasingAPhysicalDescriptorClosesTheHostFile` (`#ifdef __linux__` only) | a `PhysicalFileSystem` on a temp dir | the entry count of `/proc/self/fd` is +1 while the descriptor is held and back to the original after `reset()` |
| `TwoDescriptorsOfABuiltinReadIndependently` | in-memory fs with a builtin at `/bin/echo` | both read the full note from the start; `Write` == `kIOError` |

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Filesystem
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H
```
The `Filesystem` filter runs `FilesystemTest` and `FilesystemDescriptorTest`.
The full `L U` run covers `MountTest`, `DeviceFileSystemTest`,
`BuiltinFileTest`, `ServicesCreatorTest`, `HaisosOSTest` and the haisos
unit tests. `L H` (the haisos JS tests) shows `CREATE`/`APPEND`/`COPY`/
`OUTCOPY` still work end to end.

Before finishing, check this finds nothing in `src`, `interfaces` and `tests`:
```
grep -rn "AllocateSyntheticFd\|IsSynthetic\|RegisterFd\|LookupFd\|ReleaseFd\|LocalCloseFile\|LocalReadFile\|LocalWriteFile\|m_builtinFiles\|MockFilesystem" src interfaces tests
```
Also check that `grep -rn "\->CloseFile\|\.CloseFile" src tests` finds only
`IFileIO` users (`Cat.cpp`, `OSWriteFileTool.cpp`, `ProcessFileIO`,
`HaisosOSTest`'s `io->`, `FilesystemUtils.h`'s `IFileIO` overload).

## Docs

- `src/components/Filesystem/CLAUDE.md`:
  - the intro paragraph: no more "file-descriptor translation";
  - "Responsibilities": open returns an `IFileDescriptor` object; read and
    write are on it; it closes when released;
  - a new bullet listing each filesystem's descriptor class:
    `HostFileDescriptor` (`Filesystem.h`, per backend),
    `InMemoryFileDescriptor` (append before every write; access mode
    enforced), `DeviceFileDescriptor`, `BuiltinCommandFileDescriptor`. The
    wrappers and mounts pass the inner one up untouched; a descriptor
    outlives an unmount;
  - the `MountPoints` bullet becomes the mount table alone;
  - the `FilesystemUtils.h` bullet gains `ReadWholeDescriptor`.
- `src/components/HaisosOS/CLAUDE.md`, the `ProcessFileIO` key-class line:
  it keeps the process's open files under numbers of its own, from 3 (interim
  until the descriptor table).
- Root `CLAUDE.md`, "Directory Structure", the `interfaces/` line: add
  `IFileDescriptor.h [IFileDescriptor, IOResult kIO*]`.

## Acceptance

- [ ] `interfaces/IFileDescriptor.h` matches the signatures above exactly. The constants are at namespace scope in `Haisos`.
- [ ] `IFileSystem` has no `ReadFile`, `WriteFile` or `CloseFile`. Both `OpenFile` overloads return `std::shared_ptr<IFileDescriptor>`.
- [ ] `MountPoints` has no descriptor code. `MountableFileSystem` returns a mounted filesystem's descriptor untouched.
- [ ] Every descriptor class has a private constructor and a static `Create` returning `shared_ptr`.
- [ ] In-memory `O_APPEND` writes at the end before every write (`InMemoryAppendWritesAtTheEndBeforeEveryWrite`).
- [ ] Read-only and write-only descriptors refuse the other direction with `kIOError` (in-memory, device, builtin; physical by the host).
- [ ] `HostFileDescriptor` closes its host fd in its destructor. The Windows backend is edited in parallel, with `INT_MAX` capping.
- [ ] `IFileIO` is unchanged in signature. `ProcessFileIO` maps numbers >= 3 to descriptors and never gives out 0, 1 or 2.
- [ ] `tests/mocks/MockFilesystem.h` is deleted. The greps above find nothing.
- [ ] The build is green. `L U` and `L H` pass.
- [ ] No behaviour visible to a haisosfile changes, apart from the append fix.

## Out of scope

- `IFileIO` switching to descriptor objects, the per-process descriptor table
  (`GetDescriptor`, `AddDescriptor`, `Dup`, `Dup2`, `CloseDescriptor`,
  `kStdIn`...), and releasing descriptors when a program ends: all
  `fd--process-table`.
- `cat`, `os_read_file` and `os_write_file` moving to descriptors:
  `fd--process-table`.
- Console, empty-input and pipe descriptors: `streams--console-and-start`,
  `pipes--pipe-service`.
- POSIX unlink semantics for in-memory files (a removed file's open
  descriptor still failing, as today, is kept); seek; `/dev/stdin` and the
  like.
