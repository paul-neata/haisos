# Task coreutils--date: date, and FormatDateTime shared with ls

- Rock: coreutils
- Depends on: coreutils--mv-touch (`BuiltinDate.h/.cpp`, done: `ParseDateString`, `ParseTouchStamp`, `LocalTimeOf`, `SecondsFromLocalTime`, `SecondsFromUtc`), coreutils--sort (`GnuQuote`, `ArgMatch` in `BuiltinText.h`)
- Size: ~750 changed lines in ~9 files
- Plan checked against: develop @ 7b806fb
- PR title: Add the date builtin and a GNU strftime shared with ls

(This is the first half of the planned coreutils--date-stat, split because
date and stat together came to ~1250 changed lines; the second half is
coreutils--stat, which depends on this task.)

## Goal

A new builtin `date` (`BUILTIN rootfs date /bin/date`) that prints what GNU
coreutils 9.4 `date` prints in the C locale: `date`, `date +FORMAT`, `-d/--date
STRING`, `-f/--file DATEFILE`, `-r/--reference FILE`, `-u/--utc`,
`-I[FMT]/--iso-8601[=FMT]`, `-R/--rfc-email`, `--rfc-3339=FMT`,
`--resolution`, and `-s/--set` (and the `MMDDhhmm[[CC]YY][.ss]` operand)
refused exactly as GNU refuses an unprivileged user: `date: cannot set date:
Operation not permitted`, the date printed anyway, exit 1.

And `FormatDateTime` (contract 4's second half) in `BuiltinDate.h/.cpp`: GNU
date's strftime (gnulib `nstrftime` in the C locale), implemented portably --
the host is asked only for the local broken-down time and the zone's
abbreviation. `ls` switches to it, so `ls --time-style=+FORMAT` gains GNU's
flags (`%-d`, `%:z`, `%^a`, ...) on every platform and loses its Windows
exception.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security"), 
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`,
`commands/ls/Ls.cpp` (its `LocalTime`, `MsvcStrftimeKnows`, `FormatDateTime`,
`FormatTimeColumn`: what moves out), `src/components/libheaders/CrtInvalidParameterAsError.h`,
`src/components/Filesystem/FilesystemUtils.h` (`CurrentFileDateTime`),
`interfaces/IFileSystemService.h` (`FileDateTime`, `FileStatus`), and the
tests `LsTimeStyles`, `LsTimeStyleWithAConversionStrftimeDoesNotKnow`,
`LsLongFormatShowsAnOldTimeWithItsYear` in
`tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`.

What coreutils--mv-touch (PR #59) already put on develop in
`src/components/BuiltinCommands/BuiltinDate.h/.cpp` (namespace `Haisos`; helpers such as `DaysInMonth`, `FloorDiv` are in its anonymous namespace):

```cpp
bool ParseDateString(std::string_view text, FileDateTime now, FileDateTime& out);   // GNU -d subset, host local time
bool ParseTouchStamp(std::string_view text, FileDateTime now, FileDateTime& out);   // touch -t [[CC]YY]MMDDhhmm[.ss]
std::tm LocalTimeOf(int64_t seconds);                                              // as ls's LocalTime
std::optional<int64_t> SecondsFromLocalTime(const std::tm& local);                 // mktime, tm_isdst -1
int64_t SecondsFromUtc(int64_t year, int64_t month, int64_t day, int hour, int minute, int second);  // days_from_civil
```



What coreutils--sort already put in `src/components/BuiltinCommands/BuiltinText.h`: `std::string GnuQuote(std::string_view text);` -- GNU's `quote()` in
the C locale, which is what GNU puts around a value in a message (`'abc'`;
`a'b` becomes `'a\'b'`) -- and `ArgMatch(context, "--opt", value, choices)`
(GNU's `XARGMATCH` with its "Valid arguments are:" block). Every `'x'` in a
diagnostic below that GNU writes with `quote()` is `GnuQuote(x)`; file names
GNU writes with `quoteaf` are `ShellEscapeQuoted(name, true)`, with `quotef`
`ShellEscapeQuoted(name)`.

Read the mv-touch implementation in `BuiltinDate.cpp` first: reuse its civil-date
helpers (days from a civil date and back, local <-> UTC conversion) rather
than writing second copies; if they are in an anonymous namespace, move the
ones you need to file scope in `BuiltinDate.cpp` (still not in the header
unless both files need them).

## Changes

### `src/components/BuiltinCommands/BuiltinDate.h` / `.cpp`

Add (contract 4):

```cpp
// strftime as GNU date has it (gnulib nstrftime, C locale): t in the host's
// local zone, or in UTC when |utc|. Never fails: what it cannot format is
// written out as it stands.
std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);
```

and two overloads that make `-u` mean "parse in UTC" (the existing
three-argument functions become calls of these with `utc` false; nothing else
of mv-touch's behaviour changes):

```cpp
bool ParseDateString(std::string_view text, FileDateTime now, bool utc, FileDateTime& out);
bool ParseTouchStamp(std::string_view text, FileDateTime now, bool utc, FileDateTime& out);
```

In those, with `utc` true, step 1 of `ParseDateString`'s computation breaks
`now` down in UTC (a `gmtime` counterpart of `LocalTimeOf`, added next to it
in `BuiltinDate.cpp`) and step 4 converts zone-less fields with
`SecondsFromUtc` instead of `SecondsFromLocalTime`; `ParseTouchStamp` the
same. Explicit zones (`Z`, `UTC`, `+hh:mm`) and `@seconds` are unaffected.
Add a case for each overload to the existing `tests/unit/components/BuiltinCommands.unittests/BuiltinDateTest.cpp` (suite `BuiltinCommandsDateTest`) (`2020-01-01` with `utc`
true is 1577836800).

`FormatDateTime`, conversion by conversion. Break `t.seconds` down with
`LocalTimeOf` (or its UTC counterpart when `utc`; a failed call leaves the
`tm` zeroed, as ls does today). The zone offset is computed portably:
`SecondsFromUtc` of the broken-down fields minus `t.seconds`. The abbreviation for `%Z`: `"UTC"` when `utc`; otherwise
`std::strftime("%Z")` on the local `tm` (Linux/WASM: `EET`, `UTC`...; on
Windows the CRT gives the zone's long name -- a documented exception).
Everything else is formatted by this function itself, never by the C
library's strftime:

- After `%`: flags, any of `_` (pad with spaces), `-` (no padding at all,
  whatever the width), `0` (pad with zeros), `^` (upper case), `#` (swap
  case: names upper case -- `%#a` `TUE`, `%#b` `NOV` -- but `%#p` `pm` and
  `%#Z` `utc` lower case); then a width (digits); then an `E` or `O`
  modifier, ignored; then, for `z`, up to three `:`.
