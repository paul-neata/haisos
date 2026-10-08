# Task coreutils--printf-seq: printf and seq, on a shared printf format engine

- Rock: coreutils
- Depends on: coreutils--sort (`GnuQuote` in `BuiltinText.h`)
- Size: ~950 changed lines in ~10 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add printf and seq builtins and the BuiltinPrintf format engine

## Goal

Two new builtins, placed with `BUILTIN rootfs printf /bin/printf` and
`BUILTIN rootfs seq /bin/seq`, that print byte for byte what GNU coreutils 9.4
`printf` and `seq` print in the C locale (with UTF-8 for `\u`/`\U`, see
below), with GNU's messages and exit codes:

- `printf FORMAT [ARGUMENT]...` -- the format reused until the arguments run
  out, `\` escapes, `%b`, `%q`, every C conversion `d i o u x X f F e E g G a A
  c s`, `*` widths and precisions, GNU's numeric parsing (`0x1F`, `010`,
  `'A` character constants) and its diagnostics.
- `seq [OPTION]... LAST | FIRST LAST | FIRST INCREMENT LAST` -- integers and
  decimals, negative steps, `-f/--format`, `-s/--separator`,
  `-w/--equal-width`, GNU's precision and width rules, and GNU's exact
  big-integer fast path.

And one shared engine, `BuiltinPrintf` (contract 3 of this develop), which
later tasks reuse by name: `seq -f` here, `find -printf` widths
(search--find-actions), awk's `printf`/`sprintf` (awk--functions).

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security:
`ICurrentProcess` is the only door out of a process", "Automatic Development
Rules" 9), `src/components/BuiltinCommands/CLAUDE.md` (all of it: output
buffering, `ShellEscapeQuoted`, "Adding a builtin"),
`src/components/BuiltinCommands/BuiltinCommand.h`,
`src/components/BuiltinCommands/commands/echo/Echo.cpp` (a builtin that does
not use `BeginBuiltin` because GNU echo takes no getopt options -- printf is
the same), `src/components/BuiltinCommands/commands/cat/Cat.cpp` (a getopt
builtin), `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h`
(`RunCaptured`) and `WcTest.cpp` (the shape of a per-command test file).

What exists: `ShellEscapeQuoted(name, always)` in `BuiltinCommand.h` is GNU's
shell-escape quoting (what `%q` prints); `ParseBuiltinArgs`/`BeginBuiltin`
parse getopt-style options from a command's `Options()` table; `BuiltinContext`
gives `Out`, `Error` (`<name>: ` prepended), `ErrorText`, `TryHelp`,
`StopRequested()`. A write to a pipe with no reader stops the process (exit
141) and makes `StopRequested()` true, so a loop that checks it ends.

What earlier tasks provide, as if on develop (coreutils--sort,
`src/components/BuiltinCommands/BuiltinText.h`; read that plan for the exact
API): `std::string GnuQuote(std::string_view text);` -- GNU's `quote()` in
the C locale, which is what GNU puts around a value in a message (`'abc'`;
`a'b` becomes `'a\'b'`). Every `'x'` in a
diagnostic below that GNU writes with `quote()` is `GnuQuote(x)`; file names
GNU writes with `quoteaf` are `ShellEscapeQuoted(name, true)`, with `quotef`
`ShellEscapeQuoted(name)`.

## Changes

### New `src/components/BuiltinCommands/BuiltinPrintf.h` / `BuiltinPrintf.cpp` (contract 3)

Namespace `Haisos`. Exactly these names (more helpers may be added, none
renamed):

