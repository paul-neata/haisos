# Task awk--expressions: the awk syntax tree and its expression parser

- Rock: awk
- Depends on: awk--lexer
- Size: ~900 changed lines in ~7 files
- Plan checked against: develop @ 308a07c
- PR title: Add the awk syntax tree, its dump and the expression parser

## Goal

The awk builtin gains its syntax tree -- every node the POSIX grammar
needs, statements and program items included, with a one-line dump format
the tests compare against -- and the recursive-descent parser of awk
**expressions**: POSIX precedence and associativity, concatenation, `in`
(including `(i, j) in a`), `~ !~`, the ternary and assignment operators,
`++ --`, `$`, function and built-in calls (`length` without parentheses),
every `getline` form, and regex literals told apart from division by asking
the lexer to rescan the `/`. Syntax errors are gawk's (`syntax error`,
`unexpected newline or end of string`, with the source line and caret).

Statements, program items, functions, `print`/`printf` and the command
running the parser come in awk--parser; nothing changes for a user yet,
except small fixes left open by awk--lexer's review (#76): a lone `&` is a
`syntax error`, a comment ending in a backslash no longer moves the end of
input, and two `--help` notes and one code comment are corrected.

## Context

**Clean room** (root `CLAUDE.md`, "Clean-room rule", above every other
rule): all code is written from scratch. Never read, copy, port, translate
or paraphrase another program's source (gawk, mawk, the one true awk,
busybox, ...), whatever its licence, and never name another program's
internal functions, variables, types or token names. Behaviour is matched
from documentation (POSIX awk, the gawk manual, man pages) and from the
observed output of real awks; every example below was checked against
gawk 5.2.1 `--posix` on the host. The precedence levels and method layout
below are this plan's own design, derived from POSIX's precedence table.