- Numbers, with their default pad and minimum width: `%d %H %I %m %M %S %U %V
  %W %y %C %g` 2 zeros; `%e %k %l` 2 spaces; `%j` 3 zeros; `%Y %G` 4 zeros
  (year 99 is `0099`, as gnulib); `%u %w %q` 1; `%s` no minimum (negative
  allowed). A width wider than the default pads with the pad character
  (`%10Y` -> `0000002023`, `%_3d` -> ` 14`); `-` drops all padding (`%-d`
  -> `14`, `%-5d` -> `14`, `%-j` -> `318`).
- `%N`: nanoseconds, 9 digits; a width W shows the first W digits, truncated
  (`%3N` -> `123`), wider than 9 pads with trailing zeros (`%12N` ->
  `123456789000`); `%-N` and `%_N` are 9 digits.
- Names, C locale: `%a` (Sun..Sat) `%A` (Sunday...) `%b`/`%h` (Jan...) `%B`
  (January...) `%p` (AM/PM) `%P` (am/pm) `%Z`; a width pads them on the
  left with spaces, or zeros with `0` (`%10a` -> `       Tue`, `%010a` ->
  `0000000Tue`).
- Composites: `%c` = `%a %b %e %H:%M:%S %Y`, `%D` = `%x` = `%m/%d/%y`, `%F` =
  `%Y-%m-%d`, `%r` = `%I:%M:%S %p`, `%R` = `%H:%M`, `%T` = `%X` = `%H:%M:%S`;
  `%n` newline, `%t` tab, `%%` `%`.
- `%G %g %V` ISO 8601 week-based year and week, `%U` (Sunday weeks) and `%W`
  (Monday weeks), `%j` day of year, `%u` 1-7 Monday first, `%w` 0-6 Sunday
  first, `%q` quarter 1-4, `%k`/`%l` 24/12-hour space padded, `%C` century.