```cpp
// One conversion specification of a printf format, as glibc reads it:
// %[flags][width][.precision][length]conversion.
struct PrintfSpec {
    std::string flags;                 // in the order given, from "-+ #0'I"
    std::optional<int> width;          // digits, or the '*' argument once the caller has it
    std::optional<int> precision;      // digits ("%.f" is precision 0), or the '*' argument; nullopt: none
    bool widthFromArgument = false;     // '*' was written for the width
    bool precisionFromArgument = false; // '*' was written for the precision
    char conversion = 0;               // the character after the length modifiers, whatever it is
};

// Parses the specification starting at format[pos] == '%'. Flags are any of
// "-+ #0'I" (repeats allowed), then a width (digits, or '*'), then '.' and a
// precision (digits -- none means 0 -- or '*'), then any run of the length
// modifiers h l L q j z t (skipped: the caller decides the argument type
// from the conversion), then the conversion character, stored as is (even an
// invalid one: which conversions are valid is the caller's business --
// printf, seq, awk and find differ). "%%" gives conversion '%'.
// True with pos one past the conversion; false when the format ends before a
// conversion character (pos left at the end).
bool ParsePrintfSpec(std::string_view format, size_t& pos, PrintfSpec& spec);

// glibc printf's output of one argument under |spec|, for the conversions
// d i (Signed), o u x X (Unsigned), f F e E g G a A (Float), and c s (String:
// %s takes the whole view, %c the caller's single character as a one-byte
// view). Width and precision count bytes. A negative width (from '*') is the
// '-' flag with its absolute value; a negative precision is no precision.
// The flags ' and I are accepted and have no effect (the C locale groups
// nothing). Never throws; any conversion character not in the function's
// list formats as if it were d / u / g / s respectively.
std::string FormatPrintfSigned(const PrintfSpec& spec, intmax_t value);
std::string FormatPrintfUnsigned(const PrintfSpec& spec, uintmax_t value);
std::string FormatPrintfFloat(const PrintfSpec& spec, long double value);
std::string FormatPrintfString(const PrintfSpec& spec, std::string_view value);

// The result of one backslash escape read by AppendPrintfEscape.
enum class PrintfEscapeResult { Appended, Stop, Error };

// Reads the escape starting at text[pos] == '\\' and appends its bytes to
// |out|; pos ends one past it. GNU printf's print_esc: \" \\ \a \b \e \f \n
// \r \t \v; \c is Stop (nothing more may be output -- the caller stops
// everything, the trailing format and the rest of the arguments included);
// \xH or \xHH (no hex digit: Error, "missing hexadecimal number in escape");
// octal: with |octalZero| false (the format) \NNN, 1 to 3 octal digits; with
// it true (%b) \0NNN (the 0 then up to 3 digits) and also \NNN when the first
// digit is not 0; \uHHHH and \UHHHHHHHH (exactly 4/8 hex digits, else Error
// "missing hexadecimal number in escape"; D800-DFFF is Error "invalid
// universal character name \uD800" -- lowercase hex as GNU prints it, 4 or 8
// digits) appended as UTF-8; any other character: the backslash and that
// character as written; a backslash ending the text: the backslash alone.
// On Error, |error| holds the message (without "printf: ").
PrintfEscapeResult AppendPrintfEscape(std::string_view text, size_t& pos, bool octalZero,
                                      std::string& out, std::string& error);
```

Implementation notes:

- The `Format*` functions rebuild a C format from the spec -- `%`, the flags
  filtered to `-+ #0` (drop `'` and `I`: MSVC rejects them), the width, the
  precision, the length (`ll` with `long long` for integers -- `intmax_t` is
  `long long` on every target -- `L` for `long double`) and the conversion --
  and call `std::snprintf` twice (size, then fill). No `<regex>`, no POSIX
  headers.
- `FormatPrintfString` pads by hand (spaces, left with `-`; the `0` flag pads
  `%s`/`%c` with spaces, as glibc does: verify `printf("%05s|", "ab")` gives
  `   ab|` -- it does on glibc 2.39) and truncates `%s` to the precision; it
  must handle embedded NUL bytes (`%c` of an empty argument prints one NUL
  byte), so it never goes through `snprintf`.
- `%a`/`%A` print what the platform's `long double` gives (`0x8p-3` for 1 on
  x86-64 Linux, `0x1p+0` where `long double` is `double`, as MSVC and WASM
  have it) -- the same per-architecture difference GNU's own printf has.

### New `src/components/BuiltinCommands/commands/printf/Printf.cpp`

