# Task tools--tar-create-list: tar, creating and listing archives

- Rock: tools
- Depends on: search--grep-recursive (`FnMatch`, `kFnm*` in `BuiltinFnmatch.h`), coreutils--date (`FormatDateTime`, `ParseDateString` with its `utc` overload, in `BuiltinDate.h`)
- Size: ~950 changed lines in ~12 files (the upper end of a task: the option table alone is ~150 lines)
- Plan checked against: develop @ ccb9dbe
- PR title: Add the tar builtin: create and list archives

## Goal

A new builtin `tar` (`BUILTIN rootfs tar /bin/tar`) after GNU tar 1.35,
creating (`-c`) and listing (`-t`) uncompressed archives in the GNU format
(the default, long names as `././@LongLink` entries) or POSIX ustar, byte
for byte what GNU tar writes for the same tree with
`--sort=name --owner=haisos:0 --group=haisos:0 --mode='a=rX,u+w'` (files 0644,
directories 0755). All three
option styles work: traditional (`tar cvf x.tar src`), short (`tar -cvf
x.tar src`), long (`tar --create --file=x.tar src`). Listing prints GNU's
formats (`-t`: names; `-tv`: `-rw-r--r-- haisos/haisos     6 2024-01-02
03:04 src/a.txt`). Compression is not available: `-z`/`-j`/`-J`/`--zstd`/...
fail exactly as GNU tar does on a system without that compressor, exit 2,
and never write an uncompressed archive in its place. Every message, exit
status and the `Exiting with failure status due to previous errors` tail
are GNU's.

Extraction, comparison and archive modification (`-x -d -r -u -A
--delete`) are the next task, tools--tar-extract; this task builds the
archive reader they share.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"),
`src/components/BuiltinCommands/CLAUDE.md` (output rules, adding a builtin,
the ls row's exceptions), `BuiltinCommand.h` (`ParseBuiltinArgs`,
`BeginBuiltin`, `BuiltinHelpText`, `ReportNotTreated`/`NotTreated`,
`Out`/`Error`/`ErrorText`), `commands/mkdir/Mkdir.cpp` (a small builtin
end to end), `src/components/Filesystem/FilesystemUtils.h` (`EntryTypeOf`,
`VirtualParentOf`, `kFileOpen*`, `kFileCreateMode`, `CurrentFileDateTime`),
`interfaces/IFileIO.h` (`OpenFile`, `ReadDirectory`, `Stat`,
`GetDescriptor(kStdIn/kStdOut)`, `ResolvePath`), `interfaces/IFileSystemService.h`
(`FileStatus`, `FileDateTime`, `DirectoryEntryType`),
`interfaces/IFileDescriptor.h` (`Read`/`Write`, `kIOInterrupted`,
`IsTerminal`).

What earlier tasks provide, as if already on develop:

- search--grep-recursive, `src/components/BuiltinCommands/BuiltinFnmatch.h`:
  `bool FnMatch(std::string_view pattern, std::string_view text, int flags = 0);`
  with `kFnmPathname = 1`, `kFnmNoEscape = 2`, `kFnmPeriod = 4`,
  `kFnmLeadingDir = 8`, `kFnmCaseFold = 16` (glibc `fnmatch()`).
- coreutils--date, `src/components/BuiltinCommands/BuiltinDate.h`:
  `std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);`
  and `bool ParseDateString(std::string_view text, FileDateTime now, bool utc, FileDateTime& out);`
  (GNU `-d` subset: `@seconds`, `YYYY-MM-DD[ HH:MM[:SS]]`, relative items ...).

Haisos has no permissions, users or groups: `ls -l` shows `rwxrwxrwx` and
`haisos`, and coreutils--stat reports uid/gid `0` named `haisos`. tar
follows them for owners -- uid/gid `0`, uname/gname `haisos` -- but not
for modes: a member is written with mode `0644` (a file or device) or
`0755` (a directory), so that an archive extracted on a real system does
not make every file executable (the user's choice; `ls` inside Haisos still
shows `rwxrwxrwx`).

GNU tar 1.35 is in the task container (`tar --help`, `tar --show-defaults`):
use it for every "verify against GNU tar" below.

## Changes

### Rules that bite (root CLAUDE.md, restated)

- `ICurrentProcess` is the only door out: every file through
  `context.IO()` (`OpenFile`, `ReadDirectory`, `Stat`, `ResolvePath`), the
  standard streams through `context.IO()->GetDescriptor(...)`/`context.Out`;
  no `IFileSystem` or `IHaisosOS` held. Never `ChangeDirectory` the process
  for `-C`: compose paths (below).
- No symbolic or hard link is ever created (none can be: Haisos's
  filesystems have none, and this task only reads the disk).
- GNU's output byte for byte; every option of GNU tar 1.35 in `Options()`;
  `--help` from `BuiltinHelpText`; `--version` (`tar (HaisosOS builtin)
  1.0.0`); registered in `CreateStandardBuiltinCommands()` (which also puts
  `# BUILTIN rootfs tar /bin/tar` in the `haisos --init` template -- never
  hand-write it); sources in `src/components/BuiltinCommands/CMakeLists.txt`;
  portable C++17 (Linux, MSVC, WASM): no POSIX headers, no `<regex>`.
