# Filesystem

Thin wrapper around Linux filesystem syscalls.

## Responsibilities

- Provides file operations: open, close, read, write
- Provides directory operations: mkdir, rmdir, chdir, getcwd
- Provides custom directory listing via `ReadDirectory`

## Key Classes

- `Filesystem` - Main implementation of `IFilesystem`
