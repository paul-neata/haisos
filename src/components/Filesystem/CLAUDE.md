# Filesystem

Thin wrapper around Linux filesystem syscalls.

## Responsibilities

- Provides file operations: open, close, read, write
- Provides directory operations: mkdir, rmdir, chdir, getcwd
- Provides custom directory listing via `ReadDirectory`

## Key Classes

- `FileSystem` - Main implementation of `IFileSystem`, unrooted (operates on real, absolute/cwd-relative paths)
- `PhysicalFileSystem` - `IFileSystem` jailed to a real disk directory; validates every path stays within that root before delegating to an inner `FileSystem`
- `InMemoryFileSystem` - an empty, in-memory read/write `IFileSystem` (no real disk); files are plain byte buffers keyed by normalized path
- `ReadOnlyFileSystem` - wraps another `IFileSystem`, rejecting every write/create/remove
- `SubFileSystem` - confines a view to a sub-path of another `IFileSystem`, resolved purely lexically (no real disk access, unlike `PhysicalFileSystem`); cannot actually escape its base path by construction
- `MountFileSystem` (`MountedFileSystem`) - overlays one `IFileSystem` inside another at a path; the mount point is synthesized as a directory on listing even if the main filesystem has none there

These four are created via `IFilesystemService` (`src/components/FileSystemService/`), not directly.
