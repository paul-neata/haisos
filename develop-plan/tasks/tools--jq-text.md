# Task tools--jq-text: jq's regular expressions, @formats and dates

- Rock: tools
- Depends on: tools--jq-builtins, base--regex-match (`Regex`), coreutils--date (`FormatDateTime`; through it coreutils--mv-touch's `LocalTimeOf`, `SecondsFromLocalTime`, `SecondsFromUtc`)
- Size: ~950 changed lines in ~10 files
- Plan checked against: develop @ ccb9dbe
- PR title: jq: regular expressions, @csv/@tsv/@sh/@base64 formats and dates

## Goal

The seventh and last jq task. The rest of jq 1.7.1's library works, with
jq's results and messages:
- **Regular expressions** on Haisos's `Regex` (Perl subset): `test`,
  `match`, `capture`, `scan`, `splits`, `split/2`, `sub`, `gsub`, each with
  jq's flags string -- `g i x n s p` supported, `l` refused with a message
  of its own -- and jq's match objects (offsets and lengths in code points).
- **Formats**: `@csv @tsv @html @uri @sh @base64 @base64d` (with `@text`
  and `@json` from `tools--jq-eval`), in `@fmt "...\(x)..."` strings and as
  `format("csv")`.
- **Dates**: `now mktime gmtime localtime strftime strflocaltime strptime
  todate fromdate todateiso8601 fromdateiso8601`.

So `jq -r '.[] | @csv'`, `jq -r '@sh "rm \(.files[])"'`, `select(.name |
test("^foo"; "i"))`, `.msg | sub("(?<n>[0-9]+)"; "#\(.n)")` and `.ts |
todate` behave as on Linux.

Reference: the task container's jq (Ubuntu 24.04's `jq 1.7.1-3ubuntu0.24.04.x`,
`jq --version` prints `jq-1.7`), which uses Oniguruma for regexes. Every
expected value below was taken from it with `TZ=UTC jq -nc '<program>'`; for
anything not written here, run it there (with `TZ=UTC`) and copy the result.

## Context

Read first: `src/components/BuiltinCommands/commands/jq/CLAUDE.md` (the
"Library" section of `tools--jq-builtins`), `src/components/Regex/CLAUDE.md`
and `Regex.h`. What earlier tasks provide, by exact name (the merged code is
the truth; if a name differs, use the merged one and say so in the PR):
- `tools--jq-json`: `Value` (`Number` is a computed number, `String`,
  `Array`, `Object`, `AsString`, ...), `KindName`, `FormatNumber`,
  `DumpTruncated`, `RepairUtf8`.
- `tools--jq-eval`: `JqError`, `NativeFunction`, `NativeRegistry` (`Add`,
  `Standard()` calling every `Register*`), `ForEachArgumentCombination`
  (last argument outermost), `StandardPrelude()`, `SliceValue`, and
  `ApplyFormat` in `JqFormat.h/.cpp` -- today `text` and `json`, any other
  name failing with `<name> is not a valid format`; string interpolation
  (`@csv "\(x)"`) already calls it for each interpolated value.
- `tools--jq-builtins`: `RegisterLibraryNatives` (`tostring`, `length`,
  `split/1`, ...), the prelude with jq 1.7.1's definitions copied from the
  container's `builtin.jq` (and the python one-liner that extracts it), and
  jq at version `1.1.0`. The test helper
  `tests/unit/components/Jq.unittests/JqTestRunner.h` (`RunJq(program,
  inputJson = "null")`: outputs compact, space-separated; an uncaught error
  appends `error: <message>`).
- `base--regex-match`: `Regex::Compile(pattern, RegexOptions{syntax,
  ignoreCase, multiline}, error)` -> `shared_ptr<const Regex>` or null with
  PCRE2's message (or Haisos's own "... not supported"); `Search(text, start,
  match, flags)` leftmost-first for `RegexSyntax::Perl`, `match.groups` byte
  offsets (-1, -1) for a group that took no part; `GroupCount()`,
  `GroupNames()` (`""` for an unnamed group). The Perl subset: `.` excludes
  `\n` unless `(?s)`; `^` only at the start and `$` at the end or before a
  final `\n` unless `(?m)`; ASCII-only `\w \d \s` and case folding; bytes, not
  code points.
