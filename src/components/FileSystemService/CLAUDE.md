# FileSystemService

Service-layer factory for composing filesystems. Holds no state of its own;
every method returns a new, independent `IFileSystem` view and never mutates
the filesystems passed in.

## Responsibilities

- `CreateReadOnlyFileSystem` - wraps an existing filesystem, rejecting writes
- `CreateEmptyInMemFileSystem` - an empty, in-memory read/write filesystem
- `CreateSubFileSystem` - a view confined to a sub-path of an existing filesystem, addressed purely through `IFileSystem` (no real disk access)
- `MountFileSystem` - overlays one filesystem inside another at a path

## Key Classes

- `FileSystemService` - Main implementation of `IFilesystemService`; delegates to the concrete filesystem classes in `src/components/Filesystem/`