- `%z` `+hhmm`; `%:z` `+hh:mm`; `%::z` `+hh:mm:ss`; `%:::z` as few parts as
  needed (`+05:30`, `+00`, `-03:30`).
- Anything else (`%J`, `%:H`, `%Q`, `%5%`): copied as written from the `%`
  through that character, padded on the left with spaces to the width if one
  was given (`%5J` -> `  %5J`); a `%` ending the format is a `%`.

For anything not pinned down above (unusual flag/width mixes), verify
against `date -u -d @1700000000.123456789 '+FORMAT'` with `LC_ALL=C` in the
task container and follow it.

Also fix a bug of #59's `ParseDateItems` (`BuiltinDate.cpp`, the date branch,
`previousWasTime = withTime;`): after `ParseDate` attached a zone to a
`T`-time (`2024-01-02T03:04Z`), `previousWasTime` stays true, so a following
`+1 hour` is read as a second zone and fails; GNU adds one hour. Set
`previousWasTime = withTime && !items.hasZone` (i.e. only when `ParseDate`
attached no zone).

### `src/components/BuiltinCommands/commands/ls/Ls.cpp`

Delete `MsvcStrftimeKnows`, ls's own `FormatDateTime` and `LocalTime` (ls 1.3.0 still has all three, a copy of `BuiltinDate`'s `LocalTimeOf`, plus a "On Windows, a --time-style=+FORMAT conversion ..." help note);
include `BuiltinDate.h` and call `FormatDateTime(format, time, false)` in
`FormatTimeColumn`. Bump ls's version to `1.3.1`. Its output on Linux must
not change (the existing ls tests stay green unmodified); the Windows
exception in ls's `--help` notes and in the CLAUDE.md table (a conversion the
Microsoft C runtime lacks printing as written) is removed, since nothing is
handed to the CRT's strftime any more.

### New `src/components/BuiltinCommands/commands/date/Date.cpp`