`CreatePrintfCommand()`. Name `printf`, version `1.0.0`. `Options()` is empty
(GNU printf has only `--help`/`--version`). `Help()`: summary "format and
print data", usage `printf FORMAT [ARGUMENT]...` and `printf OPTION`, notes in
a few lines (the escapes, `%b`, `%q`, "--help and --version count only as the
sole argument", and the exceptions below).

Not `BeginBuiltin` -- GNU printf parses its arguments by hand (printf.c main):

1. Exactly one argument `--help` -> `BuiltinHelpText`; exactly one `--version`
   -> `BuiltinVersionText`; exit 0. (`printf --help x` prints `--help`.)
2. A first argument `--` is dropped.
3. No arguments left: `printf: missing operand`, the Try line, exit 1.
4. The first is FORMAT; the rest are ARGUMENTs. Run the format once; while
   it consumed at least one argument and arguments remain, run it again. Then,
   if arguments remain, `printf: warning: ignoring excess arguments, starting
   with 'x'` (quoted with `'`), exit status unchanged.

One pass over FORMAT (printf.c `print_formatted`), building output and
calling `context.Out` as it goes:

- `\`: `AppendPrintfEscape(..., octalZero=false)`. Stop -> flush and end the
  command at once, exit with the status so far. Error -> `printf: <message>`,
  exit 1 at once (output so far is kept).
- `%%` -> `%`.
- `%b`: the next argument (none: nothing) with `AppendPrintfEscape(...,
  octalZero=true)` for each backslash; Stop ends the command as above. Only
  exactly `%b` -- a flag/width before `b` is an invalid specification.
- `%q`: the next argument (none: nothing) as `ShellEscapeQuoted(arg)` (not
  `always`). Only exactly `%q`.
- Otherwise `ParsePrintfSpec`. GNU's validity table: allowed conversions
  `a A c d e E f F g G i o s u x X`; flag `'` or `I` forbids `a A c e E o s x
  X`; `#` forbids `c d i s u`; `0` forbids `c s`; a precision forbids `c`.
  An invalid one (or a format ending inside a spec) -> `printf: %z\: invalid
  conversion specification` where the text quoted is the spec from `%` up to
  and including the character after the length modifiers (nothing after it
  at the end of the format: just what is there, e.g. `%: invalid conversion
  specification`), exit 1 at once.
- `*` width: the next argument through the signed parser below (outside
  int range: `printf: invalid field width: '<arg>'`, exit 1); no argument
  left: 0. `*` precision: likewise, negative means none, over INT_MAX
  `printf: invalid precision: '<arg>'`.
- The conversion's argument is the next one, or `""` when none is left.

Numeric arguments (printf.c `STRTOX`, `verify_numeric`):

- An argument starting with `'` or `"` and at least one more byte: the value
  is that next byte -- or, when it starts a valid UTF-8 sequence, the code
  point (`'é` is 233) -- and any bytes after it give `printf: warning: <rest>:
  character(s) following character constant have been ignored` (the rest
  printed raw, not quoted).
- Otherwise `strtoimax`/`strtoumax` (base 0: `0x` hex, leading `0` octal,
  leading blanks and a sign allowed) for `d i` / `o u x X`, `strtold` for the
  float conversions (C locale; hex floats, `inf`, `nan`). Then: no digits
  consumed -> `printf: 'abc': expected a numeric value`; something left over ->
  `printf: '12abc': value not completely converted` (the value parsed so far is
  printed); out of range -> `printf: '<arg>': Numerical result out of range`
  (the clamped value is printed). Each sets the exit status to 1 and printing
  goes on. An empty argument is 0 with no message (GNU: `printf '%d' ''`
  prints `0`, exit 0). `%u` of `-1` is 18446744073709551615 (strtoumax).
  Use `std::strtoll`/`std::strtoull`/`std::strtold` and `errno` (`<cerrno>`),
  which exist on every target.
- `%c` prints the first byte of the argument (`%c` of `65` is `6`; of `''`
  a NUL byte); `%s` the whole argument.

Exit status: 0, or 1 after any diagnostic above.

### New `src/components/BuiltinCommands/commands/seq/Seq.cpp`

`CreateSeqCommand()`. Name `seq`, version `1.0.0`. `Options()`:
`{'f', "format", kFormat, Required, "FORMAT", "printf style floating-point FORMAT"}`,
`{'s', "separator", kSeparator, Required, "STRING", "separate numbers with STRING (default \\n)"}`,
`{'w', "equal-width", kEqualWidth, None, "", "pad with leading zeroes to equal width"}`.
All treated. Help usage: `seq [OPTION]... LAST`, `seq [OPTION]... FIRST LAST`,
`seq [OPTION]... FIRST INCREMENT LAST`.

Option parsing -- GNU seq stops at the first operand and at a negative
number (`seq -5 -1 -10`), which `ParseBuiltinArgs` cannot do, so split first:
walk the arguments; stop at `--` (dropped, the rest are operands), at a word
that is `-` followed by `.` or a digit, and at any word not starting with `-`
(or exactly `-`). An option word is a short cluster (if its last letter is
`f` or `s`, or `f`/`s` appears with nothing after it in the cluster, the next
word is its argument and is skipped too; letters after `f`/`s` in the cluster
are the attached argument) or a long option (`--format`/`--separator` or an
unambiguous prefix of them without `=` takes the next word). Hand the option
words to `BeginBuiltin`-equivalent processing: call `ParseBuiltinArgs` on that
prefix, then do what `BeginBuiltin` does (errors with the Try line and exit
1, `--help`, `--version`); the words after the split are the operands. (Do not
change `ParseBuiltinArgs`.)

Then, as seq.c 9.4 `main`, in this order:

1. No operand: `seq: missing operand` + Try, exit 1. More than 3: `seq: extra
   operand '<4th>'` + Try, exit 1.
2. `-f FORMAT` validated as `long_double_format`: skip text with `%%` pairs
   to the first lone `%` (none: `seq: format 'abc' has no % directive`, exit
   1); after it flags from `-+#0 '`, digits, optional `.digits`, optional `L`;
   end of format there: `seq: format '%' ends in %`; a conversion not in
   `efgaEFGA`: `seq: format '%d' has unknown %d directive`; another lone `%`
   later: `seq: format '%g %g' has too many % directives`. All exit 1 without
   the Try line. Keep the prefix and suffix texts (with `%%` turned into `%`)
   and the parsed `PrintfSpec` for `FormatPrintfFloat`.
3. `-f` with `-w`: `seq: format string may not be specified when printing
   equal width strings` + Try, exit 1.
4. The fast path (seq.c `seq_fast`): when every operand is all ASCII digits,
   the increment (if given) is between 1 and 200, there is no `-f`/`-w`, and
   the separator is exactly one byte: print FIRST..LAST stepping by INCREMENT
   with decimal-string arithmetic (arbitrary size: `seq 99999999999999999999
   99999999999999999999` prints the number), leading zeros trimmed, each
   number followed by the separator except the last, then `\n`. Nothing at
   all when FIRST > LAST.
5. Otherwise scan each operand (seq.c `scan_arg`, quoted below): `strtold`
   must consume it all (else `seq: invalid floating point argument: '3x'` +
   Try, exit 1); NaN gives `seq: invalid 'not-a-number' argument: 'nan'` +
   Try, exit 1; an increment of 0 gives `seq: invalid Zero increment value:
   '0'` + Try, exit 1. Defaults: FIRST 1, INCREMENT 1.
6. Second fast-path try (seq.c): all three precisions 0, FIRST finite and
   >= 0, LAST >= 0, 0 < INCREMENT <= 200, no `-f`/`-w`, one-byte separator ->
   format FIRST and LAST with `%.0Lf` (LAST `inf` stays `inf`: an endless
   sequence) and run step 4's printer.
7. The format: `-f`'s, else `get_default_format` (quoted below).
8. Print (seq.c `print_numbers`): nothing when out of range from the start
   (`seq 10 1` prints nothing, exit 0); else x = FIRST + i*INCREMENT for i = 0,
   1, ...; the separator between numbers, `\n` after the last; the stop rule
   includes the "print the number just past LAST if it prints as LAST and
   differently from the previous one" rounding fix (format both with the
   format, cut prefix and suffix, `strtold` the middle, compare).