- Stops promptly on `TriggerStop()` (`context.StopRequested()` between
  members and between 64 KiB chunks; a read returning `kIOInterrupted` ends
  the command); a broken stdout pipe is handled by `context.Out` (141).

### `src/components/BuiltinCommands/BuiltinCommand.h/.cpp` -- one field

GNU tar parses its arguments in order (`-C` and `-T` apply to the operands
after them). Add to `ParsedBuiltinOption`:

```cpp
// How many operands came before this option on the command line: tar's -C
// applies to the operands from that index on.
size_t operandsBefore = 0;
```

set by `ParseBuiltinArgs` for every option it records (the count of
`parsed.operands` at that moment). Nothing else changes; existing callers
ignore it.

### New directory `src/components/BuiltinCommands/commands/tar/`

Files (all namespace `Haisos`, internal names in a `Tar` sub-namespace or
`Tar`-prefixed; none implements an `interfaces/` class, so plain classes
and structs are fine):

#### `TarFormat.h` / `TarFormat.cpp` -- the header, byte for byte

```cpp
constexpr size_t kTarBlockSize = 512;

enum class TarFormat { Gnu, Ustar, V7 };

struct TarMember {
    std::string name;        // full name, long names already joined
    std::string linkName;    // typeflag '1'/'2'
    char typeflag = '0';     // '0' (or '\0') file, '5' dir, '1' hard link, '2' symlink, '3' char, '4' block, '6' fifo, '7' contiguous
    uint32_t mode = 0;       // the 12 low mode bits
    uint64_t uid = 0, gid = 0;
    std::string uname, gname;
    uint64_t size = 0;
    int64_t mtime = 0;       // seconds since the epoch
    uint32_t devMajor = 0, devMinor = 0;
};

// The blocks that announce |member|: for Gnu, a ././@LongLink 'K' entry
// (linkName > 100 bytes) and/or 'L' entry (name > 100 bytes) -- each a header
// plus its data blocks -- then the member's own header; for Ustar the name
// split at a '/' into prefix (<= 155) and name (<= 100). Returns false, with
// |error| set to GNU's text, when Ustar/V7 cannot hold the name:
//   "file name is too long (max 256); not dumped"          (ustar, > 256 bytes)
//   "file name is too long (cannot be split); not dumped"  (ustar, no '/' split fits)
//   "file name is too long (max 99); not dumped"           (v7)
bool EncodeMemberHeader(const TarMember& member, TarFormat format, std::string& blocks, std::string& error);

enum class TarHeaderStatus { Valid, ZeroBlock, Invalid };
// Decodes one 512-byte header (checksum verified: the unsigned sum of the
// bytes with the chksum field read as 8 spaces; GNU also accepts the signed
// sum -- accept either). Does not join long names (TarReader does).
TarHeaderStatus DecodeHeader(const char* block, TarMember& member);

// "-rwxrwxrwx" / "drwxr-xr-x" / "hrw-r--r--" ...: GNU tar's type letter
// ('-' file/contiguous, 'd', 'h' hard link, 'l' symlink, 'c', 'b', 'p') then
// the nine permission letters with setuid/setgid/sticky (s S t T).
std::string TarModeString(const TarMember& member);

// The compressor whose magic starts |data| ("gzip" 1f 8b, also 1f 9d;
// "bzip2" "BZh"; "xz" fd 37 7a 58 5a 00; "zstd" 28 b5 2f fd; "lzip" "LZIP";
// "lzop" 89 4c 5a 4f), or empty.
std::string CompressorForMagic(std::string_view data);
```

Header layout (offsets in bytes; every numeric field octal, zero-padded to
the field size minus one, then NUL; verified against GNU tar 1.35):

| field | offset | size | GNU writes |
|-------|--------|------|------------|
| name | 0 | 100 | the name, NUL-padded (exactly 100 bytes: no NUL) |
| mode | 100 | 8 | `0000644\0` (a file or device), `0000755\0` (a directory) |
| uid, gid | 108, 116 | 8 each | `0000000\0` |
| size | 124 | 12 | `00000000006\0` (0 for a directory) |
| mtime | 136 | 12 | `14544676445\0` |
| chksum | 148 | 8 | 6 octal digits, NUL, space: `012160\0 ` |
| typeflag | 156 | 1 | `0` file, `5` directory, `3` char device, `L`/`K` long name/link |
| linkname | 157 | 100 | NUL |
| magic+version | 257 | 8 | Gnu: `ustar  \0` (`ustar`, two spaces, NUL); Ustar: `ustar\0` then `00`; V7: all NUL |
| uname, gname | 265, 297 | 32 each | `haisos`, NUL-padded |
| devmajor, devminor | 329, 337 | 8 each | NUL (Gnu and Ustar alike), except for a device: octal like mode |
| prefix (ustar) | 345 | 155 | Ustar only; Gnu leaves 345-511 NUL |

A value too big for its octal field (size >= 8 GiB, a negative mtime) is
written GNU's base-256 way (first byte 0x80 or 0xff, big-endian two's
complement); `DecodeHeader` reads both forms.

