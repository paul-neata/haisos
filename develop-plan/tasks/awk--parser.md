# Task awk--parser: awk statements, items, functions, and parse errors

- Rock: awk
- Depends on: awk--expressions
- Size: ~900 changed lines in ~6 files
- Plan checked against: develop @ ccb9dbe
- PR title: Parse whole awk programs, with gawk's syntax errors

## Goal

The awk builtin parses whole programs -- pattern-action items, `BEGIN`/
`END`, ranges, function definitions, every POSIX statement, `print`/
`printf` with `>`, `>>` and `|` redirections -- from the program operand
or the `-f` files, and reports what gawk `--posix` reports: syntax errors
with the source line and caret (`awk: cmd. line:1: ...`), the end of a
`-f` file inside a rule, and gawk's parse-time errors (`` `next' used in
BEGIN action ``, a function defined twice, bad parameters, a function used
as a variable), all with exit status 1. A program that parses still does
not run (`awk: running programs is not implemented yet`, status 2) until
awk--interpreter.

## Context

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md`,
`AwkAst.h`, `AwkParser.h/.cpp`, `AwkLexer.h`, `AwkError.h`,
`AwkInvocation.h`, `Awk.cpp`; `tests/unit/components/Awk.unittests/`.

What earlier tasks provide (on develop): the whole AST (`Program`, `Item`,
`ItemKind`, `FunctionDefinition`, `Stmt`, `StmtKind`, `RedirectKind`,
`Expr`, `IsLvalue`) and its dump (`DumpProgram`, `DumpStmt`, `DumpExpr`,
the format in `commands/awk/CLAUDE.md`), `Parser(source, sourceIndex)`
with `ParseExpression()` and the precedence levels, the error rules
(Newline: line + 1, `unexpected newline or end of string`), `Lexer`
(`TakeWarnings`, `LineText`), `AwkSyntaxError`, `FormatAwkSyntaxError`,
`FormatAwkWarning`, `FormatAwkError`, `LoadAwkSources`.

Every expected text below was checked with gawk 5.2.1 `--posix`; the task
container has only mawk -- never change an expectation to mawk's.

## Changes

### `AwkParser.h` / `AwkParser.cpp`

Add the program entry point:

```cpp
struct ParseResult {
    // The parsed program (sources moved in); null when anything failed.
    std::shared_ptr<Program> program;
    // Everything to write to stderr, in the order found: warnings
    // (FormatAwkWarning), parse-time errors (FormatAwkError) and, last, the
    // syntax error that stopped parsing (FormatAwkSyntaxError).
    std::string diagnostics;
    bool failed = false;   // an error or a syntax error: exit status 1, nothing runs
};

