# Task awk--values: awk values, arrays, fields and records

- Rock: awk
- Depends on: awk--lexer, coreutils--printf-seq (`BuiltinPrintf.h`)
- Size: ~900 changed lines in ~9 files
- Plan checked against: develop @ 5370ec9
- PR title: Add awk's value model, arrays, field splitting and record reading

## Goal

The data layer of the awk interpreter, as a library under
`commands/awk/` with unit tests -- nothing user-visible yet:
- **values** with POSIX's three kinds (number, string, and *strnum*: input
  that looks numeric) plus uninitialized, gawk `--posix`'s string-to-number
  and number-to-string conversions (integers in full, `CONVFMT`/`OFMT`
  otherwise, `+inf`/`-nan`), and POSIX's comparison rules;
- **arrays** (associative, insertion-ordered for `for (k in a)`);
- the **field store** of the current record: `$0`, `$1`..., `NF`, split
  lazily with the `FS` in force when the record was set (`" "`, one
  character, or no splitting for `""`), field and `NF` assignment rebuilding
  `$0` with `OFS`;
- the **record reader**: records separated by `RS`'s first character,
  read in blocks from a descriptor, stopping promptly.

awk--interpreter builds the interpreter on these; awk--records adds regex
field separators and paragraph mode (`RS = ""`); awk--functions and
awk--io reuse them (`split()`, `getline`).

## Context

**Clean room** (root `CLAUDE.md`, "Clean-room rule", above every other
rule): all code is written from scratch. Never read, copy, port, translate
or paraphrase another program's source (gawk, mawk, the one true awk,
busybox, ...), whatever its licence, and never name another program's
internal functions, variables, types or flags. Behaviour is matched from
documentation (POSIX awk, the gawk manual, man pages) and from the observed
output of real awks. The value, array, field-store and reader designs below
are this plan's own, derived from POSIX's description of awk's values and
gawk's observed output -- not from any awk's internals.

**Write in pieces**: never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step.

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md`,
`AwkError.h/.cpp`; `BuiltinCommand.h` (`BuiltinContext`, its constructor
and `StopRequested`), `BuiltinPrintf.h`, `interfaces/IFileDescriptor.h`
(`Read`, `kIOInterrupted`), and how `commands/wc/Wc.cpp` reads a
descriptor and stops.

What earlier tasks provide (on develop):
- awk--lexer, awk--expressions, awk--parser (#76, #77, #78):
  `src/components/BuiltinCommands/commands/awk/` -- `AwkError.h/.cpp`
  (`kAwkName`, `AwkSource`, `AwkWarning`, `AwkSyntaxError`,
  `AwkLocationPrefix`, `FormatAwkSyntaxError`, `FormatAwkWarning`,
  `FormatAwkError`), `AwkLexer`, `AwkAst`, `AwkParser`
  (`ParseAwkProgram`), `AwkInvocation`, `Awk.cpp` (which still reports
  `awk: running programs is not implemented yet` -- unchanged here), all in
  namespace `Haisos::Awk`, and the awk `CLAUDE.md`. Tests:
  `tests/unit/components/Awk.unittests/` (`AwkLexerTest.cpp`,
  `AwkParserTest.cpp`, `AwkCommandTest.cpp`), one executable
  `Awk.unittests` whose `CMakeLists.txt` lists its sources in
  `add_executable` and already includes
  `tests/unit/components/BuiltinCommands.unittests` (for
  `BuiltinCommandsFixture.h`) and links `BuiltinCommands`, `Environment`,
  `Factory`.
- coreutils--printf-seq: `src/components/BuiltinCommands/BuiltinPrintf.h`
  (namespace `Haisos`) -- `struct PrintfSpec` (`flags`, `width`,
  `precision`, `widthFromArgument`, `precisionFromArgument`, `conversion`),
  `bool ParsePrintfSpec(std::string_view format, size_t& pos, PrintfSpec& spec)`
  (pos on the `%`; `%%` gives conversion `%`; length modifiers skipped;
  false when the format ends inside the specification),
  `FormatPrintfSigned(spec, intmax_t)`, `FormatPrintfUnsigned(spec, uintmax_t)`,
  `FormatPrintfFloat(spec, long double)`, `FormatPrintfString(spec, string_view)`
  -- glibc's output.

Every expected value below was checked with gawk 5.2.1 `--posix` on the
host (re-checked at 5370ec9).

## Changes

### `AwkError.h` / `AwkError.cpp`

Add the runtime error type:

```cpp
// A gawk "fatal:" error at run time: the program stops, status 2. The
// interpreter formats it (awk--interpreter): with a location,
// "awk: <src>:<line>: [(FILENAME=<f> FNR=<n>) ]fatal: <message>\n";
// without, "awk: fatal: <message>\n".
class AwkFatal : public std::runtime_error {
public:
    explicit AwkFatal(const std::string& message, bool withLocation = true);
    bool WithLocation() const;
};
```

### `commands/awk/AwkValue.h` / `AwkValue.cpp` (new)

Namespace `Haisos::Awk`.

```cpp
class Value {
public:
    enum class Type { Uninitialized, Number, String, StrNum };