- `coreutils--date` / `coreutils--mv-touch`
  (`src/components/BuiltinCommands/BuiltinDate.h`, included as
  `"BuiltinDate.h"`): `std::string FormatDateTime(std::string_view format,
  FileDateTime t, bool utc)` (GNU date's strftime; `%Z` is `UTC` when `utc`),
  `std::tm LocalTimeOf(int64_t seconds)`, `std::optional<int64_t>
  SecondsFromLocalTime(const std::tm& local)`, `int64_t SecondsFromUtc(int64_t
  year, int64_t month, int64_t day, int hour, int minute, int second)`
  (month 1-12; the day may be out of range). `CurrentFileDateTime()` is in
  `src/components/Filesystem/FilesystemUtils.h`.

Error messages showing a value are jq's `type_error` shape: `<KindName>
(<DumpTruncated(value, 15)>) <text>` -- written "(dump15)" below.

Rules that bite: portable C++17 (no POSIX headers, no `<regex>` -- every
regex goes through `Regex`; no `strptime`/`timegm`/`gmtime_r`: compute
calendars yourself); nothing reaches outside the process except the clock
and the host's time zone, read as `date` reads them. No `interfaces/` class
(the `Create()` rule does not apply). Bump the jq builtin's version.

## Changes

All in `src/components/BuiltinCommands/commands/jq/`, namespace `Haisos::Jq`.

### `JqNatives.h` / `JqNatives.cpp`

Declare and call from `NativeRegistry::Standard()`:

```cpp
void RegisterRegexNatives(NativeRegistry& registry);   // JqRegexNatives.cpp
void RegisterFormatNatives(NativeRegistry& registry);  // JqFormat.cpp
void RegisterDateNatives(NativeRegistry& registry);    // JqDateNatives.cpp
```

### `JqRegexNatives.cpp` (new)

**`_match_impl/3`** (`_match_impl(re; flags; testOnly)`, value arguments
through `ForEachArgumentCombination`), jq 1.7.1's `f_match`:
1. The input must be a string, else `(dump15) cannot be matched, as it is
   not a string`. `re` must be a string, else `(dump15) is not a string`.
   `flags`: null means `""`; a string; anything else `(dump15) is not a
   string`.
2. Flags: every character must be one of `g i x n s p l`, else the error
   `<the whole flags string> is not a valid modifier string` (`"gq"` ->
   `gq is not a valid modifier string`). Then:
   - `g`: global (every match, not just the first);
   - `i`: `RegexOptions::ignoreCase` (ASCII only -- documented);
   - `x`: extended -- remove from the pattern, before compiling, every
     whitespace byte (space, `\t`, `\n`, `\v`, `\f`, `\r`) and every `#`
     comment up to and including the next `\n`, **outside** bracket
     expressions; a backslash and the byte after it are copied as they are
     (`\ ` stays a literal space, `\#` a literal `#`); inside `[...]` (with
     `]` literal right after `[` or `[^`, and backslash escapes) everything
     is kept (`"[ ]"` still matches a space);
   - `n`: ignore empty matches -- an empty match is skipped (searching on
     from the next byte), not replaced by a longer one at the same place as
     Oniguruma does (`"aaa"|[match("a*?";"gn")]` finds three `a` in jq,
     nothing here) -- documented;
   - `s`: nothing (Oniguruma's single-line mode is what the Perl subset does
     anyway: `^` at the start, `$` at the end or before a final `\n`);
   - `p`: `.` also matches `\n` -- prepend `(?s)` to the pattern;
   - `l` (Oniguruma's longest match, which the leftmost-first engine cannot
     give): the error `Regex failure: the l modifier (longest match) is not
     supported`.
3. Compile with `RegexSyntax::Perl`; a failure is the error `Regex failure:
   <message>`, where the messages jq users see from Oniguruma replace
   `Regex`'s for the same mistakes:

   | `Regex` message | jq's (Oniguruma's) |
   |---|---|
   | `missing closing parenthesis` | `end pattern with unmatched parenthesis` |
   | `unmatched closing parenthesis` | `unmatched close parenthesis` |
   | `missing terminating ] for character class` | `premature end of char-class` |
   | `quantifier does not follow a repeatable item` | `target of repeat operator is not specified` |
   | `range out of order in character class` | `empty range in char class` |
   | `numbers out of order in {} quantifier` | `upper is smaller than lower in repeat range` |
   | `\ at end of pattern` | `end pattern at escape` |
   | `reference to non-existent subpattern` | `invalid backref number/name` |
   | `unknown POSIX class name` | `invalid POSIX bracket type` |

   Any other message is kept as `Regex` gives it (`Regex failure: lookaround
   assertions are not supported` -- Oniguruma supports lookaround,
   possessive quantifiers, `\p{...}`, `\Q...\E`, `(?x)` and unknown escapes
   like `\i`; Haisos refuses them: documented).
4. `testOnly` true: `true` when there is a match (a non-empty one with `n`),
   else `false`. Otherwise an **array** of match objects, found by jq 1.7.1's
   loop: `start = 0`; while `start <= size`: `Search(text, start, m)`; none
   -> stop; skip it if empty and `n`, else record it; stop unless `g`;
   `start = end + 1` after an empty match, `start = end` otherwise. So
   `"abc"` with `""` and `g` matches at 0, 1, 2 and 3; `"aa"` with `a*` and
   `g` gives `[0,2]` then `[2,0]`; an empty match may fall inside a UTF-8
   character, as in jq (`"aéc"|[match("";"g")]|map(.offset)` is
   `[0,1,2,2,3]`).

