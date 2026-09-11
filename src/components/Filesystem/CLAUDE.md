# Filesystem

Thin wrapper around Linux filesystem syscalls.

## Responsibilities

- Provides file operations: open, close, read, write
- Provides directory operations: mkdir, rmdir, chdir, getcwd
- Provides custom directory listing via `ReadDirectory`

## Key Classes

- `FileSystem` - Main implementation of `IFileSystem`, unrooted (operates on real, absolute/cwd-relative paths)
- `PhysicalFileSystem` - `IFileSystem` jailed to a real disk directory; validates every path stays within that root before delegating to an inner `FileSystem`