The GNU long-name entry, written before a member whose name exceeds 100
bytes: a header with name `././@LongLink`, mode `0000644\0`, uid/gid
`0000000\0`, size = name length + 1 (the NUL), mtime `00000000000\0`,
typeflag `L` (`K` for a long link name), magic `ustar  \0`, uname and gname
`root`, its own checksum; then the name plus a NUL, padded with NUL to a
block boundary; then the member's header whose name field holds the first
100 bytes of the name. (Verified: GNU writes `root` there even under
`--owner`.)

Golden values (GNU tar 1.35, `--mtime=@1704164645 --owner=haisos:0
--group=haisos:0 --mode='a=rX,u+w'`, `src` 0755 and `src/a.txt` 0644): the header of
`src/` has mode `0000755\0` and chksum `011174\0 `; that of `src/a.txt` (6
bytes) mode `0000644\0` and chksum `012151\0 `.

#### `TarReader.h` / `TarReader.cpp` -- members from a descriptor

```cpp
class TarReader {
public:
    // |input| is the archive (a file opened read-only, or descriptor 0).
    TarReader(BuiltinContext& context, std::shared_ptr<IFileDescriptor> input);
    enum class Next { Member, End, Error, Stopped };
    // The next member, long name/link entries ('L', 'K') joined in. Data is
    // read with ReadData/SkipData before the next call.
    Next NextMember(TarMember& member);
    // Up to |count| bytes of the current member's data; 0 at its end.
    ssize_t ReadData(char* buffer, size_t count);
    bool SkipData();
    // The raw bytes of the current member: its headers (long-name entries
    // included) and its padded data -- for tar-extract's -r/-u/--delete
    // rewriting; collected only when enabled before NextMember.
    void KeepRawBytes(bool keep);
    const std::string& RawBytes() const;
    // Whether any error was reported (makes the exit status 2).
    bool HadError() const;
};
```

