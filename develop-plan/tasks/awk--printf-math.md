# Task awk--printf-math: awk's printf, sprintf and math functions

- Rock: awk
- Depends on: awk--functions, coreutils--printf-seq (`BuiltinPrintf.h`)
- Size: ~600 changed lines in ~8 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add awk printf, sprintf and the math functions

Split off from awk--functions (which came to ~1500 lines with these): this
task adds awk's format engine on `BuiltinPrintf` and the numeric built-ins.

## Goal

`printf` and `sprintf` work as gawk 5.2 `--posix` runs them, byte for byte
(C locale): every conversion (`c d i o u x X e E f F g G a A s %`), flags
`- + space # 0 '`, widths and precisions (`*` from the arguments), `%c` of a
number (its byte) or of a string (its first byte), `%d` of values beyond 64
bits (all their digits), `%u %x %o` of negative values, `inf`/`nan`, unknown
conversions copied as written, length modifiers refused (`` `l' is not
permitted in POSIX awk formats ``), and gawk's error when the arguments run
out:

```
awk: cmd. line:1: fatal: not enough arguments to satisfy format string
	`%d %s
'
	    ^ ran out for this one
```

The math built-ins `sin cos atan2 exp log sqrt int rand srand` work too,
with gawk's warnings (`log: received negative argument -2`, `exp: argument
1000 is out of range`), and `srand`'s seed rules and return values as gawk's
(`srand()` first returns 1, the default seed). So `awk -F, 'NR > 1 { s += $3 }
END { printf "%.2f\n", s }' data.csv` prints what gawk prints.

## Context

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` (and
the files it lists: `AwkInterpreter.h/.cpp`, `AwkBuiltins.cpp`,
`AwkValue.h`), `src/components/BuiltinCommands/BuiltinPrintf.h`;
`tests/unit/components/Awk.unittests/AwkRunFixture.h` and
`AwkFunctionsTest.cpp`.

What earlier tasks provide (on develop; their plans are the authority):
- coreutils--printf-seq: `BuiltinPrintf.h` -- `struct PrintfSpec { flags,
  width, precision, widthFromArgument, precisionFromArgument, conversion }`,
  `bool ParsePrintfSpec(std::string_view format, size_t& pos, PrintfSpec& spec)`
  (flags from `-+ #0'I`, width, `.precision`, length modifiers skipped,
  `%%` gives conversion `%`), `FormatPrintfSigned(spec, intmax_t)`,
  `FormatPrintfUnsigned(spec, uintmax_t)`, `FormatPrintfFloat(spec, long
  double)`, `FormatPrintfString(spec, std::string_view)` (glibc's output;
  a negative width is the `-` flag; `0` pads `%s`/`%c` with spaces; handles
  NUL bytes).
- awk--values: `Value` (`IsNumeric()` is true for Number, StrNum and
  Uninitialized; `ToNumber`, `ToString(format)`), `AwkNumberToString`,
  `FormatAwkNumber(format, number)` (CONVFMT/OFMT -- unchanged here),
  `AwkFatal`.
- awk--interpreter: `Execute` of a `Printf` statement is the placeholder
  `AwkFatal("printf is not implemented yet")`; `Output(const Stmt& print,
  const std::string& text)` writes print's bytes (standard output; awk--io
  adds redirections); `SpecialString(kSlotCONVFMT)`.
