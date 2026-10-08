# Task tools--jq-parse: jq's lexer and parser, with jq's compile errors

- Rock: tools
- Depends on: none
- Size: ~950 changed lines in ~9 files
- Plan checked against: develop @ ccb9dbe
- PR title: jq: lexer, parser and syntax tree for the jq language subset

## Goal

The first of six jq tasks (`tools--jq-parse`, `tools--jq-json`,
`tools--jq-eval`, `tools--jq-command`, `tools--jq-builtins`,
`tools--jq-text`). A jq program text is turned into a syntax tree, or into
the compile errors jq 1.7.1 prints for it, byte for byte:

```
jq: error: syntax error, unexpected INVALID_CHARACTER, expecting end of file (Unix shell quoting issues?) at <top-level>, line 1:
.a.b)    
jq: 1 compile error
```

Nothing is runnable yet: this task is a library in `commands/jq/`
(namespace `Haisos::Jq`) with its own unit tests, as hsh's lexer and parser
were. The `jq` builtin itself is registered by `tools--jq-command`.

The reference everywhere is jq as the task container has it: Ubuntu 24.04's
package `jq 1.7.1-3ubuntu0.24.04.x` (its `jq --version` prints `jq-1.7`).
Every expected output in this plan was taken from it; for anything not
written here, run `jq -n '<program>'` in the container and copy what it
prints.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Automatic Development
Rules"), `src/components/BuiltinCommands/CLAUDE.md`,
`src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (the model for a
multi-file builtin with its own directory, namespace and CLAUDE.md), and
`commands/hsh/HshLexer.h`, `HshAst.h`, `HshParser.h` (the model for a lexer
with byte offsets, a tree with a one-line dump, a recursive-descent parser).

What exists: `BuiltinCommands` is a static library listing every source in
`src/components/BuiltinCommands/CMakeLists.txt`; `tests/unit/components/Hsh.unittests/`
shows a component test executable over it (added in `tests/unit/CMakeLists.txt`).
`scripts/test_linux.sh L U <filter>` runs every `*.unittests` whose file name
contains the filter (case-insensitively) with `--gtest_filter=*<filter>*`, so
the test suites here are named `Jq...Test` and the filter `Jq` selects them.

Rules that bite: portable C++17 (Linux, Windows/MSVC, WASM): no POSIX
headers, no `<regex>`, nothing recursive per input byte (the parser recurses
per nesting level only, and refuses nesting deeper than 256 levels -- see
below). Nothing here touches files or processes. No class here implements an
interface of `interfaces/`, so the `Create()` rule does not apply; the tree
is held by `std::unique_ptr` (it is owned by one program, never shared).

## Changes

All new files are in `src/components/BuiltinCommands/commands/jq/`,
namespace `Haisos::Jq`.

### `JqLexer.h` / `JqLexer.cpp`

```cpp
enum class TokenType {
    End,                 // end of input; bison's name "end of file"
    Invalid,             // INVALID_CHARACTER
    Ident,               // IDENT: foo, a::b
    Field,               // FIELD: .foo (text is "foo")
    Binding,             // BINDING: $foo (text is "foo")
    Loc,                 // $__loc__
    Literal,             // LITERAL: a number, text as written
    Format,              // FORMAT: @base64 (text is "base64")
    QQStringStart,       // "  (QQSTRING_START)
    QQStringText,        // text between quotes/interpolations, escapes already decoded
    QQStringInterpStart, // \(
    QQStringInterpEnd,   // the ) closing \(
    QQStringEnd,         // "  (QQSTRING_END)
    Keyword,             // as def if then elif else end and or reduce foreach try catch label import include module __loc__
    Operator,            // != == // //= |= += -= *= /= %= <= >= .. ?//
    Char,                // one of . ? = ; , : | + - * / % $ < > ( ) [ ] { }
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;   // see above; Keyword/Operator/Char: the spelling
    size_t begin = 0;   // byte offsets into the program text
    size_t end = 0;
};

