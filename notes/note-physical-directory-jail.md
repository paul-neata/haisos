# FS ... PHYSICAL is a jail at the directory, not a SubFileSystem of the full filesystem

`IFactory::CreateFullPhysicalFileSystem()` is the host's whole disk as one
filesystem: `PhysicalFileSystem("/")` on Linux, and `WindowsFullPhysicalFileSystem`
on Windows, where `/c/x` is `C:\x`. The original idea was for the haisos
executable to build each `FS <name> PHYSICAL <dir>` by taking `<dir>` out of
that filesystem, as a `SubFileSystem` of it. What was built instead (commit
8515a90, `TakePhysicalDirectory` in `src/haisos/HaisosFileSystemBuilder.cpp`)
differs deliberately:

- The directory is resolved in the full filesystem's namespace: relative to the
  haisosfile, and on Windows written as `/c/x`, `c:\x` or `c:/x` alike
  (`ResolvePhysicalPath`, `src/components/Filesystem/PhysicalPath.h`). It must
  be a directory there.
- It then becomes a filesystem of its own, `IFactory::CreatePhysicalFileSystem(<path>)`:
  a `PhysicalFileSystem` jailed at that directory.
- `FS <name> PHYSICAL /` on Windows is `CreateFullPhysicalFileSystem()` itself,
  every drive in it. On Linux, `/` is the disk's root anyway.

Why not a `SubFileSystem`:

- It confines paths only as written. The full filesystem under it follows a
  symbolic link wherever it leads, so a link inside the directory
  (`sub/up -> ..`) would take a process anywhere on the disk. A
  `PhysicalFileSystem` canonicalizes every path and follows links only within
  its own root. `HaisosFileSystemBuilderTest.ALinkInsideAPhysicalDirectoryCannotLeadOutOfIt`
  (POSIX) shows both behaviours.
- On Windows the full filesystem holds only drive letters, so a UNC path has no
  place in it. Yet `haisos.exe` started from a WSL home directory has a UNC
  current directory (`\\wsl.localhost\...`), and relative `PHYSICAL` paths
  resolve under it. `CreatePhysicalFileSystem` takes UNC paths directly.

Still open: a builtin `haisos` running inside Haisos (the "sandbox in the
sandbox" going through the full filesystem was meant to allow) cannot call
`IFactory`. It would take directories from its own OS's root, where a
`SubFileSystem` never reaches beyond that OS, but over a physical filesystem it
can still be left through a link.
