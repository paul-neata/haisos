# Task tools--jq-parse: jq's lexer and parser, with jq's compile errors

- Rock: tools
- Depends on: none
- Size: ~950 changed lines in ~9 files
- Plan checked against: develop @ 579a736
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
(namespace `Haisos::Jq`) with its own unit tests, as awk's lexer and parser
were (`awk--lexer`, `awk--parser`). The `jq` builtin itself is registered
by `tools--jq-command`.

The reference everywhere is jq 1.7.1: Ubuntu 24.04's package
`jq 1.7.1-3ubuntu0.24.04.2` (its `jq --version` prints `jq-1.7`), the one
on the planning host and in the task container, and the jq 1.7 manual
(https://jqlang.github.io/jq/manual/v1.7/). Every expected output in this
plan was checked with `jq -n '<program>'` on the planning host; for anything
not written here, run the same in the container and copy what it prints.

## Context

**Clean-room rule (the user's, above every other rule; root `CLAUDE.md`
"Clean-room rule"):** no code is copied from any other program, whatever its
licence -- jq is MIT, and the rule applies all the same. Never read, copy,
port, translate or paraphrase another program's source -- jq's, gojq's,
jaq's, and code recalled from memory -- and never name another program's
internal functions, variables, types, token or grammar-rule names, in code,
comments, tests or commit messages. Everything below is *behaviour*: the jq
1.7 manual and what jq 1.7.1 prints. The token names that appear *in jq's
error messages* (`INVALID_CHARACTER`, `IDENT`, `FIELD`, `BINDING`,
`LITERAL`, `FORMAT`, `QQSTRING_START`, ...) are observed output and are
reproduced as strings only; the code's own names (`TokenType` and its
members, `Lexer`, `ParseProgram`, the grammar's levels below) are Haisos's
own. Write the code from scratch, shaped by Haisos's own structure (awk's
and hsh's lexers and parsers are the models).

**Write in pieces:** never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step (earlier
runs died on "response exceeded the 32000 output token maximum").

Read first: the root `CLAUDE.md` ("Clean-room rule", "Builtin Commands",
"Automatic Development Rules"), `src/components/BuiltinCommands/CLAUDE.md`,
`src/components/BuiltinCommands/commands/awk/CLAUDE.md` (the most recent
model for a large multi-file builtin with its own directory, namespace and
CLAUDE.md), and `commands/awk/AwkLexer.h`, `AwkAst.h`, `AwkParser.h`,
`AwkError.h` (a lexer with positions, a tree, a recursive-descent parser,
syntax errors as values); `commands/hsh/HshAst.h` shows a tree with a
one-line dump for tests. `tests/unit/components/Awk.unittests/AwkLexerTest.cpp`
and `AwkParserTest.cpp` show the test style (include as
`"commands/awk/AwkLexer.h"`, `using Haisos::Awk::...`).

What exists on develop: `BuiltinCommands` is a static library listing every
source in `src/components/BuiltinCommands/CMakeLists.txt` (the general
`Builtin*.cpp` helpers first, then `commands/<name>/...` alphabetically,
hsh's files last). `tests/unit/components/Awk.unittests/` is a component
test executable over it (`add_executable(Awk.unittests ...)`, linking
`gtest_main BuiltinCommands Environment Factory`), added in
`tests/unit/CMakeLists.txt` by `add_subdirectory(components/Awk.unittests)`.
`scripts/test_linux.sh L U <filter>` runs every `*.unittests` whose file name
contains the filter (case-insensitively) with `--gtest_filter=*<filter>*`, so
the test suites here are named `Jq...Test` and the filter `Jq` selects them.
The shared text helpers (`BuiltinText.h`: `BuiltinLineReader`,
`ReadWholeInput`, `OpenInputOperand`), the `Regex` component,
`BuiltinCommandList.h` (`CreateStandardBuiltinCommands()`) and the option
parsing in `BuiltinCommand.h` are used by later jq tasks, not by this one.
No exported code-point-to-UTF-8 helper exists (`BuiltinPrintf.cpp`'s
`AppendUtf8` is file-local; `src/components/Unicode/` only decodes): write
a small one in `JqLexer.cpp` (`tools--jq-json` may move it to a shared jq
header later).

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
    End,                // end of input
    Invalid,            // a byte that starts no token, an unmatched closer, a bad escape
    Identifier,         // foo, a::b
    Field,              // .foo (text is "foo")
    Variable,           // $foo (text is "foo")
    LocVariable,        // $__loc__
    Number,             // a number, text as written
    Format,             // @base64 (text is "base64")
    StringStart,        // the opening "
    StringText,         // text between quotes/interpolations, escapes already decoded
    InterpolationStart, // \(
    InterpolationEnd,   // the ) closing \(
    StringEnd,          // the closing "
    Keyword,            // as def if then elif else end and or reduce foreach try catch label import include module __loc__
    Operator,           // != == // //= |= += -= *= /= %= <= >= .. ?//
    Char,               // one of . ? = ; , : | + - * / % $ < > ( ) [ ] { }
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;   // see above; Keyword/Operator/Char: the spelling
    size_t begin = 0;   // byte offsets into the program text
    size_t end = 0;
};

// How jq's messages name a token after "unexpected " / "expecting "
// (observed output): End "end of file"; Invalid "INVALID_CHARACTER";
// Identifier "IDENT"; Field "FIELD"; Variable "BINDING"; LocVariable
// "$__loc__"; Number "LITERAL"; Format "FORMAT"; StringStart "QQSTRING_START";
// StringText "QQSTRING_TEXT"; InterpolationStart "QQSTRING_INTERP_START";
// InterpolationEnd "QQSTRING_INTERP_END"; StringEnd "QQSTRING_END";
// Keyword and Operator: the spelling unquoted (if, then, ==, ..);
// Char: the character in single quotes ('}', '|').
std::string TokenNameInMessage(const Token& token);

class Lexer {
public:
    explicit Lexer(std::string_view program);
    // The next token. A bad escape in a string is returned as a token of
    // type Invalid with Error() set (see below).
    Token Next();
    const std::string& Error() const;
};
```

What the lexer recognises (the manual's language, checked against jq 1.7.1):
- Whitespace is space, tab, `\r`, `\n`. A comment runs from `#` to the end
  of the line, and no further: `jq -n $'1 # c \\\n+ 1'` prints `2` (a
  backslash at the end of a comment line does not continue it in 1.7.1).
- Keywords: `as def if then elif else end and or reduce foreach try catch
  label import include module __loc__`. `$__loc__` is the single token
  `LocVariable`. Other words are `Identifier`:
  `([a-zA-Z_][a-zA-Z_0-9]*::)*[a-zA-Z_][a-zA-Z_0-9]*`. `not`, `empty`,
  `true`, `false`, `null` are plain identifiers (the parser turns the last
  three into literals).
- `Field`: `.` immediately followed by `[a-zA-Z_][a-zA-Z_0-9]*` -- keywords
  included: `.if`, `.and` are fields (`{if: 1} | .if` prints `1`).
  `Variable`: `$` followed by an identifier (with `::` parts). A `$` not
  followed by one is the Char `$`.
- `Number`: `([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`, kept as
  written (`1.50`, `.5`, `1e2`); how it prints is `tools--jq-json`'s concern
  (jq 1.7.1 prints `1.50` as `1.50` but `1e2` as `1E+2`). Tried before
  `Field`/`.`, so `.5` is a number (`jq -n '.5'` prints `0.5`).
- `Format`: `@[a-zA-Z0-9_]+`.
- Operators, longest first: `?//`, `//=`, `!=`, `==`, `//`, `|=`, `+=`,
  `-=`, `*=`, `/=`, `%=`, `<=`, `>=`, `..`; then single characters
  `. ? = ; , : | + - * / % $ < >`.
- Brackets. Observed: a closer that does not close the innermost open
  bracket is `INVALID_CHARACTER` (`)` alone: `unexpected INVALID_CHARACTER,
  expecting end of file`; `[}`: `unexpected INVALID_CHARACTER` at the `}`).
  So the lexer keeps a stack of the open brackets: `(`, `[`, `{` push and
  are Chars; a matching `)`, `]`, `}` pops and is a Char; any other closer
  is `Invalid`. A `)` whose opener is a `\(` is `InterpolationEnd`, and
  lexing resumes inside the string.
- Strings: `"` gives `StringStart`, then alternately `StringText` (runs of
  text, escapes decoded: `\"` `\\` `\/` `\b` `\f` `\n` `\r` `\t`, and
  `\uXXXX`, a high surrogate followed by a low one combined into one
  code point, written as UTF-8) and `InterpolationStart` (`\(`, pushed on
  the bracket stack), until `"` gives `StringEnd`. A low surrogate on its
  own becomes U+FFFD (`"\ude00"` prints the replacement character, U+FFFD). An unterminated
  string ends with `End`.
- Bad escapes are errors, worded as jq words them. The *escape run* is the
  stretch of consecutive escapes holding the bad one: from a `\` through
  every escape that directly follows it (`\u` takes up to four following
  bytes, stopping early at a `\` or `"`). The error is located at the run's
  first `\`, and the message is
  `<kind> at line 1, column <N> (while parsing '"<run>"')` with N = the
  run's length + 2. Kinds, all checked on the host:

  | Program | Message |
  |---|---|
  | `"\q"` | `Invalid escape at line 1, column 4 (while parsing '"\q"')` |
  | `"\ud83d\ude00\q"` | `Invalid escape at line 1, column 16 (while parsing '"\ud83d\ude00\q"')` |
  | `"\u12"` | `Invalid \uXXXX escape at line 1, column 6 (while parsing '"\u12"')` |
  | `"\uzz"` | `Invalid \uXXXX escape at line 1, column 6 (while parsing '"\uzz"')` |
  | `"\u12\(1)"` | `Invalid \uXXXX escape at line 1, column 6 (while parsing '"\u12"')` |
  | `"\u12zz"` | `Invalid characters in \uXXXX escape at line 1, column 8 (while parsing '"\u12zz"')` |
  | `"\ud800x"` | `Invalid \uXXXX\uXXXX surrogate pair escape at line 1, column 8 (while parsing '"\ud800"')` |
  | `"\ud83d\u0041"` | `Invalid \uXXXX\uXXXX surrogate pair escape at line 1, column 14 (while parsing '"\ud83d\u0041"')` |

  (`\q` stands for any byte after `\` that starts no escape; a high
  surrogate not directly followed by a low one in the run is the surrogate
  pair error.) Put that text in `Error()` and return an `Invalid` token
  whose `begin` is the run's `\`; the parser reports that message (not a
  syntax error) at that offset: `"ab\q"` is reported with 3 spaces of
  padding, `. | "\q"` with 5.
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
  string (`{@base64 "x"}` prints `{"x":null}`); a keyword or identifier key
  is a plain String.
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
// its '\n', and K = offset - (offset of that line's first byte): jq prints
// that much padding after the line (no caret).
std::string FormatCompileError(std::string_view program, const CompileError& error);
// "jq: 1 compile error\n" or "jq: <n> compile errors\n".
std::string FormatCompileErrorCount(size_t count);
```

A recursive-descent parser with one token of lookahead. The grammar below
is Haisos's own, written from the manual's description of the language and
pinned by what jq 1.7.1 accepts, rejects and computes (each note under it
gives the observation); the level names are ours:

```
Pipe     := 'def' Definition Pipe                (scope: the whole rest)
          | Postfix 'as' Destructuring '|' Pipe  (body: the whole rest)
          | 'label' VARIABLE '|' Pipe
          | Comma ('|' Pipe)?                    ('|' right-associative, lowest)
Comma    := Alt (',' Alt)*                       (left)
Alt      := Assign ('//' Alt)?                   (right)
Assign   := Or (op Or)?     op: = |= += -= *= /= %= //=   (non-associative)
Or       := And ('or' And)* ; And := Compare ('and' Compare)*
Compare  := Add (op Add)?   op: == != < <= > >=           (non-associative)
Add      := Mul (('+'|'-') Mul)* ; Mul := Unary (('*'|'/'|'%') Unary)*
Unary    := '-' Unary-at-Mul-level                (so -1 + 2 is (-1)+2, - 1 * 3 is -(1*3))
          | 'reduce' Postfix 'as' Destructuring '(' Pipe ';' Pipe ')'
          | 'foreach' Postfix 'as' Destructuring '(' Pipe ';' Pipe (';' Pipe)? ')'
          | 'if' Pipe 'then' Pipe ('elif' Pipe 'then' Pipe)* ('else' Pipe)? 'end'
          | 'try' Postfix ('catch' Postfix)?
          | 'def' Definition Pipe                  (as an operand: 1 + def f: 2; f)
          | Postfix ('as' Destructuring '|' Pipe, when 'as' follows)
          each of reduce/foreach/if/try optionally followed by '?' (any
          number), but by no index: 'if ... end.a' and 'reduce ... (...) .a'
          are syntax errors.
Postfix  := Primary ( FIELD | '.' StringLiteral | '[' ... ']' | '.' '[' ... ']' | '?' )*
          '[' ']' iterate, '[' Pipe ']' index, '[' Pipe ':' ']', '[' ':' Pipe ']',
          '[' Pipe ':' Pipe ']' slices.
Primary  := '.' | '..' | FIELD | '.' StringLiteral | '.' '[' ... | NUMBER | StringLiteral
          | FORMAT StringLiteral? | '(' Pipe ')' | '[' Pipe? ']' | '{' ObjectEntries? '}'
          | VARIABLE | '$__loc__' | 'break' VARIABLE | IDENTIFIER ('(' Pipe (';' Pipe)* ')')?
StringLiteral := '"' (TEXT | '\(' Pipe ')')* '"'
Definition    := IDENTIFIER (':' | '(' Parameter (';' Parameter)* ')' ':') Pipe ';'
Parameter     := IDENTIFIER | VARIABLE
Destructuring := Destructure ('?//' Destructure)*
Destructure   := VARIABLE | '[' Destructure (',' Destructure)* ']'
               | '{' ObjectPatternEntry (',' ObjectPatternEntry)* '}'
ObjectPatternEntry := VARIABLE (':' Destructure)? | (IDENTIFIER|Keyword) ':' Destructure
               | StringLiteral ':' Destructure | '(' Pipe ')' ':' Destructure
ObjectEntries := Entry (',' Entry)* ','?        (one trailing ',' is allowed)
Entry    := (IDENTIFIER|Keyword|VARIABLE|'$__loc__'|StringLiteral|FORMAT StringLiteral) (':' ObjectValue)?
          | '(' Pipe ')' ':' ObjectValue
ObjectValue := ObjectValueTerm ('|' ObjectValue)?
ObjectValueTerm := '-' ObjectValueTerm | Postfix-without-bare-'?'
```

Notes on that grammar (each one is a jq 1.7.1 behaviour the tests pin):
- An object value is a pipe of (negated) postfix terms only: `{a: 1 + 1}`
  is `unexpected '+', expecting '}'`; `{a: if . then 1 else 2 end}` is
  `unexpected if` (likewise `try`, `reduce`); `{a: 1 as $x | $x}` is
  `unexpected as, expecting '}'`; `{a: .b | length}`, `{a: -1}`,
  `{a: - - 1}`, `{a: $__loc__}` are fine. Inside an object value a `?` is
  accepted only directly after an index, iterate or slice suffix, once:
  `{a: .b?}`, `{a: .[]?}`, `{a: .b?[0]?}`, `{a: .[1:]?}` are fine;
  `{a: 1?}`, `{a: .?}`, `{a: (1)?}`, `{a: .b??}` are
  `unexpected '?', expecting '}'`. Outside objects `1?`, `1??`, `..?` are
  fine.
- Objects take one trailing comma: `{a,}` prints `{"a":null}`, `{a:1,}`
  prints `{"a":1}`; `{,}` and `{a,,}` are `unexpected ','`. Arrays take
  none: `[1,]` is `unexpected ']'`. Array patterns take none either:
  `. as [$a, $b,] | 1` is `unexpected ']', expecting BINDING or '[' or '{'`;
  `. as {} | 1` is `unexpected '}'`; `. as {$__loc__} | 1` is
  `unexpected $__loc__`.
- A binding swallows the rest even as an operand: `1, 2 as $x | $x` is
  `1, (2 as $x | $x)`; `1 as $x | 2, 3` binds over `2, 3`;
  `1 + 2 as $x | 10, 20` prints `11` and `21`; `-1 as $x | 2` prints `-2`.
- `try` binds tightly: `try error("x") catch . | length` is
  `(try ... catch .) | length` (prints `1`); `try 1 + 2` is `(try 1) + 2`.
  Negation: `- "a" * 0` fails negating the product, so it is `-("a" * 0)`.
- `true`, `false`, `null` with no arguments are Literals; any other
  identifier is a Call. Keywords are allowed as object keys (`{if: 1}`), as
  object-pattern keys (`{if: [$a]}`) and after `.` through FIELD.
- `..` takes postfix suffixes (`..?`, `.. .a`, `..[0]` are fine) but not an
  identifier: `..a` is `unexpected IDENT, expecting end of file`.
- `$__loc__` records its token's line.
- Nesting deeper than 256 open brackets/constructs is a syntax error at the
  token that goes too deep, worded as any other (`syntax error, unexpected
  <X> (Unix shell quoting issues?)`). jq has no such limit (300 nested `[`
  parse there); a documented exception that keeps the recursion bounded.
- `import`, `include`, `module` are keywords that start nothing here: a
  program using them gets `syntax error, unexpected import` (resp.
  `include`, `module`) at the keyword. jq 1.7.1 says the same where the
  keyword is not at the program's start (`1 | import "a" as a; .`); at the
  start it reads the directive and looks the module up instead
  (`jq: error: module not found: a`, an empty line, `jq: 1 compile error`).
  Modules are out of scope; documented in `commands/jq/CLAUDE.md`.
- A program with no top-level expression (empty, only comments, or only
  definitions: `def f: 1;`) is the error `Top-level program not given (try
  ".")`, printed *without* location -- give it `offset` `SIZE_MAX` and have
  `FormatCompileError` print just
  `jq: error: Top-level program not given (try ".")\n` for that offset.

**Syntax error messages.** The first syntax error stops parsing (jq can
report several in one program; reporting only the first is a documented
exception -- the only second errors produced are the notes below). The
message is
`syntax error, unexpected <X>[, expecting <list>] (Unix shell quoting issues?)`
with `<X>` = `TokenNameInMessage` of the offending token. Its offset is the
offending token's `begin`; for `End` it is the `begin` of the last token
before it (0 if none) -- observed: `.a |` pads 3 (the `|`), `1 as $x` pads
5 (the `$x`), `"abc` pads 1 (the text). The `expecting` part appears in
exactly these situations, and none otherwise:

| Situation | expecting |
|---|---|
| the whole program parsed as an expression and the next token is not End (`.a.b)`, `1 2`, `1 if`, `\|`, `end`, `.. ..`, `try 1 catch 2 3`, `if . then 1 end.a` (`unexpected FIELD`)) | `end of file` |
| a nonassociative operator directly after one of the same group (`1 == 1 == 1`, `.a = 1 = 2`) | none: `unexpected ==`, `unexpected '='` |
| a `.` after a postfix term not followed by a string, FORMAT or `[` (`1 . 2`, `.a.` then End) | `FORMAT or QQSTRING_START or '['` |
| where a pattern must start: after `as`, after `[` or `,` in an array pattern, after `:` in an object pattern (`1 as`, `. as [] \| 1`, `. as {a: } \| 1`) | `BINDING or '[' or '{'` |
| an array pattern's element not followed by `,` or `]` (`. as [$a $b] \| 1`) | `',' or ']'` |
| an object pattern's `$name` entry not followed by `,` or `}` (`. as {$a b} \| 1`) | `',' or '}'` |
| an object pattern's identifier key not followed by `:` (`. as {a} \| 1`) | `':'` |
| `Postfix as Destructuring` not followed by `\|` or `?//` (outside reduce/foreach) | `'\|'` |
| `reduce`/`foreach ... as Destructuring` not followed by `(` | `'('` |
| `def NAME` not followed by `:` or `(` | `'(' or ':'` |
| inside `{...}` after a complete entry (or key), a token other than `,` or `}` (`{a`, `{a:1`, `{a: 1 \| . + 1}`, `{a: 1?}`) | `'}'` |
| a FORMAT used as an object key not followed by a string (`{@base64: 1}`: `unexpected ':'`) | `QQSTRING_START`, and a second error `May need parentheses around object key expression` at the FORMAT's offset |
| a string not terminated before End | `QQSTRING_TEXT or QQSTRING_INTERP_START or QQSTRING_END` |

