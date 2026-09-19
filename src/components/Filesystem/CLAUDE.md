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
- Provides directory operations: mkdir, rmdir
- Provides custom directory listing via `ReadDirectory`
- Holds **no current directory**. That notion belongs to a process
  (`ICurrentProcess::IO()`, an `IFileIO`), not to a filesystem: one filesystem is
  reachable from every process under an OS, so a cwd living here would be a
  cwd they all shared. Every path an `IFileSystem` is handed is therefore
  resolved against its own root -- `"foo"` and `"/foo"` mean the same thing --
  and a process's `IFileIO` resolves against its working directory before
  calling in. A running program never holds an `IFileSystem`: it holds an
  `IFileIO`, which is the only thing that can make sense of a bare name.
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
- `MountPoints` - the mount table behind that: longest-prefix path matching, plus the file-descriptor translation a mount requires (the two filesystems hand out descriptors from independent namespaces that both start at 3, so a mounted file's descriptor is re-issued from a range far above any real one and can never be confused with the host's)

These four are created via `IFileSystemService` (`src/components/FileSystemService/`), not directly.
