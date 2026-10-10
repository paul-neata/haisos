# Task awk--printf-math: awk's printf, sprintf and math functions

- Rock: awk
- Depends on: awk--functions, coreutils--printf-seq (`BuiltinPrintf.h`)
- Size: ~650 changed lines in ~10 files
- Plan checked against: develop @ b334335
- PR title: Add awk printf, sprintf and the math functions

Split off from awk--functions (which came to ~1500 lines with these): this
task adds awk's format engine on `BuiltinPrintf` and the numeric built-ins.
It also takes the open findings of awk--functions' review (#82): a
parameter bound to a variable that later became an array, the "(from x)"
names, and two small clean-ups (see "#82 follow-ups" below).

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

**Clean room** (root `CLAUDE.md`, "Clean-room rule", above every other
rule): all code is written from scratch. Never read, copy, port, translate
or paraphrase another program's source (gawk, mawk, the one true awk,
busybox, glibc's printf, ...), whatever its licence, and never name another
program's internal functions, variables, types or fields -- not in code, not
in comments. Behaviour is matched from documentation (POSIX awk, the gawk
manual, man pages) and from the observed output of real awks. Everything
below describes behaviour; every name in it is this project's own.

**Write in pieces**: never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step.

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` (and
the files it lists, above all `AwkInterpreter.h/.cpp`, `AwkBuiltins.cpp`,
`AwkValue.h`, `AwkError.h`), `src/components/BuiltinCommands/BuiltinPrintf.h`;
`tests/unit/components/Awk.unittests/AwkRunFixture.h`,
`AwkFunctionsTest.cpp` and `AwkInterpreterTest.cpp` (`NotYetAvailable`);
root `CLAUDE.md` ("Clean-room rule", "Builtin Commands").

What is on develop already (#76-#82, all in
`src/components/BuiltinCommands/commands/awk/`, namespace `Haisos::Awk`):
- `BuiltinPrintf.h` (coreutils--printf-seq, namespace `Haisos`): `struct
  PrintfSpec { std::string flags; std::optional<int> width, precision; bool
  widthFromArgument, precisionFromArgument; char conversion; }`; `bool
  ParsePrintfSpec(std::string_view format, size_t& pos, PrintfSpec& spec)`
  (at `format[pos] == '%'`: flags from `-+ #0'I`, a width of digits or `*`,
  `.` and a precision of digits -- none is 0 -- or `*`, the length modifiers
  `h l L j z t` skipped, then the conversion character stored whatever it
  is; `%%` gives `%`; true with `pos` one past the conversion, false when
  the format ends first); `FormatPrintfSigned(spec, intmax_t)`,
  `FormatPrintfUnsigned(spec, uintmax_t)`, `FormatPrintfFloat(spec, long
  double)`, `FormatPrintfString(spec, std::string_view)` (glibc's output;
  the caller puts a `*` argument into `width`/`precision`; a negative width
  is the `-` flag, a negative precision none; the `'` and `I` flags change
  nothing; `0` pads `%s`/`%c` with spaces; NUL bytes kept). Its `q` gap
  (#53: `q` is not among the skipped length modifiers) does not matter
  here: awk decides on the character after the precision itself, before
  `ParsePrintfSpec` is called, and a `q` there is an unknown conversion,
  copied -- so `BuiltinPrintf.h` stays unchanged.
- `AwkValue.h`: `Value` (`FromNumber`, `FromString`, `FromInput`,
  `GetType()`, `IsNumeric()` -- true for Number, StrNum and Uninitialized --,
  `ToNumber()`, `ToString(format)`, `ToBoolean()`), `AwkNumberToString`,
  `FormatAwkNumber(format, number)` (CONVFMT/OFMT -- unchanged here),
  `AwkIntegerOf` (a NaN or out-of-intmax value is INTMAX_MIN: not for
  printf, which needs every digit).
- `AwkError.h`: `AwkFatal(message, withLocation = true)`; the interpreter's
  `Run` reports it (``awk: <src>:<line>: [(FILENAME=<f> FNR=<n>) ]fatal:
  <message>\n``) and exits 2.
- `AwkInterpreter.h/.cpp`: `Interpreter::RunStatement`'s `case
  StmtKind::Printf:` is the placeholder `throw AwkFatal("printf is not
  implemented yet")`; the `Print` case builds its text and calls
  `Output(stmt, text)`, print's one door (a redirection is still
  ``output redirection is not implemented yet``: awk--io); `ValueOf(expr)`,
  `SpecialString(kSlotCONVFMT)`, `RuntimeWarning(message)` (``awk:
  <src>:<line>: [(FILENAME=<f> FNR=<n>) ]warning: <message>\n``). The
  printf statement's arguments are `stmt.args` (empty for a bare `printf`).
- `AwkBuiltins.cpp`: `Interpreter::CallBuiltin(const Expr& call)` (the name
  in `call.text`, the arguments in `call.operands`) handles the string
  built-ins and ends with the fallback ``function `<name>' is not
  implemented yet`` -- reached now by `sprintf`, the math functions,
  `close`, `fflush` and `system`.
- `AwkParser.cpp`'s argument-count table already checks the built-ins at
  parse time: `sprintf` 0 or more, `rand` 0, `srand` 0-1, `atan2` 2, `sin
  cos exp log sqrt int` 1.
- Tests: the `AwkRunTest` fixture (`AwkRunFixture.h`: `RunCaptured("awk",
  {args...}, stdin)` gives `out`, `err`, `status`; the files `/abc.txt`,
  `/data.csv` = `x,1,2.5\ny,2,3.25\nz,3,4\n`, `/para.txt`);
  `AwkRunTest.NotYetAvailable` in `AwkInterpreterTest.cpp` still expects
  the sprintf and printf placeholders.

Every expected text below was produced by gawk 5.2.1 `--posix` with
`LC_ALL=C` (`gawk:` replaced by `awk:`), re-run at this re-check, except
`rand()`'s values (see below). The task container has only mawk: never
change an expectation to mawk's -- the behaviour described here is the
specification.

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

**What a format means** (gawk `--posix`, observed): bytes outside a
specification are copied as they are. A specification is a `%`, then flags
from `- + space # 0 '` (any order, repeats allowed -- not `I`), then an
optional width (digits or `*`), then an optional `.` and precision (digits
or `*`), then one character c, which decides what it is:
1. c is one of `h l L j z t`: a fatal error, `` `<c>' is not permitted in
   POSIX awk formats `` (`q` is not a modifier for awk: it is an unknown
   conversion, rule 4).
2. c is `%`: one `%`, width and precision ignored (`%5%` and `%5.2%` print
   `%`); no argument is used.
3. c is one of `c d i o u x X e E f F g G a A s`: one argument, formatted
   as below. Haisos reads such a specification with `ParsePrintfSpec` from
   the `%` (it reads the same text: c is no length modifier, and an `I` never
   gets this far); a `*` takes the next argument first (its number truncated
   toward zero; a negative width means the `-` flag, a negative precision
   none).
4. anything else, or the format ending inside the specification: the text
   from the `%` through c (or to the end) is copied as written, no argument
   used (`%k` -> `%k`, `%5kc` -> `%5kc`, `%I d` -> `%I d`, a final `%5`
   -> `%5`, a final `%-` -> `%-`).

Haisos decides between the four by looking at the text itself before
calling `ParsePrintfSpec` (whose rules differ: it skips modifiers and
accepts `I`).

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
  `FormatPrintfSigned(spec, t)`; otherwise all of t's digits, as `%.0f`
  prints t with the same flags and width (`FormatPrintfFloat` with
  conversion `f`, precision 0): `%d` of 2^63 is `9223372036854775808`, of
  1e30 `1000000000000000019884624838656`; `%.3d` of -1e30 the same digits.
  `%d` of -0.5 is `0`.
- `o u x X`: t as above; -2^63 <= t < 2^64 -> `FormatPrintfUnsigned` of t
  (a negative t as its 64-bit two's complement: `%u` of -1 is
  `18446744073709551615`, `%o` of -8 `1777777777777777777770`); otherwise v
  as `%g` with the same flags, width and precision (`FormatPrintfFloat`,
  conversion `g`): `%20x` of 2^64 is `         1.84467e+19`, `%.3x` gives
  `1.84e+19`.
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

- `RunStatement`, `case StmtKind::Printf:` (replacing the placeholder): no
  arguments -> nothing (gawk: `printf` alone prints nothing). Else evaluate
  every argument left to right with `ValueOf`; the first's
  `ToString(CONVFMT)` is the format (`CONVFMT = "%.2f"; printf 3.14159265`
  prints `3.14`); `FormatAwkPrintf(format, rest, CONVFMT)`; the text goes
  to `Output(stmt, text)` -- the same door as `print`, so awk--io's
  redirections apply to both. Return `Flow::Normal`.
- The random state, new members: `std::mt19937 m_random{1};` and `int64_t
  m_seed = 1;` (`<random>`).

### `commands/awk/AwkBuiltins.cpp`

Replace the fallback for these names (`close`, `fflush` and `system` keep
it):
- **sprintf(fmt, ...)**: no argument -> `AwkFatal("sprintf: no arguments")`
  when the call runs (`if (0) print sprintf()` is fine); else
  `Value::FromString(FormatAwkPrintf(...))`, the arguments evaluated as
  printf's.
- **sin, cos, atan2(y, x), exp, log, sqrt**: the `<cmath>` functions on
  `ToNumber()`. Warnings through `RuntimeWarning`, the argument written with
  `FormatAwkNumber("%g", x)`:
  - `log` of a negative x (or -inf): `log: received negative argument <x>`,
    and the result is a NaN with the sign bit set (`-std::numeric_limits<double>::quiet_NaN()`,
    printed `-nan`, as gawk prints it on x86-64 -- the same on every platform);
    `log(0)` is `-inf` with no warning.
  - `sqrt` of a negative x: `sqrt: received negative argument <x>`, result
    `-nan` the same way.
  - `exp`: a finite x whose result is infinite or below `DBL_MIN` (0
    included): `exp: argument <x> is out of range` (`exp(1000)`, `exp(-1000)`,
    `exp(1e10)` -> `1e+10`).
- **int(x)**: `std::trunc` (NaN and infinities unchanged).
- **rand()**: Haisos's own generator -- POSIX leaves it to the
  implementation, and gawk's sequence is a documented difference. Exactly:
  two outputs x1 then x2 of `m_random`, and the result is
  `(floor(x1 / 32) * 2^26 + floor(x2 / 64)) / 2^53` (the 53-bit double built
  from two 32-bit outputs that MT19937's authors publish with the
  generator), a double in [0, 1) and the same on every platform:
  `std::mt19937` is fully specified by the C++ standard.
- **srand([x])**: returns the previous seed (a Number). With x: the seed is
  `ToNumber(x)` truncated toward zero into `int64_t` (NaN -> 0, clamped to
  the range), stored in `m_seed`; without: the current time in seconds since
  the epoch (`std::time(nullptr)`). The generator is reseeded with the low 32
  bits: `m_random.seed(static_cast<uint32_t>(static_cast<uint64_t>(m_seed)))`
  -- so `srand(2^32 + 3)` gives `srand(3)`'s sequence, as gawk's does. Before
  any `srand`, the seed is 1 and the generator is seeded with 1 (`rand()`
  without `srand` gives `srand(1)`'s sequence; the first `srand()` returns 1).

### #82 follow-ups (`AwkInterpreter.h/.cpp`, `AwkBuiltins.cpp`, the awk `CLAUDE.md`)

Small items from awk--functions' review, each with a test (all verified on
gawk 5.2.1 `--posix`):

1. **A binding chain that ends in an array, read as a scalar**
   (`AwkInterpreter.cpp` ~453, `ScalarRef(Variable&, name)`): an untyped
   parameter's chain is walked as now, every still-Untyped link typed
   Scalar -- but an Array met on the chain (the caller's variable became an
   array after the call began) is the fatal ``attempt to use array `<name>'
   in a scalar context``, as for a parameter that is an Array itself. Today
   the walk stops there and the read gives an empty scalar.
   `function f(a){x[1]=1; print "[" a "]"} BEGIN{f(x)}` -> err
   ``awk: cmd. line:1: fatal: attempt to use array `a (from x)' in a scalar context\n``,
   status 2, out empty; the same for `a = 3`, `a++` and `if (a)` in its
   place.
2. **One helper for the chain's kind**: `Variable::Kind BoundKind(const
   Variable& variable)` (a free function or a private static in
   `AwkInterpreter`) -- the kind of the first link of the binding chain that
   is not Untyped, Untyped when every link is (a global, having no binding,
   is its own kind). Items 1, 4 and 5 use it.
3. **The "(from ...)" name, built only when it is needed**
   (`AwkInterpreter.cpp` ~510): `ScalarRefOf` passes the plain name, and
   `ScalarRef(Variable&, name)` writes `<name> (from <passedFrom>)` only
   when it throws (the parameter's own `passedFrom`; empty: the plain
   name). `VariableName` then has no caller: remove it from the `.h` and
   the `.cpp`. And as gawk, the name lists the whole chain: an argument
   that is itself a parameter with a `passedFrom` gives the new parameter
   `<argument>, from <its passedFrom>` (in `CallFunction`, where
   `passedFrom` is set):
   `function g(b){print b} function f(a){g(a)} BEGIN{x[1]=1; f(x)}` ->
   ``...fatal: attempt to use array `b (from a, from x)' in a scalar context\n``;
   `function h(c){x[1]=1; print c} function g(b){h(b)} function f(a){g(a)} BEGIN{f(x)}`
   -> ``... `c (from b, from a, from x)' ...``; a local made an array,
   `function g(b){print b} function f(a, l){l[1]=1; g(l)} BEGIN{f(1)}` ->
   ``... `b (from l)' ...``. ``attempt to use scalar parameter `b' as an
   array`` keeps the plain name (`function g(b){b[1]=1} function f(a){x=1;
   g(a)} BEGIN{f(x)}`).
4. **length** (`AwkBuiltins.cpp` ~113): a bare-variable argument whose
   `BoundKind` is Array is ``length: received array argument`` -- not only
   when the parameter itself is an Array:
   `function f(a){x[1]=1; print length(a)} BEGIN{f(x)}` and
   `function g(b){x[1]=1; print length(b)} function f(a){g(a)} BEGIN{f(x)}`
   -> ``awk: cmd. line:1: fatal: length: received array argument\n``, 2.
   Unchanged: `function f(a){x = 5; print length(a)} BEGIN{f(x)}` prints
   `0` (the parameter's own value, still empty).
5. **split** (`AwkBuiltins.cpp` ~184): a second argument whose `BoundKind`
   is Scalar is ``split: second argument is not an array`` before
   `ArrayRef` is reached (today a chain ending in a scalar gives
   ``attempt to use scalar parameter ...``):
   `function f(a){x=1; split("a b", a)} BEGIN{f(x)}` and
   `function g(b){x=1; split("a", b)} function f(a){g(a)} BEGIN{f(x)}` ->
   ``awk: cmd. line:1: fatal: split: second argument is not an array\n``, 2.
6. **RunBeginItems / RunEndItems**: make the caught `FlowUnwind`'s flow
   explicit, in `RunMainItems`' shape -- `Flow flow = Flow::Normal; try {
   flow = RunStatement(*item.action); } catch (const FlowUnwind& unwind) {
   flow = unwind.flow; }`, then act on `flow == Flow::Exit` (BEGIN: set
   `m_exitFromBegin`, return; END: return). No other flow can arrive there
   (`CallFunction` makes next/nextfile out of BEGIN/END a fatal); a comment
   says so. Behaviour unchanged; the existing tests cover it.
7. **The awk `CLAUDE.md`'s `split(s, a[i])` bullet** (under "Documented
   exceptions") has a stray backtick: write it ``- `split(s, a[i])` is
   refused (`split: second argument is not an array`); gawk makes `a[i]` a
   sub-array, an extension.``

### `commands/awk/Awk.cpp`

`Version()` 1.3.0 -> 1.4.0. `Help().notes`: "printf, sprintf, the math
functions, getline and output redirections are not available yet." becomes
"getline and output redirections are not available yet."; add the
documented exceptions "rand() has its own sequence; srand's seeds and
return values are gawk's." and "%a and %A print the platform's long double
(0x8p-3 for 1 on x86-64; gawk prints 0x1p+0)."

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add
`commands/awk/AwkFormat.cpp` (after `AwkFields.cpp`, keeping the list
sorted).

### Rules that apply (root `CLAUDE.md`)

- Clean room: everything above is behaviour, verified on gawk; write the
  code from it, never from another awk's (or a C library's) source.
- `ICurrentProcess` is the only door out: nothing here reaches files or
  processes; output goes through the existing `Output` door only.
- Output byte for byte as gawk `--posix` (C locale); exceptions documented
  in `--help` notes (from `BuiltinHelpText`) and the CLAUDE.md tables; the
  version bumped (above).
- Portable C++17 (Linux, MSVC, WASM): no POSIX headers, no `<regex>`; the
  non-finite spellings are built by awk, never left to the C library.

## Tests

New `tests/unit/components/Awk.unittests/AwkPrintfTest.cpp` (added to that
directory's `CMakeLists.txt`). Programs are the awk text (C++ raw strings);
expected texts use C escapes (`\000` is a NUL byte).

Plain `TEST`s on the engine (`#include "commands/awk/AwkFormat.h"`):
- `AwkFormatTest.Engine`: `FormatAwkPrintf("%d|%s|%c", {Value::FromNumber(42.9), Value::FromString("s"), Value::FromString("")}, "%.6g")`
  is `42|s|` followed by a NUL byte; `FormatAwkPrintf("%5%|%k", {}, "%.6g")`
  is `%|%k`; `"%ld"` throws `AwkFatal` whose `what()` is
  `` `l' is not permitted in POSIX awk formats ``; `"%s %s"` with one
  argument throws with the message
  `not enough arguments to satisfy format string\n\t`%s %s'\n\t    ^ ran out for this one`.

`class AwkPrintfTest : public AwkRunTest {};` (`AwkRunFixture.h`), `TEST_F`s
on `RunCaptured` with exact out/err/status (0 and empty err unless given):
- `Conversions`:
  `BEGIN { printf "%d|%i|%5.2f|%s|%c|%c|%x|%o|%e|%g|%%|%5s|%-5d|\n", 42.9, -3.7, 3.14159, "str", 65, "hello", 255, 8, 1234.5, 0.0001, "ab", 7 }`
  -> `42|-3| 3.14|str|A|h|ff|10|1.234500e+03|0.0001|%|   ab|7    |\n`;
  `BEGIN { printf "%5.3d|%+d|% d|%#o|%#x|%E|%G|%.0e|%#.0f|%+s|%05s|%-05d\n", 7, 5, 5, 8, 255, 12345.678, 0.00001234, 12345, 3, "s", "ab", 3 }`
  -> `  007|+5| 5|010|0xff|1.234568E+04|1.234E-05|1e+04|3.|s|   ab|3    \n`;
  `BEGIN { printf "%*d|%-*d|%.*f|%*s|%.*d|\n", 5, 42, 4, 7, 2, 3.14159, -6, "ab", 3, 7 }`
  -> `   42|7   |3.14|ab    |007|\n`;
  `BEGIN { printf("%s-%s\n", "a", "b"); printf "%d|%5.1f|%s|\n", "", "", "" }` -> `a-b\n0|  0.0||\n`;
  `BEGIN { CONVFMT = "%.2f"; printf 3.14159265; print "" }` -> `3.14\n`.
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
  `BEGIN { print sprintf() }` -> err `awk: cmd. line:1: fatal: sprintf: no arguments\n`, 2;
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
  Haisos's own sequence (not gawk's; checked against `std::mt19937` and the
  formula above): `BEGIN { print rand(), rand(), rand(); srand(10); print rand() }`
  -> `0.417022 0.720324 0.000114375\n0.771321\n`.