**Unterminated notes.** A second error follows the syntax error in these
observed cases, and in no others:
- `Possibly unterminated 'if' statement`, at the `if`'s offset, when the
  syntax error falls in a branch of that `if` -- after its `then` and
  before its `end`, not inside a bracket opened there: `if . then 1`,
  `if . then 1 elif`, `if . then 1 else 2`, `if . then 1 2 end`
  (`unexpected LITERAL`), `if . then 1 else 2 3`, `if . then 1 | end`
  (`unexpected end`), `if . then )` all get it; `if .` (in the condition)
  and `if . then (1` (inside the parentheses) do not.
- `Possibly unterminated 'try' statement`, at the `try`'s offset, when the
  syntax error is where the catch body should start: `try 1 catch`,
  `try 1 catch )` get it; `try`, `try (1`, `try 1 catch (2` do not.

### Build

- `src/components/BuiltinCommands/CMakeLists.txt`: add
  `commands/jq/JqAst.cpp`, `commands/jq/JqLexer.cpp`, `commands/jq/JqParser.cpp`
  in the alphabetical `commands/` run (after `commands/head/...`, before
  `commands/ls/...`).
- New `tests/unit/components/Jq.unittests/CMakeLists.txt`, modelled on
  `Awk.unittests`' (`add_executable(Jq.unittests JqLexerTest.cpp JqParserTest.cpp)`,
  `target_link_libraries(Jq.unittests PRIVATE gtest_main BuiltinCommands)`,
  `target_compile_features(... cxx_std_17)`; the include directories of
  `Awk.unittests` are needed only once the command's tests arrive with
  `tools--jq-command`); `add_subdirectory(components/Jq.unittests)` in
  `tests/unit/CMakeLists.txt`, after the `Awk.unittests` line.