Reading rules (GNU's messages, `tar: ` prefix via `context.Error`):

- The first block starts with a compressor's magic (`CompressorForMagic`):
  the read-side compressor failure below, exit 2.
- An empty archive (0 bytes): `This does not look like a tar archive`, then
  at the end `Exiting with failure status due to previous errors`, exit 2.
- A header failing its checksum: if it is the first header, `This does not
  look like a tar archive`; then `Skipping to next header` (once per run of
  bad blocks), scanning block by block for the next valid header; exit 2 at
  the end.
- End of archive: a zero block followed by another zero block (or by end of
  file at a block boundary). A zero block followed by end of file: warning
  `A lone zero block at N` (N the 1-based number of that block), status
  unchanged. End of file at a block boundary between members: a silent end.
- End of file inside a header or a member's data: `Unexpected EOF in
  archive`, then `Error is not recoverable: exiting now`, exit 2.
- Reads loop over partial reads; check `StopRequested()` and treat
  `kIOInterrupted` as `Stopped`.

#### `TarOptions.h` / `TarOptions.cpp` -- the command line

`struct TarSettings` holding everything parsed: the mode
(`None, Create, List, Extract, Diff, Append, Update, Catenate, Delete`),
archive name (default `-`), verbosity count, the ordered name list
(`struct TarName { std::string name; std::string directory; }` -- each
operand or `-T` entry with the `-C` directory in force for it), exclusion
patterns and their matching flags, `noRecursion`, `absoluteNames`,
`format`, owner/group overrides, `numericOwner`, `mtime` (+ `clampMtime`),
`utc`, `fullTime`, `wildcards` (for member operands), `recordSize`
(default 10240), compression program (empty: none), and the extract-side
fields tools--tar-extract fills (leave room; that task adds them).

`bool ParseTarArgs(BuiltinContext& context, const IBuiltinCommand& command, TarSettings& settings, int& exitStatus);`
-- false when tar is already done (help, version, a usage error), with
`exitStatus` set. Steps:

1. **Traditional style**: when the first argument does not start with `-`
   (and there is at least one), expand it: each letter `x` becomes `-x`;
   each letter whose option takes an argument (look it up in `Options()`:
   `BuiltinArgument::Required`, e.g. `f b C T X g K L N V H`) consumes the
   next argument word after the cluster, in letter order (`tar cvfb x.tar
   20 src` is `-c -v -f x.tar -b 20 src`). A missing word is the usage
   error `option requires an argument -- 'f'`.
2. `ParseBuiltinArgs(expanded, Options())`. On `parsed.error`: print
   `tar: <error>` then `Try 'tar --help' or 'tar --usage' for more
   information.`, exit **64** (argp's EX_USAGE: verified, `tar --foo` and
   `tar -cf` exit 64). Note GNU's ambiguous-option message lists the
   candidates (`option '--ex=x' is ambiguous; possibilities: '--extract'
   '--exclude' ...`); the parser's shorter `option '--ex=x' is ambiguous`
   is accepted as is (documented exception).
3. `--help`, `-?`, `--usage`: `BuiltinHelpText(command)`, exit 0;
   `--version`: `BuiltinVersionText`, exit 0 (first one wins, as
   `BeginBuiltin` does). Then `context.ReportNotTreated(parsed)`.
4. Walk `parsed.options` in order, using `operandsBefore` to interleave
   `-C DIR` and `-T FILE` with the operands: a `-C` sets the directory for
   the operands at index >= its `operandsBefore` (and for the names of
   later `-T`s); `-T FILE` inserts that file's names at its position. `-C`
   directories are relative to the previous one (GNU chdirs), so resolve
   each against the one before (`/` joins; an absolute one replaces).
5. Mode letters: more than one of `-A -c -d -t -r -u -x --delete` (the same
   one twice is fine) -> `tar: You may not specify more than one '-Acdtrux',
   '--delete' or  '--test-label' option` (two spaces before
   `'--test-label'`, verbatim) + the Try line, exit 2. None -> `tar: You
   must specify one of the '-Acdtrux', '--delete' or '--test-label'
   options` + Try, exit 2.
6. `--format`/`-H`: `gnu`, `oldgnu` (same as gnu here), `ustar`, `v7`
   treated; `posix`, `pax` -> `context.NotTreated("--format=posix")` (the
   spelling as given) and gnu is used; anything else -> `tar: bogus: Invalid
   archive format` + the Try line, exit 2 (verified).
7. `-b N`: record size N*512 (N >= 1); `-b 0` or not a number -> `tar: 0:
   Invalid blocking factor` + Try, exit 2. `--record-size=N` must be a
   multiple of 512: else `tar: Record size must be a multiple of 512.` +
   Try, exit 2 (verified). `--sort=X` other than `none`/`name`/`inode` ->
   `tar: invalid argument 'X' for '--sort'`, `Valid arguments are:`, then
   `  - 'none'`, `  - 'name'`, `  - 'inode'` (two spaces, dash, space; one
   per line), **no** Try line, exit 2 (verified; write it directly, not
   with `ArgMatch`, which adds a Try line and exits 1).
8. Compression (`-z -j -J -Z -a --zstd --lzip --lzma --lzop -I PROG`,
   `--use-compress-program=PROG`, `--gzip --bzip2 --xz ...` long forms):
   record the program name (`gzip`, `bzip2`, `xz`, `compress`, `zstd`,
   `lzip`, `lzma`, `lzop`, PROG's first word). `-a`/`--auto-compress`
   (create only) picks it from the archive's suffix: `.gz .tgz .taz` gzip,
   `.Z .taZ` compress, `.bz2 .tbz .tbz2 .tz2` bzip2, `.lz` lzip, `.lzma
   .tlz` lzma, `.lzo` lzop, `.xz .txz` xz, `.zst .tzst` zstd; another
   suffix: no compression. See "Compression" below for what happens.

#### `TarCreate.cpp` -- `-c`

`int TarCreate(BuiltinContext& context, TarSettings& settings);`

1. No names at all (no operands, no `-T`) -> `tar: Cowardly refusing to
   create an empty archive` + `Try 'tar --help' or 'tar --usage' for more
   information.`, exit 2 (an empty `-T` list is fine: an archive of just
   the end blocks).
2. Compression requested -> the write-side failure (below), before opening
   anything.
3. Open the archive: `-` is standard output -- refused when descriptor 1
   is a terminal: `tar: Refusing to write archive contents to terminal
   (missing -f option?)` then `tar: Error is not recoverable: exiting now`,
   exit 2; otherwise written through `context.Out` (binary-safe:
   `std::string` bytes). A name: `IO().OpenFile(name,
   kFileOpenWriteCreateTruncate, kFileCreateMode)`; null -> `tar: NAME:
   Cannot open: <reason>` + `tar: Error is not recoverable: exiting now`,
   exit 2, the reason found as mkdir does (parent missing `No such file or
   directory`, parent not a directory `Not a directory`, the name a
   directory `Is a directory`, else `Permission denied`). Write errors:
   `tar: NAME: Cannot write: Input/output error` + not recoverable, exit 2.
4. For each name (in order, resolved against its `-C` directory; a missing
   `-C` directory -> `tar: DIR: Cannot open: No such file or directory` +
   not recoverable, exit 2):
   - the member name: the name as given, with GNU's `safer_name_suffix`
     unless `-P`: strip the longest prefix made of `/` and `..` components
     (`/abs/x` -> `abs/x`, `../f` -> `f`, `./../f` -> `f`, `a/../../x` ->
     `x`) and say, once per distinct stripped prefix, ``tar: Removing
     leading `/' from member names`` (the prefix as it was: `` `../' ``,
     `` `./../' ``, `` `a/../../' ``). A name that is nothing but such a
     prefix (`/`, `..`) is archived as `./` (its entries as `./x`) -- a
     Haisos choice: it must never produce an empty name.
   - `Stat` it; missing -> `tar: NAME: Cannot stat: No such file or
     directory`, continue, failure status.
   - It resolves to the archive itself (`ResolvePath` equal) -> `tar: NAME:
     archive cannot contain itself; not dumped`, status unchanged.
   - Excluded (below) -> skipped silently, contents included.
   - A directory: a `5` member named `name/` (one `/`, not two, when the
     operand already ends in `/`), size 0; then, unless `--no-recursion`,
     its entries from `ReadDirectory` (skip `.` and `..`) **sorted by name,
     bytewise** (GNU's `--sort=name`; a documented exception: GNU's
     default is directory order, which depends on the disk), each recursed
     with `name/entry`. The `--recursion`/`--no-recursion` in force for a
     name is the last one before it (use `operandsBefore`).
   - A file (a builtin's path is a file: its note is its content): a `0`
     member, size from `Stat`, then the content read in 64 KiB chunks,
     padded with NUL to 512. If fewer bytes come than `Stat` said: write
     zeros for the rest and warn `tar: NAME: File shrank by N bytes;
     padding with zeros` (failure status); more: stop at the size. Open
     failure -> `tar: NAME: Cannot open: Permission denied`, failure status.
   - A character device: a `3` member with its `deviceMajor`/`deviceMinor`
     (size 0).
   - Fields: mode `0644` for a file or device, `0755` for a directory; uid/gid `0`; uname/gname `haisos` (`--owner`,
     `--group`: `NAME`, `NAME:ID`, `+ID` or `ID` -- the name and/or number
     as given; `--numeric-owner`: uname/gname left empty); mtime from
     `Stat` (`modificationTime.seconds`), or `--mtime` (`ParseDateString(text,
     CurrentFileDateTime(), settings.utc, out)`; a value starting with `/`
     or `.` is a file whose mtime is used; unparsable -> GNU goes on with
     `tar: Substituting -9223372036854775807 for unknown date format 'X'`
     on stderr and that mtime, status unchanged; a date file that is
     missing -> `tar: F: Cannot stat: No such file or directory`, `tar: Date
     sample file not found` + Try, exit 2 -- both verified) with `--clamp-mtime` using it only for
     newer files.
   - `EncodeMemberHeader` refusing a long name -> `tar: NAME: <its error>`,
     failure status, member skipped.
   - `-v`: the member name and `\n`; `-vv`: the listing line (as `-tv`) --
     to standard output, or to standard error when the archive is standard
     output (GNU's `stdlis`).
5. End: two zero blocks, then NUL padding to a multiple of the record size
   (10240 by default; the whole archive, counted from its first byte).
6. Exit status: 2 with `tar: Exiting with failure status due to previous
   errors` (after everything else) if any member failed, else 0.

**Exclusion** (`--exclude=PATTERN`, `-X FILE` one pattern per line,
`--exclude-vcs`): a name (the path as walked, before stripping) is excluded
when a pattern matches it at its start or after any `/` in it (GNU's
unanchored default; `--anchored`: at the start only), with
`FnMatch(pattern, tail, kFnmLeadingDir | (ignoreCase ? kFnmCaseFold : 0) |
(wildcardsMatchSlash ? 0 : kFnmPathname))` -- `--wildcards-match-slash` is
the default for exclusions; `--no-wildcards` compares literally (still
leading-dir and unanchored). The `--anchored`, `--ignore-case`,
`--wildcards`, `--wildcards-match-slash` flags in force for an
`--exclude` are those given before it. `--exclude-vcs` adds GNU's list,
matched against the base name: `CVS/ RCS/ SCCS/ .git/ .arch-ids/ {arch}/`
(the `/` ones match directories only), `.cvsignore .gitignore
.gitattributes .gitmodules =RELEASE-ID =meta-update =update .bzr
.bzrignore .bzrtags .hg .hgignore .hgtags _darcs`. Verified:
`--exclude=sub` drops `src/sub` and its contents; `--exclude-vcs` drops
`v/.git`.

`-T FILE` (`--files-from`): names one per line (`\n`, a trailing `\r`
kept; with `--null`, NUL-separated), `-` meaning standard input; empty
lines skipped. A line is always a name (GNU would read `-C dir` lines as
options: not treated, documented). Missing file: `tar: FILE: Cannot open:
No such file or directory` + not recoverable, exit 2.

**Compression** (none in Haisos; GNU's messages on a system without the
compressor, verified with `PATH=/nonexistent /usr/bin/tar`):

- write side (`-c` with `-z`, etc.), to stderr, exit 2, nothing created
  (documented exception: GNU creates the archive file, empty):
  ```
  /bin/sh: 1: gzip: not found
  tar: Child returned status 127
  tar: Error is not recoverable: exiting now
  ```
- read side (`-t` with `-z`, or an archive whose first bytes are a
  compressor's magic), exit 2:
  ```
  tar (child): gzip: Cannot exec: No such file or directory
  tar (child): Error is not recoverable: exiting now
  tar: Child returned status 2
  tar: Error is not recoverable: exiting now
  ```
  (`gzip` replaced by the program's name.) Both through `ErrorText`.

#### `TarList.cpp` -- `-t`, and member selection

`int TarList(BuiltinContext& context, TarSettings& settings);`

Open the archive for reading: `-` is descriptor 0, refused when it is a
terminal (`tar: Refusing to read archive contents from terminal (missing -f
option?)` + not recoverable, exit 2); a name: `OpenFile(name,
kFileOpenReadOnly)`, null -> `tar: NAME: Cannot open: No such file or
directory` (reason as above) + `tar: Error is not recoverable: exiting
now`, exit 2. Compression requested -> the read-side failure.

`class TarMemberSelector` (shared with tools--tar-extract), built from the
operands: `bool Selects(const std::string& memberName);` and
`void ReportUnmatched(BuiltinContext&)`. Without `--wildcards` an operand
matches a member equal to it or a member under it (`src` selects `src/`
and `src/a.txt`; trailing `/` ignored on both sides); with `--wildcards`
(the flags in force: `--anchored` default for members, `--ignore-case`,
`--wildcards-match-slash`) `FnMatch(operand, member, kFnmLeadingDir |
...)`. No operands: everything. After the archive: every operand that
selected nothing -> `tar: OPERAND: Not found in archive`, failure status.
An operand holding `*?[` without `--wildcards`/`--no-wildcards`: before the
"not found" lines, once, `tar: Pattern matching characters used in file
names` and `tar: Use --wildcards to enable pattern matching, or
--no-wildcards to suppress this warning` (verified).

Each selected member: unless `-P`, the `safer_name_suffix` warning as for
create (once per prefix; the name is listed unchanged). `-t`: the name and
`\n`; `-tv` (and `-tvv`): the long line, from GNU's `print_header`:

```
<mode string> <user>/<group> <size, right-aligned> <date> <name>[ -> link | link to link]
```

- user/group: uname or, empty or `--numeric-owner`, the uid; likewise gid.
- the size column (a device: `major,minor`) is right-aligned so that
  `user/group size` takes at least `ugswidth` columns: with
  `pad = len(user) + 1 + len(group) + 1 + len(size)`, if `pad > ugswidth`
  then `ugswidth = pad`; the size is printed with width
  `ugswidth - pad + len(size)`. `ugswidth` starts at 19 and only grows,
  for the whole run. (`paul/paul` size 0 -> `paul/paul         0`;
  `haisos/haisos` size 6 -> `haisos/haisos     6`.)
- date: `FormatDateTime("%Y-%m-%d %H:%M", mtime, settings.utc)`; with
  `--full-time` `"%Y-%m-%d %H:%M:%S"`.
- name (and link names) in GNU's escape quoting (C locale): `\` -> `\\`,
  `\a \b \f \n \r \t \v`, any other byte below 0x20, 0x7f and every byte
  >= 0x80 as `\ooo`; the plain `-t` lines and every `tar: NAME:` message use
  the same quoting.
- symlink `name -> target`; hard link `name link to target`.

Exit status as for create (2 with the `Exiting with failure status` tail
after any error, `HadError()` included).

#### `Tar.cpp` -- the command

`CreateTarCommand()`; `Name()` `tar`, `Version()` `1.0.0`; `Help()`:
summary `an archiving utility`, usage `tar [OPTION...] [FILE]...`, notes
(below). `Run`: `ParseTarArgs`, then dispatch on the mode (`Create`,
`List`; the others print, for now, `tar: <mode> is not available yet`
-- tools--tar-extract replaces that), and return the status.

`Options()`: **every** option in GNU tar 1.35's `tar --help` (copy the list
from the container: `tar --help`, ~150 options, short and long spellings
together, arguments as `Required`/`Optional` as `--help` shows them). Treated
in this task (ids of your own):

`-c --create`, `-t --list`, `-f --file=ARCHIVE`, `-C --directory=DIR`,
`-v --verbose`, `-T --files-from=FILE`, `--null`, `--no-null`,
`--verbatim-files-from` (a no-op: always so), `--exclude=PATTERN`,
`-X --exclude-from=FILE`, `--exclude-vcs`, `--recursion`,
`--no-recursion`, `-P --absolute-names`, `-H --format=FORMAT`, `--owner`,
`--group`, `--numeric-owner`, `--mtime`, `--clamp-mtime`, `--sort=ORDER`
(`name` and `none` -- both give name order -- and `inode` the same,
documented), `--utc`, `--full-time`, `-b --blocking-factor`,
`--record-size`, `--anchored`, `--no-anchored`, `--wildcards`,
`--no-wildcards`, `--wildcards-match-slash`, `--no-wildcards-match-slash`,
`--ignore-case`, `--no-ignore-case`, `-z --gzip --gunzip --ungzip`,
`-j --bzip2`, `-J --xz`, `-Z --compress --uncompress`, `--zstd`, `--lzip`,
`--lzma`, `--lzop`, `-a --auto-compress`, `--no-auto-compress`,
`-I --use-compress-program=PROG`, `-? --usage` (as `--help`).

The mode options `-x --extract --get`, `-d --diff --compare`,
`-r --append`, `-u --update`, `-A --catenate --concatenate`, `--delete`
get their mode ids now (step 5 needs them); their behaviour is the next
task's. `-O --to-stdout`, `-k`, `--overwrite`, `--skip-old-files`,
`--keep-newer-files`, `--strip-components`, `-m --touch`, `-U
--unlink-first` are `kBuiltinNotTreated` in this task (tools--tar-extract
gives them ids). Everything else `kBuiltinNotTreated` (e.g.
`--transform`/`--xform`, `--checkpoint*`, `-g --listed-incremental`, `-M`,
`-W --verify`, `--totals`, `-R --block-number`, `--index-file`,
`--quoting-style`, `--mode`, `--exclude-caches*`, `--exclude-tag*`,
`--exclude-vcs-ignores`, `--exclude-backups`, `--acls`, `--xattrs`,
`--selinux`, `-p`, `--same-owner`, `--show-defaults`, `--show-transformed-names`, `--remove-files`, `-h --dereference`, `--hard-dereference`, `-l --check-links`, `--one-file-system`, `-S --sparse`).

Help notes (a few lines): no compression (`-z -j -J ...` fail as without
the compressor); members are written with mode 0644 (directories 0755), owner and group
`haisos` (uid/gid 0); directories are archived in name order (`--sort=name`);
traditional `tar cvf` style accepted; ambiguous long options are not
listed.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Declare and register `CreateTarCommand()` (alphabetical place in the
list). Add `commands/tar/Tar.cpp TarOptions.cpp TarFormat.cpp
TarReader.cpp TarCreate.cpp TarList.cpp` to the `BuiltinCommands` library.

## Tests

New `tests/unit/components/BuiltinCommands.unittests/TarTest.cpp` (added to
that directory's `CMakeLists.txt`), on `BuiltinCommandsTest` and
`RunCaptured` (the fixture's in-memory root: `/notes.txt` 17 bytes,
`/docs/a.md` `alpha`, `/docs/sub/b.md` `bravo!`). Read archives back with
`ReadWholeFile(*root, path, bytes)`. Use `--mtime=@1704164645` (2024-01-02
03:04:05 UTC) and `--utc` wherever times show, so nothing depends on the
clock or the zone. A small test helper builds a raw GNU header (name, mode,
uname, gname, size, mtime, typeflag, checksum computed) for listing
archives Haisos did not write.

- `TarCreateGnuHeaderByteForByte`: make `/src` with `/src/a.txt` =
  `hello\n`; `tar --mtime=@1704164645 -cf /s.tar src` (cwd `/`) -> status
  0, no output; `/s.tar` is 10240 bytes; block 0 is `src/`'s header and
  block 1 `src/a.txt`'s, each field per the table (name, mode `0000755\0` / `0000644\0`,
  `0000000\0` twice, size `00000000000\0` / `00000000006\0`, mtime
  `14544676445\0`, chksum `011174\0 ` / `012151\0 `, typeflag `5` / `0`,
  magic `ustar  \0`, uname/gname `haisos`); block 2 `hello\n` + NUL;
  blocks 3-19 all NUL.
- `TarCreateUstarFormat`: `--format=ustar` -> magic `ustar\0` + `00`; a
  130-byte path `d.../f` with a 60-byte directory is split into prefix and
  name; a 120-byte single name -> err `tar: <name>: file name is too long
  (cannot be split); not dumped\ntar: Exiting with failure status due to
  previous errors\n`, status 2.
- `TarCreateGnuLongName`: a 120-byte file name -> a `././@LongLink` entry
  (typeflag `L`, size `00000000171\0` -- 121, the length plus the NUL -- uname `root`) then the
  name block, then the header with the first 100 bytes; `tar -tf` lists the
  full name.
- `TarListNamesAndVerbose`: `-tf /s.tar` -> `src/\nsrc/a.txt\n`;
  `--utc -tvf /s.tar` -> `drwxr-xr-x haisos/haisos     0 2024-01-02
  03:04 src/\n-rw-r--r-- haisos/haisos     6 2024-01-02 03:04
  src/a.txt\n`; `--full-time` shows `03:04:05`; a hand-built header
  `paul/paul` mode 0644 size 6 -> `-rw-r--r-- paul/paul         6 ...`;
  `--numeric-owner` -> `0/0` and the matching padding.
- `TarThreeOptionStyles`: `tar cvf /a.tar docs`, `tar -cvf /b.tar docs`,
  `tar --create --verbose --file=/c.tar docs` (all with the same
  `--mtime`) produce identical archives and print
  `docs/\ndocs/a.md\ndocs/sub/\ndocs/sub/b.md\n` (name order); `tar cvfb
  /d.tar 20 docs` works.
- `TarCreateToStdoutAndListFromStdin`: `tar -cvf - docs` -> stdout is the
  archive (10240 bytes), stderr the verbose names; feeding it as input to
  `tar -tf -` lists the names.
- `TarDirectoryOptionAndFilesFrom`: `tar -cf /x.tar -C docs a.md -C sub
  b.md` lists `a.md\nb.md\n`; a `-T /list` file holding `docs/a.md\n
  notes.txt\n` archives those two; `--null -T` with NUL separators.
- `TarExcludes`: `--exclude=sub docs` -> `docs/ docs/a.md`; `--exclude='*.md'`
  -> `docs/ docs/sub/`; `-X /pats`; `--exclude-vcs` drops `docs/.git` (make
  it) and `docs/.gitignore`; `--no-recursion docs docs/a.md` -> `docs/
  docs/a.md`.
- `TarAbsoluteNames`: `tar -cf /x.tar /docs/a.md` -> err ``tar: Removing
  leading `/' from member names\n``, member `docs/a.md`; `-P` keeps
  `/docs/a.md`, no message; `../` from cwd `/docs/sub`.
- `TarCreateErrors`: `tar -cf /x.tar` -> `tar: Cowardly refusing to create
  an empty archive\nTry 'tar --help' or 'tar --usage' for more
  information.\n`, 2; `tar -cf /x.tar nosuch` -> `tar: nosuch: Cannot stat:
  No such file or directory\ntar: Exiting with failure status due to
  previous errors\n`, 2; `tar -cf /docs/x.tar docs` -> `tar: docs/x.tar:
  archive cannot contain itself; not dumped\n`, 0; `tar -f /x.tar` -> the
  "must specify one of" lines, 2; `tar -ctf /x.tar` -> the "more than one"
  line (two spaces before `'--test-label'`), 2; `tar --bogus` -> `tar:
  unrecognized option '--bogus'\nTry 'tar --help' or 'tar --usage' for
  more information.\n`, 64; `tar -cf` -> `option requires an argument --
  'f'`, 64.
- `TarCompressionFails`: `tar -czf /x.tgz docs` -> the three write-side
  lines, 2, `/x.tgz` absent; `tar -caf /x.tar.xz docs` -> the same with
  `xz`; `tar -tzf /s.tar` -> the four read-side lines with `gzip`, 2; a
  file starting `\x1f\x8b` listed with `-tf` -> the same.
- `TarReadErrors`: an empty file -> `tar: This does not look like a tar
  archive\ntar: Exiting with failure status due to previous errors\n`, 2;
  `/s.tar` cut at 1200 bytes -> `src/\nsrc/a.txt\n` then err `tar:
  Unexpected EOF in archive\ntar: Error is not recoverable: exiting now\n`,
  2; three blocks + one zero block -> `tar: A lone zero block at 4\n`, 0;
  `-tf /nosuch.tar` -> `tar: /nosuch.tar: Cannot open: No such file or
  directory\ntar: Error is not recoverable: exiting now\n`, 2.
- `TarListMemberSelection`: `-tf /s.tar src/a.txt nope` -> out
  `src/a.txt\n`, err `tar: nope: Not found in archive\ntar: Exiting with
  failure status due to previous errors\n`, 2; `-tf /s.tar src` -> all;
  `-tf /s.tar 'src/*'` -> the two pattern warnings + not found, 2;
  `--wildcards 'src/*.txt'` -> `src/a.txt\n`, 0.
- `TarHelpAndVersion`: `--help` starts `HaisosOS tar version 1.0.0 -` and
  ends in the `Not treated arguments:` line; `--version` -> `tar (HaisosOS
  builtin) 1.0.0\n`; `tar -cvpf /x.tar docs` reports `Parameter -p is not
  treated by HaisosOS tar v. 1.0.0` on stderr and still archives.
- Add `tar` to the exact list in `ListsEveryBuiltinSortedWithAVersion`
  (`BuiltinCommandsTest.cpp`), in byte order.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Tar*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

(`TheInitTemplatesBuiltinsAllApplyOnceUncommented` lives in
`CliParser.unittests` and must stay green.)

Manual cross-check in the container (GNU tar is there): write a directory
`src` on the host, run `./output/linux/haisos` on a haisosfile with `FS
rootfs PHYSICAL <dir>`, `CREATE_DIR /bin`, `BUILTIN rootfs tar /bin/tar`,
`RUN /bin/tar --mtime=@1704164645 -cf /h.tar src`, then `tar --sort=name
--owner=haisos:0 --group=haisos:0 --mode='a=rX,u+w' --mtime=@1704164645 -cf
ref.tar src` and `cmp h.tar ref.tar`: identical (verified for a two-level
tree on GNU tar 1.35). Also `tar -tvf h.tar` with GNU tar lists it.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `tar` in the opening list and
  a table row: version 1.0.0; treated: `-c -t` (create, list) in the three
  option styles, `-f -C -v -T --null --exclude -X --exclude-vcs
  --no-recursion -P --format=gnu|ustar|v7 --owner --group --numeric-owner
  --mtime --clamp-mtime --utc --full-time -b --record-size` and the
  pattern flags; exceptions: no compression (fails as without the
  compressor; the archive is not created), modes 0644 (directories 0755) and owner/group
  `haisos` (uid/gid 0), name order always, `-T` lines are always names,
  ambiguous long options not listed, `--format=posix|pax` not treated.
  Also a note that the `commands/tar/` files are shared with
  tools--tar-extract (the reader, the selector).
- Root `CLAUDE.md`: a `tar` row in the Builtin Commands table and `tar` in
  the builtin lists (the table intro and the directory structure line).

## Acceptance

- [ ] `tar cvf`, `-cvf` and long options all parse; `-C`/`-T` apply in command-line order (`operandsBefore`).
- [ ] Archives are GNU's bytes: header fields, checksum `NNNNNN\0 `, long names via `././@LongLink`, ustar prefix split, two end blocks, 10240-byte records; the manual `cmp` against GNU tar passes.
- [ ] `-t`/`-tv` print GNU's lines (`ugswidth` padding, escape quoting, `--utc`, `--full-time`, `--numeric-owner`).
- [ ] Every message and exit status above (64 for parser errors, 2 for the rest, the `Exiting with failure status` tail), compression failing with GNU's no-compressor text and no archive written.
- [ ] Every GNU tar 1.35 option in `Options()`; untreated ones reported; `--help`/`--version`/`man tar` work.
- [ ] Only `context.IO()` and `context.Out` used for I/O; no `ChangeDirectory`; stop checked between members and chunks.
- [ ] Registered; CMakeLists; `ListsEveryBuiltinSortedWithAVersion` updated; tests green on Linux.

## Out of scope

- `-x`, `-d`, `-r`, `-u`, `-A`, `--delete`, `-O`, `-k` and the other
  extraction options: tools--tar-extract.
- Compression, multi-volume archives, incremental dumps, pax/posix headers,
  `--transform`, ACLs/xattrs, sparse files, remote archives.
- Creating links of any kind.
