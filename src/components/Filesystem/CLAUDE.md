# Filesystem

A set of composable `IFileSystem` implementations. Only `FileSystem` (and the
`PhysicalFileSystem` built on it) touches a real disk; the rest compose other
filesystems, adding confinement, overlaying, or storage of their own.

Composition is delegation, not projection: a composed filesystem routes each
call to whichever underlying filesystem owns that path, and that filesystem
decides what the call means. Mounting a `PhysicalFileSystem` inside an
`InMemoryFileSystem` therefore still writes those paths to real disk.

There are two ways to combine filesystems. `IFileSystem::Mount`/`Unmount` change
a filesystem **in place**, adding or removing a mount point on it.
`IFileSystemService::CreateComposedFileSystem` leaves both operands untouched and
returns a **new** filesystem instead. Both share one implementation: the routing,
file-descriptor translation and mount-point listing all live in
`MountableFileSystem`, which every filesystem here derives from, so mounting
behaves identically no matter what you mount onto.

## Responsibilities

- Provides file operations: open, close, read, write
- Provides file removal (unlink, via `RemoveFile`) and directory operations: mkdir, rmdir
- Provides custom directory listing via `ReadDirectory`, which starts every
  listing of a directory with `.` and `..`, as `readdir()` does. They are put in
  by `MountableFileSystem`, once for every filesystem: each `Local*ReadDirectory`
  leaves them out, and whatever a wrapper or a mount got from the filesystem
  under it has them taken out and put back first.
- Provides `Stat` (the counterpart of `stat()`): a path's `FileStatus` --
  type, size, allocated 512-byte blocks, link count, and access, modification
  and change times, each a `FileDateTime` (seconds since the epoch plus
  nanoseconds, as `struct timespec`; always UTC). A disk file reports what
  `stat()` says (`_stat64()` on Windows: whole seconds, blocks estimated from
  4 KB clusters, and the creation time as the change time). An in-memory node
  keeps its own times -- access on every read, modification and change on
  every write, truncation, and (for a directory) every entry added or removed
  -- its blocks as its size needs them (`BlocksForSize`), and real link counts
  (a directory: 2 plus its subdirectories). A builtin is a file the size of its
  note, taking no blocks, timed from when it was placed. A mount point, and
  every directory on the way down to one, is a directory even with nothing
  underneath, timed from when the mount was made. A device is a
  `DirectoryEntryType::CharDevice` of size 0 and 0 blocks, with Linux's major
  and minor numbers for it (`deviceMajor`/`deviceMinor`, as `st_rdev`); a
  disk's character devices are reported so on Linux too. A symbolic link is
  followed, as by `stat()`, and `symbolicLink` says the path was one (only
  `PhysicalFileSystem` has links to report; composing filesystems pass it
  on). There are no permissions or owners to report yet. `EntryTypeOf` is
  built on it.
- Holds **no current directory**. That notion belongs to a process
  (`ICurrentProcess::IO()`, an `IFileIO`), not to a filesystem: one filesystem is
  reachable from every process under an OS, so a cwd living here would be a
  cwd they all shared. Every path an `IFileSystem` is handed is therefore
  resolved against its own root -- `"foo"` and `"/foo"` mean the same thing --
  and a process's `IFileIO` resolves against its working directory before
  calling in. A running program never holds an `IFileSystem`: it holds an
  `IFileIO`, which is the only thing that can make sense of a bare name.
- Keeps a **builtin command list** per filesystem (`AddBuiltinCommand`,
  `RemoveBuiltinCommand`, `IsBuiltinCommand`; see `IBuiltinCommands`). It lives
  in `MountableFileSystem`, so every filesystem has one and behaves alike. A
  builtin of a filesystem's own list is consulted **before** its mounts: it
  lists as a file in its directory, reads as `BuiltinCommandFileContent(name)`
  (`BuiltinCommandFile.h`), and cannot be opened for writing, created over, or
  removed with `RemoveFile`; `RemoveDirectory` refuses its directory and every
  one above it, checked before routing so a directory served by a mount is
  pinned too. Nothing is written anywhere -- on a `PhysicalFileSystem` the
  builtin exists only in the list. `IsBuiltinCommand` then asks whatever serves
  the path underneath: a mount, or (`LocalIsBuiltinCommand`) the filesystem a
  wrapper wraps -- `ReadOnlyFileSystem`'s inner one, `SubFileSystem`'s root,
  `ComposedFileSystem`'s main. Every other operation already reaches the
  underlying filesystem through its public methods, so its builtins stay
  protected through anything wrapping it. Placing a builtin is how an OS is
  assembled: `IFileIO` can only ask, never place.