    Value();  // Uninitialized: "" and 0 at once
    static Value FromNumber(double number);
    static Value FromString(std::string text);
    // Text that came from input -- fields, $0, var=value operands, -v values,
    // for-in keys, ARGV, getline: StrNum (keeping the text) when it looks
    // numeric (LooksNumeric), else String.
    static Value FromInput(std::string text);

    Type GetType() const;
    // Number, StrNum or Uninitialized: what makes a comparison numeric.
    bool IsNumeric() const;
    double ToNumber() const;   // Number/StrNum: the number; String: StringToNumber; Uninitialized: 0
    // Number: AwkNumberToString(number, format); String/StrNum: the text; Uninitialized: "".
    // |format| is CONVFMT for conversions, OFMT for print.
    std::string ToString(const std::string& format) const;
    // Number: != 0; StrNum: its number != 0; String: not empty; Uninitialized: false.
    bool ToBoolean() const;
};

// As gawk --posix converts a string (observed: "3x"+0 is 3, "0x1A"+0 26,
// "info"+0 +inf): leading blanks (space \t \n \v \f \r) skipped, then the
// longest prefix std::strtod accepts (decimal, hex "0x1A", "inf",
// "infinity", "nan", any case) -- none at all is 0.
double StringToNumber(std::string_view text);
// True when that conversion took at least one byte and only blanks follow it.
bool LooksNumeric(std::string_view text, double& number);

// A number as awk shows it:
//  - NaN and infinities: "+nan", "-nan", "+inf", "-inf" (the sign bit decides);
//  - an integral value: its decimal integer, every digit, whatever its size
//    and whatever |format| (as "%.0f" prints it: 1e30 is
//    1000000000000000019884624838656, 2^64 18446744073709551616; "-0" is "0");
//  - otherwise FormatAwkNumber(format, number).
std::string AwkNumberToString(double number, const std::string& format);
// |format| (CONVFMT or OFMT) applied to one number: bytes copied, "%%" a
// '%', each conversion parsed by ParsePrintfSpec and formatted with the
// number -- d i: FormatPrintfSigned of the number truncated toward zero
// (clamped to intmax_t); o u x X: FormatPrintfUnsigned of it (a negative
// value converted as glibc would, through intmax_t); c: FormatPrintfString
// of the byte (unsigned char) of the integer; e E f F g G a A:
// FormatPrintfFloat; any other conversion, or a format ending inside one,
// copied as written ("%q" stays "%q", "abc" "abc", "%5" "%5"). A '*' width
// or precision counts as absent (gawk stops with a fatal error -- a
// documented difference).
std::string FormatAwkNumber(const std::string& format, double number);

