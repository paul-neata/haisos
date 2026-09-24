# FileSystemService

Service-layer factory for composing filesystems. Holds no state of its own, and
never mutates the filesystems handed to it: each method returns a new
`IFileSystem` composed from them.

Composition here means delegation. A composed filesystem is not a projection or
a snapshot of what it wraps -- it routes each call to whichever underlying
filesystem owns that path, and that filesystem decides what the call does. So a
`PhysicalFileSystem` mounted inside an in-memory one still writes its paths
straight to disk, which is exactly the intent. (A filesystem that genuinely
*did* project -- copy-on-write, say -- would be a new implementation in
`src/components/Filesystem/`, not something these methods produce.)

## Responsibilities

- `CreateReadOnlyFileSystem` - wraps an existing filesystem, rejecting writes
- `CreateEmptyInMemFileSystem` - an empty, in-memory read/write filesystem
- `CreateSubFileSystem` - a filesystem confined to a sub-path of an existing filesystem, addressed purely through `IFileSystem` (no real disk access)
- `CreateComposedFileSystem` - overlays one filesystem inside another at a path, returning a new filesystem and leaving both operands untouched (to change one in place instead, call `IFileSystem::Mount` on it)

## Key Classes

- `FileSystemService` - Main implementation of `IFileSystemService`; delegates to the concrete filesystem classes in `src/components/Filesystem/`
