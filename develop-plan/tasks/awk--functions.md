# Task awk--functions: user-defined functions and awk's string built-ins

- Rock: awk
- Depends on: awk--records
- Size: ~950 changed lines in ~11 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add awk user-defined functions and the string built-in functions

The task as first listed (string built-ins, math, user functions, printf and
sprintf) came to ~1500 lines; printf/sprintf and the math functions are split
off into awk--printf-math, which follows this one. This task keeps what
changes the interpreter's structure (functions, frames, the argument-count
checks) and the string functions.

## Goal

awk programs can define and call functions, and use the string built-ins,
exactly as gawk 5.2 `--posix` runs them:

- **User-defined functions**: `function f(a, b) { ... return x }`, called
  before or after their definition; scalars passed by value, arrays by
  reference, an untyped variable passed by name (the callee may make it an
  array in the caller: `function fill(a) { a[1] = 1 } BEGIN { fill(x); print x[1] }`
  prints `1`); extra parameters are locals; recursion; `return` with and
  without a value; `next`, `nextfile` and `exit` inside a function act on
  the rule that called it; gawk's runtime errors and warnings (`function
  `foo' not defined`, `attempt to use scalar parameter `a' as an array`,
  `attempt to use array `a (from x)' in a scalar context`, `function `f'
  called with more arguments than declared`, `` `next' cannot be called from
  a `BEGIN' rule ``).
- **String built-ins**: `length` (with and without parentheses), `substr`,
  `index`, `split` (FS rules, a single character, `""`, regex literals and
  dynamic regexes), `sub` and `gsub` (`&`, `\&`, `\\&` as gawk `--posix`
  takes them; `$0`, fields, variables and array elements as targets), `match`
  (setting `RSTART` and `RLENGTH`), `tolower`, `toupper`.
- **Argument counts** are checked when the program is parsed, as gawk does:
  `substr("abc")` is the syntax error `1 is invalid as number of arguments
  for substr`, caret on the closing parenthesis.

Still not available (each a clear fatal error until its task): `printf`,
`sprintf` and the math functions `sin cos atan2 exp log sqrt int rand srand`
(awk--printf-math); `getline`, output redirections, `close`, `fflush`,
`system`, `ENVIRON` (awk--io).

## Context

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` and every
file it lists -- above all `AwkAst.h`, `AwkParser.h/.cpp`,
`AwkInterpreter.h/.cpp`, `AwkValue.h`, `AwkFields.h`, `AwkRegex.h`; the
tests in `tests/unit/components/Awk.unittests/` (`AwkInterpreterTest.cpp`
holds the `AwkRunTest` fixture); root `CLAUDE.md` ("Builtin Commands",
"Security"); `src/components/BuiltinCommands/CLAUDE.md`.