### `src/components/BuiltinCommands/commands/jq/CLAUDE.md` (new)

As awk's: what jq is here (a subset of jq 1.7.1's language over its own JSON
values, the reference being jq 1.7.1 as the container has it, matched from
its manual and its output only -- the clean-room note), the pipeline (lexer
-> parser -> tree -> evaluator, the evaluator arriving with later tasks), one
bullet per file of this task saying what it does, the dump format, and the
documented differences: only the first syntax error is reported (plus the
unterminated `if`/`try` notes); nesting is limited to 256; no modules
(`import`, `include`, `module` are syntax errors, even at the start). Later
jq tasks add their files to it.

## Tests

`tests/unit/components/Jq.unittests/JqLexerTest.cpp`:
- `JqLexerTest.TokensOfAProgram`: `.a | .["b"][] as $x | @base64 "v\($x)"`
  -> the expected type/text sequence (Field `a`, Char `|`, Char `.`, Char `[`,
  StringStart, StringText `b`, StringEnd, Char `]`, Char `[`, Char `]`,
  Keyword `as`, Variable `x`, Char `|`, Format `base64`, StringStart,
  StringText `v`, InterpolationStart, Variable `x`, InterpolationEnd,
  StringEnd, End) with correct begin offsets for a few.
- `JqLexerTest.NumbersKeepTheirText`: `1.50`, `.5`, `1e2`, `1E-7` are
  Numbers with that text; `.a5` and `.if` are Fields.