**Write in pieces**: never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step.

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` and
`AwkLexer.h`, `AwkError.h` (awk--lexer); `commands/hsh/HshAst.h/.cpp` and
`HshParser.cpp` (the tree and dump style followed here);
`tests/unit/components/Hsh.unittests/HshParserTest.cpp`;
`tests/unit/components/Awk.unittests/AwkLexerTest.cpp` (its `Source`,
`AllTokens`, `ExpectError` helpers).

What awk--lexer provides (merged as 4cae914, in
`src/components/BuiltinCommands/commands/awk/`: `Awk.cpp` the command,
`AwkInvocation.h/.cpp` its options and sources, `AwkLexer.h/.cpp`,
`AwkError.h/.cpp`, `CLAUDE.md`):

- `Haisos::Awk::Lexer` -- `explicit Lexer(AwkSource)`, held by value;
  `Next()`; `ScanRegex(const Token& slash)`, valid only for the `Slash` or
  `DivAssign` token `Next()` just returned (else `std::logic_error`), which
  returns a `Regex` token whose `text` has `\/` already turned into `/`;
  `TakeWarnings()`; `LineText(line)` (without its newline, `""` past the
  end); `Source()`. Newlines after `{ && || , ; do else` are already skipped;
  a source not ending in a newline gets one (a `Newline` token at the end of
  the last line), then `EndOfInput` forever.
- `Token` -- `kind`, `text` (Number: as written; String: decoded; Name/
  FuncName/Builtin: the name), `number`, `line` (from 1), `column` (byte
  offset in the line), `begin`, `end`; `DescribeToken`.
- `TokenKind`: `EndOfInput Newline Number String Regex Name FuncName
  Builtin`, the keywords (`Getline`, `In`, ...), and the punctuation used
  here: `LeftParen RightParen LeftBracket RightBracket Comma Plus Minus Star
  Slash Percent Caret Not Greater Less Pipe Question Colon Tilde NoMatch
  Dollar Assign AddAssign SubAssign MulAssign DivAssign ModAssign PowAssign
  Or And Equal NotEqual LessEqual GreaterEqual Increment Decrement`. A
  `FuncName` is a non-builtin name touching its `(`; a `Builtin` is one of
  POSIX's built-in function names whatever follows.
- `AwkError.h`: `AwkSource{name, text}`, `AwkWarning`,
  `AwkSyntaxError(message, sourceName, line, lineText, column)` with
  `Line()`, `Column()`, `LineText()`, `FormatAwkSyntaxError`,
  `kCommandLineSourceName` (`"cmd. line"`), `kAwkName`.
- The test target `Awk.unittests`
  (`tests/unit/components/Awk.unittests/CMakeLists.txt`: `add_executable(Awk.unittests
  AwkLexerTest.cpp AwkCommandTest.cpp)`), run by
  `bash ./scripts/test_linux.sh L U Awk`.

The reference is gawk 5.2 `--posix`'s observed behaviour; the task
container has only mawk -- do not "fix" expectations to mawk.

## Changes

### `src/components/BuiltinCommands/commands/awk/AwkAst.h` / `AwkAst.cpp` (new)

Namespace `Haisos::Awk`. Plain structs owned through `unique_ptr`; the
whole tree is owned by a `Program`, which the interpreter holds as
`std::shared_ptr<const Program>`. The parser fills only the fields below;
the interpreter tasks may add `mutable` fields for what they resolve
(variable slots, compiled regexes) -- never change these.

```cpp
struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// Where a node starts: the index of its AwkSource in Program::sources, and
// its line there (from 1). Runtime errors report it.
struct SourcePosition {
    int source = 0;
    int line = 0;
};

enum class ExprKind {
    Number,       // number, text: the source text ("1e3")
    String,       // text: the value
    Regex,        // text: the regex text as the lexer kept it; alone, it means $0 ~ /re/
    Variable,     // text: the name
    Field,        // $operands[0]
    Index,        // text[operands...]: the array's name, the subscripts (>= 1)
    In,           // (operands...) in text: the subscripts (>= 1), the array's name
    Unary,        // op Negate / UnaryPlus / Not; operands[0]
    Binary,       // op Add ... Or, Concat, Match, NoMatch; operands[0], operands[1]
    Conditional,  // operands[0] ? operands[1] : operands[2]
    Assign,       // op Assign ... PowAssign; operands[0] an lvalue, operands[1] the value
    IncDec,       // op PreIncrement ... PostDecrement; operands[0] an lvalue
    Call,         // a user function: text the name, operands the arguments
    BuiltinCall,  // text the built-in's name, operands the arguments; hasParentheses
    Getline,      // getlineForm; target (null: $0, NR...); operands[0] the file or command (File/Command)
};

enum class ExprOp {
    None,
    Add, Subtract, Multiply, Divide, Modulo, Power, Concat,
    Less, LessEqual, NotEqual, Equal, Greater, GreaterEqual, Match, NoMatch, And, Or,
    Negate, UnaryPlus, Not,
    Assign, AddAssign, SubtractAssign, MultiplyAssign, DivideAssign, ModuloAssign, PowerAssign,
    PreIncrement, PreDecrement, PostIncrement, PostDecrement,
};

enum class GetlineForm { Simple, File, Command };  // getline [var]; getline [var] < file; cmd | getline [var]

struct Expr {
    ExprKind kind = ExprKind::Number;
    ExprOp op = ExprOp::None;
    SourcePosition position;
    double number = 0;
    std::string text;
    std::vector<ExprPtr> operands;
    bool hasParentheses = true;            // BuiltinCall: false only for a bare `length`
    GetlineForm getlineForm = GetlineForm::Simple;
    ExprPtr target;                        // Getline's variable, field or element; null when none
};

// An lvalue: Variable, Field or Index.
bool IsLvalue(const Expr& expr);

enum class StmtKind {
    Expression, Print, Printf, If, While, Do, For, ForIn, Block,
    Next, Nextfile, Exit, Return, Break, Continue, Delete,
};

enum class RedirectKind { None, File /* > */, Append /* >> */, Pipe /* | */ };

struct Stmt {
    StmtKind kind = StmtKind::Block;
    SourcePosition position;
    ExprPtr expr;                     // Expression; If/While/Do condition; For condition (null: none); Exit/Return value (null: none)
    std::vector<ExprPtr> args;        // Print/Printf arguments (empty: print $0); Delete subscripts (empty: the whole array)
    RedirectKind redirect = RedirectKind::None;
    ExprPtr redirectTarget;           // Print/Printf with a redirection
    StmtPtr init;                     // For: a simple statement, or null
    StmtPtr update;                   // For: a simple statement, or null
    StmtPtr body;                     // If (then) / While / Do / For / ForIn
    StmtPtr elseBody;                 // If: null when no else
    std::vector<StmtPtr> statements;  // Block
    std::string name;                 // ForIn: the loop variable; Delete: the array
    std::string arrayName;            // ForIn: the array
};

enum class ItemKind { Begin, End, Main, Function };

struct FunctionDefinition {
    std::string name;
    std::vector<std::string> parameters;
    StmtPtr body;                     // a Block
    SourcePosition position;
};

struct Item {
    ItemKind kind = ItemKind::Main;
    ExprPtr pattern;                  // Main: null for an action alone
    ExprPtr rangeEnd;                 // Main: the second pattern of `p1, p2`; null otherwise
    StmtPtr action;                   // a Block; null for a pattern alone (print $0)
    int functionIndex = -1;           // Function: index in Program::functions
    SourcePosition position;
};

struct Program {
    std::vector<AwkSource> sources;
    std::vector<Item> items;                      // in source order, functions included
    std::vector<FunctionDefinition> functions;    // in source order
};

// The one-line dumps the tests compare against (format below).
std::string DumpExpr(const Expr& expr);
std::string DumpStmt(const Stmt& stmt);
std::string DumpProgram(const Program& program);   // one line per item, joined by '\n', no final newline
```

**Dump format** (exact; every test of the rock relies on it):

- Number: its source text (`1e3`, `.5`). String: `"` + the value escaped as
  `DescribeToken` escapes STR (`\\ \" \n \t`, other control bytes `\ooo`) +
  `"`. Regex: `/` + the text with every `/` written `\/` + `/`.
- Variable: the name. Field: `($ e)`. Index: `name[s1, s2]`.
- Unary: `(- e)`, `(+ e)`, `(! e)`. Binary: `(<op> a b)` with `<op>` one
  of `+ - * / % ^ < <= != == > >= ~ !~ && ||` or `concat`.
- In: `(in name s1 s2)`. Conditional: `(?: c a b)`.
- Assign: `(= lv e)`, `(+= lv e)`, `(-= ...)`, `(*= ...)`, `(/= ...)`,
  `(%= ...)`, `(^= ...)`. IncDec: `(pre++ lv)`, `(pre-- lv)`,
  `(post++ lv)`, `(post-- lv)`.
- Call: `(call f a b)`, `(call f)`. BuiltinCall: a bare `length` is
  `length`; with parentheses `(name a b)`, `(length)` for `length()`.
- Getline: `(getline)`, `(getline x)`, `(getline < "f")`,
  `(getline x < "f")`, `(| "cmd" getline)`, `(| "cmd" getline x)`.
- Statements: Block `{ s1; s2 }`, empty `{ }`; Expression: its expr;
  `print`, `print a, b`, `printf "%d\n", x`, followed by ` > t`, ` >> t` or
  ` | t` when redirected; `if (c) S`, `if (c) S else S`; `while (c) S`;
  `do S while (c)`; `for (I; C; U) S` (absent parts empty, so `for (; ; ) S`);
  `for (k in a) S`; `next`, `nextfile`, `break`, `continue`, `exit`,
  `exit e`, `return`, `return e`, `delete a[s1, s2]`, `delete a`.
- Items: `BEGIN S`, `END S`, `P S`, `P, Q S`, `P` (no action), `S` (no
  pattern), `function f(a, b) S`.

### `src/components/BuiltinCommands/commands/awk/AwkParser.h` / `AwkParser.cpp` (new)

```cpp
class Parser {
public:
    // Parses tokens of |source|, which is Program::sources[sourceIndex].
    Parser(const AwkSource& source, int sourceIndex);

    // One expression (`expr` of the POSIX grammar: assignment and ternary
    // included). Throws AwkSyntaxError.
    ExprPtr ParseExpression();

    // awk--parser adds the statement, item and program methods here.

private:
    // One token of lookahead: m_token is the current token; Advance()
    // fetches the next. The lexer never reads past m_token, so ScanRegex can
    // rescan it.
    ...
};

// For the tests: |text| (source "cmd. line") as one expression that must
// be the whole text (then the lexer's final Newline and EndOfInput).
// Throws AwkSyntaxError.
ExprPtr ParseAwkExpression(const std::string& text);
```

`Parser` is a plain class (no interface), owned by value; its lexer is a
member. It keeps exactly one token of lookahead and fetches the next only
when it consumes the current one -- the rescan of a `/` relies on it.

**Grammar, lowest precedence first** (one method per level; POSIX's
precedence table, checked against gawk `--posix`'s output; token kinds are
the lexer's `TokenKind`s):

1. `ParseExpression`: a ternary; if the result is an lvalue and the current
   token is `Assign AddAssign SubAssign MulAssign DivAssign ModAssign
   PowAssign` (`= += -= *= /= %= ^=`), an Assign whose value is
   `ParseExpression()` again (right-associative). `3 = 4` and `(x-- = 2)`
   are syntax errors at the `=`. One exception, as gawk: when the left side
   is a post-increment or post-decrement of a Field (`$1++ = 2`, `$1-- +=
   2`), the value is parsed as usual and then the error is `cannot assign a
   value to the result of a field post-increment expression` (for `--` too)
   at the token after the value, under the Newline/EndOfInput rule below
   (`($1++ = 2)`: column 9, the `)`; `$1++ = 2` alone: `unexpected newline
   or end of string`, line 2, column 8).
2. Ternary: `or-expr [ '?' ternary ':' ternary ]` (right-associative).
3. `||`, then `&&` -- left-associative (the lexer already skips newlines
   after them).
4. `in`: `match-expr { 'in' NAME }` -> In with one subscript.
5. `~ !~`: `comparison { ('~' | '!~') comparison }`, left-associative
   (gawk `--posix` accepts `a ~ b ~ c` as `(a ~ b) ~ c`: `print ("x" ~ "y"
   ~ 0)` prints `1`).
6. Comparison: `pipe-expr [ relop pipe-expr ]` with `< <= != == > >=`,
   non-associative (`1 < 2 < 3`: syntax error at the second `<`).
7. Command getline: `concat { '|' 'getline' [lvalue] }` -> Getline
   `Command`, operands[0] the command (`"a" "b" | getline` takes the
   concatenation). A `|` not followed by `getline` is a syntax error at the
   token after it. (awk--parser turns this off at the top level of a `print`
   argument, where `|` is an output pipe.)
8. Concatenation: `additive { additive }` -- left-associative, continuing
   while the current token can start a concatenated operand: `Number`,
   `String`, `Name`, `FuncName`, `Builtin`, `Dollar`, `Not`, `LeftParen`,
   `Increment`, `Decrement`, `Getline`. Never `Minus`, `Plus` (`a -1` is a
   subtraction), `Slash`/`DivAssign` (division) or `In`. So `x !y`
   concatenates `x` and `!y` (`print 1 !2` prints `10`), `1 ++x` is `1`
   then `++x` (`print 1 ++x` prints `11`), and a simple `getline` is a
   concatenated operand (`x = "a" getline` sets `x` to `a1`).
9. `+ -`, then `* / %` -- left-associative.
10. Unary: `'!' unary`, `'-' unary`, `'+' unary`, else power.
11. Power: `postfix [ '^' unary ]` -- the exponent parsed at the unary level,
    so `2^3^2` is `2^(3^2)`, `-2^2` is `-(2^2)`, `2^-1` is `2^(-1)`.
12. Postfix: `'++' lvalue` / `'--' lvalue` (pre; anything but an lvalue is a
    syntax error at its first token), else a primary followed, when it is an
    lvalue, by an optional `++`/`--` (post).
13. Primary:
    - Number, String.
    - `Slash` or `DivAssign` here (an operand is expected) -> `ScanRegex`
      on it -> Regex. Elsewhere `/` and `/=` are division operators.
    - `(` expression { `,` expression } `)`: one expression is a grouping
      (no node is kept); several must be followed by `in NAME` -> In with
      them as subscripts, else a syntax error at the token after `)`.
    - Name -> Variable, or Name `[` expression { `,` expression } `]` -> Index.
    - FuncName `(` [ expression { `,` expression } ] `)` -> Call.
    - Builtin `(` [args] `)` -> BuiltinCall; a Builtin not followed by `(`
      is allowed only for `length` (bare, `hasParentheses = false`; `length
      / 2` is a division); any other is a syntax error at the next token.
    - `$` -> Field of a dollar operand: `'++'`/`'--'` lvalue (pre), `'-'`,
      `'+'` or `'!'` then a dollar operand (unary), else a primary -- with
      **no** postfix, so `$i++` is `($i)++`, `$++i` is `$(++i)`, `$NF-1` is
      `($NF)-1`, `$-1` is `$(-1)`.
    - `getline` (simple): an optional lvalue target (when the current token
      is a Name or `$`: Name, Name `[...]`, or `$` dollar-operand), then
      optionally `<` and the file operand, parsed at the `+ -` level (step
      9: arithmetic, unary and `^` included, but no concatenation and no
      comparison): `getline < "a" "b"` reads `"a"`, then concatenates
      `"b"` (`x = getline < "f" "g"` sets `x` to `1g`); `getline < "f" + 1`
      reads the file named by `"f" + 1`; `getline < "f" < 2` compares the
      getline's result with 2 -> Getline `Simple` or `File`. (A `++`/`--`
      right after a non-lvalue file operand is left to the caller:
      `getline < "f"++;` is a syntax error at the `;`.)
    - Anything else: a syntax error at the current token.

**Errors** (`AwkSyntaxError`, parsing stops at the first): the message
`syntax error`, at the token where parsing failed -- its line, `LineText`
of that line, its column. When that token is a Newline, the message is
`unexpected newline or end of string`, the line number is the newline's
line **plus one** and the text and caret are those of the line it ends
(caret at the newline's column), as gawk reports it (`1 +`: line 2, text
`1 +`, caret at column 3). When that Newline ends a comment (its `text`
is the comment, see the lexer changes below), gawk words it differently:
the message is `syntax error`, still on line **plus one** with the text of
the line it ends, but the caret is on the comment's `#` (column =
`token.column - token.text.size()`): `1 + # c` gives line 2, column 4.
When it is EndOfInput: `unexpected newline or end of string`, at the
token's line and column. Lexer errors pass through unchanged.

### `AwkLexer.cpp` / `AwkLexer.h` (#76's open findings)

- A lone `&` (not `&&`) is `syntax error` with the caret on the `&`
  (`Fail("syntax error", ...)` at its line and column), as gawk `--posix`:
  `BEGIN { x = 1 & 2 }` and `BEGIN { x = &a }` both put the caret on the
  `&`. `@` stays `invalid char '@' in expression` (a documented exception;
  gawk reports a syntax error at the token after it).
- A comment ending in a backslash: the backslash belongs to the comment
  and continues nothing (gawk: `x = 1 # c \` newline `+ 2` leaves `x` at
  1). Today the constructor decides "the source ends in a real newline" by
  looking only at whether the byte before the final `\n` is `\`, so
  `a # c \` + newline gets an extra NL token and `EndOfInput` at line 2,
  column 0. Decide it instead in `Next()` when the end of the source is
  reached, from whether the last byte consumed was a real newline (a
  Newline token or a skipped newline) or a continuation; a continuation at
  the very end keeps today's behaviour (added newline at the end of the
  last line).
- A Newline token that ends a comment has as its `text` the comment, from
  the `#` to before the newline (the source's added newline too, when the
  comment runs to the end); every other Newline's `text` stays empty.
  `DescribeToken` still writes `NL`. Document it in `Token`'s comment.
- The comment above `FindKeyword` (~line 211) says "nullptr when the name is
  not one"; it returns `TokenKind::Name` -- say so.

### `Awk.cpp` (`--help` notes, version)

The notes overstate two exceptions; gawk also says `invalid char` for any
other byte that starts no token (a backtick, a byte >= 0x80), and gawk
`--posix` accepts a backslash-newline inside a regex. Replace the two notes
with, exactly:

```
"An `@' is reported as invalid char in expression where gawk reports a\n"
"syntax error at the next token.\n"
"A backslash-newline inside a string is a continuation; gawk --posix\n"
"refuses a physical newline in a string.\n"
```

Bump `Version()` from `1.0.0` to `1.0.1` (the `&` message changed) and the
three `1.0.0` expectations in `AwkCommandTest.VersionAndHelp`.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkAst.cpp`
after `commands/awk/Awk.cpp` and `commands/awk/AwkParser.cpp` after
`commands/awk/AwkLexer.cpp`. `tests/unit/components/Awk.unittests/CMakeLists.txt`:
add `AwkParserTest.cpp` to `add_executable(Awk.unittests ...)`, and end the
file with a newline (#76).

## Tests

`tests/unit/components/Awk.unittests/AwkParserTest.cpp` (new; added to that
directory's `CMakeLists.txt`): helpers `ExpectExpr(text, dump)` comparing
`DumpExpr(*ParseAwkExpression(text))` and `ExpectExprError(text, message,
line, column)` catching `AwkSyntaxError`. Expected dumps exactly:

- `AwkParserTest.ArithmeticPrecedence`: `1 + 2 * 3` -> `(+ 1 (* 2 3))`;
  `2 ^ 3 ^ 2` -> `(^ 2 (^ 3 2))`; `-2 ^ 2` -> `(- (^ 2 2))`; `2 ^ -1` ->
  `(^ 2 (- 1))`; `a / b / c` -> `(/ (/ a b) c)`; `7 % 3 * 2` -> `(* (% 7 3) 2)`;
  `!x * 2` -> `(* (! x) 2)`; `- - x` -> `(- (- x))`; `x++ + ++y` -> `(+ (post++ x) (pre++ y))`.
- `AwkParserTest.Concatenation`: `a b c` -> `(concat (concat a b) c)`;
  `a " " b + 1` -> `(concat (concat a " ") (+ b 1))`; `a -1` -> `(- a 1)`;
  `a (-1)` -> `(concat a (- 1))`; `x !y` -> `(concat x (! y))`;
  `a < b c` -> `(< a (concat b c))`; `f (1)` -> `(concat f 1)`;
  `1 ++x` -> `(concat 1 (pre++ x))`; `x ++ y` -> `(concat (post++ x) y)`;
  `"a" getline` -> `(concat "a" (getline))`; `1 getline < "f"` ->
  `(concat 1 (getline < "f"))`.
- `AwkParserTest.MatchInLogical`: `a ~ b c` -> `(~ a (concat b c))`;
  `a ~ /re/ && b` -> `(&& (~ a /re/) b)`; `a !~ "x"` -> `(!~ a "x")`;
  `k in a && x` -> `(&& (in a k) x)`; `(i, j) in a` -> `(in a i j)`;
  `x ~ y in a` -> `(in a (~ x y))`; `a || b && c` -> `(|| a (&& b c))`;
  `a ~ b ~ c` -> `(~ (~ a b) c)`; `1 < 2 in a` -> `(in a (< 1 2))`;
  `"b" ~ "a" < "b"` -> `(~ "b" (< "a" "b"))`.
- `AwkParserTest.AssignmentAndTernary`: `x = y = 3` -> `(= x (= y 3))`;
  `x += y ? 1 : 2` -> `(+= x (?: y 1 2))`; `a ? b : c ? d : e` ->
  `(?: a b (?: c d e))`; `$1 = 2` -> `(= ($ 1) 2)`; `a[1, "x"] ^= 2` ->
  `(^= a[1, "x"] 2)`; `n /= 2` -> `(/= n 2)`.
- `AwkParserTest.Fields`: `$i++` -> `(post++ ($ i))`; `$++i` -> `($ (pre++ i))`;
  `$NF-1` -> `(- ($ NF) 1)`; `$-1` -> `($ (- 1))`; `$$0` -> `($ ($ 0))`;
  `$a[1]` -> `($ a[1])`; `$(i+1)` -> `($ (+ i 1))`.
- `AwkParserTest.RegexOrDivision`: `/re/` -> `/re/`; `x = /=/` -> `(= x /=/)`;
  `1 /2/ 4` -> `(/ (/ 1 2) 4)`; `length / 2` -> `(/ length 2)`;
  `/a\/b/` -> `/a\/b/`; `!/x/` -> `(! /x/)`; `(/x/)` -> `/x/`.
- `AwkParserTest.Calls`: `length` -> `length`; `length()` -> `(length)`;
  `length($1)` -> `(length ($ 1))`; `substr(s, 2, 3)` -> `(substr s 2 3)`;
  `f(1, g(2))` -> `(call f 1 (call g 2))`; `f()` -> `(call f)`; `"a\tb"` -> `"a\tb"`.
- `AwkParserTest.GetlineForms`: `getline` -> `(getline)`; `getline x` ->
  `(getline x)`; `getline < "f"` -> `(getline < "f")`; `getline x < f g` ->
  `(concat (getline x < f) g)`; `getline $1` -> `(getline ($ 1))`;
  `getline a[1]` -> `(getline a[1])`; `"cmd" | getline` -> `(| "cmd" getline)`;
  `"cmd" | getline x > 0` -> `(> (| "cmd" getline x) 0)`;
  `"a" "b" | getline` -> `(| (concat "a" "b") getline)`;
  `1 < "cmd" | getline` -> `(< 1 (| "cmd" getline))`;
  `getline < "a" "b"` -> `(concat (getline < "a") "b")`;
  `getline < "f" + 1` -> `(getline < (+ "f" 1))`;
  `getline < "f" < 2` -> `(< (getline < "f") 2)`.
- `AwkParserTest.ExpressionErrors` (message, line, column): `a + * b` ->
  `syntax error`, 1, 4; `1 < 2 < 3` -> `syntax error`, 1, 6; `3 = 4` ->
  `syntax error`, 1, 2; `substr + 1` -> `syntax error`, 1, 7; `(1, 2) + 3`
  -> `syntax error`, 1, 7; `++1` -> `syntax error`, 1, 2; `1 +` ->
  `unexpected newline or end of string`, line 2, column 3 (text `1 +`);
  `a[1` -> `unexpected newline or end of string`, line 2, column 3;
  `x | y` -> `syntax error`, 1, 4; `(x-- = 2)` -> `syntax error`, 1, 5;
  `($1++ = 2)` -> `cannot assign a value to the result of a field
  post-increment expression`, 1, 9; `($1-- += 2)` -> the same message, 1,
  10; `$1++ = 2` -> `unexpected newline or end of string`, line 2, column 8;
  `1 + # c` -> `syntax error`, line 2, column 4 (text `1 + # c`).
- `AwkParserTest.ErrorTextIsGawks`: `FormatAwkSyntaxError` of the error from
  `a + * b` is `awk: cmd. line:1: a + * b\nawk: cmd. line:1:     ^ syntax error\n`.

`tests/unit/components/Awk.unittests/AwkLexerTest.cpp` (the lexer fixes):

- `AwkLexerTest.Errors`: add `ExpectError("x = 1 & 2", "syntax error", 1,
  6)` and `ExpectError("a &", "syntax error", 1, 2)`; `` ` `` keeps its
  `invalid char` case.
- `AwkLexerTest.NewlinesAndComments`: `AllTokens("a # c \\\n")` is
  `NAME(a) NL EOF`, the NL at line 1, column 7 with `text` `# c \`, the EOF
  at line 1, column 7; `Lex("a # c \\\nb")` is `NAME(a) NL NAME(b) NL EOF`;
  `AllTokens("a # c")`'s NL has `text` `# c`; `AllTokens("a\n")`'s NL has
  an empty `text`.

`AwkCommandTest.VersionAndHelp`: `1.0.1` (three places).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/awk/CLAUDE.md`: `AwkAst.h/.cpp` (the node kinds, ownership, that
later tasks add only `mutable` resolution fields) and an "AST dump" section
with the format above; `AwkParser.h/.cpp` with the precedence list, the
one-token-lookahead / `ScanRegex` protocol, the concatenation start set,
the `$` and `getline` operand rules, and the error rules (the comment
Newline and field post-increment cases included). In the lexer rules: a
lone `&` is `syntax error` at it; a backslash ending a comment continues
nothing; a Newline ending a comment carries the comment as its `text`.
The "Documented exceptions" list stays as it is (`@` only, strings only).

## Acceptance

- [ ] `AwkAst.h` declares exactly the types and fields above (later tasks
  build on them).
- [ ] `DumpExpr`/`DumpStmt`/`DumpProgram` follow the format above for every
  node kind (statements and items too, though they are only parsed in the
  next task).
- [ ] Precedence and associativity as listed; regex vs division only through
  `ScanRegex`; the parser never reads past its one token of lookahead.
- [ ] Error messages, lines and columns as specified; every test above passes.
- [ ] Lone `&` is `syntax error`; a comment ending in a backslash adds no
  NL and leaves `EndOfInput` on its line; comment Newlines carry the
  comment; the `FindKeyword` comment corrected.
- [ ] `--help` notes exactly as given (`@` only, strings only); awk is
  `1.0.1`.
- [ ] No code, names or structure taken from another awk's source.
- [ ] New sources in the CMakeLists; all unit tests green.

## Out of scope

- Statements, items, functions, `print`/`printf` and their redirections,
  the print-argument context, parse-time checks, running the parser from
  the command (awk--parser).
- Any evaluation (awk--interpreter and later).
