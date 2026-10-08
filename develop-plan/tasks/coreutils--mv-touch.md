# Task coreutils--mv-touch: the mv and touch builtins and the shared date parser

- Rock: coreutils
- Depends on: base--fs-rename-times, coreutils--rm-rmdir, coreutils--cp
- Size: ~1000 changed lines in ~10 files (at the upper edge; see Out of scope for the fallback split)
- Plan checked against: develop @ ccb9dbe
- PR title: Add the mv and touch builtins and the date parser

## Goal

`mv` and `touch` are builtin commands (`BUILTIN rootfs mv /bin/mv`, `touch`)
behaving as GNU coreutils 9.4's (Ubuntu 24.04): `mv` renames in one step, and
across filesystems (a mount) copies then removes, as GNU mv does across
devices; `touch` creates files and sets their times, from now, `-d STRING`,
`-t STAMP` or `-r FILE`. So `touch -d "2024-01-02 03:04" out/a/x; cp -a out
out2; mv out2/a/x out2/a/y` leaves `out2/a/y` with that modification time.

The date parser is written once, in `BuiltinDate.h/.cpp`, as shared contract 4:
`date -d` (coreutils--date) uses `ParseDateString`, and that task adds
`FormatDateTime` to the same files.

## Context

Read first: the root `CLAUDE.md` ("Security", "Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `src/components/Filesystem/CLAUDE.md`.

What earlier tasks provide, as if on develop:
- base--fs-rename-times: in `interfaces/IFileSystemService.h`
  `constexpr int kFileSystemError = -1;` and
  `constexpr int kFileSystemCrossDevice = -2;`; on `IFileIO` (resolving
  against the working directory) `int Rename(const std::string& oldPath,
  const std::string& newPath)` -- 0; `kFileSystemCrossDevice` when the two
  paths are on different filesystems (either side of a mount) and nothing
  moved; `kFileSystemError` for every other failure (no errno). A file at
  newPath is replaced; an empty directory is replaced by a directory. And
  `int SetTimes(const std::string& path, const std::optional<FileDateTime>&
  accessTime, const std::optional<FileDateTime>& modificationTime)` (nullopt
  leaves that time; `kFileSystemError` on a builtin's path or a read-only
  filesystem).
- coreutils--rm-rmdir: `BuiltinPrompt` (`BuiltinPrompt.h`, `bool
  Ask(const std::string& question)`), `RemoveOperand(BuiltinContext&,
  BuiltinPrompt*, const std::string& path, const RemoveOptions&)` and
  `RemoveOptions { recursive, emptyDirectories, ignoreMissing, interactive,
  verbose, preserveRoot }` (`BuiltinRemove.h`): prints `removed 'x'` /
  `removed directory 'x'` with `verbose`, entries in name order.
- coreutils--cp: `BuiltinCopy.h` -- `BackupMode`, `UpdateMode`, `CopyVerbose
  { None, Cp, Mv }`, `CopyOptions`, `CopyTarget { source, dest }`,
  `ResolveCopyTargets(context, operands, targetDirectory, noTargetDirectory,
  parents, stripTrailingSlashes)`, `CopyPath(context, prompt, source, dest,
  options)`, `BackupPathFor(context, dest, mode, suffix)`,
  `ParseBackupControl(context, word, out)`; and `Cp.cpp`, whose handling of
  `-b/--backup/-S/VERSION_CONTROL/SIMPLE_BACKUP_SUFFIX`, `--update` and the
  invalid-argument blocks mv copies (or, better, factors into `BuiltinCopy`
  if it is not there yet).

What exists: `commands/ls/Ls.cpp` has `LocalTime(int64_t)` (localtime_r /
localtime_s under `CrtInvalidParameterAsError` on Windows) in an anonymous
namespace -- follow it; `FilesystemUtils.h` has `CurrentFileDateTime()` and
the open-flag constants; `FileDateTime { int64_t seconds; uint32_t
nanoseconds; }` is in `interfaces/IFileSystemService.h`.

Rules that bite: all file access through `context.IO()` (`ICurrentProcess` is
the only door out); GNU's output byte for byte; every GNU option in
`Options()`, untreated ones `kBuiltinNotTreated`; `--help` from
`BuiltinHelpText`; registration in `CreateStandardBuiltinCommands()` (which
feeds the `haisos --init` template); portable C++17: no POSIX headers, no
`timegm`/`_mkgmtime` (compute UTC yourself, below), `std::mktime` is fine.

## Changes

### `src/components/BuiltinCommands/BuiltinDate.h` / `.cpp` (new) -- contract 4

```cpp
namespace Haisos {
// GNU date -d / touch -d, the subset below. |now| is what relative items and
// "now" start from (touch -r passes the reference file's time instead).
// Local time is the host's time zone. False if |text| is not understood.
bool ParseDateString(std::string_view text, FileDateTime now, FileDateTime& out);

// touch -t [[CC]YY]MMDDhhmm[.ss], in local time; the year defaults to |now|'s.
bool ParseTouchStamp(std::string_view text, FileDateTime now, FileDateTime& out);

// Helpers the date and stat builtins reuse:
std::tm LocalTimeOf(int64_t seconds);  // as Ls.cpp's LocalTime
// std::mktime on a copy of |local| with tm_isdst = -1 (fields may be out of
// range: mktime normalizes them, so Jan 31 + 1 month is Mar 2 or 3).
// nullopt when mktime fails.
std::optional<int64_t> SecondsFromLocalTime(const std::tm& local);
// Seconds since the epoch of a UTC civil time; month 1-12 and day may be out
// of range (normalized arithmetically). Howard Hinnant's days_from_civil.
int64_t SecondsFromUtc(int64_t year, int64_t month, int64_t day, int hour, int minute, int second);
}
```

coreutils--date adds `std::string FormatDateTime(std::string_view format,
FileDateTime t, bool utc);` here later; do not add it now, and do not name
anything else `FormatDateTime` in this file (Ls.cpp has its own in an
anonymous namespace).

**`ParseDateString` grammar.** Case-insensitive words; whitespace separates
items; items may come in any order; each of date, time of day and zone at most
once (twice is a failure); relative items accumulate. An empty text (or
only spaces) is midnight today, as GNU's `date -d ''` prints today at
00:00:00.

- `@` seconds: `@[-]N[.frac]`, alone (any other item with it: false). The
  result is exactly that, in UTC.
- Date: `YYYY-MM-DD` (year 1-4+ digits, month 1-12, day 1 to the month's
  length, leap years counted). A `T` may join it to a time: `2024-01-02T03:04:05`.
- Time of day: `HH:MM[:SS[.frac]]`, hour 0-23, minute 0-59, second 0-60,
  frac up to 9 digits kept (more truncated) as nanoseconds.
- Zone: `Z`, `UTC`, `UT`, `GMT`, or a numeric offset `+hh`, `+hhmm`,
  `+hh:mm` (`-` too). A signed number **directly after a time of day** is
  always a zone, never a relative number -- GNU's rule, which makes
  `2024-01-02 03:04 +2 hours` mean 03:04 at UTC+2, plus one hour.
- Relative item: `[N] unit [ago]`. N is an optional sign (spaces allowed
  between sign and digits: `+ 5 minutes`) and digits, or the words `last`
  (-1), `this` (0), `next` (1); no N means 1. Units, optionally with a final
  `s`: `year`, `month`, `fortnight` (14 days), `week` (7 days), `day`,
  `hour`, `minute`, `min`, `second`, `sec`. `ago` negates the item just before
  it. Words: `now` and `today` (relative 0), `yesterday` (-1 day),
  `tomorrow` (+1 day).
- Anything else (month or day names included): false.

Computation:
1. Start from the local calendar date and time of `now` (`LocalTimeOf`), its
   nanoseconds kept.
2. A date replaces the date and resets the time to 00:00:00.0; a time of day
   replaces the time (and nanoseconds) -- with neither, `now`'s time stays.
3. Add relative years, months and days to `tm_year`, `tm_mon`, `tm_mday`.
4. Convert: with a zone, `SecondsFromUtc(fields)` minus the zone's offset;
   without, `SecondsFromLocalTime`.
5. Add relative hours, minutes and seconds as seconds.

Verified against GNU touch 9.4 (host zone +0200; in the tests use only zone-
or `@`-qualified forms for exact values, or compute the expected value with
`SecondsFromLocalTime`):

| text | result |
|---|---|
| `2024-01-02T03:04:05Z` | 1704164645 s, 0 ns |
| `2024-01-02 03:04:05.123456789 +0100` | 1704161045 s, 123456789 ns |
| `2024-01-02 03:04 +2 hours` | 03:04 at +02:00 plus 1 hour = 1704161040 s |
| `2024-01-02 3 days ago` (local) | 2023-12-30 00:00 local |
| `2024-01-31 +1 month` (local) | 2024-03-02 00:00 local (mktime normalizes Feb 31) |
| `@1700000000.5` | 1700000000 s, 500000000 ns |
| `garbage` | false |

**`ParseTouchStamp`**: digits only, then optionally `.ss` (two digits).
8 digits `MMDDhhmm` (year from `now`), 10 `YYMMDDhhmm` (YY 69-99 -> 19YY,
00-68 -> 20YY), 12 `CCYYMMDDhhmm`. Ranges as above (seconds 0-60), the day
checked against the month; local time through `SecondsFromLocalTime`; 0 ns.
`202401020304.05` is 2024-01-02 03:04:05 local; `2024` is false.

### `src/components/BuiltinCommands/commands/touch/Touch.cpp` (new)

`CreateTouchCommand()`; `touch`, `1.0.0`; summary `change file timestamps`,
usage `touch [OPTION]... FILE...`.

Options (GNU touch 9.4): `-a` (only the access time), `-c, --no-create`,
`-d, --date=STRING`, `-f` (treated, "(ignored)"), `-h, --no-dereference`
(treated: "the same: there are no links"), `-m` (only the modification time),
`-r, --reference=FILE`, `-t STAMP` (Required, `STAMP`), `--time=WORD`
(Required; `atime`/`access`/`use` as `-a`, `mtime`/`modify` as `-m`). All
treated.

Behaviour, verified against GNU 9.4 (`q(x)` = `ShellEscapeQuoted(x, true)`):
- No operand: `touch: missing file operand` + Try, 1.
- Two time sources among `-d`, `-t`, `-r` (except `-r` with `-d`, allowed):
  `touch: cannot specify times from more than one source` + Try, 1.
- `-t` bad: `touch: invalid date format q(stamp)`, 1. `-d` bad: the same with
  the string.
- `--time` bad word: `touch: invalid argument 'bogus' for '--time'` /
  `Valid arguments are:` / `  - 'atime', 'access', 'use'` /
  `  - 'mtime', 'modify'` + Try, 1.
- `-r FILE`: `Stat`; missing: `touch: failed to get attributes of q(FILE): No
  such file or directory`, 1. Its access and modification times are used;
  with `-d`, the string is parsed twice, with `now` = the reference's access
  time and then its modification time.
- Times: neither `-a` nor `-m` -> both; `-a` -> access only (the other
  nullopt); `-m` -> modification only. Without a source, now
  (`CurrentFileDateTime()`, taken once).
- Each operand: `-` changes nothing (GNU touches the file open on stdout;
  document it). Missing and not `-c`: create with `OpenFile(path,
  kFileOpenWriteCreateAppend, kFileCreateMode)` (never truncating); failure:
  `touch: cannot touch q(path): <reason>` -- parent missing -> `No such file
  or directory`, parent a file -> `Not a directory`, else `Permission
  denied`. Missing with `-c`: nothing, no error. Then `SetTimes(path, a, m)`;
  failure: `touch: setting times of q(path): Permission denied`.
- Exit 1 if any operand failed.

### `src/components/BuiltinCommands/commands/mv/Mv.cpp` (new)

`CreateMvCommand()`; `mv`, `1.0.0`; summary `move (rename) files`, usage
`mv [OPTION]... [-T] SOURCE DEST`, `mv [OPTION]... SOURCE... DIRECTORY`,
`mv [OPTION]... -t DIRECTORY SOURCE...`.

Options (GNU mv 9.4): `--backup[=CONTROL]`, `-b`, `--debug` (not treated),
`-f, --force`, `-i, --interactive`, `-n, --no-clobber`, `--no-copy`,
`--strip-trailing-slashes`, `-S, --suffix=SUFFIX`, `-t,
--target-directory=DIRECTORY`, `-T, --no-target-directory`,
`--update[=UPDATE]`, `-u`, `-v, --verbose`, `-Z, --context` (not treated).
Of `-f`, `-i`, `-n` the last given wins. Backup and update handling as cp's
(same words, same invalid-argument blocks, `-S` alone turns backups on).

Per target from `ResolveCopyTargets(..., parents=false, stripTrailingSlashes)`
(GNU 9.4 messages; `q` as above):
1. `Stat(source)` fails: `mv: cannot stat q(source): No such file or directory`.
2. The source's last segment (trailing slashes stripped) is `.` or `..`:
   `mv: cannot move q(source) to q(dest): Device or resource busy`.
3. Same file (equal `ResolvePath`): `mv: q(source) and q(dest) are the same file`.
4. Directory into itself (resolved dest under resolved source + `/`):
   `mv: cannot move q(source) to a subdirectory of itself, q(dest)`.
5. `IO().IsBuiltinCommand(source)`: `mv: cannot move q(source) to q(dest):
   Permission denied` (nothing copied).
6. Dest exists: directory onto non-directory -> `mv: cannot overwrite
   non-directory q(dest) with directory q(source)`; non-directory onto
   directory -> `mv: cannot overwrite directory q(dest) with non-directory`;
   directory onto a non-empty directory -> `mv: cannot overwrite q(dest):
   Directory not empty`. Then, in order: `-n`/`--update=none` -> `mv: not
   replacing q(dest)` on stderr, failure (9.4 exits 1); `-u` and dest not
   older -> skip silently, success; `-i` -> `mv: overwrite q(dest)? `,
   declined -> failure; backup -> `Rename(dest, BackupPathFor(...))`.
7. `int r = IO().Rename(source, dest)`. 0: with `-v`, `renamed q(source) ->
   q(dest)` (plus ` (backup: q(backup))` when one was made) on stdout.
8. `r == kFileSystemCrossDevice`: with `--no-copy`, `mv: cannot move
   q(source) to q(dest): Invalid cross-device link`, failure. Otherwise copy
   then remove, as GNU mv does across devices: `CopyPath(context, nullptr,
   source, dest, {recursive = true, preserveTimes = true, verbose = -v ?
   CopyVerbose::Mv : None})`; only if it succeeded, `RemoveOperand(context,
   nullptr, source, {recursive = true, verbose = -v, preserveRoot = true})`.
   The output of `mv -v d /mnt/d` is therefore every copy line, then every
   removal line (GNU 9.4, verified):
   ```
   created directory '/mnt/d'
   created directory '/mnt/d/e'
   copied 'd/e/f' -> '/mnt/d/e/f'
   removed 'd/e/f'
   removed directory 'd/e'
   removed directory 'd'
   ```
   and for a file: `copied 'a' -> '/mnt/a'`, `removed 'a'`.
9. `r == kFileSystemError` (any other failure; no reason is given, so find
   one): dest's parent missing -> `mv: cannot move q(source) to q(dest): No
   such file or directory`; parent not a directory -> `...: Not a
   directory`; otherwise `...: Permission denied`.
Exit 1 if any target failed.

Help notes (documented exceptions): across filesystems the copy keeps
modification and access times only (no modes, owners or links exist); a
builtin cannot be moved.

### `BuiltinCommandList.h`, `CMakeLists.txt`

Declare and register `CreateMvCommand()` and `CreateTouchCommand()`
(alphabetical); add `BuiltinDate.cpp`, `commands/mv/Mv.cpp`,
`commands/touch/Touch.cpp` to the library.

## Tests

All in `tests/unit/components/BuiltinCommands.unittests/` (each new file
added to that directory's `CMakeLists.txt`), exact stdout/stderr/status.

`BuiltinDateTest.cpp` -- plain `TEST(BuiltinCommandsDateTest, ...)` (the
suite name must contain `BuiltinCommands` for the script's filter):
- `ParsesUtcAndOffsets`: the first two table rows, and `@1700000000.5`.
- `ZoneAfterTimeQuirk`: `2024-01-02 03:04 +2 hours` -> 1704161040.
- `LocalDatesAndRelativeItems`: `2024-01-02 3 days ago` equals
  `SecondsFromLocalTime` of 2023-12-30 00:00:00; `2024-01-31 +1 month`
  equals that of 2024-03-02; `1 hour ago` from a fixed `now` is now - 3600
  with now's nanoseconds; `yesterday`, `tomorrow`, `next week`, `2 fortnights`,
  `+ 5 minutes` relative to a fixed `now`.
- `RejectsWhatItDoesNotKnow`: `garbage`, `2024-13-01`, `2024-02-30`,
  `25:00`, `@1 2024-01-01`, `Jan 2 2024` -> false.
- `TouchStamps`: `202401020304.05`, `2401020304`, `6901020304` (1969),
  `01020304` (year from now) against `SecondsFromLocalTime`; `2024`,
  `202402300000` -> false.
- `SecondsFromUtcNormalizes`: 2024-02-30 equals 2024-03-01; 1970-01-01 is 0.

`TouchTest.cpp` -- `TEST_F(BuiltinCommandsTest, Touch...)`:
- `TouchCreatesAnEmptyFile`, `TouchNoCreate` (`-c /nothing`: 0, absent),
  `TouchMissingDirectory` (`touch /nodir/x` -> `touch: cannot touch
  '/nodir/x': No such file or directory\n`, 1), `TouchMissingOperand`.
- `TouchDateUtc`: `touch -d 2024-01-02T03:04:05Z /notes.txt` -> `Stat` shows
  both times 1704164645 s; content unchanged.
- `TouchAccessOrModificationOnly`: `-a -d @100` changes only the access time;
  `-m -d @200` only the modification time; `--time=atime` as `-a`.
- `TouchStamp`: `-t 202401020304.05` -> `SecondsFromLocalTime` of that.
- `TouchReference`: `-r /docs/a.md` copies its times; `-r /docs/a.md -d
  '+1 day'` adds 86400 s; `-r /nothing` -> `touch: failed to get attributes of
  '/nothing': No such file or directory\n`, 1.
- `TouchErrors`: `-t 2024` -> `touch: invalid date format '2024'\n`;
  `-d garbage`; `-d @1 -t 202001010000` -> `touch: cannot specify times from
  more than one source\n` + Try; `--time=bogus` -> the exact block + Try.
- `TouchBuiltinPath`: `touch /bin/ls` -> `touch: setting times of '/bin/ls':
  Permission denied\n`, 1.

`MvTest.cpp` -- `TEST_F(BuiltinCommandsTest, Mv...)`:
- `MvRenamesVerbose`: `mv -v /notes.txt /n` -> `renamed '/notes.txt' -> '/n'\n`.
- `MvIntoADirectory`: `mv -v /notes.txt /.hidden /docs/sub` -> two `renamed` lines.
- `MvOperandErrors`: missing operands, `mv /nothing /x` -> `mv: cannot stat
  '/nothing': No such file or directory\n`; `-t /nodir` -> `mv: target
  directory '/nodir': No such file or directory\n`; three operands to a file
  -> `mv: target '/notes.txt': Not a directory\n`.
- `MvRefusals`: `mv /docs /docs/sub` -> `mv: cannot move '/docs' to a
  subdirectory of itself, '/docs/sub/docs'\n`; `mv /docs /notes.txt` ->
  `mv: cannot overwrite non-directory '/notes.txt' with directory '/docs'\n`;
  `mv -T /notes.txt /docs` -> `mv: cannot overwrite directory '/docs' with
  non-directory\n`; `mv /notes.txt /notes.txt` -> same-file line;
  `RunCaptured("mv", {".", "x"}, nullopt, "/docs")` -> `mv: cannot move '.'
  to 'x': Device or resource busy\n`; `mv /bin/ls /ls` -> `mv: cannot move
  '/bin/ls' to '/ls': Permission denied\n`, `/ls` absent.
- `MvNoClobberInteractiveUpdateBackup`: `-n` -> `mv: not replacing
  '/.hidden'\n`, 1; `-i` with `n\n` -> `mv: overwrite '/.hidden'? `, 1; `-u`
  with a newer dest -> 0, nothing moved; `-bv` -> `renamed '/notes.txt' ->
  '/.hidden' (backup: '/.hidden~')\n`.
- `MvMissingParent`: `mv /notes.txt /nodir/x` -> `mv: cannot move
  '/notes.txt' to '/nodir/x': No such file or directory\n`.
- `MvAcrossAMountCopiesThenRemoves`: mount an empty in-memory filesystem at
  `/mnt` (`root->Mount("/mnt", ...)`); set `/docs/a.md`'s times with
  `root->SetTimes`; `mv -v /docs /mnt/d` -> out exactly `created directory
  '/mnt/d'\ncopied '/docs/a.md' -> '/mnt/d/a.md'\ncreated directory
  '/mnt/d/sub'\ncopied '/docs/sub/b.md' -> '/mnt/d/sub/b.md'\nremoved
  '/docs/a.md'\nremoved '/docs/sub/b.md'\nremoved directory '/docs/sub'\nremoved
  directory '/docs'\n` (name order), `/docs` gone, `/mnt/d/a.md` with the
  same content and modification time; a file: `copied '/notes.txt' ->
  '/mnt/n'\nremoved '/notes.txt'\n`.
- `MvNoCopyAcrossAMount`: `mv --no-copy /notes.txt /mnt/n` -> `mv: cannot move
  '/notes.txt' to '/mnt/n': Invalid cross-device link\n`, 1, nothing moved.

Update `ListsEveryBuiltinSortedWithAVersion` (add `"mv"` and `"touch"` in
sorted position). The generic tests then check `--help`, untreated options
(`mv --debug /docs` stops at "missing destination"; `touch` has none) and the
`--init` template.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Mv*:BuiltinCommandsTest.Touch*:BuiltinCommandsDateTest.*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list; rows for `mv`
  and `touch` (1.0.0) with their exceptions (touch `-` changes nothing; `-h`
  is the same as without; mv across filesystems keeps times only); under
  "Key Classes", `BuiltinDate` (the `-d` grammar subset, shared with date).
- Root `CLAUDE.md`: rows for `mv` and `touch`; the command lists.

## Acceptance

- [ ] `ParseDateString`, `ParseTouchStamp`, `LocalTimeOf`,
      `SecondsFromLocalTime`, `SecondsFromUtc` have exactly the signatures above.
- [ ] mv falls back to copy-then-remove only on `Rename(...) == kFileSystemCrossDevice`.
- [ ] Every case in Tests prints GNU 9.4's bytes and status.
- [ ] Every GNU mv/touch option in `Options()`; `--debug`, `-Z/--context` of mv untreated.
- [ ] No POSIX-only time function (`timegm`, `localtime_r` outside `#ifdef`).
- [ ] All file access through `context.IO()`; generic and `--init` tests pass; all unit tests green.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- `date` and `FormatDateTime` (coreutils--date); month/day names,
  `MM/DD/YYYY`, `noon`/`midnight`, time zones by name other than UTC.
- If this task proves too big for one run, split it: mv alone, then touch +
  `BuiltinDate` (coreutils--date would then depend on the second).