// bison's name for a token in "unexpected X" / "expecting X":
// End "end of file"; Invalid "INVALID_CHARACTER"; Ident "IDENT"; Field "FIELD";
// Binding "BINDING"; Loc "$__loc__"; Literal "LITERAL"; Format "FORMAT";
// QQStringStart "QQSTRING_START"; QQStringText "QQSTRING_TEXT";
// QQStringInterpStart "QQSTRING_INTERP_START"; QQStringInterpEnd "QQSTRING_INTERP_END";
// QQStringEnd "QQSTRING_END"; Keyword and Operator: the spelling unquoted
// (if, then, ==, ..); Char: the character in single quotes ('}', '|').
std::string BisonTokenName(const Token& token);

class Lexer {
public:
    explicit Lexer(std::string_view program);
    // The next token. A lexical error (a bad escape in a string) is
    // returned as a token of type Invalid with |error| set (see below).
    Token Next();
    const std::string& Error() const;
};
```

Lexing, as jq 1.7.1's `lexer.l`:
- Whitespace is space, tab, `\r`, `\n`. A comment runs from `#` to the end
  of the line; jq 1.7.1 lets a comment continue onto the next line when the
  line ends with an odd number of backslashes (verify in the container:
  `jq -n '1 # c \` newline `+ 1'` prints `1`, i.e. the second line was
  part of the comment).
- Keywords: `as def if then elif else end and or reduce foreach try catch
  label import include module __loc__`. `$__loc__` is the single token
  `Loc`. Words not in that list are `Ident`:
  `([a-zA-Z_][a-zA-Z_0-9]*::)*[a-zA-Z_][a-zA-Z_0-9]*`. `not`, `empty`,
  `true`, `false`, `null` are plain identifiers (the parser turns the last
  three into literals).
- `Field`: `.` immediately followed by `[a-zA-Z_][a-zA-Z_0-9]*`. `Binding`:
  `$` followed by an identifier (with `::` parts). A `$` not followed by one
  is the Char `$`.
- `Literal`: `([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`, kept as
  written (`1.50`, `.5`, `1e2`): jq 1.7.1 prints a literal as written, which
  `tools--jq-json` handles. Checked before `Field`/`.` (so `.5` is a number).
- `Format`: `@[a-zA-Z0-9_]+`.
- Operators, longest first: `?//`, `//=`, `!=`, `==`, `//`, `|=`, `+=`,
  `-=`, `*=`, `/=`, `%=`, `<=`, `>=`, `..`; then single characters
  `. ? = ; , : | + - * / % $ < >`.
- Brackets, exactly as jq's lexer: a stack of open brackets. `(`, `[`,
  `{` push and are returned as Chars. A closer `)`, `]`, `}` that matches
  the top of the stack pops it and is returned as a Char; one that does not
  match (or with an empty stack) is `Invalid` -- that is why a stray `)` is
  reported as `INVALID_CHARACTER`. A `)` whose matching opener is a `\(`
  is `QQStringInterpEnd`, and lexing resumes inside the string.
- Strings: `"` gives `QQStringStart`, then alternately `QQStringText`
  (runs of text, escapes decoded: `\"` `\\` `\/` `\b` `\f` `\n` `\r` `\t`,
  `\uXXXX` with surrogate pairs combined into UTF-8; a lone surrogate
  becomes U+FFFD) and `QQStringInterpStart` (`\(`, pushed on the bracket
  stack), until `"` gives `QQStringEnd`. Any other byte after `\` is a lexical
  error: jq reports it as `Invalid escape at line 1, column 4 (while parsing
  '"\q"')` -- the message of jq's JSON parser run on the whole string
  literal (line/column within the literal, counting the closing quote; the
  literal quoted as written). Produce exactly that text in `Error()` and
  return an `Invalid` token at the string's start; the parser then reports
  that message (not a syntax error) at the literal's offset. An unterminated
  string ends with `End`.
- Any other byte is `Invalid`.

### `JqAst.h` / `JqAst.cpp`

```cpp
enum class NodeType {
    Identity, RecurseDefault, Index, Slice, Iterate, Try, Literal, String,
    Format, Array, Object, Negate, Pipe, Comma, Alternative, Binary, And, Or,
    Assign, If, Reduce, Foreach, Bind, Defs, Call, Variable, Loc, Label, Break,
};