ParseResult ParseAwkProgram(std::vector<AwkSource> sources);
```

Each source is parsed by its own `Parser` (its index in `sources` as
`sourceIndex`), items and functions appended to one `Program` in order. A
rule may not span two sources. Lexer warnings are moved into
`diagnostics` after every item (and before a syntax error), so they come
out in source order.

New `Parser` methods (names are the implementer's; the structure is fixed):

**Items** -- `{ Newline | ';' }`, then items until EndOfInput:
- `function` (or `func`) Name-or-FuncName `(` [Name { `,` Name }] `)`
  { Newline } block -> `FunctionDefinition` + an `Item` of kind
  `Function`.
- `BEGIN` block, `END` block (anything but `{` after them is a syntax error
  at that token).
- pattern [ `,` pattern ] [ block ] -> `Main` (a pattern is an
  expression; no block means "print $0": `action` null).
- block alone -> `Main` with no pattern.
- After an item: Newlines and `;` are skipped. An item ending in `}` may be
  followed directly by the next item (`BEGIN{}END{}`); an item ending in a
  pattern must be followed by Newline, `;` or the end -- so `NR==1\n{ print }`
  is two items, as POSIX says.
- EndOfInput inside an item of a `-f` source (name not `cmd. line`): the
  syntax error `source files / command-line arguments must contain complete
  functions or rules`, line = the EndOfInput token's line, line text
  `(END OF FILE)`, column 0. (gawk's caret column there depends on
  internals: with a final newline it is 0, as here.) In the `cmd. line`
  source the general rule applies (`unexpected newline or end of string`).

**Statements** (inside a block `{ ... }`):
- Newline and `;` alone are skipped; `}` ends the block.
- block; `if (` expr `)` {Newline} statement, then -- after skipping
  Newlines -- `else` {Newline (the lexer skips them)} statement; `while (`
  expr `)` {Newline} statement; `do` statement `while (` expr `)`
  terminator; `for (` [simple] `;` [expr] `;` [simple] `)` {Newline}
  statement; `for (` Name `in` Name `)` {Newline} statement.
- A body that is a lone `;` (`if (x) ;`, `while (x);`) is an empty Block.
- for-in detection: after `for (`, parse an expression; if it is an In with
  one subscript that is a Variable and the current token is `)`, it is a
  ForIn (`name` = the variable, `arrayName` = the array); otherwise it is
  the init of a classic `for` (then `;` must follow).
- Simple statements, each ended by a terminator -- `;`, Newline, or nothing
  before `}` (and EndOfInput, which then fails at the item level):
  `print` [args] [redirection]; `printf` [args] [redirection] (gawk accepts
  `printf` alone); `next`; `nextfile`; `exit` [expr]; `return` [expr];
  `break`; `continue`; `delete` Name [ `[` expr {`,` expr} `]` ]; or an
  expression (an Expression statement). `exit`/`return` take an expression
  unless a terminator or `}` follows.

**print and printf** -- the print context:
- Arguments are expressions separated by `,`. At their top level (not inside
  `( )`, `[ ]` or call arguments) `>` is not a comparison and `|` is not
  `| getline`: both end the list. Implement with a parser flag that
  `ParseExpression` levels consult and that `(`, `[` and call arguments
  clear for their contents (saved and restored).
- Redirection: `>` -> File, `>>` -> Append, `|` -> Pipe, then the target
  parsed at the **concatenation** level (`print "x" > "out" ".txt"` writes
  `out.txt`; no comparison or ternary: `print 1 > 2 ? "a" : "b"` is a
  syntax error at `?`). A terminator must follow.
- `print (`: parse `(` expr { `,` expr } `)`; if a terminator, `}`, `>`,
  `>>` or `|` follows, those are the arguments (`print (1, 2) > "f"`).
  Otherwise, with one expression, it was a grouping: continue parsing the
  first argument with it as its leftmost primary (`print (1)(2)` is
  `(concat 1 2)`, `print (a) + b`), then further arguments; with several,
  `in` must follow (`print (1, 2) in a`), else a syntax error. Add a
  `Parser` hook for "continue an expression from this primary" (e.g. a
  pending primary that the primary level returns first).

**Parse-time checks** -- errors (`FormatAwkError`, at the line of the
token named; parsing goes on, the program does not run, status 1), texts
exactly:
- `` `next' used in BEGIN action ``, `` `next' used in END action ``,
  `` `nextfile' used in BEGIN action ``, `` `nextfile' used in END action ``
  (in a BEGIN/END action, not in a function).
- `` `break' is not allowed outside a loop or switch ``,
  `` `continue' is not allowed outside a loop `` (gawk prints these twice:
  once here, a documented exception).
- `` function name `f' previously defined `` (at the second definition).
- `` function `f': parameter #2, `a', duplicates parameter #1 ``;
  `` function `f': cannot use function name as parameter name `` (gawk adds
  a second, location-less line: not reproduced, documented);
  `` function `f': cannot use special variable `NR' as a function parameter ``
  for each of `ARGC ARGV CONVFMT ENVIRON FILENAME FNR FS NF NR OFMT OFS ORS
  RLENGTH RS RSTART SUBSEP`.
- After the whole program: a defined function's name used as a variable or
  array -- a Variable, Index or In node, a ForIn variable or array, a
  Delete, a getline target -- gives
  `` function `f' called with space between name and `(',\nor used as a variable or an array ``
  (one message holding a newline, as gawk prints it), at that use's line.

Syntax errors (stop parsing, `AwkSyntaxError`):
- `` `return' used outside function context `` at the `return` token.
- `` `length' is a built-in function, it cannot be redefined `` at the name
  after `function` when it is a Builtin token.
- Everything else as awk--expressions defined.

### `Awk.cpp`

`Run`: after `LoadAwkSources`, `ParseAwkProgram`; write `diagnostics` to
stderr (`ErrorText`); `failed` -> status 1; otherwise still the
not-implemented message and status 2 (awk--interpreter replaces it). Add
the documented exceptions above to `Help().notes`.

## Tests

In `tests/unit/components/Awk.unittests/AwkParserTest.cpp`, a new suite
`AwkProgramParserTest`: `ExpectProgram(text, dump)` compares
`DumpProgram(*ParseAwkProgram({{"cmd. line", text}}).program)`;
`ExpectDiagnostics(text, diagnostics)` checks `failed` and the exact
`diagnostics`. Expected values exactly:

- `Items`: `BEGIN { x = 1 } END { print x }` -> `BEGIN { (= x 1) }\nEND { print x }`;
  `NR > 1 { s += $3 } END { printf "%.2f\n", s }` -> `(> NR 1) { (+= s ($ 3)) }\nEND { printf "%.2f\n", s }`;
  `NR==1\n{ print }` and `NR==1;{ print }` -> `(== NR 1)\n{ print }`;
  `/a/,/b/` -> `/a/, /b/`; `$1 == "x", 0 { next }` -> `(== ($ 1) "x"), 0 { next }`;
  `{}` -> `{ }`; `BEGIN{}END{}` -> `BEGIN { }\nEND { }`; `\n\n;BEGIN{}\n\n` -> `BEGIN { }`.
- `Functions`: `function f(a, b) { return a + b }\nBEGIN { print f(1, 2) }` ->
  `function f(a, b) { return (+ a b) }\nBEGIN { print (call f 1 2) }`;
  `func g() {}` -> `function g() { }`; `function h(x)\n\n{ }` -> `function h(x) { }`.
- `Statements`: `{ if (x) print 1; else print 2 }` and `{ if (x)\n print 1\n else\n print 2 }`
  -> `{ if (x) print 1 else print 2 }`; `{ while (i < 3) i++ }` -> `{ while ((< i 3)) (post++ i) }`;
  `{ do x++; while (x < 3) }` -> `{ do (post++ x) while ((< x 3)) }`;
  `{ for (i = 0; i < 3; i++) s += i }` -> `{ for ((= i 0); (< i 3); (post++ i)) (+= s i) }`;
  `{ for (;;) break }` -> `{ for (; ; ) break }`;
  `{ for (k in a) delete a[k]; delete a }` -> `{ for (k in a) delete a[k]; delete a }`;
  `{ exit } END { exit 3 }` -> `{ exit }\nEND { exit 3 }`; `{ next; nextfile }` -> `{ next; nextfile }`;
  `{ getline line < "f" }` -> `{ (getline line < "f") }`; `{ ; ; x }` -> `{ x }`;
  `{ if (x) ; }` -> `{ if (x) { } }`; `{\n  a = 1\n  b = 2\n}` -> `{ (= a 1); (= b 2) }`.
- `PrintAndRedirections`: `{ print > "f" }` -> `{ print > "f" }`;
  `{ print $1, $2 > "out" ".txt" }` -> `{ print ($ 1), ($ 2) > (concat "out" ".txt") }`;
  `{ print a >> f }` -> `{ print a >> f }`; `{ print | "sort" }` -> `{ print | "sort" }`;
  `{ print (1)(2) }` -> `{ print (concat 1 2) }`; `{ print (1, 2) > "f" }` -> `{ print 1, 2 > "f" }`;
  `{ print (1, 2) in a }` -> `{ print (in a 1 2) }`; `{ print (a > b) }` -> `{ print (> a b) }`;
  `{ printf("%s\n", $1) | "cat" }` -> `{ printf "%s\n", ($ 1) | "cat" }`;
  `{ print "x" | "cat"; "date" | getline d }` -> `{ print "x" | "cat"; (| "date" getline d) }`;
  `{ x = "a" "b" > "c" }` -> `{ (= x (> (concat "a" "b") "c")) }`;
  `{ print length $0 }` -> `{ print (concat length ($ 0)) }`; `{ printf }` -> `{ printf }`.
- `SyntaxErrors` (diagnostics exactly; `failed`):
  - `BEGIN { x = = 1 }` -> `awk: cmd. line:1: BEGIN { x = = 1 }\nawk: cmd. line:1:             ^ syntax error\n`
  - `BEGIN { print 1 }}` -> caret under the second `}` (column 17)
  - `BEGIN{x=1` -> `awk: cmd. line:1: BEGIN{x=1\nawk: cmd. line:1:          ^ unexpected newline or end of string\n`
  - `BEGIN { print 1; print 2;` -> line 1, column 26, `unexpected newline or end of string`
  - `BEGIN {\nprint 1\nx = \n}` -> `awk: cmd. line:4: x = \nawk: cmd. line:4:     ^ unexpected newline or end of string\n`
  - `BEGIN { print 1 +* 2 }` -> column 17; `BEGIN { if (x) }` -> column 15;
    `BEGIN { print "a" > }` -> column 20; `BEGIN { getline < }` -> column 18;
    `BEGIN { print 1 > 2 ? "a" : "b" }` -> column 20 (all `syntax error`)
  - `BEGIN { return }` -> `awk: cmd. line:1: BEGIN { return }\nawk: cmd. line:1:         ^ `return' used outside function context\n`
  - `function length() {}` -> column 9, `` `length' is a built-in function, it cannot be redefined ``
  - `BEGIN { x = "\q" ; = }` -> the escape warning line first, then the syntax error.
- `ParseTimeErrors` (diagnostics exactly):
  `BEGIN { next }` -> ``awk: cmd. line:1: error: `next' used in BEGIN action\n``;
  `END { nextfile }` -> ``awk: cmd. line:1: error: `nextfile' used in END action\n``;
  `BEGIN { break }` -> ``awk: cmd. line:1: error: `break' is not allowed outside a loop or switch\n``;
  `BEGIN { continue }` -> ``awk: cmd. line:1: error: `continue' is not allowed outside a loop\n``;
  `function f(a) {} function f(b) {}` -> ``awk: cmd. line:1: error: function name `f' previously defined\n``;
  `function f(a, a) {}` -> ``awk: cmd. line:1: error: function `f': parameter #2, `a', duplicates parameter #1\n``;
  `function f(NR) {}` -> ``awk: cmd. line:1: error: function `f': cannot use special variable `NR' as a function parameter\n``;
  `function f(f) {}` -> ``awk: cmd. line:1: error: function `f': cannot use function name as parameter name\n``;
  `function f(x) { return } BEGIN { f (1) }` ->
  ``awk: cmd. line:1: error: function `f' called with space between name and `(',\nor used as a variable or an array\n``;
  `{ while (x) { break; continue } } function g() { next }` -> no diagnostics, not failed.
- `SeveralSources`: with the sources
  `[{"/a.awk", "BEGIN { x = 1 }\n"}, {"/b.awk", "END { print x }\n"}]` ->
  `BEGIN { (= x 1) }\nEND { print x }`, and the END item's position has
  source 1, line 1; `[{"/p.awk", "BEGIN {\n"}]` ->
  `awk: /p.awk:1: (END OF FILE)\nawk: /p.awk:1: ^ source files / command-line arguments must contain complete functions or rules\n`.

In `AwkCommandTest.cpp`:
- `AwkCommandTest.SyntaxErrorsAreReported`: `RunCaptured("awk", {"BEGIN { x = = 1 }"})`
  -> stdout empty, stderr the first `SyntaxErrors` text, status 1; with
  `/p.awk` written (`BEGIN {\n`) and `-f /p.awk` -> the `(END OF FILE)` text, 1.
- `AwkCommandTest.ParsedProgramsDoNotRunYet`: `BEGIN { print 1 }` -> the
  not-implemented message, 2 (awk--interpreter removes this test).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/awk/CLAUDE.md`: a "Parsing programs" section -- items and their
separation, statements and terminators, the print context and the
`print (` rule, redirection targets at the concatenation level, sources
parsed one by one, the parse-time checks and their texts, the
diagnostics order; the new documented exceptions (break/continue reported
once, the parameter-name line). `src/components/BuiltinCommands/CLAUDE.md`
and root `CLAUDE.md`: the awk rows say programs are parsed (with gawk's
errors) but not run yet.

## Acceptance

- [ ] `ParseAwkProgram` builds the AST for every construct listed, with
  positions (source index, line) on every item, statement and expression.
- [ ] The print context and `print (` disambiguation behave as specified.
- [ ] Every syntax error and parse-time error text, line and column as specified.
- [ ] `awk` reports diagnostics on stderr with status 1, and nothing on stdout.
- [ ] All `Awk` and other unit tests green.

## Out of scope

- Running anything (awk--interpreter, awk--records); calls to undefined
  functions (awk--functions reports them before the program runs).
- gawk extensions: `switch`, `BEGINFILE`/`ENDFILE`, `@include`, `|&`, `**`.