// What CompareValues returns when a numeric comparison has a NaN on either
// side: the two are unordered, so < <= == > >= are all false and != true
// (gawk --posix: x = "nan"+0; x == x, x < x and x > x are all 0).
inline constexpr int kAwkUnordered = 2;
// POSIX's comparison: numeric (by ToNumber) when both are IsNumeric(),
// else the two ToString(convfmt) compared byte by byte as unsigned chars.
// Numeric: -1 if a < b, 1 if a > b, 0 if equal, kAwkUnordered with a NaN.
// String: negative, zero or positive (never kAwkUnordered). The interpreter
// maps the result to each relational operator, kAwkUnordered first.
int CompareValues(const Value& a, const Value& b, const std::string& convfmt);

// [A-Za-z_][A-Za-z0-9_]*: a legal awk variable name (for -v and var=value).
bool IsAwkIdentifier(std::string_view name);

// An awk array: string keys, insertion-ordered (what `for (k in a)` visits;
// gawk's order is unspecified -- a documented difference).
class AwkArray {
public:
    Value* Find(const std::string& key);          // null when absent
    Value& GetOrCreate(const std::string& key);   // creates an Uninitialized element
    bool Contains(const std::string& key) const;
    void Remove(const std::string& key);
    void Clear();
    size_t Size() const;
    std::vector<std::string> Keys() const;        // a snapshot, insertion order
};
```

`Value` and `AwkArray` are plain value classes (no interface). `AwkArray`
must make `Find`/`GetOrCreate`/`Remove` O(1) on average (e.g. an
`unordered_map` from key to a position in a list of key/value pairs);
element references stay valid until that element is removed.

### `commands/awk/AwkFields.h` / `AwkFields.cpp` (new)

```cpp
// Splits |text| as awk splits a record by |fs|: " " -- runs of blanks
// (space, tab, newline), leading and trailing ones ignored; "" -- no
// splitting (one field, none for empty text: gawk --posix); one other
// byte -- every occurrence of that byte, literally (also "\t", "|", "."),
// empty fields kept; an empty text has no fields. Returns false, |fields|
// untouched, for an fs of two or more bytes (a regex: awk--records).
bool SplitAwkFields(std::string_view text, const std::string& fs, std::vector<std::string>& fields);

class FieldStore {
public:
    // How a record is split when its fields are first needed: given the
    // record and the FS saved with it (and whether RS was "" then). The
    // interpreter supplies it -- SplitAwkFields, then regexes and paragraph
    // mode in awk--records. Throws AwkFatal on its own errors.
    using Splitter = std::function<void(std::string_view record, const std::string& fs,
                                        bool paragraphMode, std::vector<std::string>& fields)>;
    explicit FieldStore(Splitter splitter);

    // $0 = record: fields are split lazily, with |fs| as it is now (a later
    // change of FS does not affect this record).
    void SetRecord(std::string record, const std::string& fs, bool paragraphMode = false);
    // $0 (rebuilt first if a field or NF was assigned since).
    const std::string& Record();
    // $index: 0 is $0 as a FromInput value; 1..NF the fields (FromInput);
    // beyond NF an Uninitialized value. Negative: AwkFatal
    // "attempt to access field -1".
    Value Field(intmax_t index);
    // $index = value: 0 sets the record (re-split with the FS saved with
    // the current record); n > NF first extends with empty fields; then $0
    // is rebuilt -- the fields joined by |ofs|, each a Number converted with
    // |convfmt| (AwkNumberToString). Negative: the same AwkFatal.
    void SetField(intmax_t index, const Value& value, const std::string& ofs, const std::string& convfmt);
    size_t NF();
    // NF = n: truncates or extends with empty fields, rebuilds $0 with |ofs|.
    // Negative: AwkFatal "NF set to negative value".
    void SetNF(intmax_t nf, const std::string& ofs);
};
```

Assigned field values keep their type (a number assigned to `$2` reads
back as that number); fields from splitting are `FromInput`. Rebuilding is
lazy or eager -- the implementer's choice -- but `Record()` must always
give the rebuilt `$0`. Verified behaviours to reproduce (these are tested):
- record ` a b `, `$3 = "c"` -> `$0` `a b c`, NF 3; then `NF = 2` -> `a b`;
  then `$5 = "e"` -> `a b   e`, NF 5;
- record `a b c`, OFS `:`, `$7 = "z"` -> `a:b:c::::z`, NF 7;
- record `a b c`, `NF = 5` -> `a b c  ` (two trailing spaces), NF 5; then
  `$2 = ""` -> `a  c  `;
- empty record, `NF = 2` -> `$0` is one space; then `$2 = "b"` -> ` b`;
- `$0 = "  x  "` -> NF 1, `$1` `x`.

### `commands/awk/AwkInput.h` / `AwkInput.cpp` (new)

```cpp
enum class RecordReadResult { Record, End, Error, Stopped };