**Match objects**, byte-exact including key order. A code point index of a
byte offset b is the number of bytes before b that are not UTF-8
continuation bytes (`0x80`-`0xBF`); a code point length is the same count
over the span. Strings are the span's bytes through `RepairUtf8`.
- the match: `{"offset": cp(begin), "length": cplen, "string": s,
  "captures": [...]}` (an empty match: length 0, string `""`);
- each capture group 1..GroupCount(), `name` being `GroupNames()[k]` or null
  when `""`:
  - not taking part: `{"offset":-1,"string":null,"length":0,"name":N}`;
  - taking part but empty: `{"offset":cp(begin),"string":"","length":0,"name":N}`;
  - otherwise `{"offset":cp(begin),"length":cplen,"string":s,"name":N}`.
  (The two empty shapes put `string` before `length`: jq 1.7.1 builds them
  in another branch.)
- every number a computed `Value::Number`.

**`_nwise/1`** (`_nwise($n)`): jq's `def n: if length <= $n then . else
.[0:$n], (.[$n:] | n) end;` as an iterative native (a string of 2000
matches must not hit the call-depth limit through `splits`): an array or a
string, emitted in slices of `$n` (`SliceValue`, so strings slice by code
point), the last one shorter; an input not longer than `$n` -- the empty one
included -- emitted once whole.

### `JqPrelude.cpp`

Append, **verbatim** from the container's `builtin.jq` (the extraction of
`tools--jq-builtins`), these definitions: `match/2`, `match/1`, `test/2`,
`test/1`, `capture/2`, `capture/1`, `scan/2`, `scan/1`, `_nwise/2`,
`splits/2`, `splits/1`, `split/2`, `sub/3`, `sub/2`, `gsub/3`, `gsub/2`,
`fromdateiso8601/0`, `todateiso8601/0`, `fromdate/0`, `todate/0`. Not
`_nwise/1` (native above). The licence notice of `tools--jq-builtins` covers
them.

### `JqFormat.h` / `JqFormat.cpp`

`ApplyFormat` gains these names (others still `<name> is not a valid
format`; jq 1.7.1 has no `@base32`/`@base32d`, so neither does Haisos):
- `csv`: the input must be an array, else `(dump15) cannot be csv-formatted,
  only array`; the fields joined by `,`: a number `FormatNumber` (literal
  kept: `1.50`), NaN an empty field; a string `"` + itself with every `"`
  doubled + `"`; `true`/`false`; null an empty field; anything else
  `(dump15) is not valid in a csv row`.
- `tsv`: the same with `tsv-formatted` and `\t` between fields; a string
  unquoted with `\` -> `\\`, tab -> `\t`, newline -> `\n`, CR -> `\r` (two
  characters each); an invalid field's message is also `is not valid in a
  csv row` (jq 1.7.1's).
- `html`: a non-string through `ApplyFormat("text")` first; then `<` ->
  `&lt;`, `>` -> `&gt;`, `&` -> `&amp;`, `'` -> `&apos;` (the named entity, not `&#39;`), `"` -> `&quot;`;
  every other byte as is.
- `uri`: `text` first; bytes `A-Z a-z 0-9 - _ . ~` as they are, every other
  byte `%XX` with uppercase hex (`"a b/ç"` -> `a%20b%2F%C3%A7`).
- `sh`: an array's elements (any other input is one element) joined by one
  space; a string `'` + itself with each `'` written `'\''` + `'`; a number,
  boolean or null its `text`; an array or object `(dump15) can not be
  escaped for shell`.
- `base64`: `text` first; standard alphabet, `=` padding.
- `base64d`: `text` first; decode up to the first `=` (the rest ignored:
  `"YWJj=ZGVm"` -> `abc`); a byte outside the alphabet before it ->
  `string (dump15) is not valid base64 data`; one leftover character (6
  bits) -> `string (dump15) trailing base64 byte found`; two or three give one
  or two bytes; the bytes through `RepairUtf8`.

**`format/1`** (`RegisterFormatNatives`): for each output of the argument, a
string -> `ApplyFormat(it, input)`; anything else `(dump15) is not a valid
format`.

### `JqDateNatives.cpp` (new)