Check `StopRequested()` on every number in both printers (`seq 1 inf` is
endless; it must end on `TriggerStop()` and on a broken pipe).

`scan_arg` (width and precision of an operand; `arg` is the operand after
leading blanks and `+`):

```
precision = INT_MAX; width = 0
if no '.' and no 'p' in arg: precision = 0
if arg has no 'x'/'X' and its value is finite:
    width = strlen(arg); fraction_len = 0
    if '.' at d:
        fraction_len = count of chars after d up to an 'e'/'E' or the end
        precision = fraction_len
        width += (fraction_len == 0) ? -1 : ((d is arg's first char or char before d is not a digit) ? 1 : 0)
    if an 'e' (else 'E') at e:
        exponent = strtol(after e)
        precision += exponent < 0 ? -exponent : -min(precision, exponent)
        width -= strlen(arg) - (e - arg)
        if exponent < 0:
            if '.' present: if e == d + 1: width++
            else: width++
            exponent = -exponent
        else:
            if '.' present and precision == 0 and fraction_len: width--
            exponent -= min(fraction_len, exponent)
        width += exponent
```

`get_default_format`:

```
prec = max(first.precision, step.precision)
if prec != INT_MAX and last.precision != INT_MAX:
    if -w:
        first_width = first.width + (prec - first.precision)
        last_width  = last.width + (prec - last.precision)
        if last.precision and prec == 0: last_width--
        if last.precision == 0 and prec: last_width++
        if first.precision == 0 and prec: first_width++
        return "%0<max(first_width,last_width)>.<prec>Lf"
    return "%.<prec>Lf"
return "%Lg"
```