- `JqLexerTest.OperatorsLongestFirst`: `?// //= // |= != == <= >= ..`.
- `JqLexerTest.UnmatchedCloserIsInvalid`: `)` alone, `[}` (the `}`).
- `JqLexerTest.StringEscapes`: `"\t\u00e9\ud83d\ude00\\\/"` decodes
  to tab, `é`, U+1F600 (4 bytes of UTF-8), backslash, slash;
  `"\ude00"` to U+FFFD; every row of the bad-escape table above gives
  that `Error()` text and an Invalid token at the run's `\`.
- `JqLexerTest.Comments`: `1 # x \` then `+ 1` on the next line lexes
  `1 + 1` (the comment ends at the line's end).

`tests/unit/components/Jq.unittests/JqParserTest.cpp` -- `DumpNode` of the
root for each program (expected dumps written by hand from the format above):
- `JqParserTest.PathsAndPostfix`: `.`, `..`, `.a.b`, `.a?.b`, `."a"[0]`,
  `.a.[0]`, `.[]?`, `.[1:]`, `.[:2]`, `.[1:2]`, `$x.a`, `1 .a`, `..[0]`,
  `1??`.
- `JqParserTest.Precedence`: `1 + 2 * 3`, `-1 + 2` -> `(+ (neg 1) 2)`,
  `- 1 * 3` -> `(neg (* 1 3))`, `.a // .b // .c` (right), `1, 2 | 3`,
  `.a = 1 | .b`, `1 < 2 and 2 < 3 or false`, `try 1 + 2` -> `(+ (try 1) 2)`,
  `try error("x") catch . | length`, `if . then 1 end?` -> `(try (if . 1 _))`.