enum class PatternType { Variable, Array, Object };
struct Node;
struct Pattern {
    PatternType type = PatternType::Variable;
    std::string name;                         // Variable: without '$'
    std::vector<Pattern> elements;            // Array
    struct Entry {
        std::string variable;                 // "$a" or "$a: P" form: "a"; else empty
        std::unique_ptr<Node> key;            // ident/keyword/string/(expr) key; null for "$a"
        std::unique_ptr<Pattern> value;       // null for a bare "$a"
    };
    std::vector<Entry> entries;               // Object
};

struct FunctionDefinition {
    std::string name;
    std::vector<std::string> params;          // "f" for a filter param, "$x" for a value param
    std::unique_ptr<Node> body;
    size_t begin = 0;                         // offset of "def"
};

struct Node {
    NodeType type;
    size_t begin = 0, end = 0;                // byte offsets of the construct in the program
    int line = 1;                             // 1-based line of |begin| (for $__loc__ and errors)
    std::string text;                         // Literal text; Binary/Assign operator; Call/Variable/
                                              // Label/Break/Format name (no '$'/'@'); String: format name or ""
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::string> stringParts;     // String: literal parts; children are the interpolations,
                                              // stringParts.size() == children.size() + 1
    std::vector<std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>>> entries; // Object: key, value
    std::vector<Pattern> patterns;            // Bind/Reduce/Foreach: alternatives joined by ?//
    std::vector<FunctionDefinition> definitions; // Defs
    bool hasCatch = false;                    // Try
    bool hasElse = false;                     // If
};
std::string DumpNode(const Node& node);
```

Children by type (nullptr where something is absent):
- `Index`: [target, key] -- `.a` is `Index(Identity, String "a")`, `.[e]`,
  `."s"`, `T.a`, `T[e]`, `T.[e]`.
- `Slice`: [target, from-or-null, to-or-null]. `Iterate`: [target].
- `Try`: [body, catch-or-absent] -- also the postfix `?`.
- `Literal`: text is the number as written, or `true`/`false`/`null`.
- `String`: interpolations, see `stringParts`; `text` the format (`@base64
  "..."`) or empty. `Format`: `@name` alone, applied to `.`.
- `Array`: [body] or none (`[]`). `Object`: `entries`, shorthands
  desugared by the parser: `{a}` is `{"a": .a}`, `{$x}` is `{"x": $x}`,
  `{"a b"}` / `{"\(e)"}` is `{key: .[key]}`, `{$__loc__}` is
  `{"__loc__": $__loc__}`, `{@base64 "x"}` likewise with the formatted
  string; a keyword or identifier key is a plain String.
- `Negate`: [operand]. `Pipe`, `Comma`, `Alternative`, `And`, `Or`:
  [left, right]. `Binary`: text `+ - * / % == != < <= > >=`, [left, right].
  `Assign`: text `= |= += -= *= /= %= //=`, [left, right].
- `If`: [cond1, then1, cond2, then2, ..., else-if-hasElse].
- `Reduce`: [source, init, update] + patterns. `Foreach`: [source, init,
  update, extract-if-given] + patterns. `Bind`: [source, body] + patterns.
- `Defs`: `definitions` then [scope] -- `def f: 1; def g: 2; f` is one
  Defs node with two definitions.
- `Call`: text the name, children the arguments. `Variable`: text the name.
  `Loc`: `line` is used. `Label`: text the name, [body]. `Break`: text.

**Dump format** (`DumpNode`, one line, what the tests compare):
`.` `..` `(index T K)` `(slice T F E)` with `_` for an absent bound,
`(iterate T)`, `(try B)` `(try B C)`, a Literal's text, a String with no
interpolation and no format as a JSON string (`"a\n"`), otherwise
`(string FMT P0 E1 P1 ...)` with FMT `@name` or `_` and the parts as JSON
strings, `(format @base64)`, `(array)` / `(array B)`, `(object (K V) ...)`,
`(neg E)`, `(| A B)` `(, A B)` `(// A B)` `(and A B)` `(or A B)`,
`(+ A B)` (any Binary or Assign: its operator), `(if C T C T ... E)` with
`_` for no else, `(reduce S PAT I U)`, `(foreach S PAT I U [X])`,
`(as S PAT B)`, `(defs (def NAME (PARAMS) BODY) ... SCOPE)` with PARAMS
space-separated (`(def f (g $x) BODY)`, `(def f () 1)`), `(call NAME
ARG...)`, `$name`, `(loc LINE)`, `(label $name B)`, `(break $name)`.
Patterns: `$x`, `(arr P ...)`, `(obj ENTRY ...)` with an entry `($a)`,
`($a P)` or `(K P)`; alternatives `(?// P1 P2 ...)`.

