# ANSI text on Windows: UTF-8 inside Haisos, converted where it meets Windows

Haisos keeps all text in UTF-8: JSON for the LLM, Lua strings, names in every
`IFileSystem`. Windows' narrow calls -- `_open`, `_stat64`, `FindFirstFileA`,
`std::ofstream(std::string)`, and under MSVC `std::filesystem::path(std::string)`
and `path::string()` -- read and write text in the ANSI code page instead (1252
on a Western machine). So the UTF-8 name `café.txt` names another file there
(`cafÃ©.txt`), a name outside the code page cannot be written at all, and
`path::string()` throws on one. The approach: UTF-8 everywhere inside, and at the
boundary the UTF-16 ("W") calls, converting with `Utf8ToWide`/`WideToUtf8`
(`src/components/libheaders/WideText.h`).

Done (commit 8515a90):

- Files: `src/components/Filesystem/windows/WindowsFilesystem.cpp` calls
  `_wopen`, `_wmkdir`, `_wrmdir`, `_wunlink`, `_wstat64`, `FindFirstFileW`,
  `GetFileAttributesW` and `GetFullPathNameW`; `PhysicalFileSystem` builds host
  paths with `std::filesystem::u8path` and reads them back with `u8string()`;
  `WindowsFullPhysicalFileSystem` uses the W drive calls. A name that is not
  valid UTF-8 names no file (EINVAL). Test: `PhysicalFileSystemNameTest.NamesAreUtf8`.
- WinHTTP URLs and headers (`src/components/HTTPClient/windows/WinHTTPClient.cpp`).
- The haisosfile path on the command line: `LocateHaisosFile` in
  `src/haisos/main.cpp` converts it with `std::filesystem::path(arg).u8string()`.

Still ANSI, to do:

- The command line. `main(int argc, char* argv[])` receives ANSI, where a
  character outside the code page is already lost (replaced) before Haisos sees
  it. Nothing but the haisosfile path is converted: `-- key=value` overrides
  reach haisosfile paths and `RUN` arguments as ANSI bytes, and the `-l`/`-L`
  log paths go to `std::ofstream` as ANSI (`src/haisos/ReopeningLogFile.cpp`).
  Fix: `wmain` (or `GetCommandLineW` + `CommandLineToArgvW`), every argument
  turned into UTF-8 once, and log files opened with `std::filesystem::u8path`.
- The environment. `ENV NAME` imports the host's value with `std::getenv`
  (`src/haisos/main.cpp`): ANSI, e.g. `USERPROFILE` for a user named José. Fix:
  `_wgetenv` + `WideToUtf8`.
- The console. `Console` writes with `std::cout` and reads lines with
  `std::getline(std::cin)` (`src/components/Console/Console.cpp`), and the
  Logger writes to stderr, all in the console's code page (OEM 437/850 by
  default). Non-ASCII agent output shows garbled, and non-ASCII typed to an
  interactive agent (`RUN -i`) arrives as bytes that are not UTF-8. Fix:
  `SetConsoleOutputCP(CP_UTF8)` and `SetConsoleCP(CP_UTF8)` at start-up, or
  `WriteConsoleW`/`ReadConsoleW` when the handle is a console (bytes as they
  are when redirected).