### `src/components/BuiltinCommands/BuiltinCommandList.h`, `CMakeLists.txt`

Declare `CreatePrintfCommand()` and `CreateSeqCommand()`; add both to
`CreateStandardBuiltinCommands()` (that is what puts `# BUILTIN rootfs printf
/bin/printf` and the seq line into the `haisos --init` template -- never edit
`GetHaisosFileTemplate`). Add `BuiltinPrintf.cpp`,
`commands/printf/Printf.cpp`, `commands/seq/Seq.cpp` to the `BuiltinCommands`
library.

Rules that bite (root `CLAUDE.md`): files and everything else only through
`context.IO()`/`context.Process()` (neither command needs more than its
streams); GNU's output and messages byte for byte; every option of the real
command in `Options()`; `--help` from `BuiltinHelpText` (never hand-written);
`--version`; portable C++17 (Linux, MSVC, WASM).

Documented exceptions (in each `--help` notes, and the CLAUDE.md table):
printf -- `%q` keeps bytes >= 0x80 as they are, as `ShellEscapeQuoted` does
(GNU writes an invalid UTF-8 byte as `$'\200'`); `\u`/`\U` always write UTF-8
(GNU does in a UTF-8 locale); `%a`/`%A` follow the platform's `long double`.
seq -- none beyond `%a`.

## Tests

`tests/unit/components/BuiltinCommands.unittests/PrintfTest.cpp` and
`SeqTest.cpp` on the fixture's `RunCaptured` (exact `out`, `err`, `status`);
add both to that directory's `CMakeLists.txt`. Every expected string below was
checked against GNU coreutils 9.4 with `LC_ALL=C`; for any further case,
verify against `/usr/bin/printf` / `/usr/bin/seq` in the task container
(Ubuntu 24.04, coreutils 9.4) with `LC_ALL=C`.

`PrintfTest.cpp`:
- `PrintfReusesTheFormatUntilArgumentsRunOut`: `'%d %d\n' 1 2 3` -> `"1 2\n3 0\n"`;
  `'%s %s\n' a b c` -> `"a b\nc \n"`; `'%s\n'` -> `"\n"`.
- `PrintfConversions`: `'%5.2f|%-5s|%05d|%x|%o|%e|%g\n' 3.14159 ab 42 255 8 1234.5 0.0001`
  -> `" 3.14|ab   |00042|ff|10|1.234500e+03|0.0001\n"`; `'%#x %#o %+d % d\n' 255 8 5 5`
  -> `"0xff 010 +5  5\n"`; `'%.3d|%*d|%-*d|\n' 5 6 7 8 9` -> `"005|     7|9       |\n"`;
  `'%10.4s|\n' abcdef` -> `"      abcd|\n"`; `'%G %E %F\n' 1e-10 1e10 1.5` -> `"1E-10 1.000000E+10 1.500000\n"`;
  `'%ld %hd %lld %Lf %jd %zd\n' 1 2 3 4.5 5 6` -> `"1 2 3 4.500000 5 6\n"`.
- `PrintfNumericArguments`: `'%i\n' 0x1F 010 -5 +3 ' 7'` -> `"31\n8\n-5\n3\n7\n"`;
  `'%d\n' "'A" '"B'` -> `"65\n66\n"`; `%u` of `-1` -> `"18446744073709551615\n"`.
- `PrintfNumericDiagnostics`: `'%d\n' abc` -> out `"0\n"`, err
  `"printf: 'abc': expected a numeric value\n"`, status 1; `12abc` -> out `"12\n"`,
  err `"printf: '12abc': value not completely converted\n"`; `'%d\n' 99999999999999999999` ->
  out `"9223372036854775807\n"`, err `"printf: '99999999999999999999': Numerical result out of range\n"`;
  `'%d\n' ''` -> `"0\n"`, status 0.