// Reads records from one input (a file, standard input; later getline's
// files and commands), in 64 KiB blocks, never past what it needs.
class RecordReader {
public:
    RecordReader(BuiltinContext& context, std::shared_ptr<IFileDescriptor> input);
    // The next record, separated by the first byte of |rs| (RS as it is at
    // this call: a change applies from the next record on; gawk --posix
    // uses only RS's first byte). The separator is not part of the record;
    // a last record without one still counts; an input ending right after a
    // separator has no empty record after it ("a\n\n" gives "a" and "").
    // |rs| == "" (paragraph mode) is awk--records' (until then the caller
    // never passes it). Stopped when context.StopRequested() or a read
    // returned kIOInterrupted; Error on any other negative read.
    RecordReadResult Next(const std::string& rs, std::string& record);
};
```

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkValue.cpp`,
`commands/awk/AwkFields.cpp`, `commands/awk/AwkInput.cpp`.
Portable C++17 (no POSIX headers; `std::strtod`, `std::signbit`,
`std::isnan` are fine -- Haisos never calls `setlocale`, so `strtod`
uses the C locale).

## Tests

`tests/unit/components/Awk.unittests/AwkValueTest.cpp` (new; add it to
`add_executable(Awk.unittests ...)` in that directory's `CMakeLists.txt`),
plain `TEST`s:
- `AwkValueTest.StringToNumber`: `"3x"` 3, `" 12 "` 12, `"0x1A"` 26,
  `"1e3x"` 1000, `".e1"` 0, `"+"` 0, `""` 0, `"nancy"` NaN, `"info"` +inf,
  `"-inf"` -inf.
- `AwkValueTest.LooksNumeric`: true for `"0x11"` (17), `"+inf"`, `" 12 "`,
  `"nan"`, `"1e400"` (+inf); false for `" -3.5e2x"`, `".5."`, `"1e"`, `""`, `"a"`.
- `AwkValueTest.NumberToString` (format `%.6g` unless said): 1e30 ->
  `1000000000000000019884624838656`; -1e30 -> `-1000000000000000019884624838656`;
  2^64 -> `18446744073709551616`; 0.1+0.2 -> `0.3`; 1e-5 -> `1e-05`;
  123456.7 -> `123457`; 1234567.8 -> `1.23457e+06`; -0.0 -> `0`;
  2^31 -> `2147483648`; -2^63 -> `-9223372036854775808`; 1e6*1e6 ->
  `1000000000000`; 100.0/3 -> `33.3333`; +inf -> `+inf`; -inf -> `-inf`;
  NaN with the sign bit clear -> `+nan`, with it set -> `-nan`; 3.14159 with `%.2f` -> `3.14`;
  2.5 with `%d` -> `2`; 255.5 with `%x` -> `ff`; 3.14159 with `x=%.1f%%` -> `x=3.1%`.
- `AwkValueTest.Comparisons`: Number 1 vs Number 1.0 -> 0; String "10" vs
  String "9" -> negative; StrNum "10" vs StrNum "9" -> positive; StrNum
  "10" vs String "9" -> negative (string); Uninitialized vs Number 0 -> 0;
  Uninitialized vs String "" -> 0; Number 2 vs Number 10 -> negative;
  Number NaN vs Number NaN, and vs Number 1 -> `kAwkUnordered`; StrNum
  "nan" vs Number 5 -> `kAwkUnordered` (numeric: `"nan"` from input is a
  StrNum under `--posix`).
- `AwkValueTest.Truth`: Number 0 false, String "0" true, StrNum "0" false,
  String "" false, Uninitialized false, StrNum " 1 " true.
- `AwkValueTest.ArrayKeepsInsertionOrder`: insert z a m, `Keys()` z a m;
  remove a, insert a -> z m a; `Contains`, `Size`, `Clear`; a reference from
  `GetOrCreate` survives inserting 1000 more keys.
- `AwkValueTest.SplitFields`: `SplitAwkFields` of ` a  b\tc\n` by `" "` ->
  a b c; `a:b::c` by `":"` -> a b "" c; `a|b.c` by `"|"` -> `a`, `b.c`;
  `abc` by `""` -> `abc`; `` by `":"` -> none; `x` by `"ab"` -> false.
- `AwkValueTest.FieldStore`: every verified behaviour listed above; FS saved
  at `SetRecord` (`SetRecord("a:b", " ")` then fields read after the caller
  changed FS to `:` still give `$1` `a:b`); `Field(-1)` and `SetNF(-1)`
  throw `AwkFatal` with the texts above; an assigned number `$2 = 3.5`
  rebuilds `$0` with `%.6g`.
- `AwkValueTest.RecordReader`: over an in-memory file (the fixture's
  filesystem, or a `MockFileDescriptor` from `tests/mocks/` fed bytes):
  `a\nb` -> `a`, `b`, End; `a\n\n` -> `a`, ``, End; RS `X` on `aXbXc` ->
  `a b c`; RS `ab` on `xaby\nzabw` -> `x`, `by\nz`, `bw`; RS changed between
  calls takes effect at the next record; a 200 KiB line comes back whole.
  The reader needs a `BuiltinContext`: build one as
  `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`'s
  `BuiltinContextTest` tests do -- `ProcessFileIO::Create({}, "/")` with
  `InstallStandardStreams` of `Mocks::MockFileDescriptor`s, its
  `FakeProcess` and `FindStandardCommand` (both in that file's anonymous
  namespace: write the same small Haisos test helpers into this test file,
  `FindStandardCommand("awk")`), `BuiltinContext context(process, *awk, {},
  stop)` with an `std::atomic<bool> stop`. Also: a stop flag set before
  `Next` gives `Stopped`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/BuiltinCommands/commands/awk/CLAUDE.md`: a bullet each for
`AwkValue.h/.cpp`, `AwkFields.h/.cpp` and `AwkInput.h/.cpp` in the file
list (before `Awk.cpp`'s), `AwkFatal` added to the `AwkError.h/.cpp`
bullet, and a "Values, fields and records" section -- the four types and
where StrNum comes from, the conversions (the strtod-prefix rule, integral
values in full, CONVFMT/OFMT otherwise, inf/nan spellings), the comparison
rule (`kAwkUnordered`), insertion-ordered arrays, the field store (FS saved
per record, lazy split, rebuild with OFS), the record reader (RS's first
byte, the paragraph mode to come). Under "Documented exceptions": `for (k
in a)` visits keys in insertion order (gawk's order is unspecified); a `*`
in CONVFMT/OFMT counts as absent (gawk: fatal); strnum NaNs compare
unordered (gawk 5.2.1 `--posix` gives `$1 == $2` true for fields `nan` and
`+nan`).

## Acceptance

- [ ] The declarations above, exactly; nothing here touches `IFileIO`
  beyond the descriptor handed to `RecordReader`.
- [ ] Every listed conversion, comparison and field behaviour reproduced
  and tested.
- [ ] `RecordReader` stops promptly (checks `StopRequested()` before each
  read); bytes read past the record it returns stay in its own buffer for
  the next call (it is the only reader of its descriptor).
- [ ] New sources in the CMakeLists; all unit tests green.
- [ ] Clean room: every line written from scratch from behaviour (POSIX,
  the gawk manual, observed gawk output); no other awk's source read or
  mirrored, none of its internal names used.

## Out of scope

- The interpreter, variables, special variables, evaluation (awk--interpreter).
- Regex field separators, `RS = ""` paragraph mode (awk--records).
- `split()`, `printf`/`sprintf` (awk--functions), `getline` (awk--io).
