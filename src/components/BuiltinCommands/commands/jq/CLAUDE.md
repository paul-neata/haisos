# jq

`jq` here is the jq 1.7.1 language, so far its front half: a lexer, a parser
and the syntax tree they build, matching jq 1.7.1's compile errors byte for
byte. Nothing runs yet -- the evaluator (and with it the `jq` builtin
itself, its option table, `--help` and its tests in
`tests/unit/components/BuiltinCommands.unittests/`) is a later task of the
jq rock. The reference is the jq 1.7 manual and the observed behaviour of
the real `jq` (its error messages included, token names and all); no jq,
gojq or jaq source was read or copied -- everything here is written from
scratch, as everywhere in Haisos. All of it lives in namespace
`Haisos::Jq`, as plain portable C++17 (built for Linux, Windows/MSVC and
WASM: no POSIX headers, no `<regex>`, and nothing recursive per input
byte -- nesting is iterative, recursion per construct, refused past 256
levels).

The pipeline is lexer -> parser -> syntax tree (an evaluator joins later,
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