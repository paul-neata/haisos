# Task tools--jq-json: jq's values, JSON reader and JSON writer

- Rock: tools
- Depends on: tools--jq-parse (merged, #85: `commands/jq/` and `Jq.unittests` exist)
- Size: ~900 changed lines in ~14 files (most small)
- Plan checked against: develop @ 79ff345
- PR title: jq: JSON values, reader and writer as jq 1.7.1 prints them

## Goal

The second jq task: the values a jq program works on, and the reading and
printing of JSON exactly as jq 1.7.1 does it -- numbers printed as jq prints
them (a number read from input keeps its literal text: `1.0` stays `1.0`,
`100000000000000000001` stays exact), objects in insertion order, jq's
pretty-printing (`--indent n`, `--tab`, `-c`, `-S`, `-a`) and colours (`-C`)
byte for byte, and jq's input parse errors (`jq: parse error: Unfinished
JSON term at EOF at line 2, column 0`). Still a library in
`commands/jq/` (namespace `Haisos::Jq`); `tools--jq-eval` evaluates over
these values and `tools--jq-command` reads and prints with them.

Reference: the jq 1.7 manual (https://jqlang.github.io/jq/manual/v1.7/),
RFC 8259 (JSON), and the observed output of jq 1.7.1 (Ubuntu 24.04
`jq 1.7.1-3ubuntu0.24.04.x`, `jq --version` prints `jq-1.7`; on the
planning host and in the task container alike). Every expected output in
this plan was checked on the planning host's jq 1.7.1. For anything not
written out, run it in the container (`printf '...' | jq -c .`,
`jq -n '<expr>'`) and copy what it prints.

**Why not `nlohmann::json`** (the goal names it): jq values need three things
nlohmann's do not give -- the literal text of a number (jq 1.7.1 prints
`1.0`, `1.50`, `1E+2` as read, and compares two literals exactly), objects
in insertion order with jq's replace-in-place/delete/append behaviour, and
cheap copies (the evaluator copies values on every step; a jq value is
immutable and shared). And jq's own messages and output format need a
reader and writer of their own anyway. So `Jq::Value` is a small immutable
shared value of its own, and nlohmann is not used by jq -- neither as a
dependency nor as a model: none of its code is copied or mirrored.
(Flagged to the user in the rock's report; nothing else depends on this
choice.)

## Context

**Clean-room rule (the user's, above every other rule; root `CLAUDE.md`
"Clean-room rule"):** no code is copied from any other program or project,
whatever its licence -- jq is MIT and nlohmann/json (in `extern/`) is a
dependency, and the rule applies to both all the same. Never read, copy,
port, translate or paraphrase another program's source -- jq's, gojq's,
jaq's, nlohmann's, a dtoa's, a decimal library's, and code recalled from
memory -- and never name another program's internal functions, variables
or types in code, comments, tests or commit messages. Everything below is
*behaviour*: the jq 1.7 manual, RFC 8259, the published General Decimal
Arithmetic specification (its to-scientific-string layout, which jq's
literal printing follows), and what jq 1.7.1 prints. The algorithms below
(the number layouts, the shortest round-trip search, the parser's explicit
stack) are Haisos's own designs that reproduce that output; write the code
from scratch, shaped by Haisos's own structure (`commands/jq/JqLexer` and
`JqParser` from `tools--jq-parse` are the models for style).

**Write in pieces:** never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step (earlier
runs died on "response exceeded the 32000 output token maximum").

Read first: the root `CLAUDE.md` ("Clean-room rule", "Automatic
Development Rules"), `src/components/BuiltinCommands/commands/jq/CLAUDE.md`
(written by `tools--jq-parse`: the lexer, parser and tree, and the jq
directory's conventions), `commands/jq/JqLexer.h` (its `Number` tokens keep
the text as written -- `tools--jq-eval` turns them into values with
`CanonicalNumberLiteral` below) and `commands/jq/JqLexer.cpp`'s
`AppendUtf8` (moved out below), `src/components/Unicode/Unicode.h`
(`DecodeUtf8`), and `tests/unit/components/Jq.unittests/CMakeLists.txt`.

Rules that bite: portable C++17 (no POSIX headers, no `<regex>`, no
`std::locale`; `std::snprintf`/`std::strtod` are fine -- use the "C" locale
behaviour they have by default); nothing here touches files or processes;
no class implements an `interfaces/` interface (so `Create()` does not
apply). Values are held by value; their payloads by `std::shared_ptr<const ...>`.

## Changes

New files in `src/components/BuiltinCommands/commands/jq/`, namespace
`Haisos::Jq`.

### `JqUtf8.h` / `JqUtf8.cpp` (shared UTF-8 helpers)

Move `AppendUtf8` out of `JqLexer.cpp`'s anonymous namespace into this
pair, unchanged, and have `JqLexer.cpp` include `JqUtf8.h` (the lexer's
tests must still pass as they are). Add `RepairUtf8` next to it:

```cpp
// Appends |codePoint| to |out| as UTF-8 (moved from JqLexer.cpp).
void AppendUtf8(std::string& out, unsigned int codePoint);

// jq's handling of bytes that are not valid UTF-8 (JSON strings, -R lines,
// --arg values, --rawfile contents): every ill-formed piece becomes one
// U+FFFD, everything valid is kept. See "UTF-8 repair" below.
std::string RepairUtf8(std::string_view bytes);
```

**UTF-8 repair** (observed: `printf 'a<bytes>b' | jq -R .`):
- A byte that can start no sequence -- `80`-`BF` (a stray continuation),
  `C0`, `C1`, `F5`-`FF` -- is one U+FFFD on its own: `C0 AF` is two,
  `80 80` two, `F5 80 80 80` four, `F8 88 80 80 80` five, `FE` one.
- A lead byte `C2`-`DF`, `E0`-`EF` or `F0`-`F4` takes the continuation
  bytes that follow it, up to the 1, 2 or 3 it needs. All there: the
  character -- or, when it is an overlong form, a surrogate or above
  U+10FFFF, one U+FFFD for the whole sequence (`E0 80 80`, `ED A0 80`,
  `F4 90 80 80` each one). Cut short by a non-continuation byte or the
  end: one U+FFFD for the lead and the continuation bytes taken, and the
  next byte is read afresh (`E2 82 b` is U+FFFD then `b`; `C3 C3 A9` is
  U+FFFD then `é`; `E2 82 E2 82 AC` U+FFFD then `€`; a trailing
  `F0 9F 98` one U+FFFD).
`Unicode::DecodeUtf8` reports these differently (an overlong or surrogate
sequence as one invalid byte at a time), so `RepairUtf8` does its own
lead-byte walk; `DecodeUtf8` is still the right tool for reading text
known to be valid (the writer's `-a` below).

### `JqValue.h` / `JqValue.cpp`

```cpp
// jq's kinds, in jq's sort order.
enum class Kind { Null, False, True, Number, String, Array, Object };
const char* KindName(Kind kind);  // "null" "boolean" "boolean" "number" "string" "array" "object"

class Value;
using ObjectEntry = std::pair<std::string, Value>;

class Value {
public:
    Value();                                       // null
    static Value Null();
    static Value Boolean(bool b);
    static Value Number(double d);                 // a computed number: no literal
    static Value NumberLiteral(double d, std::string canonicalLiteral);  // keeps the text (see below)
    static Value String(std::string utf8);
    static Value Array(std::vector<Value> elements);
    static Value Object(std::vector<ObjectEntry> entries);  // keys unique, in order

    Kind GetKind() const;
    bool IsTruthy() const;                         // not null, not false
    double AsNumber() const;
    const std::string* Literal() const;            // the canonical literal, or null
    const std::string& AsString() const;
    const std::vector<Value>& AsArray() const;
    const std::vector<ObjectEntry>& AsObject() const;
    const Value* Find(const std::string& key) const;     // object member or null

    // Copies with one change (the original is untouched):
    Value WithMember(const std::string& key, Value v) const;  // replace in place, or append
    Value WithoutMember(const std::string& key) const;        // removed; order of the rest kept
    Value WithElement(size_t index, Value v) const;           // index < size
};

// jq's total order, as `sort`, `<` and `==` show it: by Kind first;
// numbers -- when both have a literal, compared exactly as decimals
// (CompareDecimalLiterals), otherwise as doubles; a NaN is smaller than
// every number and than itself (`nan < nan` and `nan <= nan` are true,
// `nan == nan` and `nan > nan` false, `1 > nan` true, so Compare(nan, x)
// is -1 for every number x, NaN included, and Compare(x, nan) is +1 for
// every other x); strings byte by byte, a prefix first; arrays element by
// element, then the shorter first; objects by their sorted key lists first
// (compared as arrays of strings), then by their values taken in sorted
// key order.
int Compare(const Value& a, const Value& b);
bool Equal(const Value& a, const Value& b);       // Compare == 0

// Exact comparison of two canonical literals (sign, digits, exponent):
// "1.0" == "1", "1.10" == "1.1", "100000000000000000001" >
// "100000000000000000000", "-0" == "0".
int CompareDecimalLiterals(const std::string& a, const std::string& b);

// "1.0" "1E+2" ... from the text as written in JSON or a jq program: see
// "Number literals". Returns false if |text| is not a number.
bool CanonicalNumberLiteral(std::string_view text, std::string& canonical, double& value);
```

Implementation notes: payload by `std::shared_ptr<const std::string>` /
`std::shared_ptr<const std::vector<Value>>` / an object payload holding the
`std::vector<ObjectEntry>` plus an index (`std::unordered_map<std::string,
size_t>`, or linear search below ~16 members -- either is fine). Copying a
`Value` never copies its payload. Object behaviour (jq's, observed):
`{"a":1,"b":2} + {"c":0,"a":3}` is `{"a":3,"b":2,"c":0}`; `del(.a) | .a = 3`
gives `{"b":2,"a":3}`; a duplicate key in input keeps the first position
and the last value (`{"a":1,"b":2,"a":3}` -> `{"a":3,"b":2}`). A literal
and a computed number compare as doubles
(`100000000000000000001 == (100000000000000000000+0)` is true).

**Number literals.** jq 1.7.1 prints a number read from JSON (or written
in a program) by its literal text, put in a canonical form -- the layout
of the General Decimal Arithmetic specification's to-scientific-string,
checked against jq's output. Read the text as sign, coefficient digits and
exponent (`1.50` is 150 with exponent -2, `.5` is 5 and -1, `1e2` is 1
and 2, `01` is 1 and 0, `1.` is 1 and 0, `+.5e-1` is 5 and -2; a `+` sign
is dropped); drop leading zeros of the coefficient (keep one digit); keep
trailing zeros. With `n` digits and exponent `e`, `adjusted = e + n - 1`:
- if `e <= 0` and `adjusted >= -6`: plain notation -- the digits with a
  `.` inserted `-e` places from the right; if that leaves no digit before
  the point, `0.` and zeros first (`0.00001`, `1.000`, `0.0`, `-0`,
  `0.05`);
- otherwise scientific: the first digit, then `.` and the rest if `n > 1`,
  then `E`, `+` or `-`, and `|adjusted|` (`1E+2`, `1.5E+3`, `1E-7`,
  `0E+5`, `1E+1000`).
Expected (host jq 1.7.1, `echo X | jq -c .`): `1.0`->`1.0`,
`1.000`->`1.000`, `1.50`->`1.50`, `1e2`->`1E+2`, `1E01`->`1E+1`,
`1.e1`->`1E+1`, `100000000000000000000`->itself,
`100000000000000000001`->itself, `1E1000`->`1E+1000`,
`0.00001`->`0.00001`, `1e-7`->`1E-7`, `3.141592653589793238`->itself,
`-0`->`-0`, `-00`->`-0`, `-0.0`->`-0.0`, `0.0e0`->`0.0`, `1E-6`->`0.000001`,
`0.1e-6`->`1E-7`, `12.3400`->`12.3400`, `01`->`1`, `00`->`0`, `1.`->`1`,
`.5`->`0.5`, `-.5`->`-0.5`, `+.5e-1`->`0.05`, `+1`->`1`,
`1.5e3`->`1.5E+3`, `0E5`->`0E+5`; a 1000-digit integer is printed whole.
The double is `strtod` of the text (out of range: +-infinity; it is still
printed by its literal). One limit: an adjusted exponent above 999999999
keeps no literal -- the value is the infinity, printed as a computed one
(`1e1000000000` and `10e999999999` -> `1.7976931348623157e+308`). Below
-999999999 jq's output turns irregular; keep the literal as the rule above
gives it, and list that as a documented difference.

**Computed numbers** (no literal: the result of arithmetic, `nan`,
`infinite`, ...) are printed as jq 1.7.1 prints them (observed):
NaN as `null`; +-infinity as `1.7976931348623157e+308` /
`-1.7976931348623157e+308`; zero as `0` or `-0`; otherwise the shortest
decimal digit string that reads back as the same double -- at most 17
significant digits. A way to find it: try `std::snprintf("%.*e", p - 1, x)`
for p = 1..17 and keep the first that `strtod`s back exactly, then drop
trailing zeros. With those `nd` digits `D` and the decimal exponent `X`
of the scientific form (`x = d.ddd x 10^X`), the layout is:
- `X < -4` or `X >= nd + 15` (the integer would need 16 or more zeros
  after its digits): exponential -- first digit, `.` and the rest if
  `nd > 1`, `e`, sign `+`/`-`, `|X|` with at least two digits (`1e+16`,
  `1e-05`, `1.2e+17`, `1.7976931348623157e+308`, `5e-324`,
  `3.3333333333333335e-05`, `1.2345678901234569e+32`);
- `-4 <= X < 0`: `0.`, `-X - 1` zeros, `D` (`0.0001`,
  `0.30000000000000004`);
- `X >= 0`: `D` with `.` after `X + 1` digits, or zero-padded to `X + 1`
  digits and no `.` when `X + 1 >= nd` (`1000000000000000`,
  `12000000000000000`, `123456789012345680`, `1000000000000000.5`,
  `33.333333333333336`, `1234567890123456700000000000000`).
Expected (host jq 1.7.1, `jq -nc`): `1e15+0`->`1000000000000000`,
`1e16+0`->`1e+16`, `12e15+0`->`12000000000000000`, `12e16+0`->`1.2e+17`,
`1e17+0`->`1e+17`, `0.0001+0`->`0.0001`, `0.001+0`->`0.001`,
`0.00001+0`->`1e-05`, `2e-7+0`->`2e-07`, `123e-7+0`->`1.23e-05`,
`1/3`->`0.3333333333333333`, `100/3`->`33.333333333333336`,
`1/30000`->`3.3333333333333335e-05`, `0.1+0.2`->`0.30000000000000004`,
`0*-1`->`-0`, `3.0+0`->`3`, `123456789012345678+0`->`123456789012345680`,
`100000000000000000001+0`->`1e+20`,
`12345678901234567e14+0`->`1234567890123456700000000000000`,
`123456789012345678e15+0`->`1.2345678901234569e+32`,
`1000000000000000.5+0`->`1000000000000000.5`, `5e-324+0`->`5e-324`,
`1e300*1e300`->`1.7976931348623157e+308`, `infinite`->the same,
`nan`->`null`, `0.00001234+0`->`1.234e-05`.

### `JqJsonWriter.h` / `JqJsonWriter.cpp`

```cpp
struct WriteOptions {
    int indent = 2;        // spaces per level (0..7); 0 = compact (no newlines, no space after ':')
    bool tab = false;      // one tab per level instead (pretty)
    bool sortKeys = false; // -S: every object's keys in byte order, recursively
    bool ascii = false;    // -a
    bool color = false;    // -C
};
std::string WriteJson(const Value& value, const WriteOptions& options);
std::string FormatNumber(const Value& number);       // as above: literal or computed
std::string QuoteJsonString(std::string_view s, bool ascii);
// The compact dump (no colour, not ascii) as jq's error messages show a
// value: whole when it is at most |bufferSize - 1| bytes, else its first
// |bufferSize - 4| bytes through RepairUtf8 (a character cut by the limit
// becomes one U+FFFD) followed by "...". Error messages use 15 (11 bytes +
// "...") or 30 (26 + "...").
std::string DumpTruncated(const Value& value, size_t bufferSize);
```

Strings: `"` -> `\"`, `\` -> `\\`, `\b \f \n \r \t` by name, every other
byte below 0x20 and 0x7F as `\u00XX` with lowercase hex (`\u0001`,
`\u001f`, `\u007f`); `/` as is; U+0080-U+009F, U+2028 and every other
code point as is (strings always hold valid UTF-8 -- the reader repairs
its input); with `ascii`, every code point >= 0x80 as `\uXXXX` lowercase
(decode with `Unicode::DecodeUtf8`), above U+FFFF as a surrogate pair
(`"é😀"` -> `"é😀"`).

Layout (pretty): `[` newline, each element at level+1 indentation followed
by `,` except the last, newline, `]` at the level's indentation; objects the
same with `"key": value`; empty containers `[]` and `{}`; the top-level
value has no trailing newline (the command adds the separator). Compact: no
whitespace at all (`{"a":[1,{}]}`). `--indent 0` is compact; `--indent 1`
and `--indent 7` indent by 1 and 7 spaces (jq refuses 8 and above: the
command's business). `sortKeys` orders every object's keys byte by byte
(`{"é":1,"z":2,"Z":3,"aa":4,"a":5,"":6}` -> `"" "Z" "a" "aa" "z" "é"`),
inside arrays too.

Colours (`color`), jq 1.7.1's defaults, byte for byte (ESC = 0x1B):
null `0;90`, false `0;39`, true `0;39`, numbers `0;39`, strings `0;32`,
arrays `1;39`, objects `1;39`, object keys `1;34`; `C(x)` is `ESC[<x>m`,
`R` is `ESC[0m`. The bytes jq writes (observed with `jq -C ... | cat -v`),
as a grammar:
- a scalar: `C(kind)` text `R`;
- an empty array/object: `C(kind)[]R` / `C(kind){}R`;
- an array: `C(arr)[`, then per element: `,` if not the first; pretty: `\n`
  and the indentation; the element; `C(arr)`. After the last: pretty: `\n`
  and the indentation; `C(arr)]R`;
- an object: `C(obj){`, then per member: `,` if not the first; pretty: `\n`
  and the indentation; `R`; `C(key)"key"R`; `C(obj):` (pretty: `: `) `R`;
  the value; `C(obj)`. After the last: pretty: `\n` and the indentation;
  `C(obj)}R`.
Expected (host jq 1.7.1): `jq -Cc -n '{"a":null}'` is
`ESC[1;39m{ESC[0mESC[1;34m"a"ESC[0mESC[1;39m:ESC[0mESC[0;90mnullESC[0mESC[1;39mESC[1;39m}ESC[0m`,
`jq -Cc -n '[1,2]'` is
`ESC[1;39m[ESC[0;39m1ESC[0mESC[1;39m,ESC[0;39m2ESC[0mESC[1;39mESC[1;39m]ESC[0m`,
and `jq -C -n '[[]]'` is
`ESC[1;39m[\n  ESC[1;39m[]ESC[0mESC[1;39m\nESC[1;39m]ESC[0m`.

### `JqJsonReader.h` / `JqJsonReader.cpp`

A push parser: bytes go in, complete values come out, and errors carry
jq's message, line and column -- one reader may be fed several files in a
row (jq parses all input files as one stream, so a parse error's line
counts across files). It keeps an explicit stack of open containers (no
recursion). `JqJsonReader.h` includes `JqUtf8.h`, so the later tasks find
`RepairUtf8` through it.

```cpp
class JsonReader {
public:
    // Feed the next bytes; every top-level value completed by them is
    // appended to |out|. On the first error returns false with |error| set to
    // jq's text without the "jq: parse error: " prefix, e.g.
    // "Expected separator between values at line 1, column 7"; the reader
    // is then unusable.
    bool Feed(std::string_view bytes, std::vector<Value>& out, std::string& error);
    // End of all input: completes a pending top-level scalar (a number or
    // literal needs a delimiter), or fails with "... at EOF at line L, column C"
    // ("Unfinished JSON term at EOF at line 2, column 0").
    bool Finish(std::vector<Value>& out, std::string& error);
};

// One whole text holding exactly one value (fromjson, --argjson, tonumber):
// false with |error| set to the same messages -- "Unexpected extra JSON
// values" when more than one value, "Expected JSON value" when none (host
// jq: `jq -n '"" | fromjson'` fails with `Expected JSON value (while
// parsing '')`, `jq -n '"1 2" | fromjson'` with `Unexpected extra JSON
// values (while parsing '1 2')` -- the caller adds the parenthesis).
bool ParseSingleJson(std::string_view text, Value& out, std::string& error);
```

Positions, as jq reports them (observed): line starts at 1, column at 0;
each byte consumed increments the column, `\n` increments the line and
resets the column to 0; an error's position is after the offending byte.
Outside strings, a byte is either whitespace (space, `\t`, `\r`, `\n`), a
structural character `[ { : , ] }`, `"` (starts a string), or part of a
*literal token* (any other byte, accumulated). A literal token ends at the
next whitespace, structural character or `"`, and is checked then
(observed): a token starting with `t` must be exactly `true` and one
starting with `f` exactly `false`, one starting with `nu` exactly `null` --
otherwise `Invalid literal` (`t`, `tru`, `truex`, `fals`, `nu`, `nul`,
`null2`, `nuLL`); every other token must be a number -- an optional `+`
or `-`, then digits with at most one `.` (at least one digit: `01`, `1.`,
`.5`, `+1`, `-0` are fine), an optional exponent `e`/`E` with optional
sign and at least one digit -- or, in any case, `nan`, `inf`, `infinity`
with an optional sign (`nAn`, `naN`, `-NaN`, `+NaN`, `Inf`, `INFINITY`,
`-inf`, `+infinity`) -- otherwise `Invalid numeric literal` (`n`, `na`,
`nil`, `nUll`, `Null`, `True`, `F`, `nanx`, `nan1`, `Infinite`, `1e5x`,
`1e`, `1e+`, `--1`, `0x10`, `1.2.3`, `.`, `x`). NaN prints as `null`;
infinity as `1.7976931348623157e+308` (with its sign); both carry no
literal. Messages (all verified on the host):

| input | message |
|---|---|
| `[1,2` | `Unfinished JSON term at EOF at line 1, column 4` |
| `[1,2\n` | `Unfinished JSON term at EOF at line 2, column 0` |
| `{"a" 1}` | `Expected separator between values at line 1, column 7` |
| `{"a":1,}` | `Expected another key-value pair at line 1, column 8` |
| `[1,]` | `Expected another array element at line 1, column 4` |
| `tru` | `Invalid literal at EOF at line 1, column 3` |
| `[1] x` | `Invalid numeric literal at EOF at line 1, column 5` (after `[1]` was produced) |
| `{a:1}` | `Invalid numeric literal at line 1, column 3` |
| `-` | `Invalid numeric literal at EOF at line 1, column 1` |
| `"abc` | `Unfinished string at EOF at line 1, column 4` |
| `"\x"` | `Invalid escape at line 1, column 4` |
| `"\u12"` | `Invalid \uXXXX escape at line 1, column 6` |
| `"a` newline `b"` | `Invalid string: control characters from U+0000 through U+001F must be escaped at line 2, column 2` |
| `"\ud800"` | `Invalid \uXXXX\uXXXX surrogate pair escape at line 1, column 8` |
| `"\ud800x"` | `Invalid \uXXXX\uXXXX surrogate pair escape at line 1, column 9` |
| `"\ud800A"` | `Invalid \uXXXX\uXXXX surrogate pair escape at line 1, column 14` |
| `{"a":1}}` | `Unmatched '}' at line 1, column 8` (after `{"a":1}` was produced) |
| `]` | `Unmatched ']' at line 1, column 1` |

String contents are checked when the closing quote arrives (hence the
columns above). A lone low surrogate `\udc00` becomes U+FFFD. Invalid UTF-8
in a string goes through `RepairUtf8` (host jq: `printf '"\xffa\xc3"' |
jq .` prints `"�a�"`, `printf '"\xe2\x82a\xf0\x9f\x98"' | jq .` also
`"�a�"`). Nesting deeper than 256 is `Exceeds depth limit for parsing`
(the 257th `[` fails at `line 1, column 257`; 256 levels read fine).
Several top-level values may follow one another with or without
whitespace (`1 2`, `{"a":1}{"a":2}`, `true false null`).

### Build

- `src/components/BuiltinCommands/CMakeLists.txt`: next to
  `commands/jq/JqAst.cpp`, `JqLexer.cpp`, `JqParser.cpp`, add
  `commands/jq/JqUtf8.cpp`, `commands/jq/JqValue.cpp`,
  `commands/jq/JqJsonWriter.cpp`, `commands/jq/JqJsonReader.cpp`.
- `tests/unit/components/Jq.unittests/CMakeLists.txt` (from
  `tools--jq-parse`; already linked to `BuiltinCommands` and listed in
  `tests/unit/CMakeLists.txt`): add `JqValueTest.cpp`,
  `JqJsonWriterTest.cpp`, `JqJsonReaderTest.cpp` to the
  `add_executable(Jq.unittests ...)` list after `JqLexerTest.cpp
  JqParserTest.cpp`.

## Tests

All in `tests/unit/components/Jq.unittests/`; every expected string is
host jq 1.7.1's output, as listed above.

`JqValueTest.cpp`:
- `JqValueTest.ObjectsKeepInsertionOrder`: WithMember replacing in place,
  appending; WithoutMember then WithMember appends; Find.
- `JqValueTest.CompareFollowsJqOrder`: sorting
  `[null,true,false,0,-1,"a","B",[1],[0,5],{"b":1},{"a":2},{"a":1,"b":0}]`
  with `Compare` gives `[null,false,true,-1,0,"B","a",[0,5],[1],{"a":2},{"a":1,"b":0},{"b":1}]`
  (jq's `sort`); `[{"a":[1]},{"a":[0,5]},{"a":1,"b":0},{"b":1},{"a":2}]`
  sorts to `[{"a":2},{"a":[0,5]},{"a":[1]},{"a":1,"b":0},{"b":1}]`; NaN is
  smaller than every number and than itself (`Compare(nan, nan) == -1`);
  `1.0` equals `1`; literals `100000000000000000001` and
  `100000000000000000000` differ, but the first equals the computed
  `1e20`; `[1.10, 1.1]` are equal.
- `JqValueTest.CanonicalLiterals`: every pair of the "Number literals"
  list, plus `CanonicalNumberLiteral` refusing `1e`, `--1`, `.`.
- `JqValueTest.CopiesShareTheirPayload`: copying a large array is cheap
  (the copy's `AsArray()` is the same object, `&a.AsArray() == &b.AsArray()`).

`JqJsonWriterTest.cpp`:
- `JqJsonWriterTest.ComputedNumbers`: every pair of the "Computed numbers"
  list, plus NaN `null`, +-infinity, `-0`.
- `JqJsonWriterTest.PrettyAndCompact`: `[1,[2,{"a":[],"b":{}}],{},"x"]`
  pretty (2 spaces) is
  `[\n  1,\n  [\n    2,\n    {\n      "a": [],\n      "b": {}\n    }\n  ],\n  {},\n  "x"\n]`;
  compact is the input; `indent 1` and `indent 3` (`[1,{"a":[]}]` ->
  `[\n   1,\n   {\n      "a": []\n   }\n]`); `tab`
  (`{"a":[1]}` -> `{\n\t"a": [\n\t\t1\n\t]\n}`).
- `JqJsonWriterTest.SortKeys`: `{"b":{"d":1,"c":2},"a":[]}` with sortKeys,
  compact -> `{"a":[],"b":{"c":2,"d":1}}`;
  `{"é":1,"z":2,"Z":3,"aa":4,"a":5,"":6,"b":[{"y":1,"x":2}]}` ->
  `{"":6,"Z":3,"a":5,"aa":4,"b":[{"x":2,"y":1}],"z":2,"é":1}`.
- `JqJsonWriterTest.StringEscapes`: `"é\u0001\u007f\u001f/"` normal ->
  `"é\u0001\u007f\u001f/"` (é raw), ascii -> `"é\u0001\u007f\u001f/"`;
  `"é😀"` ascii -> `"é😀"`; `\b\f\n\r\t"\` by name;
  U+0080, U+009F, U+2028 raw.
- `JqJsonWriterTest.Colours`: the three expected byte strings above, plus
  `jq -C -n '{"a":[1,"x",null,true,false,{}],"b":{}}'` pretty -- on the
  host it is (one line per `\n`):
  ```
  ESC[1;39m{
    ESC[0mESC[1;34m"a"ESC[0mESC[1;39m: ESC[0mESC[1;39m[
      ESC[0;39m1ESC[0mESC[1;39m,
      ESC[0;32m"x"ESC[0mESC[1;39m,
      ESC[0;90mnullESC[0mESC[1;39m,
      ESC[0;39mtrueESC[0mESC[1;39m,
      ESC[0;39mfalseESC[0mESC[1;39m,
      ESC[1;39m{}ESC[0mESC[1;39m
    ESC[1;39m]ESC[0mESC[1;39m,
    ESC[0mESC[1;34m"b"ESC[0mESC[1;39m: ESC[0mESC[1;39m{}ESC[0mESC[1;39m
  ESC[1;39m}ESC[0m
  ```
  (the block's two-space margin is not part of it; re-check with
  `| od -c` in the container if in doubt).
- `JqJsonWriterTest.DumpTruncated`: `"abcdefghijklmnopqrstuvwxyz"` with 15
  -> `"abcdefghij...`; `"abcdefghijkl"` (14 bytes) whole, `"abcdefghijklm"`
  -> `"abcdefghij...`; `"abcdefghi€xyz0123"` -> `"abcdefghi` U+FFFD `...`
  (the cut `€`), `"aé€😀bcdefghijk"` -> `"aé€😀...`;
  `{"file":"<top-level>","line":1}` with 30 ->
  `{"file":"<top-level>","lin...`.

`JqJsonReaderTest.cpp`:
- `JqJsonReaderTest.ReadsSeveralValues`: `1 2 {"a":1}{"a":2}\n[3]` fed in
  arbitrary 1-3 byte pieces -> the five values in order (`2` is only
  produced once the space after it arrives; `[3]` before `Finish`, a
  bare `3` only at `Finish`).
- `JqJsonReaderTest.KeepsLiterals`: `[1.0, 1e2, -0]` read then written
  compact -> `[1.0,1E+2,-0]`.
- `JqJsonReaderTest.ErrorMessages`: every row of the table (feed all, then
  `Finish`); values produced before the error are still in `out`.
- `JqJsonReaderTest.LiteralTokens`: every `Invalid literal` and `Invalid
  numeric literal` example above, with jq's column (the token's length,
  `at EOF` when it is the whole input: `nax` -> `Invalid numeric literal
  at EOF at line 1, column 3`, `nul1` -> `Invalid literal at EOF at line 1,
  column 4`).
- `JqJsonReaderTest.LineCountsAcrossFeeds`: `{"a":1}\n` then `{` then
  `Finish` -> `Unfinished JSON term at EOF at line 2, column 1` (host:
  `jq -c . a.json b.json` with those two files).
- `JqJsonReaderTest.Literals`: `nan`, `NaN`, `Infinity`, `-Infinity`,
  `+inf`, `true false null`; `01` -> 1; `.5` -> 0.5.
- `JqJsonReaderTest.InvalidUtf8Repaired`: the two strings above; and
  `RepairUtf8` on every case of "UTF-8 repair" (`a` + bytes + `b`).
- `JqJsonReaderTest.DepthLimit`: 10001 nested `[` -> `Exceeds depth limit
  for parsing at line 1, column 257` (host: `python3 -c "print('['*10001)" |
  jq .`); 256 levels `[[...]]` read fine.
- `JqJsonReaderTest.ParseSingle`: `"1 2"` -> `Unexpected extra JSON values`;
  `""` -> `Expected JSON value`; `"[1,2]"` -> the array.

The existing `JqLexerTest`/`JqParserTest` must pass unchanged after
`AppendUtf8` moves.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/BuiltinCommands/commands/jq/CLAUDE.md`: its opening says
"so far its front half" -- widen it to the values, the JSON reader and the
writer; one bullet per new file (`JqUtf8`: `AppendUtf8` and jq's UTF-8
repair; `JqValue`: immutable shared values, jq's order, number literals;
`JqJsonWriter`: jq's layout, number format, colours, truncated dumps;
`JqJsonReader`: the push parser and jq's messages), why jq has its own
values rather than nlohmann's (the paragraph above, in two sentences), and
under "Documented differences from jq 1.7.1" the literal exponents below
-999999999.

## Acceptance

- [ ] Number printing: every listed literal and computed case byte-exact.
- [ ] Pretty, compact, tab, indent, sort-keys, ascii and colour output
      byte-exact against host jq 1.7.1.
- [ ] Every reader message in the table byte-exact, with values before the
      error delivered; no recursion in the reader; depth limit 256.
- [ ] `RepairUtf8` matches every listed case; `AppendUtf8` lives in
      `JqUtf8.h` only, the lexer's tests unchanged and passing.
- [ ] `Value` copies share payloads; objects keep insertion order as jq.
- [ ] Clean room: nothing copied or mirrored from jq, nlohmann/json or any
      other program; no other program's internal names in code, comments
      or tests.
- [ ] Portable C++17, no `<regex>`, no POSIX headers; `Jq.unittests` passes.

## Out of scope

- Evaluation (`tools--jq-eval`), the command and its options
  (`tools--jq-command`); the parser and lexer beyond moving `AppendUtf8`.
- `JQ_COLORS` (the command's business, and not treated there), `--seq`,
  `--stream` parsing; `--indent` range checking (the command's).