`CreateDateCommand()`. Name `date`, version `1.0.0`. Help: summary "print the
system date and time", usage `date [OPTION]... [+FORMAT]` and `date
[-u|--utc|--universal] [MMDDhhmm[[CC]YY][.ss]]`; notes listing the format
conversions in a few lines and the exceptions below.

`Options()` -- every option of GNU date 9.4, aliases as separate rows sharing
an id:
`{'d', "date", kDate, Required, "STRING", "display time described by STRING"}`,
`{0, "debug", kBuiltinNotTreated}`,
`{'f', "file", kFile, Required, "DATEFILE", "like --date, once per line of DATEFILE"}`,
`{'I', "iso-8601", kIso, OptionalAttached, "FMT", "ISO 8601: date hours minutes seconds ns"}`,
`{'r', "reference", kReference, Required, "FILE", "last modification time of FILE"}`,
`{0, "resolution", kResolution, None, "", "the timestamp resolution"}`,
`{'R', "rfc-email", kRfcEmail, None, "", "RFC 5322 format"}`, plus
`{0, "rfc-822", kRfcEmail, ...}` and `{0, "rfc-2822", kRfcEmail, ...}`,
`{0, "rfc-3339", kRfc3339, Required, "FMT", "RFC 3339: date seconds ns"}`,
`{'s', "set", kSet, Required, "STRING", "set time (refused: no clock setting)"}`,
`{'u', "utc", kUtc, None, "", "Coordinated Universal Time"}`, plus
`{0, "uct", kUtc, ...}` and `{0, "universal", kUtc, ...}`.
Every treated row needs a description (the generic help test checks it).

Through `BeginBuiltin` (usage errors exit 1). Then, as date.c `main`:

1. Options in order. A second output format (from `-I`, `-R`,
   `--rfc-3339`) -> `date: multiple output formats specified`, exit 1, no Try
   line. `-I` with no argument is `date`; its argument and `--rfc-3339`'s are
   matched as GNU's argmatch does: exact, or an unambiguous prefix
   (`-Ih` hours, `-Ins` ns) -- `ArgMatch(context, "--iso-8601", ...)` /
   `ArgMatch(context, "--rfc-3339", ...)`, choices in GNU's order; otherwise
   ```
   date: invalid argument 'x' for '--iso-8601'
   Valid arguments are:
     - 'hours'
     - 'minutes'
     - 'date'
     - 'seconds'
     - 'ns'
   Try 'date --help' for more information.
   ```
   (exit 1; for `--rfc-3339` the list is `'date'`, `'seconds'`, `'ns'`).
   Formats: `-I`: date `%Y-%m-%d`, seconds `%Y-%m-%dT%H:%M:%S%:z`, ns
   `%Y-%m-%dT%H:%M:%S,%N%:z`, hours `%Y-%m-%dT%H%:z`, minutes
   `%Y-%m-%dT%H:%M%:z`; `--rfc-3339`: date `%Y-%m-%d`, seconds `%Y-%m-%d
   %H:%M:%S%:z`, ns `%Y-%m-%d %H:%M:%S.%N%:z`; `-R`: `%a, %d %b %Y %H:%M:%S %z`.
   A repeated `-d`/`-s`: the last wins.
2. More than one of `-d`, `-f`, `-r`, `--resolution` -> `date: the options to
   specify dates for printing are mutually exclusive` + Try, exit 1. `-s`
   with any of them -> `date: the options to print and set the time may not
   be used together` + Try, exit 1.
3. Operands: two or more -> `date: extra operand '<2nd>'` + Try, exit 1. One
   starting with `+` is the format (a format already given -> `multiple output
   formats specified`, exit 1). Another one with `-d/-f/-r/--resolution/-s`
   given ->
   ```
   date: the argument 'x' lacks a leading '+';
   when using an option to specify date(s), any non-option
   argument must be a format string beginning with '+'
   Try 'date --help' for more information.
   ```
   exit 1. Otherwise it is a time to set, `MMDDhhmm[[CC]YY][.ss]`: rearrange it
   into `[[CC]YY]MMDDhhmm[.ss]` (8 digits: as is; 10: the last 2 first; 12:
   the last 4 first; anything else invalid) and parse it with
   `ParseTouchStamp(..., utc, ...)`; invalid -> `date: invalid date 'x'`, exit 1.
4. Default format `%a %b %e %H:%M:%S %Z %Y` (`--resolution` alone: `%s.%N`).
   Each `%-N` in the format becomes `%9N` (date.c `adjust_resolution`; the
   resolution is the nanosecond).
5. The time: now (`CurrentFileDateTime()`); `-d STRING` through
   `ParseDateString(text, now, utc, out)`, an empty or all-blank STRING
   meaning today at 00:00 (GNU's parse_datetime), invalid -> `date: invalid
   date 'foo'`, exit 1; `-r FILE` its modification time from
   `context.IO().Stat` (failure: `date: FILE: No such file or directory`, exit
   1 -- `FILE` unquoted unless `ShellEscapeQuoted` would quote it, GNU's
   `quotef`); `--resolution` the time 0.000000001; `-s STRING` parsed as
   `-d`.
6. `-f DATEFILE` (`-` is standard input, descriptor 0): read it to the end,
   each line (without its `\n`) as a `-d` string, each printed with the
   format and a newline; an invalid line -> `date: invalid date 'xyz'` and go
   on, exit 1 at the end; a file that cannot be opened -> `date: DATEFILE: No
   such file or directory`, exit 1. Check `StopRequested()` per line and
   treat `kIOInterrupted` as a stop.
7. Setting (`-s`, or the operand of step 3): `date: cannot set date:
   Operation not permitted`, then print the date anyway, exit 1.
8. Print `FormatDateTime(format, time, utc) + "\n"`.

Rules that bite: only `context.IO()` for files (`-r`, `-f`); every GNU option
in `Options()`; `--help` from `BuiltinHelpText`; GNU's messages; portable
C++17 (`localtime_s`/`gmtime_s` on Windows, `_r` elsewhere).

Documented exceptions (`--help` notes and the CLAUDE.md row): the clock
cannot be set (`-s` and the set operand are refused as for an unprivileged
user); `TZ` in the process environment is not consulted -- local time is the
host's zone, `-u` is UTC; `-d` understands the subset `ParseDateString`
does; `%Z` on Windows is the zone's Windows name; `--debug` not treated.

### `BuiltinCommandList.h`, `src/components/BuiltinCommands/CMakeLists.txt`

Declare `CreateDateCommand()` and add it to `CreateStandardBuiltinCommands()`
in `BuiltinCommandList.h` (this alone puts it in the `haisos --init` template);
add `commands/date/Date.cpp` to the source list in
`src/components/BuiltinCommands/CMakeLists.txt` (next to `commands/touch/Touch.cpp`;
the root `CMakeLists.txt` needs nothing).

## Tests

New `tests/unit/components/BuiltinCommands.unittests/DateTest.cpp` (add to the
`add_executable(BuiltinCommands.unittests ...)` list in that directory's `CMakeLists.txt`), on `RunCaptured`. Every case passes `-u`
and a fixed `-d @...` unless it says otherwise, so no test depends on the
host's zone or clock. Expected outputs are GNU coreutils 9.4's (`LC_ALL=C`);
verify any new one with `/usr/bin/date` in the task container.

- `DateDefaultFormat`: `-u -d @1700000000` -> `"Tue Nov 14 22:13:20 UTC 2023\n"`;
  `-u -d @-1` -> `"Wed Dec 31 23:59:59 UTC 1969\n"`.
- `DateFormatConversions`: `-u -d @1700000000.123456789` with
  `+%q|%P|%p|%c|%x|%X|%r|%D|%T|%R|%h|%C|%g|%G|%V|%U|%W|%j|%u|%w|%k|%l|%I|%s|%N|%3N|%-N|%:z|%::z|%:::z|%z|%Z|%^a|%#Z|%#a|%^B|%10Y|%-d|%_m|%05e|%-H|%_5S|%Ey|%Od`
  -> `"4|pm|PM|Tue Nov 14 22:13:20 2023|11/14/23|22:13:20|10:13:20 PM|11/14/23|22:13:20|22:13|Nov|20|23|2023|46|46|46|318|2|2|22|10|10|1700000000|123456789|123|123456789|+00:00|+00:00:00|+00|+0000|UTC|TUE|utc|TUE|NOVEMBER|0000002023|14|11|00014|22|   20|23|14\n"`.