A broken-down time is jq's array `[year, month 0-11, day 1-31, hours,
minutes, seconds (may hold a fraction), weekday 0-6 Sunday first, day of
year 0-365]`. "Its first 8 elements numbers" is the test of a valid one
(more elements are ignored). Helpers in this file: Howard Hinnant's
`civil_from_days` (and `days_from_civil`, or `SecondsFromUtc`), a UTC
breakdown of a `int64_t`, and the conversion of a broken-down array to UTC
seconds: each field truncated to an integer, the month normalized into the
year (`year += floor(m / 12)`, `m = m mod 12`, so `[2023,13,40,0,0,0,0,0]`
is 2024-03-11) before `SecondsFromUtc(year, m + 1, day, H, M, S)` (hours,
minutes and seconds out of range are added arithmetically -- check
`SecondsFromUtc` does, else add them yourself).

- `now/0`: `CurrentFileDateTime()` as seconds plus nanoseconds / 1e9 (computed).
- `gmtime/0`: a number, else `gmtime() requires numeric inputs`; not
  finite, `>= 67768036191676800` or `< -67768040609740800` -> `error
  converting number of seconds since epoch to datetime`; the whole seconds
  are the number truncated toward zero, broken down in UTC; the seconds field
  is that second **plus `x - floor(x)`** (`-1.5|gmtime` is
  `[1969,11,31,23,59,59.5,3,364]`, `1700000000.5|gmtime` ends `20.5,2,317]`).
- `localtime/0`: the same with `localtime() requires numeric inputs`, the
  breakdown by `LocalTimeOf` (year `tm_year + 1900`, ...).
- `mktime/0`: not an array -> `mktime requires array inputs`; not a valid
  broken-down time -> `mktime requires parsed datetime inputs`; the UTC
  seconds as above; a result of exactly -1 -> `invalid gmtime representation`
  (jq 1.7.1 takes -1 for a failure: `"1969-12-31T23:59:59Z"|fromdate` fails).
- `strftime/1`: the format must be a string, else `strftime/1 requires a
  string format` (the container's jq crashes on an assertion there; this is
  jq's source text). A number input is taken as UTC seconds (truncated); an
  array must be a valid broken-down time (converted to UTC seconds as above);
  anything else `strftime/1 requires parsed datetime inputs`. Then
  `FormatDateTime(glibcFormat, {seconds, 0}, true)` where `glibcFormat` is the
  format with the conversions glibc's strftime does not know made literal:
  scan each `%`, its flags (`_ - 0 ^ #`), width, `E`/`O`, colons and
  conversion; when it has a colon (`%:z`, `%::z`) or its conversion is `N`
  or `q`, double its `%` so it prints as written (`"%:z %N %q %P"` ->
  `%:z %N %q pm`); `%%` is copied as is. An empty result is the error
  `strftime/1: unknown system failure` (glibc returns 0; `strftime("")`).
- `strflocaltime/1`: the same with `strflocaltime/1 ...` in every message;
  a number is local seconds, an array is local fields converted with
  `SecondsFromLocalTime` (nullopt -> `strflocaltime/1 requires parsed
  datetime inputs`); `FormatDateTime(..., false)`.
