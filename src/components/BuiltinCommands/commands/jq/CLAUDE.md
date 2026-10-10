# jq

`jq` here is the jq 1.7.1 language, so far its front half: a lexer, a
parser, the syntax tree they build, jq's JSON values, and the JSON reader
and writer -- matching jq 1.7.1 byte for byte, its compile errors, its
parse errors and its output layout included. Nothing runs yet -- the
evaluator (and with it the `jq` builtin itself, its option table,
`--help` and its tests in
`tests/unit/components/BuiltinCommands.unittests/`) is a later task of
the jq rock. The reference is the jq 1.7 manual and the observed
behaviour of the real `jq` (its error messages included, token names and
all); no jq, gojq or jaq source was read or copied -- everything here is
written from scratch, as everywhere in Haisos. All of it lives in
namespace `Haisos::Jq`, as plain portable C++17 (built for Linux,
Windows/MSVC and WASM: no POSIX headers, no `<regex>`, and nothing
recursive per input byte -- nesting is iterative, recursion per
construct, refused past 256 levels).

The pipeline is lexer -> parser -> syntax tree, over values the JSON
reader produces and the JSON writer prints (an evaluator joins later,
each stage a task of its own):

- `JqLexer.h/.cpp` - the `Lexer` of the jq language subset. It tracks
  three states -- the top level, inside a `"` string, inside a `\(`
  interpolation -- and one bracket stack that keeps them honest: a `)`
  closes an interpolation only, and while one is open only that `)` is
  accepted (anything else unmatched). The tokens:
  - Blanks separate tokens; `#` comments run to the end of the line.
  - `.name` is a Field, `..` an operator, `.5` a number (its text kept as
    written); `$name` a Variable, `$__loc__` a LocVariable; `@name` a
    Format. Names, numbers and formats are otherwise jq's own.
  - The multi-byte operators `?//`, `//=`, `//`, `|=`, `!=`, `==`, `<=`,
    `>=`, `..` (and `+= -= *= /= %=`), longest first. The 19 keywords:
    `as def if then elif else end and or reduce foreach try catch label
    break import include module __loc__` (jq rejects the module ones and
    `break` in expressions; the parser reports them).
  - A string is a StringStart, then StringText runs (escapes decoded:
    the standard set, `\uXXXX` and its surrogate pairs, a lone surrogate
    U+FFFD), InterpolationStart/End pairs around a whole sub-program, and
    a StringEnd. A bad escape makes the whole escape run one Invalid
    token with jq's message in `Error()` (the first problem's wording,
    jq's column arithmetic included); an unmatched closer is an Invalid
    with no error of its own.
- `JqAst.h/.cpp` - the tree: `Node` (one kind per construct, the kinds in
  the header), `Pattern` (a destructuring alternative), and
  `FunctionDefinition`. The tree is owned by one program, held by
  `std::unique_ptr` (it implements nothing in `interfaces/`, so the
  `Create()` rule does not apply). Object shorthand keys and keywords are
  desugared by the parser (`{a}` becomes `("a" (index . "a"))`,
  `{if: 1}` `("if" 1)`, `{$x}` `("x" $x)`); `$__loc__` is a Loc node
  holding its line. `DumpNode` writes the one-line form the tests
  compare -- its exact grammar is documented in the header.
