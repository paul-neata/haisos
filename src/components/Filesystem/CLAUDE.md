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
- Provides custom directory listing via `ReadDirectory`
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
  underneath, timed from when the mount was made. There are no permissions or
  owners to report yet. `EntryTypeOf` is built on it.
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
  view wraps -- `ReadOnlyFileSystem`'s inner one, `SubFileSystem`'s root,
  `ComposedFileSystem`'s main. Every other operation already reaches the
  underlying filesystem through its public methods, so its builtins stay
  protected through any view of it. Placing a builtin is how an OS is
  assembled: `IFileIO` can only ask, never place.
- `FileSystem` uses platform-specific backends:
  - **Linux**: POSIX calls (`linux/PosixFilesystem.cpp`)
  - **Windows**: Windows CRT (`windows/WindowsFilesystem.cpp`)
  - **WASM**: POSIX calls (`linux/PosixFilesystem.cpp`, same source as Linux)

## Key Classes

- `FileSystem` - Main implementation of `IFileSystem`, unrooted (operates on real paths)
- `PhysicalFileSystem` - `IFileSystem` jailed to a real disk directory; validates every path stays within that root before delegating to an inner `FileSystem`
- `InMemoryFileSystem` - an empty, in-memory read/write `IFileSystem` (no real disk); files are plain byte buffers keyed by normalized path
- `ReadOnlyFileSystem` - wraps another `IFileSystem`, rejecting every write/create/remove
- `SubFileSystem` - confines access to a sub-path of another `IFileSystem`, resolved purely lexically (no real disk access, unlike `PhysicalFileSystem`); cannot actually escape its base path by construction
- `ComposedFileSystem` - overlays one `IFileSystem` inside another at a path, without touching either; it is a filesystem that delegates to `main` with the overlay registered as a mount point
- `MountableFileSystem` - the base every filesystem here derives from; implements `Mount`/`Unmount` and the routing they need, so a subclass only implements the `Local*` operations for the paths it owns itself
- `MountPoints` - the mount table behind that: longest-prefix path matching, plus the file-descriptor translation a mount requires (the two filesystems hand out descriptors from independent namespaces that both start at 3, so a mounted file's descriptor is re-issued from a range far above any real one and can never be confused with the host's). The synthetic range is allocated from one program-wide counter (`AllocateSyntheticFd`), not one per table: filesystems stack, and an inner one's synthetic fd passes up through the outer one untranslated, so per-table counters would collide. Builtin command files take their fds from the same counter.
- `FilesystemUtils.h` / `VirtualPath.h` - header-only helpers shared beyond this component: `ReadWholeFile`, `EntryTypeOf` (what is at a path -- `Stat`'s type, or nothing), the open-flag constants, and lexical path handling (`NormalizeVirtualPath`, `VirtualParentOf`, `VirtualLastSegment`)

These four are created via `IFileSystemService` (`src/components/FileSystemService/`), not directly.