- awk--functions: `Interpreter::CallBuiltin` in `AwkBuiltins.cpp`, with
  `sprintf` and `sin cos atan2 exp log sqrt int rand srand` still the
  placeholder fatal (``function `<name>' is not implemented yet``);
  `RuntimeWarning(message)` (`awk: <src>:<line>: [(FILENAME=<f> FNR=<n>) ]warning: <message>\n`);
  the parser already checks argument counts (`sprintf` takes any number;
  `rand` 0; `srand` 0-1; `atan2` 2; the others 1); the `AwkRunTest`
  fixture in `AwkRunFixture.h`.

Every expected text below was produced by gawk 5.2.1 `--posix` with
`LC_ALL=C` (`gawk:` replaced by `awk:`), except `rand()`'s values (see
below). The task container has only mawk: never change an expectation to
mawk's, and never look for or copy gawk's (or any other awk's) source code
-- the behaviour described here is the specification.

## Changes

### `commands/awk/AwkFormat.h` / `AwkFormat.cpp` (new)

Namespace `Haisos::Awk`.

```cpp
// awk's printf/sprintf: |format| applied to |arguments| (already evaluated,
// in order), as gawk --posix does. |convfmt| converts a number for %s.
// Throws AwkFatal: a length modifier ("`l' is not permitted in POSIX awk
// formats"), or the arguments running out (the message below).
std::string FormatAwkPrintf(const std::string& format, const std::vector<Value>& arguments,
                            const std::string& convfmt);
```

One pass over `format`, bytes copied until a `%`. At a `%`, pre-scan the
specification (awk's rules differ from `ParsePrintfSpec`'s, so look first):
flags from `- + space # 0 '` only (not `I`), then a width (digits or `*`),
then `.` and a precision (digits or `*`), then one character c:
1. c is `h l L j z t` -> `AwkFatal("`<c>' is not permitted in POSIX awk
   formats")` (`q` is not a modifier for awk: it is an unknown conversion).
2. c is `%` -> one `%` (width and precision ignored: `%5%` and `%5.2%` print
   `%`); no argument is used.
3. c is one of `c d i o u x X e E f F g G a A s` -> `ParsePrintfSpec` from
   the `%` (it reads the same text) and format one argument as below; a `*`
   takes the next argument first (its number truncated toward zero; a
   negative width means the `-` flag, a negative precision none).
4. anything else, or the format ending inside the specification -> the text
   from the `%` through c (or to the end) is copied as written, no argument
   used (`%k` -> `%k`, `%5kc` -> `%5kc`, `%I d` -> `%I d`, a final `%5`
   -> `%5`).

**Running out**: when a `*` or a conversion needs an argument and none is
left, throw `AwkFatal` with exactly this message (the interpreter adds the
location prefix, `fatal: ` and the final newline):

```
not enough arguments to satisfy format string\n\t`<format>'\n\t<k spaces>^ ran out for this one
```

where `<format>` is the whole format as given (its newlines included) and k
is the byte offset in it of the character being handled -- the `*`, or the
conversion character. So `printf "ab %d %s|%d\n", 1` gives
`...\n\t`ab %d %s|%d\n'\n\t       ^ ran out for this one` (7 spaces: the `s`).
Nothing is output for that printf (the whole text is built first). Extra
arguments are ignored silently.

**Conversions** (v = the argument's `ToNumber()` for the numeric ones):
- Non-finite v (any numeric conversion): awk builds the text itself (glibc
  and MSVC spell them differently): a sign -- `-` when the sign bit is set,
  else `+` with the `+` flag, else a space with the space flag, else none --
  then `inf` or `nan`, uppercase only for `E F G A`; padded with spaces to
  the width (on the right with `-`; the `0` flag never pads these). So `%5d`
  of +inf is `  inf`, `%X` of +inf is `inf`, `%d` of `log(-1)` is `-nan`,
  `%F` of +inf `INF`.
- `d i`: t = v truncated toward zero; -2^63 <= t < 2^63 ->
  `FormatPrintfSigned(spec, t)`; otherwise all t's digits: `FormatPrintfFloat`
  of t with the same flags and width, conversion `f`, precision 0
  (`%d` of 2^63 is `9223372036854775808`, of 1e30
  `1000000000000000019884624838656`; `%.3d` of -1e30 the same digits).
  `%d` of -0.5 is `0`.
- `o u x X`: t as above; -2^63 <= t < 2^64 -> `FormatPrintfUnsigned` of t
  (a negative t as its 64-bit two's complement: `%u` of -1 is
  `18446744073709551615`, `%o` of -8 `1777777777777777777770`); otherwise
  `FormatPrintfFloat` of v with the same flags, width and precision and
  conversion `g` (`%20x` of 2^64 is `         1.84467e+19`, `%.3x` gives
  `1.84e+19`).
- `e E f F g G a A`: `FormatPrintfFloat(spec, v)`. (`%a`/`%A` show the
  platform's `long double`: `0x8p-3` for 1 on x86-64 where gawk prints
  `0x1p+0` -- a documented exception.)
- `c`: a numeric value (`IsNumeric()`: Number, StrNum -- an input field
  `65` --, Uninitialized) -> the byte `(unsigned char)` of t (`%c` of 65.7
  is `A`, of 321 `A`, of -191 `A`, of 256 or of an unset variable a NUL
  byte); a String -> its first byte, or a NUL byte when it is empty; then
  `FormatPrintfString` of that one byte (the precision is ignored: `%.3c` of
  `"xyz"` is `x`).
- `s`: the value's `ToString(convfmt)` (an integral number as its digits),
  `FormatPrintfString`.

### `commands/awk/AwkInterpreter.h` / `.cpp`

- `Execute(Printf)`: no arguments -> nothing (gawk: `printf` alone prints
  nothing). Else evaluate every argument left to right, the first's
  `ToString(CONVFMT)` is the format; `FormatAwkPrintf(format, rest, CONVFMT)`;
  the text goes to `Output(stmt, text)` -- the same hook as `print`, so
  awk--io's redirections apply to both.
- The random state: `std::mt19937 m_random{1};` and `int64_t m_seed = 1;`
  (`<random>`).

### `commands/awk/AwkBuiltins.cpp`

Replace the placeholders:
- **sprintf(fmt, ...)**: no argument -> `AwkFatal("sprintf: no arguments")`
  when the call runs (`if (0) print sprintf()` is fine); else
  `Value::FromString(FormatAwkPrintf(...))` as printf does.
- **sin, cos, atan2(y, x), exp, log, sqrt**: the `<cmath>` functions on
  `ToNumber()`. Warnings through `RuntimeWarning`, the argument written with
  `FormatAwkNumber("%g", x)`:
  - `log` of a negative x (or -inf): `log: received negative argument <x>`,
    and the result is a NaN with the sign bit set (`-std::numeric_limits<double>::quiet_NaN()`,
    printed `-nan`, as gawk on x86-64 prints it -- the same on every platform);
    `log(0)` is `-inf` with no warning.
  - `sqrt` of a negative x: `sqrt: received negative argument <x>`, result
    `-nan` the same way.
  - `exp`: a finite x whose result is infinite or below `DBL_MIN` (0
    included): `exp: argument <x> is out of range` (`exp(1000)`, `exp(-1000)`,
    `exp(1e10)` -> `1e+10`).
- **int(x)**: `std::trunc` (NaN and infinities unchanged).
- **rand()**: Haisos's own generator -- POSIX leaves it to the
  implementation, and gawk's sequence is a documented difference. Exactly:
  `a = m_random() >> 5; b = m_random() >> 6; return (a * 67108864.0 + b) / 9007199254740992.0;`
  (a double in [0, 1), the same on every platform: `std::mt19937` is fully
  specified by the standard).
- **srand([x])**: returns the previous seed (a Number). With x: the seed is
  `ToNumber(x)` truncated toward zero into `int64_t` (NaN -> 0, clamped to
  the range), stored in `m_seed`; without: the current time in seconds since
  the epoch (`std::time(nullptr)`). The generator is reseeded with the low 32
  bits: `m_random.seed(static_cast<uint32_t>(static_cast<uint64_t>(m_seed)))`
  -- so `srand(2^32 + 3)` gives `srand(3)`'s sequence, as gawk. Before any
  `srand`, the seed is 1 and the generator is seeded with 1 (`rand()` without
  `srand` gives `srand(1)`'s sequence; the first `srand()` returns 1).

### `commands/awk/Awk.cpp`

`Help().notes`: drop printf/sprintf/math from "not available yet"; add the
documented exceptions `rand() has its own sequence (srand's seeds and return
values are gawk's)` and `%a/%A print the platform's long double`.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkFormat.cpp`.

### Rules that apply (root `CLAUDE.md`)

- `ICurrentProcess` is the only door out: nothing here reaches files or
  processes; output goes through the existing `Output` hook only.
- Output byte for byte as gawk `--posix` (C locale); exceptions documented
  in `--help` notes (from `BuiltinHelpText`) and the CLAUDE.md table.
- Portable C++17 (Linux, MSVC, WASM): no POSIX headers, no `<regex>`; the
  non-finite spellings are built by awk, never left to the C library.

## Tests

New `tests/unit/components/Awk.unittests/AwkPrintfTest.cpp` (in that
directory's `CMakeLists.txt`). Programs are the awk text (C++ raw strings);
expected texts use C escapes (`\000` is a NUL byte).

Plain `TEST`s on the engine:
- `AwkFormatTest.Engine`: `FormatAwkPrintf("%d|%s|%c", {Number 42.9, String "s", String ""}, "%.6g")`
  is `42|s|` followed by a NUL byte; `FormatAwkPrintf("%5%|%k", {}, "%.6g")`
  is `%|%k`; `"%ld"` throws `AwkFatal` with ``what()`` `` `l' is not permitted in POSIX awk formats ``;
  `"%s %s"` with one argument throws with the message `not enough arguments to satisfy format string\n\t`%s %s'\n\t    ^ ran out for this one`.

`class AwkPrintfTest : public AwkRunTest {};` (`AwkRunFixture.h`), `TEST_F`s
on `RunCaptured` with exact out/err/status (0 and empty err unless given):
- `Conversions`:
  `BEGIN { printf "%d|%i|%5.2f|%s|%c|%c|%x|%o|%e|%g|%%|%5s|%-5d|\n", 42.9, -3.7, 3.14159, "str", 65, "hello", 255, 8, 1234.5, 0.0001, "ab", 7 }`
  -> `42|-3| 3.14|str|A|h|ff|10|1.234500e+03|0.0001|%|   ab|7    |\n`;
  `BEGIN { printf "%5.3d|%+d|% d|%#o|%#x|%E|%G|%.0e|%#.0f|%+s|%05s|%-05d\n", 7, 5, 5, 8, 255, 12345.678, 0.00001234, 12345, 3, "s", "ab", 3 }`
  -> `  007|+5| 5|010|0xff|1.234568E+04|1.234E-05|1e+04|3.|s|   ab|3    \n`;
  `BEGIN { printf "%*d|%-*d|%.*f|%*s|%.*d|\n", 5, 42, 4, 7, 2, 3.14159, -6, "ab", 3, 7 }`
  -> `   42|7   |3.14|ab    |007|\n`;
  `BEGIN { printf("%s-%s\n", "a", "b"); printf "%d|%5.1f|%s|\n", "", "", "" }` -> `a-b\n0|  0.0||\n`.
- `CharacterConversion`: input `65 hello 3.0\n`,
  `{ printf "%c|%c|%c|%c|%c|%5c|%-3c|%.3c|\n", $1, $2, 321, -191, 65.7, "x", 66, "xyz"; printf "[%s][%s][%d]\n", $3, $3 + 0, $3 }`
  -> `A|h|A|A|A|    x|B  |x|\n[3.0][3][3]\n`;
  `BEGIN { printf "%c|%c|%c|", 256, "", x }` -> `\000|\000|\000|`.
- `NumbersAndStrings`:
  `BEGIN { CONVFMT = "%.2f"; OFMT = "%.3f"; x = 3.14159265; printf "%s|%s|%d\n", x, x "", x; printf "%s %s %s\n", 1e6, 1e16, 100000000000000000000 }`
  -> `3.14|3.14|3\n1000000 10000000000000000 100000000000000000000\n`;
  `BEGIN { printf "%d %d %d %d\n", 2^53, 2^63, 2^64, -2^63; printf "%d %d %d|%.3d\n", 1e30, -1e30, "abc", -1e30; printf "%i|%d|%d|%d\n", "0x1A", " 12abc", -0.5, 2^53 + 1 }`
  -> `9007199254740992 9223372036854775808 18446744073709551616 -9223372036854775808\n1000000000000000019884624838656 -1000000000000000019884624838656 0|-1000000000000000019884624838656\n26|12|0|9007199254740992\n`;
  `BEGIN { printf "%u|%x|%X|%o|%u\n", -1, -1, 255, -8, 3.9; printf "%20x|%.3x|%-12o|%u\n", 2^64, 2^64, -1e30, -2^63 - 5000 }`
  -> `18446744073709551615|ffffffffffffffff|FF|1777777777777777777770|3\n         1.84467e+19|1.84e+19|-1e+30      |-9.22337e+18\n`.
- `NonFinite`:
  `BEGIN { inf = -log(0); nan = log(-1); printf "%5d|%-6d|%05d|%+d|% f|%05.1f|%E|%X|%d|%F|%e\n", inf, inf, inf, inf, inf, -inf, inf, inf, nan, inf, nan }`
  -> out `  inf|inf   |  inf|+inf| inf| -inf|INF|inf|-nan|INF|-nan\n`,
  err `awk: cmd. line:1: warning: log: received negative argument -1\n`.
- `UnknownAndRefused`:
  `BEGIN { printf "a%kb|%5kc|%qd|%be|%I d|%'d|%5%|%5.2%|\n", 1234567; printf "a%5"; printf "|a%"; printf "|a%-"; print "|" }`
  -> `a%kb|%5kc|%qd|%be|%I d|1234567|%|%|\na%5|a%|a%-|\n`;
  each of `%ld`, `%5hd`, `%Lf`, `%jd`, `%zd` (as `BEGIN { printf "<spec>\n", 1 }`)
  -> err ``awk: cmd. line:1: fatal: `<c>' is not permitted in POSIX awk formats\n``
  with `<c>` the modifier letter (`l h L j z`), status 2, out empty.
- `RunningOutOfArguments` (status 2, out empty, err exactly):
  `BEGIN { printf "ab %d %s|%d\n", 1 }` -> ``awk: cmd. line:1: fatal: not enough arguments to satisfy format string\n\t`ab %d %s|%d\n'\n\t       ^ ran out for this one\n``;
  `BEGIN { printf "%*d|\n" }` -> ``...\n\t`%*d|\n'\n\t ^ ran out for this one\n``;
  `BEGIN { printf "%*d|\n", 5 }` -> ``...\n\t`%*d|\n'\n\t  ^ ran out for this one\n``;
  input `a\n`, `{ printf "%d %s\n", 1 }` -> ``awk: cmd. line:1: (FILENAME=- FNR=1) fatal: not enough arguments to satisfy format string\n\t`%d %s\n'\n\t    ^ ran out for this one\n``;
  `BEGIN { x = sprintf("%s %s", "a") }` -> ``awk: cmd. line:1: fatal: not enough arguments to satisfy format string\n\t`%s %s'\n\t    ^ ran out for this one\n``.
  (`...` stands for `awk: cmd. line:1: fatal: not enough arguments to satisfy format string`.)
- `SprintfAndBarePrintf`:
  `BEGIN { printf "x\n", 1, 2; printf; x = sprintf("%d%%", 50); print x, length(x); print sprintf("abc"), sprintf(5) }`
  -> `x\n50% 3\nabc 5\n`; input `a b\n`, `{ printf }` -> nothing;
  `BEGIN { print sprintf() }` -> `awk: cmd. line:1: fatal: sprintf: no arguments\n`, 2;
  `BEGIN { if (0) print sprintf(); print "ok" }` -> `ok\n`.
- `SumsWithPrintf`: `-F, 'NR > 1 { s += $3 } END { printf "%.2f\n", s }' /data.csv` -> `7.25\n`.
- `MathFunctions`:
  `BEGIN { print sin(0), cos(0), atan2(0, -1), exp(1), log(10), sqrt(2), int(3.9), int(-3.9), int("4.5abc"), int(""), int(1e30), int(-0.5), log(0), exp("x") }`
  -> `0 1 3.14159 2.71828 2.30259 1.41421 3 -3 4 0 1000000000000000019884624838656 0 -inf 1\n`;
  `BEGIN { print atan2(1, 0), atan2(-1, -1), atan2(0, 0), cos("a"), log(1) }` -> `1.5708 -2.35619 0 1 0\n`;
  `BEGIN { print exp(1000); print exp(-1000); print exp(1e10) }` -> out `+inf\n0\n+inf\n`,
  err `awk: cmd. line:1: warning: exp: argument 1000 is out of range\nawk: cmd. line:1: warning: exp: argument -1000 is out of range\nawk: cmd. line:1: warning: exp: argument 1e+10 is out of range\n`;
  `BEGIN { print sqrt(-4); print log(-2); print log(-1.5) }` -> out `-nan\n-nan\n-nan\n`,
  err `awk: cmd. line:1: warning: sqrt: received negative argument -4\nawk: cmd. line:1: warning: log: received negative argument -2\nawk: cmd. line:1: warning: log: received negative argument -1.5\n`;
  input `q\n`, `{ print sqrt(-1) }` -> out `-nan\n`, err
  `awk: cmd. line:1: (FILENAME=- FNR=1) warning: sqrt: received negative argument -1\n`;
  `BEGIN { print int(-log(0)), int(log(-1)) }` -> out `+inf -nan\n`, err `awk: cmd. line:1: warning: log: received negative argument -1\n`.
- `RandAndSrand`:
  `BEGIN { print srand(7); print srand(3.7), srand(-2), srand("abc"), srand(2^40), srand(5) }` -> `1\n7 3 -2 0 1099511627776\n` (gawk's);
  `BEGIN { srand(10); a = rand(); b = rand(); srand(10); c = rand(); print (a == c), (a != b), (a >= 0 && a < 1); srand(); x = srand(); print (x > 1700000000) }` -> `1 1 1\n1\n` (gawk's);
  `BEGIN { a = rand(); srand(1); b = rand(); print (a == b) }` -> `1\n`;
  `BEGIN { srand(2^32 + 3); a = rand(); srand(3); b = rand(); print (a == b) }` -> `1\n`;
  `BEGIN { srand(0); a = rand(); srand(1); b = rand(); print (a == b) }` -> `0\n`;
  Haisos's own sequence (not gawk's): `BEGIN { print rand(), rand(), rand(); srand(10); print rand() }`
  -> `0.417022 0.720324 0.000114375\n0.771321\n`.

Change awk--functions' `AwkFunctionsTest.NotYetAvailable` (sprintf now
runs): make it check that `close("x")` is still
``awk: cmd. line:1: fatal: function `close' is not implemented yet\n``, 2
(awk--io changes it). Remove awk--interpreter's printf placeholder
expectation if one is left.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
./output/linux/Awk.unittests --gtest_filter='AwkPrintfTest.*:AwkFormatTest.*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/awk/CLAUDE.md`: a "printf and sprintf" section (the pre-scan
  and its four outcomes, each conversion's rule, the non-finite spellings,
  the running-out message and its caret) and a "Math" section (the warnings,
  `-nan`, the generator and seed rules); `AwkFormat.h/.cpp` in the file list;
  the two exceptions under "Documented exceptions".
- `src/components/BuiltinCommands/CLAUDE.md` and root `CLAUDE.md`: the awk
  rows gain printf/sprintf and the math functions, with the two exceptions.

## Acceptance

- [ ] `FormatAwkPrintf` declared exactly; every conversion rule above, the
  refused modifiers, the copied unknown specifications, the running-out
  message byte for byte.
- [ ] `printf` goes through `Output` (so redirections in awk--io cover it);
  `sprintf()` with no argument fails only when it runs.
- [ ] The math warnings and `-nan` exactly; `rand()` is the specified
  `std::mt19937` formula; `srand` seeds and returns as specified.
- [ ] Every test above passes; no expectation changed to mawk's; all unit
  tests green; CLAUDE.md files updated.

## Out of scope

- Output redirection of `printf` (`>`, `>>`, `|`: awk--io, through `Output`).
- gawk extensions: `%'d` grouping (the flag is accepted, the C locale groups
  nothing), positional `%1$s`, `PROCINFO`, `-M`.