- `DateUnknownConversionsAreWrittenOut`: `+%J|%-J|%5J|%:x|%t|%n|%%|%` ->
  `"%J|%-J|  %5J|%:x|\t|\n|%|%\n"`.
- `DateIsoAndRfcFormats`: `-u -d @1700000000.5` with `-I` -> `"2023-11-14\n"`,
  `-Ihours` -> `"2023-11-14T22+00:00\n"`, `-Iminutes` -> `"2023-11-14T22:13+00:00\n"`,
  `-Iseconds` -> `"2023-11-14T22:13:20+00:00\n"`, `-Ins` -> `"2023-11-14T22:13:20,500000000+00:00\n"`,
  `--rfc-3339=ns` -> `"2023-11-14 22:13:20.500000000+00:00\n"`, `--rfc-3339=seconds`,
  `-R` -> `"Tue, 14 Nov 2023 22:13:20 +0000\n"`, `--rfc-email` the same.
- `DateDateStrings`: `-u -d '2020-01-01 12:30'` -> `"Wed Jan  1 12:30:00 UTC 2020\n"`;
  `-u -d '2020-01-01 +1 day' +%F` -> `"2020-01-02\n"`; `-u -d @86400 +%F` -> `"1970-01-02\n"`.
- `DateFromAFile`: stdin `"\n@0\nfoo\n@86400\n"`, `-u -f -` -> out has 3 lines,
  the 2nd and 3rd `"Thu Jan  1 00:00:00 UTC 1970"` and `"Fri Jan  2 00:00:00 UTC 1970"`
  (the 1st is today at 00:00: check it ends with `"00:00:00 UTC <current year>"`),
  err `"date: invalid date 'foo'\n"`, status 1; `-f /nonexist` -> err
  `"date: /nonexist: No such file or directory\n"`, status 1.
- `DateReference`: a file written by the fixture, `-u -r /notes.txt +%s` prints
  that file's modification seconds (compare with `root->Stat`); `-r /nonexist`
  -> `"date: /nonexist: No such file or directory\n"`, status 1.
- `DateSetIsRefused`: `-u -s '2020-01-01'` -> err `"date: cannot set date: Operation not permitted\n"`,
  out `"Wed Jan  1 00:00:00 UTC 2020\n"`, status 1; `-u 0102030426` -> out
  `"Fri Jan  2 03:04:00 UTC 2026\n"`, same err, status 1; `-u x` -> err `"date: invalid date 'x'\n"`, status 1.