### `JqParser.h` / `JqParser.cpp`

```cpp
struct CompileError {
    std::string message;   // "syntax error, unexpected '}' (Unix shell quoting issues?)"
    size_t offset = 0;     // where jq places it (see below)
};
struct ParseResult {
    std::unique_ptr<Node> root;          // null when errors is not empty
    std::vector<CompileError> errors;
};
ParseResult ParseProgram(std::string_view program);

// One error as jq prints it:
//   "jq: error: <message> at <top-level>, line <L>:\n<that line><K spaces>\n"
// where L is the 1-based line holding |offset|, the line is printed without
// its '\n', and K = offset - (offset of that line's first byte) -- jq meant
// to print a caret there and prints only the padding.
std::string FormatCompileError(std::string_view program, const CompileError& error);
// "jq: 1 compile error\n" or "jq: <n> compile errors\n".
std::string FormatCompileErrorCount(size_t count);
```

A recursive-descent parser with one token of lookahead, reproducing jq
1.7.1's bison grammar (`parser.y`):

```
Pipe     := 'def' FuncDef Pipe                  (scope: the whole rest)
          | Term 'as' Patterns '|' Pipe         (body: the whole rest)
          | 'label' BINDING '|' Pipe
          | Comma ('|' Pipe)?                   ('|' right-associative, lowest)
Comma    := Alt (',' Alt)*                      (left)
Alt      := Assign ('//' Alt)?                  (right)
Assign   := Or (op Or)?     op: = |= += -= *= /= %= //=   (non-associative)
Or       := And ('or' And)* ; And := Compare ('and' Compare)*
Compare  := Add (op Add)?   op: == != < <= > >=           (non-associative)
Add      := Mul (('+'|'-') Mul)* ; Mul := Unary (('*'|'/'|'%') Unary)*
Unary    := '-' Unary-at-Mul-level                (so -1 + 2 is (-1)+2, - 1 * 3 is -(1*3))
          | 'reduce' Postfix 'as' Patterns '(' Pipe ';' Pipe ')'
          | 'foreach' Postfix 'as' Patterns '(' Pipe ';' Pipe (';' Pipe)? ')'
          | 'if' Pipe 'then' Pipe ('elif' Pipe 'then' Pipe)* ('else' Pipe)? 'end'
          | 'try' Postfix ('catch' Postfix)?
          | Postfix ('as' ... as above, when 'as' follows)
          each optionally followed by '?' (Exp '?'), but no index chain:
          'if ... end.a' and 'reduce ... (...) .a' are syntax errors.
Postfix  := Primary ( FIELD | '.' String | '[' ... ']' | '.' '[' ... ']' | '?' )*
          '[' ']' iterate, '[' Pipe ']' index, '[' Pipe ':' ']', '[' ':' Pipe ']',
          '[' Pipe ':' Pipe ']' slices.
Primary  := '.' | '..' | FIELD | '.' String | '.' '[' ... | LITERAL | String
          | FORMAT String? | '(' Pipe ')' | '[' Pipe? ']' | '{' ObjEntries? '}'
          | BINDING | '$__loc__' | 'break' BINDING | IDENT ('(' Pipe (';' Pipe)* ')')?
String   := QQSTRING_START (QQSTRING_TEXT | QQSTRING_INTERP_START Pipe QQSTRING_INTERP_END)* QQSTRING_END
FuncDef  := IDENT (':' | '(' Param (';' Param)* ')' ':') Pipe ';'     Param := IDENT | BINDING
Patterns := Pattern ('?//' Pattern)*
Pattern  := BINDING | '[' Pattern (',' Pattern)* ']' | '{' ObjPat (',' ObjPat)* '}'
ObjPat   := BINDING (':' Pattern)? | (IDENT|Keyword) ':' Pattern | String ':' Pattern | '(' Pipe ')' ':' Pattern
ObjEntries := Entry (',' Entry)*       (a trailing ',' is a syntax error)
Entry    := (IDENT|Keyword|BINDING|'$__loc__'|String|FORMAT String) (':' ObjVal)?
          | '(' Pipe ')' ':' ObjVal
ObjVal   := ObjValTerm ('|' ObjVal)?   ObjValTerm := '-' ObjValTerm | Postfix
```

