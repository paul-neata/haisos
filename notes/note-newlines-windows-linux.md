# Newlines on Windows and Linux

A Linux file holds exactly what is written; the Windows C runtime also has a
text mode, which translates line endings. How Haisos deals with that:

- **Files through an `IFileSystem` are byte-exact on both**, as long as they are
  opened with the `kFileOpen*` constants of
  `src/components/Filesystem/FilesystemUtils.h`, which add `_O_BINARY` on
  Windows. Everything in `src/` does (`ReadWholeFile` and so program loading,
  `os_write_file`, `cat`, `COPY`/`OUTCOPY`): an agent writing `"a\nb"` gets LF
  on disk on Windows too, and a CRLF file is read with its `\r`. A caller
  passing bare `O_RDONLY`/`O_WRONLY` -- only tests do -- gets text mode on
  Windows instead: `\n` written as `\r\n`, `\r\n` read as `\n`, and a 0x1A byte
  read as end of file. Possible hardening: have the Windows `FileSystem`
  (`src/components/Filesystem/windows/WindowsFilesystem.cpp`) always add
  `_O_BINARY`, so no caller can get text mode.
- **A haisosfile may have CRLF line endings** -- written on Windows, e.g. by
  `haisos --init`, whose `std::ofstream` is in text mode, or checked out by git
  with `core.autocrlf`. `StripCarriageReturn` in `src/haisos/HaisosFileParser.cpp`
  drops the `\r` of every line, so directives parse alike, and `CREATE`/`APPEND`
  content, `multiline` blocks included, is joined with `\n` alone.
- **Programs are read as they are**: a `.md` agent program with CRLF reaches
  the LLM with `\r\n` in it; Lua takes either line ending.
- **Console input**: `Console::ReadLine` (`src/components/Console/Console.cpp`)
  drops a trailing `\r`, so a line typed on Windows, or piped from a CRLF file,
  reaches an interactive agent without it.
- **Output**: the console, the Logger's stderr and the log files (`-l`, `-L`:
  `ReopeningLogFile`) are written in text mode, so on Windows their lines end
  in `\r\n` -- also when redirected to a file or a pipe. A script checking
  `haisos.exe`'s output strips `\r` (`tr -d '\r'`) before matching whole lines.
- **Tests**: a test writing a host file with `std::ofstream` (text mode), or
  opening one with bare `O_*` flags, gets `\r\n` on Windows; open with
  `std::ios::binary`, or the `kFileOpen*` constants, whenever the bytes are
  checked.