- Takes paths separated by `/`, and on Windows by `\` too, which the host
  takes as a separator there (`IsVirtualPathSeparator`, `VirtualPath.h`):
  every filesystem splits a path the same way the disk does, or `..\x` would
  slip past a `SubFileSystem` and `mnt\x` past a mount. Elsewhere `\` is part
  of a name, as it is to the host. Every path handed back is `/`-separated.
- Names are UTF-8 on every platform. On Windows they reach the host through
  its UTF-16 calls (`_wopen`, `FindFirstFileW`, ... with `WideText.h`), never
  through the ANSI code page, which holds few of the characters a name may
  have.
- `FileSystem` uses platform-specific backends:
  - **Linux**: POSIX calls (`linux/PosixFilesystem.cpp`)
  - **Windows**: Windows CRT and API, wide-character (`windows/WindowsFilesystem.cpp`)
  - **WASM**: POSIX calls (`linux/PosixFilesystem.cpp`, same source as Linux)

  Each also defines `FileSystem::IsLink` (is a host path itself a link: a
  symbolic link, or on Windows a junction or a WSL symlink, by its reparse
  tag -- `std::filesystem` misses both), `FileSystem::IsDevicePath` (does the
  host take a path for a device: on Windows, as `GetFullPathNameW` says --
  `NUL.txt` is the null device on Windows 10 and a file on Windows 11, so no
  fixed list could say) and `NoCriticalErrorDialogs`, which
  every host call runs under: on Windows a drive that is not ready (no medium
  in a removable drive) then fails the call instead of showing the "no disk"
  dialog and waiting for a click that may never come.

## Key Classes

- `FileSystem` - Main implementation of `IFileSystem`, unrooted (operates on real paths)
- `PhysicalFileSystem` - `IFileSystem` jailed to a real disk directory; validates every path stays within that root before delegating to an inner `FileSystem`. Its root is a physical path (`PhysicalPath.h`): absolute or relative to the current directory, and on Windows written with `\` or `/`, with a drive (`c:\x`, `c:/x`), as a UNC path (`\\server\share\x`), or as a path of the full physical filesystem (`/c/x`); `Create("/")` there returns `WindowsFullPhysicalFileSystem`. A root naming no directory is logged, and every call then fails. All of it happens in `ResolveOnHost`: the path is split as the mounts over it split it (`SplitVirtualPath`: a `..` climbing above the root is refused, so no `..` can hide a component from `weakly_canonical`'s walk either), each segment must be a plain host name (`IsPlainHostName`: no NUL; on Windows no `:`, wildcard, trailing dot or space, or device name such as `con`, any of which would reach something other than the path says, past mounts and builtins), the segments are placed on the host (`HostPathOf`, under the root) -- where the host must not take the path for a device (`FileSystem::IsDevicePath`) -- canonicalized, and checked to lie within (`IsWithin`). Symbolic links already on the disk (nothing here can create one; on Windows a junction is one too) are followed only while they stay within the root, and a path ending in a link that leads nowhere is refused -- `weakly_canonical` cannot resolve a dangling link, and `open()` with `O_CREAT` would create its target, outside the root perhaps. `LocalOpenFile` also adds `O_NOFOLLOW` on POSIX, in case a link appears between the check and the open; a directory swapped for a link higher up the path in that window is not caught (it takes something outside Haisos to do it). `RemoveFile`, `RemoveDirectory` and `CreateDirectory` resolve only the directory holding the last component and take that component as it is, as `unlink()`/`rmdir()`/`mkdir()` do: a link is removed itself, never what it points at, and the root itself can be neither removed nor created. `Stat` follows links but reports one in `FileStatus::symbolicLink`, which the haisosfile's `DELETE` uses to remove a link rather than descend into it. A physical directory taken from the full physical filesystem is a `PhysicalFileSystem` jailed there, never a `SubFileSystem` of the full one, whose confinement goes by the path as written: a link inside would lead anywhere on the disk
- `WindowsFullPhysicalFileSystem` (`windows/`, Windows only) - the machine's whole disk as one filesystem, as Cygwin shows it: `/` holds a directory per drive, named by its letter in lowercase (`GetLogicalDriveStringsW`), and `/c/Users/x.txt` is `C:\Users\x.txt`. What `IFactory::CreateFullPhysicalFileSystem()` returns on Windows; on Linux that is a `PhysicalFileSystem` at `/`. A `PhysicalFileSystem` with no root of its own: it overrides `HostPathOf` (the drive letter, in either case, then the rest) and `IsWithin` (anywhere on a drive, so a link may lead from one drive to another, but not to a share), and lists `/` itself. A drive of removable or optical media (`GetDriveTypeW`) with no medium in lists as an empty directory. Nothing is created or removed at the top. A UNC path is on no drive, so not in it; `PhysicalFileSystem` takes one directly
- `InMemoryFileSystem` - an empty, in-memory read/write `IFileSystem` (no real disk); files are plain byte buffers keyed by normalized path
- `DeviceFileSystem` - a device filesystem, as Linux's `/dev`: a root directory holding the character devices `null` (writes discarded, reads end at once) and `zero` (writes discarded, reads return endless 0 bytes). Nothing can be created or removed in it -- files, directories, or builtins (`LocalCanHoldBuiltinCommands`) -- but a device opens with any flags, so `> /dev/null` works
- `ReadOnlyFileSystem` - wraps another `IFileSystem`, rejecting every write/create/remove
- `SubFileSystem` - confines access to a sub-path of another `IFileSystem`, resolved purely lexically (no real disk access, unlike `PhysicalFileSystem`): no path as written can leave its base, but a symbolic link under its base on a physical filesystem leads wherever that filesystem lets it -- which is why a directory of the disk is jailed with a `PhysicalFileSystem` of its own instead
- `ComposedFileSystem` - overlays one `IFileSystem` inside another at a path, without touching either; it is a filesystem that delegates to `main` with the overlay registered as a mount point
- `MountableFileSystem` - the base every filesystem here derives from; implements `Mount`/`Unmount` and the routing they need, so a subclass only implements the `Local*` operations for the paths it owns itself
- `MountPoints` - the mount table behind that: longest-prefix path matching, plus the file-descriptor translation a mount requires (the two filesystems hand out descriptors from independent namespaces that both start at 3, so a mounted file's descriptor is re-issued from a range far above any real one and can never be confused with the host's). The synthetic range is allocated from one program-wide counter (`AllocateSyntheticFd`), not one per table: filesystems stack, and an inner one's synthetic fd passes up through the outer one untranslated, so per-table counters would collide. Builtin command files take their fds from the same counter.
- `FilesystemUtils.h` / `VirtualPath.h` - header-only helpers shared beyond this component: `ReadWholeFile`, `EntryTypeOf` (what is at a path -- `Stat`'s type, or nothing), the open-flag constants, and lexical path handling (`IsVirtualPathSeparator`, `NormalizeVirtualPath`, `SplitVirtualPath`, `VirtualParentOf`, `VirtualLastSegment`)
- `PhysicalPath.h` - physical paths, how the host's disk is named to Haisos (a physical filesystem's root, a haisosfile's `FS ... PHYSICAL`, `COPY`/`OUTCOPY` host paths): `ResolvePhysicalPath` turns one into an absolute host path, `IsFullFileSystemRoot` tells the root of the full physical filesystem apart, and `IsPlainHostName` says whether a name reaches the host as that very name. The Windows and POSIX rules are pure string handling, each testable on either platform (`ResolveWindowsPhysicalPath`, `IsPlainWindowsName`, ...)
- `NoCriticalErrorDialogs.h` - see the backends above

The in-memory, device, read-only, sub-path and composed ones are created via `IFileSystemService` (`src/components/FileSystemService/`), the physical ones via `IFactory` (`CreatePhysicalFileSystem`, `CreateFullPhysicalFileSystem`), not directly.