Notes on that grammar (each one is a jq behaviour the tests pin):
- An object value is jq's `ExpD`: pipes of postfix terms only, so
  `{a: 1 + 1}` is an error (`unexpected '+', expecting '}'`) and
  `{a: .b | length}` is fine.
- `1, 2 as $x | $x` is `1, (2 as $x | $x)`; `1 as $x | 2, 3` binds over `2, 3`.
- `try` binds tightly: `try error("x") catch . | length` is
  `(try ... catch .) | length`; `try 1 + 2` is `(try 1) + 2`.
- `true`, `false`, `null` with no arguments are Literals; any other
  identifier is a Call. Keywords are allowed as object keys (`{if: 1}`) and
  after `.` only through FIELD (`.if` lexes as FIELD).
- `$__loc__` records its token's line.
- Nesting deeper than 256 open brackets/constructs is a syntax error at the
  token that goes too deep (jq has no such limit; a documented exception
  that keeps the recursion bounded).
- `import`, `include`, `module` are keywords that start nothing: a program
  using them gets a syntax error (`unexpected import`); modules are out of
  scope (documented in `commands/jq/CLAUDE.md`).
- A program with no top-level expression (empty, only comments, or only
  definitions: `def f: 1;`) is the error `Top-level program not given (try
  ".")`, printed by `tools--jq-command` *without* location -- give it
  `offset` `SIZE_MAX` and have `FormatCompileError` print just
  `jq: error: Top-level program not given (try ".")\n` for that offset.

**Syntax error messages.** The first syntax error stops parsing (jq's bison
may recover inside brackets and report more; reporting only the first is a
documented exception). The message is
`syntax error, unexpected <X>[, expecting <list>] (Unix shell quoting issues?)`
with `<X>` = `BisonTokenName` of the offending token. Its offset is the
offending token's `begin`; for `End` it is the `begin` of the last token
before it (0 if none) -- bison keeps the previous location at end of input.
The `expecting` part is bison's list; reproduce it in exactly these
situations, and print none otherwise:

| Situation | expecting |
|---|---|
| the whole program parsed as an expression and the next token is not End (`.a.b)`, `1 2`, `1 if`, `\|`, `end`, `.. ..`) | `end of file` |
| a nonassociative operator directly after one of the same group (`1 == 1 == 1`, `.a = 1 = 2`) | none: `unexpected ==` |
| a `.` after a postfix term not followed by a string, FORMAT or `[` (`1 . 2`, `.a.` then End) | `FORMAT or QQSTRING_START or '['` |
| after `as`, no pattern | `BINDING or '[' or '{'` |
| `Term as Patterns` not followed by `\|` or `?//` (outside reduce/foreach) | `'\|'` |
| `reduce`/`foreach ... as Patterns` not followed by `(` | `'('` |
| `def NAME` not followed by `:` or `(` | `'(' or ':'` |
| inside `{...}` after a complete entry (or key), a token other than `,` or `}` (`{a`, `{a:1`, `{a: 1 \| . + 1}`) | `'}'` |
| a FORMAT used as an object key not followed by a string (`{@base64: 1}`) | `QQSTRING_START`, and a second error `May need parentheses around object key expression` at the FORMAT's offset |
| a string not terminated before End | `QQSTRING_TEXT or QQSTRING_INTERP_START or QQSTRING_END` |

When the error happens after `if ... then` and before that `if`'s `end`, a
second error follows: `Possibly unterminated 'if' statement` at the `if`'s
offset; after `try ... catch` (in the catch body), `Possibly unterminated
'try' statement` at the `try`'s offset (verify each in the container).

### Build

- `src/components/BuiltinCommands/CMakeLists.txt`: add
  `commands/jq/JqLexer.cpp`, `commands/jq/JqAst.cpp`, `commands/jq/JqParser.cpp`.