- `DateUsageErrors`: `-d 1 -r /` -> err `"date: the options to specify dates for printing are mutually exclusive\nTry 'date --help' for more information.\n"`;
  `-R -I` -> err `"date: multiple output formats specified\n"`; `+%Y +%m` -> err
  `"date: extra operand '+%m'\nTry 'date --help' for more information.\n"`;
  `-d @0 x` -> the three-line "lacks a leading '+'" message plus Try; `-Ix` ->
  the argmatch message above; `--resolution` -> out `"0.000000001\n"`, status 0.
- `DateNowIsNow`: `+%s` without `-d` prints a number within 5 s of the
  test's own clock.
- `TEST(FormatDateTimeTest, MatchesGnu)` (direct calls, no process): `FormatDateTime("%Y-%m-%d %H:%M:%S.%N %:z", {1700000000, 5}, true)`
  -> `"2023-11-14 22:13:20.000000005 +00:00"`; `"%G-W%V-%u"` of 2021-01-03 UTC
  (1609632000) -> `"2020-W53-7"`; `"%U %W"` of the same -> `"01 00"`; `"%Y|%C|%y"`
  of 0099-01-01 UTC (-59042995200) -> `"0099|00|99"`; `"%10a|%010a|%12N|%#p"` of
  `{1700000000, 123456789}` -> `"       Tue|0000000Tue|123456789000|pm"`; with
  `utc` false, `"%z"` matches `[+-][0-9]{4}` and `"%s"` is `"1700000000"`.

In `BuiltinDateTest.cpp`: `2024-01-02T03:04Z +1 hour` parses to 2024-01-02 04:04:00 UTC (also in DateTest: `-u -d '2024-01-02T03:04Z +1 hour' +%F\ %T`), while `2024-01-02T03:04 +01:00` still reads the zone.

The existing ls tests (`LsTimeStyles`, `LsTimeStyleWithAConversionStrftimeDoesNotKnow`,
`LsLongFormatShowsAnOldTimeWithItsYear`, ...) must pass unmodified; add to
`BuiltinCommandsTest.cpp` `LsTimeStyleTakesGnuFlags`: `ls -l
--time-style=+%-d.%_m.%Y /docs/a.md` matches
`-rwxrwxrwx 1 haisos haisos 5 [1-9][0-9]?\.[ 1][0-9]\.[0-9]{4} /docs/a\.md`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Date*:*FormatDateTime*:BuiltinCommandsDateTest.*:BuiltinCommandsTest.Ls*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

(The script's filter matches test executable names, so `BuiltinCommands`
is the narrowest it takes; the direct run narrows to this task's tests.
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` lives in
`CliParser.unittests`.) Add the new builtin names to the exact list in
`ListsEveryBuiltinSortedWithAVersion` (`BuiltinCommandsTest.cpp`), in byte
order.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: a `date` row (version,
  treated options, the exceptions above); the ls row's version (1.3.0 now) becoming 1.3.1 and its
  Windows strftime exception removed; a sentence that `BuiltinDate`
  (`ParseDateString`, `ParseTouchStamp`, `FormatDateTime`) is where dates are
  parsed and formatted for every builtin.
- Root `CLAUDE.md`: a `date` row in the Builtin Commands table and `date` in
  the builtin lists.

## Acceptance

- [ ] `FormatDateTime(std::string_view, FileDateTime, bool)` declared in `BuiltinDate.h`, formatting everything itself (no `std::strftime` except for `%Z`'s local abbreviation).
- [ ] The `utc` overloads of `ParseDateString`/`ParseTouchStamp` exist; the old signatures behave as before.
- [ ] ls uses it; ls output on Linux unchanged; ls 1.3.1; the Windows exception gone from its help and the docs.
- [ ] date: every GNU option in `Options()`; formats, `-I`/`--rfc-3339` argmatch with prefixes, `-d`, `-f`, `-r`, `--resolution`, `-u`, the set refusal with the date still printed and exit 1, every message above byte for byte.
- [ ] Tests independent of the host's zone (all `-u`) and clock; build and unit tests green on Linux.
- [ ] `2024-01-02T03:04Z +1 hour` is one hour later (previousWasTime not left set by an attached zone), with a test.
- [ ] Registered; CMakeLists (`src/components/BuiltinCommands/CMakeLists.txt`, test list); docs rows.

## Out of scope

- `stat` (coreutils--stat), `touch -d` (coreutils--mv-touch).
- `TZ` from the process environment, `--debug`, locales other than C.
- Actually setting any clock.