- `JqParser.h/.cpp` - `ParseProgram`: a recursive-descent parser over the
  lexer, precedence jq's (`|` right, `,` left, `//` right, the
  assignments non-associative, `and`/`or` left, comparisons
  non-associative, `+ -` then `* / %`, unary `-`, postfix). A `def` chain
  scopes over the whole rest of its pipe expression; `as` binds a
  pattern (the `?//` alternatives included), `reduce`, `foreach`,
  `if/elif/else/end`, `try ... catch`, `label $x | ...` and `break $x`
  are the rest of the language. `try`'s body gets the restricted
  postfix: `?` is absorbed only directly after a suffix, once per
  suffix; a `?` after that wraps the whole `try` in another one, and no
  `catch` may follow it (jq's own grammar quirk). Every failure is jq's
  message, byte for byte: `Fail` builds jq's
  `syntax error, unexpected X[, expecting Y] (Unix shell quoting issues?)`,
  and `FormatCompileError`/`FormatCompileErrorCount` print it with jq's
  location block (the offending line, jq's space padding, no caret) and
  the `jq: N compile error(s)` count line.
- `JqUtf8.h/.cpp` - `AppendUtf8` (a code point as its UTF-8 bytes) and
  `RepairUtf8`, jq's string-byte repair: a well-formed sequence is kept
  as it is, every ill-formed one becomes U+FFFD, one per sequence, the
  bytes around it untouched.
- `JqValue.h/.cpp` - `Value`, a JSON value as jq holds one: null, false,
  true, number, string, array, object, immutable and shared -- a copy
  shares its payload (`std::shared_ptr<const ...>`), a changed copy
  (`WithElement`, `WithMember`, `WithoutMember`) keeps the untouched
  rest shared. Numbers carry their literal: one read from the input
  keeps a canonical spelling (`CanonicalNumberLiteral`: leading zeros
  stripped, the exponent shifted in, plain while the exponent stays in
  range, `d[.ddd]E±n` otherwise), so the output
  echoes `1.0` as `1.0`; a computed number prints its shortest
  round-trip form instead. Objects keep insertion order (a repeated key
  its first place, the last value); `Compare` is jq's total order --
  null, false, NaN, numbers, strings (by UTF-8 bytes), arrays
  (lexicographic), objects (sorted key lists first, then the values in
  sorted key order) -- with NaN a number before every other, unequal to
  itself. The JSON handling is written from scratch rather than through
  the project's `nlohmann/json` dependency: jq's values carry a
  canonical literal and jq's total order, which that library's values do
  not model, and jq's numbers, error messages and positions are
  byte-specific in a way its parser does not expose.
- `JqJsonWriter.h/.cpp` - `WriteJson` with `WriteOptions` (indent 2,
  tab, sort keys, ASCII escapes, colour): jq's layouts byte for byte --
  pretty (empty containers inline, one per line), compact (`indent 0`),
  `--tab`'s tabs; `FormatNumber` (a literal kept, a computed number its
  shortest round-trip, plain while the exponent stays in range, the
  infinities clamped, NaN null); `QuoteJsonString` (jq's escapes, ASCII
  mode's `\uXXXX` and surrogate pairs); `DumpTruncated` (a value cut to
  a width, never inside a UTF-8 sequence); and jq's ANSI colours for
  `--color-output` (field names blue, strings green, null grey, the
  rest uncoloured, the punctuation bold white).
- `JqJsonReader.h/.cpp` - `JsonReader`, a push parser (`Feed` a piece,
  `Finish` the end) into a caller's `std::vector<Value>`: several values
  one after another, no recursion per input byte (nesting iterative,
  refused past 256 levels), jq's extensions (`nan`/`NaN`,
  `Infinity`, a leading `+`, `.5`), a repeated object key keeping its
  first place, its literal numbers kept, and ill-formed UTF-8 in strings
  repaired. Every failure is jq's message with its line and column --
  `"<message>[ at EOF] at line L, column C"` -- byte for byte, a
  container's strings and containers committed at their own closing byte
  and a top-level value only once its delimiter is seen (jq prints
  nothing for `1,`). `ParseSingleJson` wraps it for one value exactly.

## Documented differences from jq 1.7.1

- Only the first syntax error is reported (jq can find several), with
  jq's unterminated-if/try notes and its object-key note for a FORMAT
  key joined where jq adds them. Where jq continues past an escape error
  (`if . then "\q"` gives it three errors), parsing stops at the one.
- Modules are not implemented: `import`, `include` and `module` are
  syntax errors wherever they stand (jq's messages for them differ,
  since they are statements of its grammar).
- jq's semantic errors are not reproduced (`$x is not defined`,
  `a::b/1 is not defined`, `break requires a label to break to`,
  `Cannot use number (1) as object key`, the `Invalid path expression`
  family): a program jq rejects only semantically parses here.
- The `May need parentheses around object key expression` note is added
  only where the plan pinned it (a FORMAT key followed by `:`), not for
  jq's `{a: 1 b: 2}`-style cases.
- Nesting is refused past 256 levels (jq's own limit is higher); the
  token that goes too deep is reported as unexpected, jq's wording.
- A number literal with an exponent below -999999999 keeps its exact
  decimal (`1e-9999999999` reads back as `1E-9999999999`), where jq
  1.7.1 wraps the exponent into its 32-bit range and echoes the same
  input as `0E-1147483646`. An adjusted exponent above 999999999 keeps
  no literal, as jq's.