- New `tests/unit/components/Jq.unittests/CMakeLists.txt`, modelled on
  `Hsh.unittests`' (`add_executable(Jq.unittests JqLexerTest.cpp JqParserTest.cpp)`,
  includes `${CMAKE_SOURCE_DIR}/src/components/BuiltinCommands/commands/jq`,
  links `gtest_main BuiltinCommands`); `add_subdirectory(components/Jq.unittests)`
  in `tests/unit/CMakeLists.txt`.

### `src/components/BuiltinCommands/commands/jq/CLAUDE.md` (new)

As hsh's: what jq is here (a subset of jq 1.7.1's language over its own JSON
values, the reference being the container's jq), the pipeline (lexer ->
parser -> tree -> evaluator, the last three files arriving with later
tasks), one bullet per file of this task saying what it does, the dump
format, and the documented differences: only the first syntax error is
reported; nesting is limited to 256; no modules (`import`, `include`,
`module`). Later jq tasks add their files to it.

## Tests

`tests/unit/components/Jq.unittests/JqLexerTest.cpp`:
- `JqLexerTest.TokensOfAProgram`: `.a | .["b"][] as $x | @base64 "v\($x)"`
  -> the expected type/text sequence (Field `a`, Char `|`, Char `.`, Char `[`,
  QQStringStart, QQStringText `b`, QQStringEnd, Char `]`, Char `[`, Char `]`,
  Keyword `as`, Binding `x`, Char `|`, Format `base64`, QQStringStart,
  QQStringText `v`, QQStringInterpStart, Binding `x`, QQStringInterpEnd,
  QQStringEnd, End) with correct begin offsets for a few.
- `JqLexerTest.NumbersKeepTheirText`: `1.50`, `.5`, `1e2`, `1E-7` are
  Literals with that text; `.a5` is a Field.
- `JqLexerTest.OperatorsLongestFirst`: `?// //= // |= != == <= >= ..`.
- `JqLexerTest.UnmatchedCloserIsInvalid`: `)` alone, `[}` (the `}`).
- `JqLexerTest.StringEscapes`: `"\té😀\\"` decodes to tab,
  `é`, the emoji, backslash; `"\q"` gives the error text
  `Invalid escape at line 1, column 4 (while parsing '"\q"')`.
- `JqLexerTest.Comments`: `1 # x` then `+ 1` on the next line lexes `1 + 1`.

`tests/unit/components/Jq.unittests/JqParserTest.cpp` -- `DumpNode` of the
root for each program (expected dumps written by hand from the format above):
- `JqParserTest.PathsAndPostfix`: `.`, `..`, `.a.b`, `.a?.b`, `."a"[0]`,
  `.a.[0]`, `.[]?`, `.[1:]`, `.[:2]`, `.[1:2]`, `$x.a`, `1 .a`.
- `JqParserTest.Precedence`: `1 + 2 * 3`, `-1 + 2` -> `(+ (neg 1) 2)`,
  `- 1 * 3` -> `(neg (* 1 3))`, `.a // .b // .c` (right), `1, 2 | 3`,
  `.a = 1 | .b`, `1 < 2 and 2 < 3 or false`, `try 1 + 2` -> `(+ (try 1) 2)`,
  `try error("x") catch . | length`.
- `JqParserTest.Bindings`: `1, 2 as $x | $x`, `1 as $x | 2, 3`,
  `. as [$a, {b: $c, $d}] | $a`, `.[] as [$a] ?// $a | $a`,
  `reduce .[] as $x (0; . + $x)`, `foreach .[] as $x (0; . + $x; [$x, .])`,
  `label $f | 1, break $f`.
- `JqParserTest.Definitions`: `def f: 1; def g(a; $b): a + $b; f | g(1; 2)`
  -> `(defs (def f () 1) (def g (a $b) (+ (call a) $b)) (| (call f) (call g 1 2)))`;
  `1 + def f: 2; f`.
- `JqParserTest.ConditionalsAndStrings`: `if . then 1 elif .a then 2 end`
  -> `(if . 1 (index . "a") 2 _)`; `"a\(1 + 2)b"`; `@base64 "x\(.)"`;
  `@csv`; `"\(1,2)"`.
- `JqParserTest.ObjectsDesugared`: `{a, $x, "b c", (.k): 1, if: 2, $__loc__}`
  -> each entry with its desugared value; `{a: .b | length}`.
