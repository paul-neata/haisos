# FileSystemService

Service-layer wrapper over a single `IFileSystem` instance.

## Responsibilities

- Holds and exposes the `IFileSystem` it was constructed with (physical/rooted, unrooted, or a future composed filesystem)

## Key Classes

- `FileSystemService` - Main implementation of `IFilesystemService`