In `AwkFunctionsTest.cpp`, a new `TEST_F(AwkFunctionsTest, LateArrayBinding)`
with the #82 follow-up cases above (items 1, 3, 4 and 5: each program, its
exact err, status 2 and empty out; the `length(a)` of a scalar-bound `a`
printing `0\n`, status 0). The existing `FunctionErrors` expectations
(``a (from x)``, ``scalar parameter `a'``) stay as they are.

`AwkRunTest.NotYetAvailable` (`AwkInterpreterTest.cpp`): drop the sprintf
and printf runs (both work now); add `BEGIN { close("x") }` ->
err ``awk: cmd. line:1: fatal: function `close' is not implemented yet\n``,
status 2 (awk--io changes it); keep the redirection and getline runs.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
./output/linux/Awk.unittests --gtest_filter='AwkPrintfTest.*:AwkFormatTest.*:AwkFunctionsTest.*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/awk/CLAUDE.md`: `AwkFormat.h/.cpp` in the file list, and
  `AwkBuiltins.cpp`'s line naming sprintf and the math functions too; a
  "printf and sprintf" section (what a format means -- the four outcomes,
  each conversion's rule, the non-finite spellings, the running-out message
  and its caret) and a "Math" section (the warnings, `-nan`, the generator
  and the seed rules); "Running"'s hooks bullet and the end of "Built-in
  functions" no longer list printf, sprintf and the math functions as not
  implemented; "Functions": an Array met on a parameter's chain read as a
  scalar is the fatal, and the "(from a, from x)" chain naming; "Documented
  exceptions": the two new ones (rand's sequence, `%a`/`%A`), the "parts
  that do not run yet" bullet down to getline and the output redirections,
  and the `split(s, a[i])` bullet fixed (item 7).
- `src/components/BuiltinCommands/CLAUDE.md`: the awk row -- version 1.4.0,
  printf/sprintf and the math functions among the treated, the two
  exceptions, the not-implemented list down to getline, the output
  redirections, close, fflush and system; the `BuiltinPrintf.h` bullet says
  awk's printf/sprintf use it (find's `-printf` scans its own directives).