- `JqParserTest.SyntaxErrors` -- `ParseProgram` then `FormatCompileError`
  of each error, then `FormatCompileErrorCount`; the whole text compared.
  Every expected text below is jq's (`·` stands for a space here only; write
  real spaces):
  - `.a |` -> `jq: error: syntax error, unexpected end of file (Unix shell quoting issues?) at <top-level>, line 1:\n.a |···\njq: 1 compile error\n`
  - `{a:}` -> `... unexpected '}' (Unix shell ...) at <top-level>, line 1:\n{a:}···\n`
  - `.a.b)` -> `... unexpected INVALID_CHARACTER, expecting end of file (...) ...:\n.a.b)····\n`
  - `if . then 1` -> the end-of-file error with 10 spaces, then
    `jq: error: Possibly unterminated 'if' statement at <top-level>, line 1:\nif . then 1\n`, then `jq: 2 compile errors\n`
  - `try 1 catch` -> the end-of-file error (6 spaces), the `'try'` note, `2 compile errors`
  - `1 . 2` -> `unexpected LITERAL, expecting FORMAT or QQSTRING_START or '['`
  - `1 as` -> `unexpected end of file, expecting BINDING or '[' or '{'`; `1 as $x` -> `expecting '|'`
  - `reduce . as $x` -> `unexpected end of file, expecting '('` (12 spaces)
  - `def f` -> `expecting '(' or ':'`
  - `{a:1` -> `expecting '}'`; `{a: 1 | . + 1}` -> `unexpected '+', expecting '}'` (11 spaces)
  - `"abc` -> `unexpected end of file, expecting QQSTRING_TEXT or QQSTRING_INTERP_START or QQSTRING_END` (1 space)
  - `1 == 1 == 1` -> `unexpected ==` (no expecting)
  - `.. ..` -> `unexpected .., expecting end of file`; `1 $x` -> `unexpected BINDING, expecting end of file`
  - `"\(1 2)"` -> `unexpected LITERAL` (no expecting)
  - `.a\n| .b )` -> `line 2:\n| .b )·····\n` (the error's own line)
  - `{@base64: 2}` -> the `expecting QQSTRING_START` error and `May need parentheses around object key expression`, `2 compile errors`
  - `"\q"` -> `jq: error: Invalid escape at line 1, column 4 (while parsing '"\q"') at <top-level>, line 1:\n"\q"\n`
  - `def f: 1;` and `` (empty) and ` # c` -> `jq: error: Top-level program not given (try ".")\n` and `jq: 1 compile error\n`
- `JqParserTest.DeepNestingIsRefused`: 300 nested `[` gives a syntax
  error, no crash; 200 nested parentheses parse.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U
```

## Docs

- New `src/components/BuiltinCommands/commands/jq/CLAUDE.md` (above).
- `src/components/BuiltinCommands/CLAUDE.md`: in "Key Classes", a bullet
  `commands/jq/` -- jq, its own CLAUDE.md (as for `commands/hsh/`).
- No root `CLAUDE.md` change yet (the builtin arrives with `tools--jq-command`).

## Acceptance

- [ ] `ParseProgram` builds the tree for every construct of the grammar
      above; `DumpNode` gives the documented format.
- [ ] Every syntax error in the tests is byte-exact, offsets (trailing
      spaces) included; the expecting lists appear exactly in the listed
      situations.
- [ ] Lexer brackets behave as jq's (stray closers are INVALID_CHARACTER;
      `\(...)` nests).
- [ ] No recursion per input byte; nesting limit enforced; no POSIX
      headers, no `<regex>`.
- [ ] `Jq.unittests` exists, is built, and passes; the whole unit suite passes.
- [ ] `commands/jq/CLAUDE.md` written; BuiltinCommands CLAUDE.md points to it.

## Out of scope

- Values, JSON reading and printing (`tools--jq-json`); evaluation and the
  "is not defined" compile errors (`tools--jq-eval`); the `jq` command
  (`tools--jq-command`); builtins (`tools--jq-builtins`, `tools--jq-text`).
- Modules (`import`/`include`/`module`), `?//` error-suppression semantics
  beyond parsing, bison's error recovery (second and later syntax errors).