- `strptime/1`: input and format strings, else `strptime/1 requires string
  inputs and arguments`. Parse as glibc's `strptime` in the C locale, the
  subset below; any mismatch, or an unsupported conversion, is `date "<input>"
  does not match format "<format>"` (both as written). After the format:
  nothing left -> fine; the rest starting with whitespace -> appended to the
  result as one more element, a string (`"2023-11-14 "` gives
  `[2023,10,14,0,0,0,2,317," "]` -- jq 1.7.1's behaviour); anything else ->
  the mismatch error. Result: the broken-down array, fields not parsed being
  0 (year 1900, day 0), weekday and day of year computed from the date
  (documented: jq gives odd values for inputs with no day; not pinned).
  The subset:
  - whitespace in the format (and `%n`, `%t`) matches any run of whitespace,
    none included; other characters match themselves; `%%` a `%`;
  - numbers skip leading whitespace, then up to N digits within a range:
    `%Y` 4 digits 0-9999, `%m` 1-12, `%d`/`%e` 1-31, `%H` 0-23, `%I` 1-12,
    `%M` 0-59, `%S` 0-61, `%j` 1-366 (3 digits), `%y` 0-99 (69-99 -> 19xx,
    00-68 -> 20xx); a value out of range is a mismatch;
  - `%b`/`%B`/`%h`: a month name, full or three letters, any case (full
    tried first); `%a`/`%A`: a weekday name, same (parsed, then ignored);
  - `%p`: `AM`/`PM` any case, applied to `%I` (PM adds 12, 12 AM is 0);
  - `%z`: `Z`, or `+`/`-` then `hh`, `hhmm` or `hh:mm` -- parsed and
    ignored (as jq: `+0200` does not shift the hours); `%Z`: a run of
    non-whitespace, ignored;
  - `%s`: an optional `-` and digits -- epoch seconds, broken down with
    `LocalTimeOf` (glibc does);
  - `%T` = `%H:%M:%S`, `%R` = `%H:%M`, `%D` = `%m/%d/%y`, `%F` =
    `%Y-%m-%d`, `%r` = `%I:%M:%S %p`;
  - `%j` with no month and day parsed sets them from the day of year
    (`"2023 318"|strptime("%Y %j")` is `[2023,10,14,0,0,0,2,317]`).

`todate`/`fromdate` and the `iso8601` pair are the prelude's jq
definitions over these.

### `Jq.cpp`

Version `1.2.0`. `Help()` notes: replace the regex note of
`tools--jq-command` with one line: regular expressions are Haisos's Perl
subset on bytes (ASCII classes and case folding; no lookaround; flag `l`
refused).

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add
`commands/jq/JqRegexNatives.cpp`, `commands/jq/JqDateNatives.cpp`.
`tests/unit/components/Jq.unittests/CMakeLists.txt`: add `JqTextTest.cpp`.

## Tests

`tests/unit/components/Jq.unittests/JqTextTest.cpp` (new): table-driven
`EXPECT_EQ(RunJq(program, input), expected)` with `SCOPED_TRACE`; input
`null` unless given. Mind the two levels of escaping in jq programs written
as C++ strings (use raw strings `R"(...)"`).

- `JqRegexTest.MatchObjects`:
  `"foo bar foo"|match("foo")` -> `{"offset":0,"length":3,"string":"foo","captures":[]}`;
  `"foo bar foo"|[match("foo";"g")|.offset]` -> `[0,8]`;
  `"test 123 abc 456"|[match("[0-9]+";"g")|[.offset,.length,.string]]` -> `[[5,3,"123"],[13,3,"456"]]`;
  `"éa"|match("a")` -> `{"offset":1,"length":1,"string":"a","captures":[]}`;
  `"aé b"|match("b")|.offset` -> `3`;
  `"abc"|match("(x)?b")` -> `{"offset":1,"length":1,"string":"b","captures":[{"offset":-1,"string":null,"length":0,"name":null}]}`;
  `"abc"|match("(x?)b")` -> `{"offset":1,"length":1,"string":"b","captures":[{"offset":1,"string":"","length":0,"name":null}]}`;
  `"abc"|match("(?<e>)b")|.captures` -> `[{"offset":1,"string":"","length":0,"name":"e"}]`;
  `"xyz"|[match("(?<n>y)|(?<m>q)")|.captures[]]` -> `[{"offset":1,"length":1,"string":"y","name":"n"},{"offset":-1,"string":null,"length":0,"name":"m"}]`;
  `"a-b"|[match("(?<l>[a-z])-(?<r>[a-z])")|.captures[]|[.name,.string]]` -> `[["l","a"],["r","b"]]`;
  `"abc"|match(["B","i"])|.string` -> `"b"`.
- `JqRegexTest.GlobalAndEmptyMatches`:
  `"abc"|[match("";"g")|.offset]` -> `[0,1,2,3]`; `"aéc"|[match("";"g")]|map(.offset)` -> `[0,1,2,2,3]`;
  `"aa"|[match("a*";"g")|[.offset,.length]]` -> `[[0,2],[2,0]]`;
  `"aab"|[match("a*";"g")|[.offset,.length]]` -> `[[0,2],[2,0],[3,0]]`;
  `"abc"|[match("c";"g")|.offset]` -> `[2]`; `"ab"|[match("";"gn")]` -> `[]`; `"aaa"|[match("a";"gn")|.offset]` -> `[0,1,2]`;
  `"abc"|gsub("";"-")` -> `"-a-b-c-"`; `"abc"|gsub("x*";"-")` -> `"-a-b-c-"`; `"abc"|[splits("")]` -> `["","a","b","c",""]`.
- `JqRegexTest.Flags`:
  `"AbC"|[match("[a-z]";"gi")|.string]` -> `["A","b","C"]`; `"ABC"|test("b";"i")` -> `true`;
  `"a b"|test("a b";"x")` -> `false`; `"ab"|[match("a b";"xg")|.string]` -> `["ab"]`;
  `" "|test("[ ]";"x")` -> `true`; `" "|test("\\ ";"x")` -> `true` (the pattern `\ `);
  `"ab"|test("a # c\nb";"x")` -> `true` (a real newline in the pattern); `"a#b"|test("a\\#b";"x")` -> `true`;
  `"a\nb"|test("a.b")`, `test("a.b";"s")`, `test("a.b";"p")` -> `false`, `false`, `true`;
  `"a\nb"|[match(".";"gp")|.string]` -> `["a","\n","b"]`; `"a\nb"|test("^b";"p")` -> `false`;
  `"a\nb"|test("(?m)^b")`, `test("(?s)a.b")` -> `true`, `true`; `"a\n"|test("a$")` -> `true`;
  `"abc"|test("a";null)`, `test("a";"")`, `test("a";"gixnsp")` -> `true` each;
  `"abc"|test("a";"gq")` -> `error: gq is not a valid modifier string`;
  `"a"|test("(";"q")` -> `error: q is not a valid modifier string` (flags checked before the pattern);
  `"abc"|test("a";"l")` -> `error: Regex failure: the l modifier (longest match) is not supported`;
  `"abc"|test("a";1)` -> `error: number (1) is not a string`.
- `JqRegexTest.Errors` (input `"xab"`, `test(P)`, expected `error: Regex failure: ...`):
  `(` end pattern with unmatched parenthesis; `)` unmatched close parenthesis;
  `[a` premature end of char-class; `*a` target of repeat operator is not specified;
  `[z-a]` empty range in char class; `a{3,2}` upper is smaller than lower in repeat range;
  `\` (one backslash) end pattern at escape; `(a)\2` invalid backref number/name;
  `[[:foo:]]` invalid POSIX bracket type; and Haisos's own: `(?=a)` lookaround assertions are not supported.
  Type errors: `1|test("a")` -> `error: number (1) cannot be matched, as it is not a string`;
  `1|test("a";1)` -> the same; `"a"|test(1;null)` -> `error: number (1) is not a string`;
  `"abc"|test(1)` -> `error: number not a string or array`; `"abc"|test({})` -> `error: object not a string or array`;
  `[splits("a")]` -> `error: null (null) cannot be matched, as it is not a string`.
- `JqRegexTest.Functions`:
  `"xyzzy-14"|capture("(?<a>[a-z]+)-(?<n>[0-9]+)")` -> `{"a":"xyzzy","n":"14"}`; `"ab"|capture("(?<a>x)?b")` -> `{"a":null}`;
  `"test 123 abc 456"|[scan("[0-9]+")]` -> `["123","456"]`; `"a1b2"|[scan("([a-z])([0-9])")]` -> `[["a","1"],["b","2"]]`;
  `"abAB"|[scan("a";"i")]` -> `["a","A"]`;
  `"ab,cd, ef"|[splits(", *")]` -> `["ab","cd","ef"]`; `"a1b22"|[splits("[0-9]+")]` -> `["a","b",""]`;
  `"aBc"|split("b";"i")` -> `["a","c"]`; `"a, b,c"|split(", *"; null)` -> `["a","b","c"]`;
  `"abc"|sub("b";"X")` -> `"aXc"`; `"abcb"|gsub("b";"X")` -> `"aXcX"`; `"abc"|sub("z";"X")` -> `"abc"`;
  `"abc"|sub("(?<x>b)";"[\(.x)]")` -> `"a[b]c"`; `"abc"|[sub("b";"1","2")]` -> `["a1c","a2c"]`;
  `"abab"|[gsub("a";"1","2")]` -> `["1b1b","2b2b"]`; `"aBc"|gsub("b";"X";"i")` -> `"aXc"`;
  `"abc"|sub("(?<x>.)"; "\(.x)\(.x)"; "g")` -> `"aabbcc"`;
  `"Hello World"|gsub("(?<w>[A-Z])"; "_\(.w|ascii_downcase)")` -> `"_hello _world"`;
  `"aé"|gsub("é";"e")` -> `"ae"`; `"abc"|sub("b";1)` -> `error: string ("a") and number (1) cannot be added`;
  `[range(2000)|tostring]|join(",")|split(",";null)|length` -> `2000` (no call-depth error).
- `JqFormatTest.CsvTsv`:
  `[1,"a\"b",null,true,1.50,"x,y"]|@csv` -> `"1,\"a\"\"b\",,true,1.50,\"x,y\""`;
  `[nan,infinite,0.1,1e20,100000000000000000001]|@csv` -> `",1.7976931348623157e+308,0.1,1E+20,100000000000000000001"`;
  `[1,"a\tb\\c\nd\re",null,false,1.50]|@tsv` -> `"1\ta\\tb\\\\c\\nd\\re\t\tfalse\t1.50"`; `[nan,"a"]|@tsv` -> `"\ta"`;
  `[[1]]|@csv` -> `error: array ([1]) is not valid in a csv row`; `[{}]|@tsv` -> `error: object ({}) is not valid in a csv row`;
  `1|@csv` -> `error: number (1) cannot be csv-formatted, only array`; `"a"|@tsv` -> `error: string ("a") cannot be tsv-formatted, only array`.
- `JqFormatTest.HtmlUriSh`:
  `"<a href=\"x\">&'é</a>"|@html` -> `"&lt;a href=&quot;x&quot;&gt;&amp;&apos;é&lt;/a&gt;"`;
  `[1,"<"]|@html` -> `"[1,&quot;&lt;&quot;]"`;
  `"a b/ç~-_.!*'()?=&+:,"|@uri` -> `"a%20b%2F%C3%A7~-_.%21%2A%27%28%29%3F%3D%26%2B%3A%2C"`;
  `1|@uri` -> `"1"`; `[1]|@uri` -> `"%5B1%5D"`;
  `"it's"|@sh` -> `"'it'\\''s'"`; `[1,"a b",null,false,"x'y"]|@sh` -> `"1 'a b' null false 'x'\\''y'"`;
  `1.50|@sh` -> `"1.50"`; `[[1]]|@sh` -> `error: array ([1]) can not be escaped for shell`;
  `{}|@sh` -> `error: object ({}) can not be escaped for shell`.
- `JqFormatTest.Base64`:
  `"héllo"|@base64` -> `"aMOpbGxv"`; `1|@base64` -> `"MQ=="`; `"a\u0000b"|@base64` -> `"YQBi"`;
  `"aGVsbG8=", "aGVsbG8", "aGVsbA===", "aG=Vs", "YWJj=ZGVm", "" | @base64d` -> `"hello" "hello" "hell" "h" "abc" ""`;
  `"YQBi"|@base64d|explode` -> `[97,0,98]`; `"/w=="|@base64d|explode` -> `[65533]`;
  `"!!!!"|@base64d` -> `error: string ("!!!!") is not valid base64 data`;
  `"aGVs bG8="|@base64d` -> `error: string ("aGVs bG8=") is not valid base64 data`;
  `"a"|@base64d` -> `error: string ("a") trailing base64 byte found`; `1|@base64d` -> `error: string ("1") trailing base64 byte found`.
- `JqFormatTest.FormatsEverywhere`:
  `[1]|format("csv")` -> `"1"`; `"x"|format("csv")` -> `error: string ("x") cannot be csv-formatted, only array`;
  `"x"|format(1)` -> `error: number (1) is not a valid format`; `"x"|@base32` -> `error: base32 is not a valid format`;
  `@sh "echo \("a'b")"` -> `"echo 'a'\\''b'"`; `@uri "q=\("a b")&"` -> `"q=a%20b&"`;
  `@csv "\(["a",1])-\(1)"` -> `error: number (1) cannot be csv-formatted, only array`.
- `JqDatesTest.BreakdownAndBack`:
  `1700000000|gmtime` -> `[2023,10,14,22,13,20,2,317]`; `1700000000.5|gmtime` -> `[2023,10,14,22,13,20.5,2,317]`;
  `-1|gmtime` -> `[1969,11,31,23,59,59,3,364]`; `-1.5|gmtime` -> `[1969,11,31,23,59,59.5,3,364]`;
  `1700000000|gmtime|mktime` -> `1700000000`; `[2023,10,14,22,13,20.5,2,317]|mktime` -> `1700000000`;
  `[2023,13,40,0,0,0,0,0]|mktime` -> `1710115200`; `[-1,0,1,0,0,0,0,0]|mktime` -> `-62198755200`;
  `[2023,10,14,1,1,1,0,0,9]|mktime` -> `1699923661`;
  `"a"|gmtime` -> `error: gmtime() requires numeric inputs`; `1e20, infinite, nan | gmtime` -> the first error only:
  `error: error converting number of seconds since epoch to datetime`;
  `"a"|mktime` -> `error: mktime requires array inputs`; `[2023,10,14,1,1,1]|mktime` and
  `[2023,"x",1,1,1,1,1,1]|mktime` -> `error: mktime requires parsed datetime inputs`;
  `"a"|localtime` -> `error: localtime() requires numeric inputs`.
- `JqDatesTest.Strftime`:
  `1700000000|strftime("%A %B %d %j %e %H:%M:%S %Z %z %c %s")` -> `"Tuesday November 14 318 14 22:13:20 UTC +0000 Tue Nov 14 22:13:20 2023 1700000000"`;
  `1700000000|strftime("%-d %_m %e %k %10Y %:z %N %q %P")` -> `"14 11 14 22 0000002023 %:z %N %q pm"`;
  `[2023.7,10.2,14,1,1,1.9,0,0]|strftime("%Y-%m-%d %H:%M:%S")` -> `"2023-11-14 01:01:01"`;
  `[2023,10,14,1,1,1,0,0,"x"]|strftime("%Y")` -> `"2023"`;
  `1700000000|todate`, `1700000000.9|todate` -> `"2023-11-14T22:13:20Z"` each; `1e12|todate` -> `"33658-09-27T01:46:40Z"`;
  `"x"|strftime("%Y")`, `[2023]|strftime("%Y")`, `[1700000000]|todate` -> `error: strftime/1 requires parsed datetime inputs`;
  `1|strftime(1)` -> `error: strftime/1 requires a string format`;
  `1700000000|strftime("")` -> `error: strftime/1: unknown system failure`;
  `"a"|strflocaltime("%Y")` -> `error: strflocaltime/1 requires parsed datetime inputs`.
- `JqDatesTest.Strptime`:
  `"2023-11-14T22:13:20Z"|fromdate` -> `1700000000`;
  `"2023-11-14T22:13:20"|fromdate` -> `error: date "2023-11-14T22:13:20" does not match format "%Y-%m-%dT%H:%M:%SZ"`;
  `"1969-12-31T23:59:59Z"|fromdate` -> `error: invalid gmtime representation`;
  `"2023-11-14 10:00"|strptime("%Y-%m-%d %H:%M")` -> `[2023,10,14,10,0,0,2,317]`;
  `"10/Nov/2023"|strptime("%d/%b/%Y")` -> `[2023,10,10,0,0,0,5,313]`;
  `"november 14 2023"|strptime("%B %d %Y")`, `"Tuesday 2023-11-14"|strptime("%A %Y-%m-%d")`,
  `"23-11-14"|strptime("%y-%m-%d")`, `"20231114"|strptime("%Y%m%d")`, `"2023 318"|strptime("%Y %j")`
  -> `[2023,10,14,0,0,0,2,317]` each; `"69-01-01"|strptime("%y-%m-%d")` -> `[1969,0,1,0,0,0,3,0]`;
  `"  2023-1-5"|strptime("%Y-%m-%d")` -> `[2023,0,5,0,0,0,4,4]`;
  `"Tue, 14 Nov 2023 22:13:20 +0200"|strptime("%a, %d %b %Y %H:%M:%S %z")` -> `[2023,10,14,22,13,20,2,317]`;
  `"2023-11-14 EET"|strptime("%Y-%m-%d %Z")` -> `[2023,10,14,0,0,0,2,317]`;
  `"2023-11-14 "|strptime("%Y-%m-%d")` -> `[2023,10,14,0,0,0,2,317," "]`;
  `"2023-11-14x"|strptime("%Y-%m-%d")` -> `error: date "2023-11-14x" does not match format "%Y-%m-%d"`;
  `"x"|strptime("%Q")` -> `error: date "x" does not match format "%Q"`;
  `1|strptime("%Y")` -> `error: strptime/1 requires string inputs and arguments`.
- `JqDatesTest.LocalTimeAndNow` (independent of the host's zone): `now|type`
  -> `"number"`; `now > 1700000000` -> `true`; `1700000000|localtime` equals
  the array built in the test from `LocalTimeOf(1700000000)`;
  `1700000000|localtime|strflocaltime("%Y-%m-%d %H")` equals
  `FormatDateTime("%Y-%m-%d %H", {1700000000, 0}, false)`.

`tests/unit/components/BuiltinCommands.unittests/JqTest.cpp`:
- New `TEST_F(BuiltinCommandsTest, JqFormatsAndRegexThroughTheCommand)`: input
  `[["a",1],["b c",2]]` -> `jq -r '.[] | @csv'` prints `"a",1\n"b c",2\n`;
  `jq -r '.[] | @tsv'` prints `a\t1\nb c\t2\n`; `jq -r '.[][0] | @sh "echo \(.)"'`
  prints `echo 'a'\necho 'b c'\n`; `jq -c '[.[][0] | select(test("^B"; "i"))]'`
  prints `["b c"]\n`.
- Update every expectation pinning version `1.1.0` to `1.2.0`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Jq*:BuiltinCommandsTest.Every*:BuiltinCommandsTest.Lists*'
bash ./scripts/test_linux.sh L U BuiltinCommands
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/jq/CLAUDE.md`: sections "Regular expressions" (the flags and
  what each becomes, the `x` pre-processing, the Oniguruma message table,
  code point offsets by counting lead bytes, jq 1.7.1's global loop),
  "Formats", "Dates" (UTC via `FormatDateTime`, the glibc-unknown
  conversions kept literal, the strptime subset); file bullets for
  `JqRegexNatives.cpp` and `JqDateNatives.cpp`; and the documented
  differences: bytes not code points for `.` and classes (a non-ASCII
  character is several bytes: `"é"|match(".")` takes its first byte),
  ASCII-only `\w`/`\d`/`\s` and case folding (`"aÉ"|test("é";"i")` is false),
  no lookaround/possessive/`\p{}`/`\Q\E`/`(?x)`, unknown escapes refused,
  quantifier counts above 65535 refused, `l` refused, `n` skips rather than
  extends, a broken-down time's weekday and day of year are recomputed by
  `strftime`, `%Z` in `strftime` is `UTC` whatever the host zone.
- `src/components/BuiltinCommands/CLAUDE.md`: the `jq` row -- version
  1.2.0, regexes, formats and dates added, the exceptions above in short.
- Root `CLAUDE.md`: nothing.

## Acceptance

- [ ] Every test case gives the container's jq output (`TZ=UTC`), messages
      byte-exact; match objects byte-exact including their key orders.
- [ ] Flags `g i x n s p` behave as specified; `l` and invalid flags refused
      with the specified messages; compile errors mapped by the table.
- [ ] Offsets and lengths in code points; jq 1.7.1's global loop (empty
      matches advance one byte; a match at the very end is found).
- [ ] `_nwise` iterative; 2000 splits run.
- [ ] Formats as specified; `@base32` still "not a valid format".
- [ ] Dates without `strptime`/`timegm`/`gmtime_r`; `FormatDateTime` used for
      `strftime`; nothing reaches outside the process but the clock and zone.
- [ ] jq version `1.2.0`; `Jq.unittests`, `BuiltinCommands.unittests` and
      the whole unit suite pass.

## Out of scope

- Oniguruma features `Regex` lacks (lookaround, possessive quantifiers,
  Unicode properties and classes, `\Q...\E`, `(?x)`, longest match), UTF-8
  aware matching -- the `Regex` component's business, not this task's.
- `@base32`/`@base32d` (not in jq 1.7.1), `dateadd`/`datesub`/`date` (not in
  jq 1.7.1), `JQ_COLORS`, `--seq` input, modules.
- Everything of `tools--jq-builtins` and the earlier jq tasks.
