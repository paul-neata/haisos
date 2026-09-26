# Keep windows.h out of common files

`<windows.h>` defines macros named after Win32 functions: `CreateDirectory`
(expanding to `CreateDirectoryA` or `CreateDirectoryW`), `RemoveDirectory`,
`GetCurrentDirectory`, and `min`/`max` unless `NOMINMAX` is set. Haisos
interfaces have methods of those very names: `IFileSystem::CreateDirectory` and
`RemoveDirectory` (`interfaces/IFileSystemService.h`), and
`IFileIO::GetCurrentDirectory`. In a file including `windows.h`, they are
renamed in that file only: a call on a class declared before the include no
longer compiles, and a class declared after it gets a method named unlike
everywhere else.

So, as the code does now:

- `windows.h` is included only in Windows-only `.cpp` files -- the `windows/`
  folders: `src/components/Filesystem/windows/WindowsFilesystem.cpp`,
  `src/components/Filesystem/windows/WindowsFullPhysicalFileSystem.cpp`,
  `src/components/HTTPClient/windows/WinHTTPClient.cpp` -- never in a header
  that other files include, and never in a cross-platform source.
- There it comes after every Haisos header, followed by `#undef CreateDirectory`,
  `#undef RemoveDirectory` and `#undef GetCurrentDirectory`. The Windows parts
  of the tests in `tests/unit/components/Filesystem.unittests/` do the same.
- `src/components/libheaders/WideText.h` (UTF-8/UTF-16 conversion) includes
  `windows.h` itself, so it is included only from Windows-only `.cpp` files.
- When common code needs something Windows-specific, it is declared
  platform-neutrally in a common header and defined once per platform backend,
  as `NoCriticalErrorDialogs` (`src/components/Filesystem/NoCriticalErrorDialogs.h`,
  defined in `linux/PosixFilesystem.cpp` and `windows/WindowsFilesystem.cpp`),
  `FileSystem::IsLink` and `FileSystem::IsDevicePath` are. That is how
  `PhysicalFileSystem.cpp` stays common.
- Windows rules that are only string handling need no `windows.h` at all:
  `src/components/Filesystem/PhysicalPath.cpp` implements the Windows path rules
  in plain C++, compiled -- and unit-tested -- on Linux too.