- Root `CLAUDE.md`: the awk row in "Builtin Commands" the same way.

## Acceptance

- [ ] `FormatAwkPrintf` declared exactly; every conversion rule above, the
  refused modifiers, the copied unknown specifications, the running-out
  message byte for byte; `BuiltinPrintf.h` unchanged.
- [ ] `printf` goes through `Output` (so redirections in awk--io cover it);
  `sprintf()` with no argument fails only when it runs.
- [ ] The math warnings and `-nan` exactly; `rand()` is the specified
  `std::mt19937` formula; `srand` seeds and returns as specified.
- [ ] The #82 follow-ups: an array at a chain's end read as a scalar is
  gawk's fatal; length and split give their own messages through
  `BoundKind`; the "(from ...)" name built only on the error path and
  listing the whole chain; `VariableName` removed; BEGIN/END's caught flow
  explicit; the CLAUDE.md backtick fixed.
- [ ] Version 1.4.0; every test above passes; no expectation changed to
  mawk's; all unit tests green; the three CLAUDE.md files updated.
- [ ] Clean room: no other awk's (or C library's) source read; no other
  program's internal names in code or comments.

## Out of scope

- Output redirection of `printf` (`>`, `>>`, `|`: awk--io, through `Output`);
  `close`, `fflush`, `system`, `getline`.
- gawk extensions: `%'d` grouping (the flag is accepted, the C locale groups
  nothing), positional `%1$s`, `PROCINFO`, `-M`.
- The Windows/WASM stack depth of 200 nested calls (#82's other medium
  finding): not this task's.