- `JqParserTest.Bindings`: `1, 2 as $x | $x`, `1 as $x | 2, 3`,
  `1 + 2 as $x | 10, 20` -> `(+ 1 (as 2 $x (, 10 20)))`,
  `-1 as $x | 2` -> `(neg (as 1 $x 2))`,
  `. as [$a, {b: $c, $d}] | $a`, `.[] as [$a] ?// $a | $a`,
  `reduce .[] as $x (0; . + $x)`, `foreach .[] as $x (0; . + $x; [$x, .])`,
  `label $f | 1, break $f`.
- `JqParserTest.Definitions`: `def f: 1; def g(a; $b): a + $b; f | g(1; 2)`
  -> `(defs (def f () 1) (def g (a $b) (+ (call a) $b)) (| (call f) (call g 1 2)))`;
  `1 + def f: 2; f`; `def f: def g: 3; g; f`.
- `JqParserTest.ConditionalsAndStrings`: `if . then 1 elif .a then 2 end`
  -> `(if . 1 (index . "a") 2 _)`; `"a\(1 + 2)b"`; `@base64 "x\(.)"`;
  `@csv`; `"\(1,2)"`.
- `JqParserTest.ObjectsDesugared`: `{a, $x, "b c", (.k): 1, if: 2, $__loc__}`
  -> each entry with its desugared value; `{a: .b | length}`; `{a,}`;
  `{a: .b?[0]?}`; `{a: - - 1}`.
