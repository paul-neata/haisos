# Big rocks

## links -- Follow symbolic links anywhere
Removes every piece of link-specific code from the disk-backed filesystems:
links and junctions on the disk are followed wherever they lead, as the host
follows them, and `DELETE` treats a link like whatever it points at.
Touches `interfaces/IFileSystemService.h` (`FileStatus`), the Filesystem
component (`PhysicalFileSystem`, `WindowsFullPhysicalFileSystem`,
`FileSystem::IsLink` on both platforms), `src/haisos/HaisosFileOperations.cpp`
(`DELETE`), their tests, and the docs that describe the confinement. About 350-450
changed lines, mostly removals.
Tasks: links--follow-anywhere