- `PrintfEscapes`: format `'a\x41\101\c xyz'` -> `"aAA"`, status 0;
  `'x\0101y\n'` -> `"x\b1y\n"`; `'\q\n'` -> `"\\q\n"`; `'\u0041\u00e9\n'` ->
  `"A\xc3\xa9\n"`; `'\u12'` -> err `"printf: missing hexadecimal number in escape\n"`, status 1.
- `PrintfPercentB`: `'%b|\n' 'a\tb' 'x\0101y'` -> `"a\tb|\nxAy|\n"`; `'%b' 'a\cb' c` -> `"a"`, status 0.
- `PrintfPercentQ`: `'%q\n' 'a b' "it's" $'x\ny' '' abc` -> `"'a b'\n\"it's\"\n'x'$'\\n''y'\n''\nabc\n"`.
- `PrintfInvalidSpecifications`: `'ab%zcd' 1` -> `"ab1d"`, status 0 (z is a length
  modifier); `'%z\n'` -> err `"printf: %z\\: invalid conversion specification\n"`, status 1;
  `'%5q|'` and `'%-10b|'` -> the same kind of message; `'%'` -> err
  `"printf: %: invalid conversion specification\n"`.
- `PrintfExcessArgumentsWarn`: `lit extra` -> out `"lit"`, err
  `"printf: warning: ignoring excess arguments, starting with 'extra'\n"`, status 0.
- `PrintfMissingOperandAndOptions`: no argument -> err `"printf: missing operand\nTry 'printf --help' for more information.\n"`,
  status 1; `-x` -> out `"-x"`; `-- -x` -> out `"-x"`; `--help x` -> out `"--help"`, err
  `"printf: warning: ignoring excess arguments, starting with 'x'\n"`, status 0.
- `PrintfCharacterConstantWarning`: `'%d\n' "'ab"` -> out `"97\n"`, err
  `"printf: warning: b: character(s) following character constant have been ignored\n"`, status 0.

`SeqTest.cpp`:
- `SeqCountsUpToLast`: `3` -> `"1\n2\n3\n"`; `2 5`; `1 2 10` -> `"1\n3\n5\n7\n9\n"`; `10 1` -> `""`, status 0.
- `SeqNegativeAndDecimalSteps`: `5 -2 0` -> `"5\n3\n1\n"`; `0.1 0.3 1` -> `"0.1\n0.4\n0.7\n1.0\n"`;
  `1 0.5 3` -> `"1.0\n1.5\n2.0\n2.5\n3.0\n"`; `-0 2` -> `"-0\n1\n2\n"`; `1.0 3` -> `"1.0\n2.0\n3.0\n"`;
  `1 3.0` -> `"1\n2\n3\n"`; `0.000001 0.000001 0.000003` -> `"0.000001\n0.000002\n0.000003\n"`.
- `SeqEqualWidth`: `-w 8 11` -> `"08\n09\n10\n11\n"`; `-w -3 1` -> `"-3\n-2\n-1\n00\n01\n"`;
  `-w 5 -1 -2` -> `"05\n04\n03\n02\n01\n00\n-1\n-2\n"`; `-w 0.5 1.75 3` -> `"0.50\n2.25\n"`;
  `-w 1e1 1e1` -> `"10\n"`; `--equal-width 9 10` -> `"09\n10\n"`.
- `SeqSeparatorAndFormat`: `-s, 1 4` -> `"1,2,3,4\n"`; `-s ', ' 1 3` -> `"1, 2, 3\n"`;
  `-s '' 1 3` -> `"123\n"`; `-f '%03g' 1 3` -> `"001\n002\n003\n"`;
  `-f 'x%.2fy' 1 0.5 2` -> `"x1.00y\nx1.50y\nx2.00y\n"`; `-f '%%%g%%' 1 1` -> `"%1%\n"`;
  `-f %e 1 2` -> `"1.000000e+00\n2.000000e+00\n"`.
- `SeqBigIntegersAndExponents`: `99999999999999999999 99999999999999999999` ->
  `"99999999999999999999\n"`; `1e2 1e2` -> `"100\n"`; `0x10 0x12` -> `"16\n17\n18\n"`.