- `JqParserTest.SyntaxErrors` -- `ParseProgram` then `FormatCompileError`
  of each error, then `FormatCompileErrorCount`; the whole text compared.
  Every expected text below is jq 1.7.1's, checked on the planning host
  (`·` stands for a space here only; write real spaces; the padding is the
  offending offset minus its line's start):
  - `.a |` -> `jq: error: syntax error, unexpected end of file (Unix shell quoting issues?) at <top-level>, line 1:\n.a |···\njq: 1 compile error\n`
  - `{a:}` -> `... unexpected '}' (Unix shell ...) at <top-level>, line 1:\n{a:}···\n`
  - `.a.b)` -> `... unexpected INVALID_CHARACTER, expecting end of file (...) ...:\n.a.b)····\n`
  - `if . then 1` -> the end-of-file error with 10 spaces, then
    `jq: error: Possibly unterminated 'if' statement at <top-level>, line 1:\nif . then 1\n`, then `jq: 2 compile errors\n`
  - `if . then 1 2 end` -> `unexpected LITERAL` (12 spaces), the `'if'` note, `2 compile errors`; `if . then (1` -> the end-of-file error (11 spaces) alone
  - `try 1 catch` -> the end-of-file error (6 spaces), the `'try'` note, `2 compile errors`; `try 1 catch 2 3` -> `unexpected LITERAL, expecting end of file` (14 spaces) alone
  - `1 . 2` -> `unexpected LITERAL, expecting FORMAT or QQSTRING_START or '['` (4 spaces)
  - `1 as` -> `unexpected end of file, expecting BINDING or '[' or '{'` (2 spaces); `1 as $x` -> `expecting '|'` (5 spaces)
  - `. as [$a $b] | 1` -> `unexpected BINDING, expecting ',' or ']'` (9 spaces); `. as {a} | 1` -> `unexpected '}', expecting ':'` (7 spaces)
  - `reduce . as $x` -> `unexpected end of file, expecting '('` (12 spaces)
  - `def f` -> `expecting '(' or ':'` (4 spaces)
  - `{a:1` -> `expecting '}'` (3 spaces); `{a: 1 | . + 1}` -> `unexpected '+', expecting '}'` (10 spaces); `{a: 1?}` -> `unexpected '?', expecting '}'` (5 spaces); `{a: if . then 1 else 2 end}` -> `unexpected if` (4 spaces); `{,}` -> `unexpected ','` (1 space); `[1,]` -> `unexpected ']'` (3 spaces)
  - `"abc` -> `unexpected end of file, expecting QQSTRING_TEXT or QQSTRING_INTERP_START or QQSTRING_END` (1 space)
  - `1 == 1 == 1` -> `unexpected ==` (no expecting, 7 spaces)
  - `.. ..` -> `unexpected .., expecting end of file` (3 spaces); `1 $x` -> `unexpected BINDING, expecting end of file` (2 spaces); `..a` -> `unexpected IDENT, expecting end of file` (2 spaces)
  - `"\(1 2)"` -> `unexpected LITERAL` (no expecting, 5 spaces)
  - `.a\n| .b )` -> `line 2:\n| .b )·····\n` (the error's own line)
  - `{@base64: 2}` -> `unexpected ':', expecting QQSTRING_START` (8 spaces) and `jq: error: May need parentheses around object key expression at <top-level>, line 1:\n{@base64: 2}·\n`, `2 compile errors`
  - `"\q"` -> `jq: error: Invalid escape at line 1, column 4 (while parsing '"\q"') at <top-level>, line 1:\n"\q"·\n`, `1 compile error`; `"ab\q"` the same message with 3 spaces
  - `1 | import "a" as a; .` -> `unexpected import` (4 spaces)
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
- `src/components/BuiltinCommands/CLAUDE.md`: in "Key Classes", after the
  `commands/awk/` bullet, a bullet `commands/jq/` -- `jq`, the jq 1.7.1
  language (lexer and parser so far); has its own CLAUDE.md.
- No root `CLAUDE.md` change yet (the builtin arrives with `tools--jq-command`).

## Acceptance

- [ ] `ParseProgram` builds the tree for every construct of the grammar
      above; `DumpNode` gives the documented format.
- [ ] Every syntax error in the tests is byte-exact, offsets (trailing
      spaces) included; the expecting lists and the unterminated notes
      appear exactly in the listed situations.
- [ ] Lexer brackets behave as jq's (stray closers are INVALID_CHARACTER;
      `\(...)` nests); bad escapes give jq's messages at the run's `\`.
- [ ] No recursion per input byte; nesting limit enforced; no POSIX
      headers, no `<regex>`.
- [ ] Clean room: no other program's source read; no jq/gojq/jaq internal
      names in code, comments or tests.
- [ ] `Jq.unittests` exists, is built, and passes; the whole unit suite passes.
- [ ] `commands/jq/CLAUDE.md` written; BuiltinCommands CLAUDE.md points to it.

## Out of scope

- Values, JSON reading and printing (`tools--jq-json`); evaluation and the
  "is not defined" compile errors (`tools--jq-eval`); the `jq` command
  (`tools--jq-command`); builtins (`tools--jq-builtins`, `tools--jq-text`).
- Modules (`import`/`include`/`module`, and jq's `module not found` error),
  `?//` error-suppression semantics beyond parsing, jq's further syntax
  errors after the first.