What earlier tasks provide (on develop; their plans in `develop-plan/tasks/`
are the authority):
- awk--expressions / awk--parser: the AST -- `Expr` (`kind`, `op`,
  `position`, `number`, `text`, `operands`, `hasParentheses`,
  `getlineForm`, `target`), `ExprKind::Call` (a user function: `text` the
  name, `operands` the arguments) and `ExprKind::BuiltinCall` (`text` the
  built-in's name; a bare `length` has `hasParentheses == false` and no
  operands), `Stmt`/`StmtKind` (`Return` with `expr` null or the value),
  `FunctionDefinition` (`name`, `parameters`, `body`, `position`),
  `Program::functions`, `ItemKind`; `Parser` with one token of lookahead and
  its syntax-error helper (the one that throws `AwkSyntaxError` "syntax
  error" at the current token: its line, `LineText`, column);
  `ParseAwkProgram`. The parser already rejects a function name used as a
  variable, duplicate parameters, special variables as parameters,
  `return` outside a function.
- awk--interpreter: `Interpreter` with `Prepare`, `Evaluate`, `Execute`,
  `GlobalCell(slot)`, `ScalarRef(const Expr&)`, `ArrayRef(slot, name)`,
  `Assign(const Expr& lvalue, const Value&)` (Variable -- `NF` through
  `SetNF` --, Index, Field), `SpecialString(slot)`, `NextMainRecord`,
  `Output`, `SplitRecord`, the placeholders `CallBuiltin(const Expr&)` and
  `CallFunction(const Expr&)` (both `AwkFatal("function `<name>' is not
  implemented yet")`), `EvaluateGetline` (placeholder), `ThrowIfStopped()`;
  `Cell { kind (Untyped/Scalar/Array), scalar, array (shared_ptr<AwkArray>) }`;
  `SpecialSlot` (`kSlotRSTART`, `kSlotRLENGTH`, `kSlotFS`, `kSlotCONVFMT`,
  ...); `Flow { Normal, Break, Continue, Next, NextFile, Exit, Return }`;
  `m_position` (fatal errors report it), `m_exitCode`; `Expr::slot`,
  `Stmt::slot`, `Stmt::arraySlot` (global slots); the fatal error format
  (`awk: <src>:<line>: [(FILENAME=<f> FNR=<n>) ]fatal: <message>\n`, status 2)
  and the errors ``attempt to use array `x' in a scalar context`` /
  ``attempt to use scalar `x' as an array``.
- awk--values: `Value` (`FromNumber`, `FromString`, `FromInput`,
  `GetType`, `IsNumeric`, `ToNumber`, `ToString(format)`, `ToBoolean`),
  `AwkArray` (`Find`, `GetOrCreate`, `Contains`, `Remove`, `Clear`, `Size`,
  `Keys`), `SplitAwkFields(text, fs, fields)` (`" "`, one byte literally,
  `""` one field; false for a longer fs), `FieldStore`, `AwkFatal`.
- awk--records: `AwkRegexCache`, `TranslateAwkRegex`, `Expr::compiledRegex`
  (a Regex literal's, compiled before BEGIN), `Interpreter::MatchRegex(const
  Expr& regexOperand, const std::string& text)` (a literal's compiled regex,
  or the operand's string value through the cache; dynamic warnings written
  with the runtime prefix; a bad one `AwkFatal("invalid regexp: <error>:
  /<text>/")`), the regex field splitting in `SplitRecord` (empty matches
  never separate; a match at the start gives an empty first field).
- base--regex-match: `Regex::Search(text, start, RegexMatch&, flags)` --
  leftmost-longest; `^` matches only at offset 0 of the text (never at
  `start > 0`), `$` only at its end; `RegexMatch::groups[0]` is the match
  (begin, end).

Every expected text below was produced by gawk 5.2.1 `--posix` with
`LC_ALL=C` (with `gawk:` replaced by `awk:`). The task container has only
mawk, whose behaviour differs in many of these cases: never change an
expectation to mawk's, and never look for or copy gawk's (or any other awk's)
source code -- the behaviour described here is the specification.

## Changes

### `commands/awk/AwkParser.cpp` -- argument counts of the built-ins

When a `Builtin` token's call is parsed, with the current token on the
closing `)` (not yet consumed), check the number of arguments and throw
through the parser's syntax-error helper -- at that `)` token, with the
message below instead of `syntax error`:

| Built-in | Accepted | Otherwise |
|----------|----------|-----------|
| `length` | 0-1 (a bare `length` is 0) | `<n> is invalid as number of arguments for length` |
| `substr` | 2-3 | same pattern |
| `index`, `atan2` | 2 | same pattern |
| `split` | 2-4 (a 4th is refused at run time, see below) | same pattern |
| `sub`, `gsub` | 2-3 | same pattern |
| `match` | 2; 3 -> `match: third argument is a gawk extension` | same pattern |
| `sprintf` | any (0 is refused at run time, awk--printf-math) | -- |
| `sin cos exp log sqrt int tolower toupper system` | 1 | same pattern |
| `rand` | 0 | same pattern |
| `srand`, `fflush` | 0-1 | same pattern |
| `close` | 1; 2 -> `close: second argument is a gawk extension` | same pattern |

`<n>` is the number of arguments given (`0 is invalid as number of
arguments for substr`). Example (gawk's, verified): `BEGIN{print substr("abc")}`
-> `awk: cmd. line:1: BEGIN{print substr("abc")}\nawk: cmd. line:1:                         ^ 1 is invalid as number of arguments for substr\n`
(caret at column 24, the `)`), status 1. A call spanning lines reports the
line of its `)`.

### `commands/awk/AwkAst.h`

Resolution fields only (all `mutable`, filled by `Prepare`; nothing the
parser sets changes):
- `Expr::localSlot` (`int`, -1): Variable, Index, In -- the parameter index
  when the name is a parameter of the function the expression is in.
- `Expr::functionIndex` (`int`, -1): Call -- the index in
  `Program::functions`; -1 when no function of that name is defined.
- `Stmt::localSlot`, `Stmt::localArraySlot` (`int`, -1): ForIn's variable
  and array, Delete's array, when they are parameters.

### `commands/awk/AwkInterpreter.h` / `.cpp` -- functions

Additions to the fixed structure (other private helpers are the
implementer's):

```cpp
// The deepest chain of active user-function calls (a documented exception:
// gawk has no fixed limit). One more call is AwkFatal("function call nesting
// too deep (more than 200 calls)").
inline constexpr size_t kAwkMaxCallDepth = 200;

struct Cell {
    ...                               // kind, scalar, array: unchanged
    // A parameter given an untyped variable by name: the caller's cell (a
    // global, or a local of the calling frame, itself possibly bound).
    Cell* binding = nullptr;
    // A parameter given an array (or bound): the name written at the call,
    // for "attempt to use array `a (from x)' in a scalar context".
    std::string passedFrom;
};

// One active call of a user function.
struct Frame {
    const FunctionDefinition* function = nullptr;
    std::vector<Cell> locals;         // one per parameter, sized once at the call
    Value returnValue;                // Uninitialized unless `return expr` ran
};

// Unwinds a `next`, `nextfile` or `exit` run inside a function out of the
// expression that called it, to the rule loop in Run.
struct FlowUnwind { Flow flow; };
```

Members: `std::vector<std::unique_ptr<Frame>> m_frames;` (frames never move,
so a `Cell*` binding stays valid while its frame is active) and the kind of
the item being run (`ItemKind m_currentItemKind`, for the BEGIN/END check).
Add `void RuntimeWarning(const std::string& message)`: the one place a
runtime warning is written -- `awk: <src>:<line>: [(FILENAME=<f> FNR=<n>) ]warning: <message>\n`,
the same prefix as a fatal error at `m_position`; awk--records' dynamic-regex
warnings go through it too (move them if they format the line themselves).

**Prepare**: walk each function body with its parameter list: a Variable,
Index or In whose name is a parameter gets `localSlot` (and no global slot is
used for it there); ForIn and Delete likewise through `Stmt::localSlot` /
`localArraySlot`. Every Call gets `functionIndex` from a name -> index map of
`Program::functions`. Names outside functions, and non-parameter names inside
them, stay globals.

**Cells**: replace the slot-only lookups with cell-based ones used everywhere
a variable is reached (Evaluate of Variable/Index/In, Assign, ForIn, Delete,
and the built-ins below): the cell of a node is the current frame's
`locals[localSlot]` when `localSlot >= 0`, else the global cell. Then:
- **As a scalar** (read or assigned): Untyped -> Scalar; when the cell is
  bound, every cell of its binding chain that is still Untyped becomes Scalar
  too (values unchanged: `function f(a) { a = 1 } BEGIN { f(x); print "x=" x }`
  prints `x=`, and `x` can no longer be an array). An Array cell is
  `AwkFatal("attempt to use array `<name>' in a scalar context")`, where
  `<name>` is `a (from x)` for a parameter whose `passedFrom` is `x`.
- **As an array**: Untyped and bound -> follow the chain to its last cell; if
  that is Untyped, create the array there; if it is an Array, take it; if it
  is Scalar, fail as below. Every cell of the chain (and this one) then shares
  that `shared_ptr<AwkArray>`. Untyped and unbound -> a new array. A Scalar
  parameter (a local) is `AwkFatal("attempt to use scalar parameter `<a>' as
  an array")`; a Scalar global keeps the existing ``attempt to use scalar `x'
  as an array``.

**CallFunction(const Expr& call)**:
1. `functionIndex < 0` -> `AwkFatal("function `<name>' not defined")` (only
   when the call runs: `if (0) foo()` is fine).
2. More arguments than parameters -> `RuntimeWarning("function `<f>' called
   with more arguments than declared")` at every such call; the extra
   arguments are still evaluated (left to right, for their side effects) and
   dropped.
3. `m_frames.size() >= kAwkMaxCallDepth` -> the depth fatal above.
4. Build the frame, arguments left to right: an argument that is a bare
   Variable (global or local; special variables are always scalars) whose
   cell is an Array -> the parameter is an Array sharing it, `passedFrom` the
   variable's name; Untyped -> an Untyped parameter with `binding` = that cell
   and `passedFrom` the name; otherwise -> `Evaluate(arg)` (an element
   `a[1]` is created by that, as gawk) and the parameter is a Scalar holding
   the value. Parameters with no argument are Untyped, unbound.
5. Push the frame (an RAII guard pops it on every exit path: return, fatal,
   `FlowUnwind`, stop), save and restore `m_position`, `Execute(*body)`.
6. The body's flow: Normal or Return -> the call's value is
   `frame.returnValue`. Next / NextFile -> when the running item is BEGIN or
   END: `AwkFatal("`next' cannot be called from a `BEGIN' rule")` (resp.
   `` `nextfile' `` and `` `END' ``, exactly that quoting); otherwise throw
   `FlowUnwind{flow}`. Exit -> throw `FlowUnwind{Flow::Exit}` (`m_exitCode`
   already set by the `exit` statement).

**Execute(Return)**: store the evaluated value (or nothing) in
`m_frames.back()->returnValue`, Flow Return. (awk--interpreter's placeholder
fatal goes.)

**Run**: catch `FlowUnwind` wherever a pattern or an action is evaluated
(BEGIN, main items, END) and treat it as that statement's flow. Set
`m_currentItemKind` before each item.

### `commands/awk/AwkBuiltins.cpp` (new) -- `Interpreter::CallBuiltin`

The definition of `CallBuiltin` moves to this file (a member of
`Interpreter`; add it to `AwkInterpreter.h`'s private helpers as needed). It
dispatches on `call.text`; the argument counts are already right (parser).
`sprintf` and the math functions keep the placeholder fatal (awk--printf-math
replaces it), `close`, `fflush`, `system` too (awk--io). Strings are
`ToString(CONVFMT)` of the arguments, numbers `ToNumber()`; every index is in
bytes (Haisos runs in the C locale).

Add a helper used by `match`, `sub`, `gsub` and `split`:
`std::shared_ptr<const Regex> RegexOperand(const Expr& operand)` -- a Regex
literal node gives its `compiledRegex`; anything else its value's
`ToString(CONVFMT)` through `AwkRegexCache` (warnings through
`RuntimeWarning`, a bad one `AwkFatal("invalid regexp: <error>: /<text>/")`,
as `MatchRegex` does; make `MatchRegex` use it). In these argument positions a
Regex literal is the regex itself, never `$0 ~ /re/`.

- **length** -- no argument: `length($0)`. A bare Variable argument whose
  cell is an Array: `AwkFatal("length: received array argument")` (gawk
  `--posix`; an Untyped one is taken as a scalar). Otherwise the byte length of
  the value's string (`length(12345)` 5, `length(1/3)` 8 -- `0.333333`).
- **substr(s, m[, n])** -- the start and length truncated toward zero; a
  start below 1 is taken as 1 **without shortening the length** (gawk:
  `substr("hello", -1, 3)` is `hel`, `substr("hello", 0, 2)` is `he`); a NaN
  start is 1, a start past the end (or +inf) gives `""`; a length that is NaN,
  0 or negative gives `""`, +inf or past the end means to the end.
- **index(s, t)** -- 1 + the byte offset of the first `t` in `s`, 0 when
  none; an empty `t` gives 1 (`index("", "")` too).
- **split(s, a[, fs])** -- a 4th argument: `AwkFatal("split: fourth argument
  is a gawk extension")` when the call runs. `a` must be a bare Variable whose
  cell is (or becomes, through the rules above) an array, else
  `AwkFatal("split: second argument is not an array")`. The array is
  cleared first (even for an empty `s`); the pieces go to `a[1]..a[n]` as
  `Value::FromInput` (so `a[1] == 1000` for `1e3`); returns n. The separator:
  - no `fs`: the current FS, with the record rules (`SplitAwkFields`, then a
    regex for a longer FS) -- including FS `""`, which gives one piece;
  - `fs` a Regex literal: that regex (so `/ /` is a single-space regex, not
    the blank rule);
  - `fs` any other value, as a string: `" "` -> the blank rule; `""` -> one
    piece per byte (unlike FS `""`); one byte -> that byte literally (`"."`,
    `"|"`, `"\\"`); longer -> a dynamic regex.
  Regex splitting is the one `SplitRecord` uses for a regex FS (awk--records):
  factor it into `void SplitByRegex(std::string_view text, const Regex& regex,
  std::vector<std::string>& fields)` in `AwkRegex.h/.cpp` if it is not a
  function of its own already, and use it from both. Paragraph mode does not
  apply to `split`. An empty `s` gives 0 pieces.
- **sub(r, t[, target]) / gsub(r, t[, target])** -- target default `$0`.
  Read the target's string (an Array cell is ``attempt to use array `x' in a
  scalar context``), substitute, and only when at least one substitution was
  made assign the result through `Assign` (so `$0` is re-split, a field
  rebuilds `$0` with OFS keeping NF, `NF` and fields work, a numeric
  variable becomes a string; with no match the target keeps its value and
  type). A target that is not an lvalue (a constant, `1 + 2`) is worked on as
  a temporary: the count is returned, nothing assigned. Returns the count.
  - The replacement `t`'s string, scanned left to right: `\\` -> one `\`;
    `\&` -> a literal `&`; `&` -> the matched text; a `\` before any other
    byte is kept with that byte (`\q` stays `\q`); a final lone `\` is kept.
  - Matching (both use `Regex::Search` from an offset; `^` never matches past
    offset 0): `sub` replaces the first match only. `gsub`: from position 0,
    find the leftmost-longest match at or after the position; an **empty**
    match starting exactly where the previous match ended is not a
    substitution -- copy one byte and go on; otherwise copy the text before
    the match, append the replacement, and continue at the match's end -- or,
    for an empty match, copy the byte at that position (if any) and continue
    one past it. Stop when the position passes the end; copy what is left.
    (`gsub(/l*/, "X")` on `hello` gives `XhXeXoX`, 4; `gsub(/x*/, "-")` on
    `abc` gives `-a-b-c-`; `gsub(/^/, "X")` on `""` gives `X`.)
- **match(s, r)** -- the leftmost-longest match: `RSTART` = its offset + 1,
  `RLENGTH` = its length (both Numbers, in the special slots); none:
  `RSTART` 0, `RLENGTH` -1. Returns `RSTART`. (`match("foobar", /$/)` is 7,
  RLENGTH 0.)
- **tolower / toupper** -- ASCII letters only; every other byte unchanged.

### `commands/awk/Awk.cpp`

`Help().notes`: drop "functions ... not available yet" for what this task
adds (printf, sprintf, the math functions, getline and redirections remain
listed); add the documented exception `user-defined functions may nest at
most 200 calls deep`.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add
`commands/awk/AwkBuiltins.cpp`.

### Rules that apply (root `CLAUDE.md`)

- `ICurrentProcess` is the only door out of a process: nothing in this task
  reaches files or processes at all; it stays inside the interpreter.
- The builtin rules: the output and messages are gawk `--posix`'s, byte for
  byte (C locale); documented exceptions go in `--help`'s notes (from
  `BuiltinHelpText`, never hand-written) and the CLAUDE.md table.
- Stops promptly: `ThrowIfStopped()` is already checked in every loop; a
  deep recursion is bounded by `kAwkMaxCallDepth`; `gsub`/`split` over a long
  text need no stop checks (they are linear).
- Portable C++17 (Linux, MSVC, WASM): no POSIX headers, no `<regex>`.

## Tests

Move the `AwkRunTest` fixture (and its `SetUp` writing `/abc.txt` =
`a b c\nd e f\n` and `/data.csv`) from `AwkInterpreterTest.cpp` into a new
header `tests/unit/components/Awk.unittests/AwkRunFixture.h`, included by
`AwkInterpreterTest.cpp`, `AwkRegexTest.cpp` (if it uses it) and the new
files. New file `tests/unit/components/Awk.unittests/AwkFunctionsTest.cpp`
(add it to that directory's `CMakeLists.txt`): `class AwkFunctionsTest :
public AwkRunTest {};`, `TEST_F`s on `RunCaptured("awk", {program, operands...}, input)`,
checking `out`, `err` and `status` exactly (status 0 and empty err unless
given). Table-driven within a test is fine.

Notation: a program is the awk text, as it would stand between shell single
quotes (write it as a C++ raw string, `R"(...)"`, so awk's own backslashes
survive); an expected text uses C escapes (`\n` newline, `\\` one backslash,
`\000` a NUL byte).

- `UserFunctions`:
  `function f(a, b) { return a + b } BEGIN { print f(1, 2) }` -> `3\n`;
  `function fact(n) { return n <= 1 ? 1 : n * fact(n - 1) } BEGIN { print fact(10), fact(20), fact(25) }`
  -> `3628800 2432902008176640000 15511210043330986055303168\n`;
  `BEGIN { print g(2) } function g(x) { return x * x }` -> `4\n`;
  `function f(a,  i) { i++; t = i; return } BEGIN { x = f(1); print "[" x "]", length(x); f(); print t }` -> `[] 0\n1\n`;
  `function f(s) { s = s "x"; return s } BEGIN { t = "a"; print f(t), t }` -> `ax a\n`;
  `BEGIN { x = 5; f(); print x } function f() { x = 7 }` -> `7\n`;
  `function f(x, y) { y = x * 2; return y } BEGIN { y = 5; print f(3), y }` -> `6 5\n`;
  `function f(n) { if (n > 0) f(n - 1); print n } BEGIN { f(3) }` -> `0\n1\n2\n3\n`;
  `function f(a, b) { print a "|" b "|" } BEGIN { f(1); f() }` -> `1||\n||\n`;
  `function f(x) { $0 = x; return NF } BEGIN { print f("a b c") }` -> `3\n`;
  `function f(n) { return n == 0 ? 0 : 1 + f(n - 1) } BEGIN { print f(199) }` -> `199\n` (200 active calls).
- `ArraysByReference`:
  `function f(a) { a[2] = 2; delete a[1] } BEGIN { x[1] = 1; f(x); for (k in x) print k, x[k] }` -> `2 2\n`;
  `function f(a) { a[1] = 1 } BEGIN { f(x); print x[1] }` -> `1\n`;
  `function f(a) { a[1] = 1 } function g(  y) { f(y); print y[1] } BEGIN { g() }` -> `1\n`;
  `function g(b) { b["z"] = 9 } function f(a) { g(a) } BEGIN { f(arr); print arr["z"] }` -> `9\n`;
  `function f(a) { split("p q r", a) } BEGIN { f(arr); print arr[3] }` -> `r\n`;
  `function f(a, i) { for (i in a) delete a[i] } BEGIN { x[1]; x[2]; f(x); for (k in x) n++; print n + 0 }` -> `0\n`;
  `function f(x) { } BEGIN { f(a[1]); for (k in a) n++; print n + 0 }` -> `1\n`;
  `function f(a) { } BEGIN { f(x); x[1] = 2; print x[1] }` -> `2\n`;
  `function f(a) { a = 1 } BEGIN { f(x); print "x=" x }` -> `x=\n`.
- `FlowInsideFunctions`:
  `function f() { next } { f(); print "no" } END { print NR }` `/abc.txt` -> `2\n`;
  `function f() { exit 4 } BEGIN { f(); print "no" }` -> out empty, status 4;
  `function f() { nextfile } FNR == 1 { print FILENAME; f() }` `/abc.txt /data.csv` -> `/abc.txt\n/data.csv\n`.
- `FunctionErrors` (err exactly, status 2 unless said):
  `BEGIN { print "x"; foo() }` -> out `x\n`, err ``awk: cmd. line:1: fatal: function `foo' not defined\n``;
  `BEGIN { if (0) foo(); print "ok" }` -> `ok\n`, status 0;
  `function f(a) { a[1] = 1 } BEGIN { x = 1; f(x) }` and `function f(a) { a[1] = 1 } BEGIN { f(1) }`
  -> ``awk: cmd. line:1: fatal: attempt to use scalar parameter `a' as an array\n``;
  `function f(a) { a = 1 } BEGIN { x[1] = 1; f(x) }` and `function f(a) { return a } BEGIN { x[1]; print f(x) }`
  -> ``awk: cmd. line:1: fatal: attempt to use array `a (from x)' in a scalar context\n``;
  `function f(a) { print "[" a "]" } BEGIN { f(x); x[1] = 2 }` -> out `[]\n`,
  err ``awk: cmd. line:1: fatal: attempt to use scalar `x' as an array\n``;
  `function g(b) { b = 1 } function f(a) { g(a); a[1] = 2 } BEGIN { f(arr) }`
  -> ``awk: cmd. line:1: fatal: attempt to use scalar parameter `a' as an array\n``;
  `function f() { next } BEGIN { f() }` -> ``awk: cmd. line:1: fatal: `next' cannot be called from a `BEGIN' rule\n``;
  `function f() { nextfile } BEGIN { f() }` -> ``awk: cmd. line:1: fatal: `nextfile' cannot be called from a `BEGIN' rule\n``;
  `function f(a) { return a } BEGIN { print f(1, y = 5); print y }` -> out `1\n5\n`,
  err ``awk: cmd. line:1: warning: function `f' called with more arguments than declared\n``, status 0;
  `function f(n) { return n == 0 ? 0 : 1 + f(n - 1) } BEGIN { print f(200) }`
  -> `awk: cmd. line:1: fatal: function call nesting too deep (more than 200 calls)\n`.
- `LengthSubstrIndexCase`:
  `BEGIN { print length("abc"), length(12345), length(1/3), length() }` -> `3 5 8 0\n`;
  input `hello world\n`, `{ print length, length(), length $1, length($2) }` -> `11 11 11hello 5\n`;
  `BEGIN { x[1]; print length(x) }` and `function f(a) { return length(a) } BEGIN { x[1]; print f(x) }`
  -> `awk: cmd. line:1: fatal: length: received array argument\n`, 2;
  `BEGIN { s = "hello"; print substr(s, 2), substr(s, 2, 3), substr(s, 0), "[" substr(s, -1, 3) "]", substr(s, 1.5), substr(s, 1.5, 2.3), "[" substr(s, 10) "]", "[" substr(s, 3, 0) "]", "[" substr(s, 3, -1) "]", substr(12345, 2, 2) }`
  -> `ello ell hello [hel] hello he [] [] [] 23\n`;
  `BEGIN { s = "hello"; print substr(s, 2.5, 1), substr(s, 3.5, 1.5), substr(s, 2.5, 2.5), substr(s, -2, 4), substr(s, 1, 1e30), substr(s, -1e30, 1e30), substr(s, "x", 2), "[" substr(s, 1, 0.5) "]" }`
  -> `e l el hell hello hello he []\n`;
  `BEGIN { print index("hello", "ll"), index("hello", ""), index("", "a"), index(12345, 34), index("abc", "abcd") }` -> `3 1 0 3 0\n`;
  `BEGIN { print toupper("abc\344x1"), tolower("ABC-Z"), toupper(1.5) }` -> `ABC\344X1 abc-z 1.5\n` (`\344` is one byte, kept).
- `Split` (with `function show(n, a,  i, s) { s = n ":"; for (i = 1; i <= n; i++) s = s "[" a[i] "]"; return s }` prepended to each program):
  `BEGIN { print show(split("  a b\tc\n", a), a); print show(split("a:b::c", a, ":"), a); print show(split("a.b|c", a, "."), a); print show(split("abc", a, ""), a); print show(split("", a), a); print show(split("a1b22c", a, /[0-9]+/), a); print show(split(" a b ", a, / /), a); print show(split(" a  b ", a, "[ ]"), a); print show(split("abc", a, "b*"), a); print show(split("aXbxc", a, "x"), a) }`
  -> `3:[a][b][c]\n4:[a][b][][c]\n2:[a][b|c]\n3:[a][b][c]\n0:\n3:[a][b][c]\n4:[][a][b][]\n5:[][a][][b][]\n2:[a][c]\n2:[aXb][c]\n`;
  `BEGIN { FS = ","; print show(split("x,y z", a), a); FS = ""; print show(split("abc", a), a); n = split("1e3 2", b, " "); print (b[1] == 1000), (b[2] < 10); a["old"] = 1; split("q", a); print ("old" in a) }`
  -> `2:[x][y z]\n1:[abc]\n1 1\n0\n`;
  `BEGIN { x = 1; split("a b", x) }` -> `awk: cmd. line:1: fatal: split: second argument is not an array\n`, 2;
  `BEGIN { split("a", x, " ", y) }` -> `awk: cmd. line:1: fatal: split: fourth argument is a gawk extension\n`, 2.
- `SubAndGsub`:
  `BEGIN { r[1] = "&"; r[2] = "\\&"; r[3] = "\\\\&"; r[4] = "\\\\\\&"; r[5] = "\\q"; r[6] = "\\\\q"; r[7] = "a\\"; r[8] = "x&y&"; for (i = 1; i <= 8; i++) { s = "abc"; sub(/b/, r[i], s); print i, s } }`
  -> `1 abc\n2 a&c\n3 a\\bc\n4 a\\&c\n5 a\\qc\n6 a\\qc\n7 aa\\c\n8 axbybc\n`;
  `BEGIN { s = "aaa"; print gsub(/a/, "-&-", s), s; s = "abc"; print gsub(/x*/, "-", s), s; s = "hello"; print gsub(/l*/, "X", s), s; s = "abab"; print gsub(/^a/, "X", s), s; s = ""; print gsub(/^/, "X", s), s; s = "abc"; print gsub(/$/, "X", s), s; s = "a.b.c"; print gsub(".", "-", s), s; s = "a.b.c"; print gsub("\\.", "-", s), s; s = "aaa"; print sub(/a/, "b", s), s }`
  -> `3 -a--a--a-\n4 -a-b-c-\n4 XhXeXoX\n1 Xbab\n1 X\n1 abcX\n5 -----\n2 a-b-c\n1 baa\n`;
  input `one two three\n`, `{ print gsub(/o/, "0"), $0, NF, $2; $0 = "a b c"; sub(/b/, "x y", $2); print $0, NF; $2 = "q"; print gsub(/ /, "_"), $0, NF }`
  -> `2 0ne tw0 three 3 tw0\na x y c 3\n2 a_q_c 1\n`;
  `BEGIN { x = 15; print sub(/5/, "6", x), x, x + 1; y = 0.1 + 0.2; print gsub(/z/, "", y), (y == 0.3); z = 0.1 + 0.2; print gsub(/3/, "3", z), (z == 0.3); a["k"] = "xx"; print gsub(/x/, "y", a["k"]), a["k"]; print sub(/a/, "b", "abc"); n = sub(/^/, "X", u); print n, u }`
  -> `1 16 17\n0 0\n1 1\n2 yy\n1\n1 X\n`;
  `BEGIN { x[1]; sub(/a/, "b", x) }` -> ``awk: cmd. line:1: fatal: attempt to use array `x' in a scalar context\n``, 2;
  `BEGIN { s = "a+b"; gsub("+", "-", s) }` -> `awk: cmd. line:1: fatal: invalid regexp: Invalid preceding regular expression: /+/\n`, 2.
- `Match`:
  `BEGIN { print match("foobar", /o+/), RSTART, RLENGTH; print match("foobar", "z"), RSTART, RLENGTH; print match("foobar", /x*/), RSTART, RLENGTH; print match("foobar", /$/), RSTART, RLENGTH; print match(12345, 3), RSTART, RLENGTH; r = "b.r"; print match("foobar", r), RSTART, RLENGTH; print match("xabcabc", /(abc)+/), RSTART, RLENGTH }`
  -> `2 2 2\n0 0 -1\n1 1 0\n7 7 0\n3 3 1\n4 4 3\n2 2 6\n`.
- `ArgumentCountErrors` (status 1, out empty, err exactly the two lines):
  `BEGIN{print substr()}` -> caret column 19, `0 is invalid as number of arguments for substr`;
  `BEGIN{print substr("abc")}` -> column 24, `1 is invalid as number of arguments for substr`;
  `BEGIN{print length(1,2)}` -> column 22, `2 is invalid as number of arguments for length`;
  `BEGIN{split("a",x," ",y,z)}` -> column 25, `5 is invalid as number of arguments for split`;
  `BEGIN{print atan2(1)}` -> column 19, `1 is invalid as number of arguments for atan2`;
  `BEGIN{print "x"; print match("a",/a/,m)}` -> column 38, `match: third argument is a gawk extension`;
  `BEGIN{print "x"; close("a","b")}` -> column 30, `close: second argument is a gawk extension`;
  `BEGIN{x = substr("abc", 1,` newline `2, 3)}` -> `awk: cmd. line:2: 2, 3)}\nawk: cmd. line:2:     ^ 4 is invalid as number of arguments for substr\n`.
  (Each line pair is `awk: cmd. line:1: <program line>\nawk: cmd. line:1: <column spaces>^ <message>\n`.)
- `NotYetAvailable` (keep awk--interpreter's test, changed): `BEGIN { print sprintf("%d", 1) }`
  -> ``awk: cmd. line:1: fatal: function `sprintf' is not implemented yet\n``, 2
  (awk--printf-math changes it).

Update awk--interpreter's `AwkRunTest.NotYetAvailable` cases that called
user functions or the string built-ins (they now run).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
./output/linux/Awk.unittests --gtest_filter='AwkFunctionsTest.*:AwkRunTest.*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/awk/CLAUDE.md`: a "Functions" section -- local resolution
  (`localSlot`, `functionIndex`), frames and the binding chain (the scalar and
  array rules with their error texts), `FlowUnwind`, the depth limit,
  `RuntimeWarning`; a "Built-in functions" section -- the argument-count
  table and its errors, `RegexOperand`, `split`'s separator rules (and how
  they differ from FS), the `sub`/`gsub` replacement and empty-match rules,
  `substr`'s gawk rule for a start below 1; `AwkBuiltins.cpp` in the file
  list; the depth limit under "Documented exceptions".
- `src/components/BuiltinCommands/CLAUDE.md` and root `CLAUDE.md`: the awk
  rows gain user functions and the string functions (and the 200-call
  exception); printf, math, getline and redirections still listed as to come.

## Acceptance

- [ ] Every test above passes with the exact bytes; no expectation was
  changed to mawk's.
- [ ] Argument counts are refused at parse time with gawk's messages and
  caret at the `)`.
- [ ] Arrays by reference, untyped variables bound by name through any chain
  of calls, scalars by value, locals; every error text as specified.
- [ ] `next`/`nextfile`/`exit` inside functions act on the calling rule;
  frames are popped on every exit path.
- [ ] `split`, `sub`, `gsub`, `match` use `RegexOperand` (literal or cached
  dynamic regex); `split`'s separator rules and the `gsub` empty-match rule
  as specified.
- [ ] printf/sprintf/math still fail with their placeholder message; nothing
  reaches files or processes.
- [ ] New source and test file in their CMakeLists; all unit tests green;
  the three CLAUDE.md files updated.

## Out of scope

- `printf`, `sprintf`, `sin cos atan2 exp log sqrt int rand srand`
  (awk--printf-math).
- `getline`, output redirections, `close`, `fflush`, `system`, `ENVIRON`
  (awk--io).
- gawk extensions: indirect calls (`@f()`), `length(array)`, `split`'s
  fourth argument, `match`'s third, `gensub`, `asort`, `IGNORECASE`.