- `SeqErrors` (each exits 1): no operand -> `"seq: missing operand\nTry 'seq --help' for more information.\n"`;
  `a` -> `"seq: invalid floating point argument: 'a'\nTry ..."`; `1 0 3` ->
  `"seq: invalid Zero increment value: '0'\nTry ..."`; `nan` -> `"seq: invalid 'not-a-number' argument: 'nan'\nTry ..."`;
  `1 2 3 4` -> `"seq: extra operand '4'\nTry ..."`; `-f '%d' 1 2` -> `"seq: format '%d' has unknown %d directive\n"`;
  `-f '%g %g' 1 2` -> `"seq: format '%g %g' has too many % directives\n"`; `-f abc 1 2` ->
  `"seq: format 'abc' has no % directive\n"`; `-f % 1` -> `"seq: format '%' ends in %\n"`;
  `-w -f %g 1 2` -> `"seq: format string may not be specified when printing equal width strings\nTry ..."`.
- `SeqEndlessStopsOnStop`: start `/bin/seq 1 inf` with `StartProcess` and an
  in-memory stdout, sleep ~50 ms, `TriggerStop()`, `WaitToFinish(kWaitMs)` is
  true and the exit code is 143.
- `TEST(BuiltinPrintfTest, FormatsLikeGlibc)` (a plain test of the engine, no process):
  `ParsePrintfSpec("%-08.3Lf", pos, spec)` gives flags "-0", width 8,
  precision 3, conversion 'f', pos 8; `"%*.*d"` sets both `FromArgument`
  flags; `"%5"` returns false. `FormatPrintfSigned` with spec `%+5d` and 42 ->
  `"  +42"`; with a width of -5 -> `"42   "`; `FormatPrintfFloat` `%.2e` of
  12345.678L -> `"1.23e+04"`; `FormatPrintfString` `%-4c` of `"x"` -> `"x   "`,
  `%.2s` of `"abcdef"` -> `"ab"`, `%3s` of a one-NUL-byte view -> two spaces and the NUL.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Printf*:BuiltinCommandsTest.Seq*:*BuiltinPrintf*'
bash ./scripts/test_linux.sh L U CliParser
bash ./scripts/test_linux.sh L U
```

(The script's filter matches test executable names, so `BuiltinCommands`
is the narrowest it takes; the direct run narrows to this task's tests.
`TheInitTemplatesBuiltinsAllApplyOnceUncommented` lives in
`CliParser.unittests`.) Add the new builtin names to the exact list in
`ListsEveryBuiltinSortedWithAVersion` (`BuiltinCommandsTest.cpp`), in byte
order.

The generic tests in `BuiltinCommandsTest.cpp` (`EveryBuiltinsHelpHasTheSameShape`,
`EveryBuiltinHasAVersion`, `EveryBuiltinsManPageIsItsHelp`) must pass for both
new commands unchanged.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the opening list of commands;
  a row each for `printf` and `seq` in the commands table (version, what is
  treated, the documented exceptions above); a short paragraph on
  `BuiltinPrintf` (what it is, who reuses it).
- Root `CLAUDE.md`: rows for `printf` and `seq` in the Builtin Commands table;
  `printf` and `seq` in the builtin lists of the "Builtin Commands" paragraph
  and of the `BuiltinCommands/` line of the directory tree.

## Acceptance

- [ ] `BuiltinPrintf.h` declares exactly the contract-3 names and signatures above.
- [ ] printf: format reuse, every conversion, `*`, `%b`, `%q`, escapes, character constants, GNU's three numeric diagnostics with exit 1, invalid-spec and excess-argument messages, `--help`/`--version` only as sole argument.
- [ ] seq: option split before negative numbers, both fast paths, `scan_arg`/`get_default_format` rules, `-f` validation messages, `-w`, `-s`, the rounding fix, stops on `TriggerStop()`.
- [ ] Every expected output in the tests is GNU's (C locale).
- [ ] Registered in `CreateStandardBuiltinCommands()`; sources and tests in their CMakeLists; init-template test green.
- [ ] No POSIX headers, no `<regex>`; builds on Linux; unit tests green.
- [ ] Docs rows added, exceptions documented in `--help` notes too.

## Out of scope

- `printf -v` (bash only, not GNU printf), a shell-builtin printf in hsh (hsh
  finds `/bin/printf` through `PATH`).
- awk's printf semantics (awk--functions), find -printf (search--find-actions):
  they only reuse `BuiltinPrintf`.
- Locale-dependent output (thousands grouping, other decimal points).
