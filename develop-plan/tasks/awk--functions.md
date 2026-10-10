# Task awk--functions: user-defined functions and awk's string built-ins

- Rock: awk
- Depends on: awk--records
- Size: ~950 changed lines in ~11 files
- Plan checked against: develop @ 8ca0cbc
- PR title: Add awk user-defined functions and the string built-in functions

The task as first listed (string built-ins, math, user functions, printf and
sprintf) came to ~1500 lines; printf/sprintf and the math functions are split
off into awk--printf-math, which follows this one. This task keeps what
changes the interpreter's structure (functions, their calls, the argument-count
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

**Clean room** (root `CLAUDE.md`, "Clean-room rule", above every other
rule): all code is written from scratch. Never read, copy, port, translate
or paraphrase another program's source (gawk, mawk, the one true awk,
busybox, ...), whatever its licence, and never name another program's
internal functions, variables, types or flags -- not in code, not in
comments. Behaviour is matched from documentation (POSIX awk, the gawk
manual, man pages) and from the observed output of real awks. Everything
below describes behaviour; every name in it is this project's own.

**Write in pieces**: never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step.

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` and every
file it lists -- above all `AwkAst.h`, `AwkParser.h/.cpp`,
`AwkInterpreter.h/.cpp`, `AwkValue.h`, `AwkFields.h/.cpp`, `AwkRegex.h`; the
tests in `tests/unit/components/Awk.unittests/` (`AwkInterpreterTest.cpp`
holds the `AwkRunTest` fixture); root `CLAUDE.md` ("Clean-room rule",
"Builtin Commands", "Security"); `src/components/BuiltinCommands/CLAUDE.md`.

What is on develop already (#76-#81, all in
`src/components/BuiltinCommands/commands/awk/`, namespace `Haisos::Awk`):
- The tree and the parser (#77, #78): `Expr` (`kind`, `op`, `position`,
  `number`, `text`, `operands`, `hasParentheses`, `parenthesized`,
  `getlineForm`, `target`; the interpreter's `mutable` `slot` and
  `compiledRegex`), `ExprKind::Call` (a user function: `text` the name,
  `operands` the arguments), `ExprKind::BuiltinCall` (`text` the built-in's
  name; a bare `length` has `hasParentheses == false` and no operands),
  `Stmt`/`StmtKind` (`Return` with `expr` null or the value; `ForIn`'s
  `name`/`arrayName` resolved into `mutable` `slot`/`arraySlot`, `Delete`'s
  `name` into `arraySlot`), `FunctionDefinition` (`name`, `parameters`,
  `body`, `position`), `Program::functions`, `Item` (`kind`, `pattern`,
  `rangeEnd`, `action`, `functionIndex`), `ItemKind { Begin, End, Main,
  Function }`. `Parser` keeps one token of lookahead (`m_token`,
  `Advance()`); `[[noreturn]] void Fail(const Token&, const std::string&
  message)` throws `AwkSyntaxError` with that message at the token's line
  and column (for an ordinary token such as `)`); `ParseCallArguments()`
  returns with `m_token` on the closing `)`, not consumed -- `ParsePrimary`'s
  `FuncName` and `Builtin` cases then `Advance()` past it. Already refused
  when parsing: a defined function's name used as a variable or array,
  duplicate parameters, the function's name or a special variable as a
  parameter, a function defined twice, `return` outside a function,
  redefining `length`, `next`/`nextfile` written directly in BEGIN/END
  (through a function it is this task's run-time check).
- Values and fields (#79): `Value` (`FromNumber`, `FromString`,
  `FromInput`, `GetType`, `ToNumber`, `ToString(format)`, `ToBoolean`),
  `AwkArray` (`Find`, `GetOrCreate`, `Contains`, `Remove`, `Clear`, `Size`,
  `Keys`), `AwkIntegerOf`, `SplitAwkFields(text, fs, fields)` (FS `" "`,
  `""`, one byte; false for a longer fs), `FieldStore` (`SetRecord(record,
  fs, paragraphMode)`, `Record`, `Field`, `SetField`, `NF`, `SetNF`),
  `AwkFatal(message, withLocation = true)`.
- The interpreter (#80, `AwkInterpreter.h/.cpp`): `Variable { Kind
  {Untyped, Scalar, Array}; scalar; array (shared_ptr<AwkArray>) }`;
  `SpecialSlot` (`kSlotFS`, `kSlotRS`, `kSlotCONVFMT`, `kSlotRSTART`,
  `kSlotRLENGTH`, `kSlotARGV`, `kSlotENVIRON`, `kSlotNF`, ... -- NF's
  `Variable` is unused and stays Untyped, NF lives in the `FieldStore`);
  `Flow { Normal, Break, Continue, Next, NextFile, Exit, Return }`.
  `Prepare` with `SlotOf`, `ResolveExpr`, `ResolveStmt` (today every name,
  inside function bodies too, gets a global slot); `ValueOf(const Expr&)`;
  `RunStatement(const Stmt&)`; `RunBeginItems`, `RunMainLoop`,
  `RunMainItems`, `RunEndItems`, `MatchesPattern`; `GlobalVariable(slot)`,
  `ScalarRef(const Expr&)`, `ScalarRef(slot, name)` (``attempt to use array
  `x' in a scalar context``), `ArrayRef(slot, name)` (``attempt to use
  scalar `x' as an array``), `Assign(lvalue, value)` through `PlaceOf`/
  `ReadPlace`/`WritePlace` (a Variable through `AssignSlot`, which sends NF
  to `SetNF`; an Index; a Field through `FieldStore::SetField`),
  `Subscript`, `SpecialString(slot)`, `Output`, `ThrowIfStopped`,
  `ReportFatal`, `LocatedPrefix`; members `m_globals`
  (`std::vector<Variable>`), `m_globalSlots`, `m_position`, `m_exitCode`,
  `m_exitFromBegin`. The placeholders this task replaces: `CallBuiltin(const
  Expr&)` and `CallFunction(const Expr&)` (both `AwkFatal("function
  `<name>' is not implemented yet")`) and `RunStatement`'s `Return` case
  (`AwkFatal("return is not implemented yet")`). The fatal format: `awk:
  <src>:<line>: [(FILENAME=<f> FNR=<n>) ]fatal: <message>\n`, status 2;
  without a location `awk: fatal: <message>\n`.
- Regexes (#81, `AwkRegex.h/.cpp` and the interpreter):
  `TranslateAwkRegex`, `AwkRegexAsWritten`, `AwkRegexCache::Get(awkRegex,
  warnings, error)` (null with `error` set when it does not compile;
  `warnings` only on a compile, not a hit), `SplitByRegex(text, regex,
  fields)` (already a function of its own: `split` reuses it as is);
  `Interpreter::MatchRegexLiteral(regex, text)` (a literal's
  `compiledRegex`), `MatchRegex(regex, text)` (a non-parenthesized Regex
  literal itself; anything else -- a parenthesized literal too -- its
  value's `ToString(CONVFMT)` through `m_regexCache`, a bad one
  `AwkFatal("invalid regexp: <error>: /<AwkRegexAsWritten(text)>/")`),
  `RegexWarnings(messages, atRuntime)` (each message once a run),
  `RuntimeWarning(message)` (already there: `LocatedPrefix()` + `warning:
  <message>`), `SplitRecord` (the FS rules and paragraph mode),
  `CompileLiteralRegexes` (every `/re/`, in function bodies too, compiled
  before BEGIN).
- `Regex::Search(text, start, RegexMatch&, flags)` -- leftmost-longest; `^`
  matches only at offset 0 of the text (never at `start > 0`), `$` only at
  its end; `RegexMatch::groups[0]` is the match (begin, end).
- `ParsePrintfSpec` (`BuiltinPrintf.h`) is not used by this task (CONVFMT
  conversions go through the existing `FormatAwkNumber`, unchanged); its
  `q` length modifier (#53) is awk--printf-math's concern.

Every expected text below was produced by gawk 5.2.1 `--posix` with
`LC_ALL=C` (with `gawk:` replaced by `awk:`), re-run at this re-check. The
task container has only mawk, whose behaviour differs in many of these
cases: never change an expectation to mawk's -- the behaviour described
here is the specification.

## Changes

### `commands/awk/AwkParser.cpp` -- argument counts of the built-ins

In `ParsePrimary`'s `Builtin` case, after `ParseCallArguments()` has
returned (`m_token` on the closing `)`, not yet consumed), check the number
of arguments and, when it is wrong, `Fail(m_token, <message>)` -- at that
`)` token, with the message below instead of `syntax error`:

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
line of its `)`. A small table (name, minimum, maximum, and the special
message for match's 3rd and close's 2nd argument) is enough.

### `commands/awk/AwkAst.h`

Resolution fields only (all `mutable`, filled by `Prepare`; nothing the
parser sets changes; the dump ignores them):
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

struct Variable {
    ...                               // kind, scalar, array: unchanged
    // A parameter given an untyped variable by name: the caller's variable
    // (a global, or a local of the calling function, itself possibly bound).
    Variable* binding = nullptr;
    // A parameter given an array (or bound): the name written at the call,
    // for "attempt to use array `a (from x)' in a scalar context".
    std::string passedFrom;
};

// One active call of a user function.
struct ActiveCall {
    const FunctionDefinition* function = nullptr;
    std::vector<Variable> locals;     // one per parameter, sized once at the call
    Value returnValue;                // Uninitialized unless `return expr` ran
};

// Unwinds a `next`, `nextfile` or `exit` run inside a function out of the
// expression that called it, to the item loops of Run.
struct FlowUnwind { Flow flow; };
```

Members: `std::vector<std::unique_ptr<ActiveCall>> m_calls;` (they never move,
so a `Variable*` binding into one stays valid while that call is
active), and the kind of the item being run (`ItemKind m_currentItemKind`,
for the BEGIN/END check). `m_globals` becomes a `std::deque<Variable>`
(indexing unchanged): `SlotOf` can still add a global while a function is
active (a `var=value` operand naming a new variable, later a getline), and
a deque's `emplace_back` never moves the existing elements, so a binding to
a global stays valid. `RuntimeWarning` and `RegexWarnings` exist already --
use them.

**Prepare**: walk each function body with its parameter list: a Variable,
Index or In whose name is a parameter gets `localSlot` (and no global slot
is made for it there); ForIn and Delete likewise through `Stmt::localSlot` /
`localArraySlot`. Every Call (in items and function bodies) gets
`functionIndex` from a name -> index map of `Program::functions`. Names
outside functions, and non-parameter names inside them, stay globals
(`ResolveExpr`/`ResolveStmt` take the current function's parameters, or
none).

**Variables**: everywhere a variable is reached (`ValueOf` of Variable/
Index/In, `PlaceOf`/`ReadPlace`/`WritePlace`/`AssignSlot`, ForIn, Delete,
and the built-ins below), the node's `Variable` is the current call's
`locals[localSlot]` when `localSlot >= 0`, else `GlobalVariable(slot)`.
Give `ScalarRef` and `ArrayRef` overloads taking that `Variable&` and the
name to report; then:
- **As a scalar** (read or assigned): Untyped -> Scalar; when it is bound,
  every variable of its binding chain that is still Untyped becomes Scalar
  too (values unchanged: `function f(a) { a = 1 } BEGIN { f(x); print "x=" x }`
  prints `x=`, and `x` can no longer be an array). An Array is
  `AwkFatal("attempt to use array `<name>' in a scalar context")`, where
  `<name>` is `a (from x)` for a parameter whose `passedFrom` is `x`.
- **As an array**: Untyped and bound -> follow the chain to its last
  variable; if that is Untyped, create the array there; if it is an Array,
  take it; if it is Scalar, fail as below. Every variable of the chain (and
  this one) then shares that `shared_ptr<AwkArray>` (kind Array). Untyped
  and unbound -> a new array. A Scalar parameter (a local) is
  `AwkFatal("attempt to use scalar parameter `<a>' as an array")`; a
  Scalar global keeps the existing ``attempt to use scalar `x' as an
  array``.

**CallFunction(const Expr& call)**:
1. `functionIndex < 0` -> `AwkFatal("function `<name>' not defined")` (only
   when the call runs: `if (0) foo()` is fine).
2. More arguments than parameters -> `RuntimeWarning("function `<f>' called
   with more arguments than declared")` at every such call that runs; the
   extra arguments are still evaluated (left to right, for their side
   effects) and dropped.
3. `m_calls.size() >= kAwkMaxCallDepth` -> the depth fatal above.
4. Build the `ActiveCall`, arguments left to right: an argument that is a bare,
   non-parenthesized Variable (global or local) whose variable is an Array
   -> the parameter is an Array sharing it, `passedFrom` the variable's
   name; Untyped -> an Untyped parameter with `binding` = that variable and
   `passedFrom` the name; otherwise -> `ValueOf(arg)` (an element `a[1]` is
   created by that, as gawk) and the parameter is a Scalar holding the
   value. The special variables are always passed by value -- NF too, whose
   own `Variable` is Untyped (`kSlotNF`: take `ValueOf`, never bind it).
   Parameters with no argument are Untyped, unbound.
5. Push it (an RAII guard pops it on every exit path: return, fatal,
   `FlowUnwind`, stop), save `m_position` and restore it after the call,
   `RunStatement(*body)`.
6. The body's flow: Normal or Return -> the call's value is
   its `returnValue`. Next / NextFile -> when the running item is BEGIN or
   END: `AwkFatal("`next' cannot be called from a `BEGIN' rule")` (resp.
   `` `nextfile' `` and `` `END' ``, exactly that quoting); otherwise throw
   `FlowUnwind{flow}`. Exit -> throw `FlowUnwind{Flow::Exit}` (`m_exitCode`
   already set by the `exit` statement).

**RunStatement(Return)**: store the evaluated value (or nothing) in
`m_calls.back()->returnValue`, `Flow::Return` (the placeholder fatal
goes). Return already passes through the loops and blocks as any non-normal
flow.

**Run**: catch `FlowUnwind` wherever a pattern or an action is evaluated
-- `RunBeginItems`, `RunMainItems` (around `MatchesPattern` too: `function
f() { next } f() { print "no" }` skips the record) and `RunEndItems` -- and
treat it as that statement's flow (an Exit in BEGIN sets `m_exitFromBegin`
as a plain `exit` does). Set `m_currentItemKind` before each item.

### `commands/awk/AwkBuiltins.cpp` (new) -- `Interpreter::CallBuiltin`

The definition of `CallBuiltin` moves to this file (a member of
`Interpreter`; add the private helpers it needs to `AwkInterpreter.h`). It
dispatches on `call.text`; the argument counts are already right (parser).
`sprintf` and the math functions keep the placeholder fatal ``function
`<name>' is not implemented yet`` (awk--printf-math replaces it), `close`,
`fflush`, `system` too (awk--io). Strings are `ToString(CONVFMT)` of the
arguments, numbers `ToNumber()`; every index is in bytes (Haisos runs in the
C locale).

Add a helper used by `match`, `sub`, `gsub` and `split`:
`std::shared_ptr<const Regex> RegexOperand(const Expr& operand)` -- a
non-parenthesized Regex literal gives its `compiledRegex`; anything else (a
parenthesized literal included, as `MatchRegex` takes it) its value's
`ToString(CONVFMT)` through `m_regexCache` (warnings through
`RegexWarnings(warnings, true)`, a bad one `AwkFatal("invalid regexp:
<error>: /<AwkRegexAsWritten(text)>/")`). Make `MatchRegex` use it, so the
dynamic-regex code exists once. In these argument positions a Regex literal
is the regex itself, never `$0 ~ /re/`.

- **length** -- no argument: `length($0)`. A bare Variable argument whose
  variable is an Array: `AwkFatal("length: received array argument")` (gawk
  `--posix`); an Untyped one is taken as a scalar (and becomes Scalar).
  Otherwise the byte length of the value's string (`length(12345)` 5,
  `length(1/3)` 8 -- `0.333333`).
- **substr(s, m[, n])** -- the start and length truncated toward zero; a
  start below 1 is taken as 1 **without shortening the length** (gawk:
  `substr("hello", -1, 3)` is `hel`, `substr("hello", 0, 2)` is `he`); a NaN
  start is 1, a start past the end (or +inf) gives `""`; a length that is NaN,
  0 or negative gives `""`, +inf or past the end means to the end.
- **index(s, t)** -- 1 + the byte offset of the first `t` in `s`, 0 when
  none; an empty `t` gives 1 (`index("", "")` too).
- **split(s, a[, fs])** -- a 4th argument: `AwkFatal("split: fourth argument
  is a gawk extension")` when the call runs. `a` must be a bare Variable whose
  variable is (or becomes, through the rules above) an array, else
  `AwkFatal("split: second argument is not an array")` -- a constant, an
  element `q[1]`, a Scalar global or special variable, a Scalar parameter
  all give that message. The array is cleared first (even for an empty
  `s`); the pieces go to `a[1]..a[n]` as `Value::FromInput` (so `a[1] ==
  1000` for `1e3`); returns n. The separator:
  - no `fs`: the current FS, with the record rules (`SplitAwkFields`, then
    `SplitByRegex` for a longer FS) -- including FS `""`, which gives one
    piece;
  - `fs` a non-parenthesized Regex literal: that regex (so `/ /` is a
    single-space regex, not the blank rule);
  - `fs` any other value, as a string: `" "` -> the blank rule; `""` -> one
    piece per byte (unlike FS `""`); one byte -> that byte literally (`"."`,
    `"|"`, `"\\"`); longer -> a dynamic regex (`RegexOperand`).
  Regex splitting is `SplitByRegex` (`AwkRegex.h`), the one `SplitRecord`
  uses. Paragraph mode does not apply to `split`. An empty `s` gives 0
  pieces.
- **sub(r, t[, target]) / gsub(r, t[, target])** -- target default `$0`.
  Read the target's string (an Array variable is ``attempt to use array
  `x' in a scalar context``), substitute, and only when at least one
  substitution was made assign the result through `Assign` (so `$0` is
  re-split, a field rebuilds `$0` with OFS keeping NF, `NF` and fields
  work, a numeric variable becomes a string; with no match the target keeps
  its value and type). A target that is not an lvalue (a constant, `1 + 2`)
  is worked on as a temporary: the count is returned, nothing assigned.
  Returns the count.
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
  `RLENGTH` = its length (both Numbers, in the special slots, made Scalar);
  none: `RSTART` 0, `RLENGTH` -1. Returns `RSTART`. (`match("foobar", /$/)`
  is 7, RLENGTH 0.)
- **tolower / toupper** -- ASCII letters only; every other byte unchanged.

### Fixes carried over from awk--records (#81 review)

- **A regex FS is checked when it is assigned** (`AwkInterpreter.cpp`,
  today only `SplitRecord` compiles it, so `BEGIN { FS = "a(" } { print
  "hi" }` prints `hi`). Add `void CheckFieldSeparator(bool withLocation)`:
  when `SpecialString(kSlotFS)` has two or more bytes, get it from
  `m_regexCache` (which keeps it compiled for `SplitRecord`); a failure is
  `AwkFatal("invalid regexp: <error>: /<AwkRegexAsWritten(fs)>/",
  withLocation)` -- gawk's own message, nothing internal in it -- and the
  escape warnings are given there and then: with a location through
  `RegexWarnings(warnings, true)`; without one as `awk: warning:
  <message>\n` (still once per message per run, `m_regexWarningsGiven`).
  Call it after every assignment to FS: in `AssignSlot` when `slot ==
  kSlotFS` (a program assignment, `sub`/`gsub` on FS, a for-in variable --
  with the location), and location-less for `-F` (`ApplyPreAssignment`),
  `-v FS=...` and a `FS=...` operand (both through `AssignName`: give
  `AssignSlot` a `bool located = true` parameter that `AssignName` sets to
  false). gawk, verified: the fatal comes at the assignment, before any
  input is read, also in a BEGIN-only program, status 2.
- **`$0 = v` takes the FS and RS of that moment** (`AwkFields.cpp` ~140:
  `SetField(0, ...)` re-splits with the FS and paragraph flag saved with the
  record that was read). gawk, verified: `{ FS = ":"; $0 = $0; print $1 }`
  on `a:b c` prints `a` (the classic re-split idiom), and after `RS = ""`
  set mid-record, `$0 = "p\nq r"` with FS `x` has NF 2 (and NF 1 the other
  way round). In `Interpreter::WritePlace`, a Field place with index 0 calls
  `m_fields.SetRecord(value.ToString(CONVFMT), SpecialString(kSlotFS),
  SpecialString(kSlotRS).empty())`; `FieldStore::SetField`'s index-0 branch
  stays for the store's own users, its comment saying the interpreter sets
  `$0` through `SetRecord`. `sub`/`gsub` on `$0` go through `Assign`, so
  they follow.
- **`AwkRunTest.NotYetAvailable`** (`AwkInterpreterTest.cpp` ~610): drop the
  leftover first `RunCaptured(... length("ab") ...)` whose result is unused,
  and turn the `length` case into the `sprintf` one (see Tests).

### `commands/awk/Awk.cpp`

`Version()` 1.2.0 -> 1.3.0. `Help().notes`: the last note becomes
"printf, sprintf, the math functions, getline and output redirections are
not available yet."; add the documented exceptions "User-defined functions
may nest at most 200 calls deep; gawk has no fixed limit." and "`split(s,
a[i])' is refused (second argument is not an array); gawk makes a[i] a
sub-array, an extension."

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add
`commands/awk/AwkBuiltins.cpp` (after `AwkAst.cpp`).

### Rules that apply (root `CLAUDE.md`)

- Clean room: everything above is behaviour, verified on gawk; write the
  code from it, never from another awk's source.
- `ICurrentProcess` is the only door out of a process: nothing in this task
  reaches files or processes at all; it stays inside the interpreter.
- The builtin rules: the output and messages are gawk `--posix`'s, byte for
  byte (C locale); documented exceptions go in `--help`'s notes (from
  `BuiltinHelpText`, never hand-written) and the CLAUDE.md tables; bump the
  version (above).
- Stops promptly: `ThrowIfStopped()` is already checked in every loop; a
  deep recursion is bounded by `kAwkMaxCallDepth`; `gsub`/`split` over a long
  text need no stop checks (they are linear).
- Portable C++17 (Linux, MSVC, WASM): no POSIX headers, no `<regex>`.

## Tests

Move the `AwkRunTest` fixture -- its `SetUp` (writing `/abc.txt` =
`a b c\nd e f\n`, `/data.csv` and `/para.txt`) and `StartAwk` -- from
`AwkInterpreterTest.cpp` into a new header
`tests/unit/components/Awk.unittests/AwkRunFixture.h`, included by
`AwkInterpreterTest.cpp` and the new file (`AwkRegexTest.cpp` does not use
it). New file `tests/unit/components/Awk.unittests/AwkFunctionsTest.cpp`
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
  input `a b c\n`, `function f(a) { a = 9; return a "|" NF } { print f(NF), NF, f(NR), f(FS) "." }` -> `9|3 3 9|3 9|3.\n` (specials, NF included, by value);
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
  input `a b c\n`, `function f() { next } f() { print "no" } END { print "end", NR }` -> `end 1\n` (from a pattern);
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
  `function f() { next } END { f() }` -> ``awk: cmd. line:1: fatal: `next' cannot be called from a `END' rule\n``;
  `function f(a) { return a } BEGIN { print f(1, y = 5); print y }` -> out `1\n5\n`,
  err ``awk: cmd. line:1: warning: function `f' called with more arguments than declared\n``, status 0;
  `function f(a) { } BEGIN { for (i = 0; i < 2; i++) f(1, 2); print "ok" }` -> out `ok\n`, that warning twice, status 0;
  `function f(n) { return n == 0 ? 0 : 1 + f(n - 1) } BEGIN { print f(200) }`
  -> `awk: cmd. line:1: fatal: function call nesting too deep (more than 200 calls)\n` (Haisos's limit; gawk prints 200).
- `LengthSubstrIndexCase`:
  `BEGIN { print length("abc"), length(12345), length(1/3), length() }` -> `3 5 8 0\n`;
  input `hello world\n`, `{ print length, length(), length $1, length($2) }` -> `11 11 11hello 5\n`;
  `BEGIN { x[1]; print length(x) }` and `function f(a) { return length(a) } BEGIN { x[1]; print f(x) }`
  -> `awk: cmd. line:1: fatal: length: received array argument\n`, 2;
  `BEGIN { print length(x); x[1] = 1 }` -> out `0\n`, err ``awk: cmd. line:1: fatal: attempt to use scalar `x' as an array\n``, 2;
  `BEGIN { s = "hello"; print substr(s, 2), substr(s, 2, 3), substr(s, 0), "[" substr(s, -1, 3) "]", substr(s, 1.5), substr(s, 1.5, 2.3), "[" substr(s, 10) "]", "[" substr(s, 3, 0) "]", "[" substr(s, 3, -1) "]", substr(12345, 2, 2) }`
  -> `ello ell hello [hel] hello he [] [] [] 23\n`;
  `BEGIN { s = "hello"; print substr(s, 2.5, 1), substr(s, 3.5, 1.5), substr(s, 2.5, 2.5), substr(s, -2, 4), substr(s, 1, 1e30), substr(s, -1e30, 1e30), substr(s, "x", 2), "[" substr(s, 1, 0.5) "]" }`
  -> `e l el hell hello hello he []\n`;
  `BEGIN { s = "hello"; print substr(s, 2.7, 1), substr(s, 2, 1.7), substr(s, -0.5, 3), "[" substr(s, 2, "nan") "]", "[" substr(s, "+inf") "]", substr(s, 2, "+inf") }`
  -> `e e hel [] [] ello\n` (truncation, not rounding);
  `BEGIN { print index("hello", "ll"), index("hello", ""), index("", "a"), index(12345, 34), index("abc", "abcd") }` -> `3 1 0 3 0\n`;
  `BEGIN { print toupper("abc\344x1"), tolower("ABC-Z"), toupper(1.5) }` -> `ABC\344X1 abc-z 1.5\n` (`\344` is one byte, kept).
- `Split` (with `function show(n, a,  i, s) { s = n ":"; for (i = 1; i <= n; i++) s = s "[" a[i] "]"; return s }` prepended to each program):
  `BEGIN { print show(split("  a b\tc\n", a), a); print show(split("a:b::c", a, ":"), a); print show(split("a.b|c", a, "."), a); print show(split("abc", a, ""), a); print show(split("", a), a); print show(split("a1b22c", a, /[0-9]+/), a); print show(split(" a b ", a, / /), a); print show(split(" a  b ", a, "[ ]"), a); print show(split("abc", a, "b*"), a); print show(split("aXbxc", a, "x"), a) }`
  -> `3:[a][b][c]\n4:[a][b][][c]\n2:[a][b|c]\n3:[a][b][c]\n0:\n3:[a][b][c]\n4:[][a][b][]\n5:[][a][][b][]\n2:[a][c]\n2:[aXb][c]\n`;
  `BEGIN { FS = ","; print show(split("x,y z", a), a); FS = ""; print show(split("abc", a), a); n = split("1e3 2", b, " "); print (b[1] == 1000), (b[2] < 10); a["old"] = 1; split("q", a); print ("old" in a) }`
  -> `2:[x][y z]\n1:[abc]\n1 1\n0\n`;
  `BEGIN { x = 1; split("a b", x) }`, `BEGIN { split("a b", 3) }`, `BEGIN { split("a b", FS) }` and
  `function f(p) { p = 1; split("a", p) } BEGIN { f() }` -> `awk: cmd. line:1: fatal: split: second argument is not an array\n`, 2;
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
  input `b\n`, `{ s = "a0b1"; n = sub((/b/), "X", s); print n, s; print split("a1b0c", q, (/z/)), q[1]; print match("x1", (/b/)) }`
  -> `1 a0bX\n2 a1b\n2\n` (a parenthesized literal is the value of `$0 ~ /re/`, used as a dynamic regex);
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
  `BEGIN{gsub(/a/,"b",c,d)}` -> column 22, `4 is invalid as number of arguments for gsub`;
  `BEGIN{rand(1)}` -> column 12, `1 is invalid as number of arguments for rand`;
  `BEGIN{print "x"; print match("a",/a/,m)}` -> column 38, `match: third argument is a gawk extension`;
  `BEGIN{print "x"; close("a","b")}` -> column 30, `close: second argument is a gawk extension`;
  `BEGIN{x = substr("abc", 1,` newline `2, 3)}` -> `awk: cmd. line:2: 2, 3)}\nawk: cmd. line:2:     ^ 4 is invalid as number of arguments for substr\n`.
  (Each line pair is `awk: cmd. line:1: <program line>\nawk: cmd. line:1: <column spaces>^ <message>\n`.)
- `NotYetAvailable` (awk--interpreter's test in `AwkInterpreterTest.cpp`,
  changed): the leftover unused first run goes; the `length` case becomes
  `BEGIN { print sprintf("%d", 1) }` -> ``awk: cmd. line:1: fatal: function `sprintf' is not implemented yet\n``, 2
  (awk--printf-math changes it); the printf, redirection and getline cases
  stay.

In `AwkInterpreterTest.cpp` (the #81 fixes, `AwkRunTest`):
- `FieldSeparatorCheckedWhenAssigned` (out empty, status 2 unless said):
  input `x\n`, `BEGIN { FS = "a(" } { print "hi" }` and, with no input,
  `BEGIN { FS = "a(" }` -> `awk: cmd. line:1: fatal: invalid regexp: Unmatched ( or \(: /a(/\n`;
  input `q\n`, `{ FS = "a(" }` -> `awk: cmd. line:1: (FILENAME=- FNR=1) fatal: invalid regexp: Unmatched ( or \(: /a(/\n`;
  `-F 'a(' '{ print "hi" }'` (input `x\n`), `-v 'FS=a(' 'BEGIN { print "no" }'`
  and `'{ print }' 'FS=a('` (input `x\n`) -> `awk: fatal: invalid regexp: Unmatched ( or \(: /a(/\n`;
  `BEGIN { print "1"; FS = "a\\q"; print "2" }` -> out `1\n2\n`,
  err ``awk: cmd. line:1: warning: regexp escape sequence `\q' is not a known regexp operator\n``, status 0;
  `-v 'FS=a\\q' 'BEGIN { print 1 }'` -> out `1\n`,
  err ``awk: warning: regexp escape sequence `\q' is not a known regexp operator\n``, status 0;
  `BEGIN { FS = "("; print split("a(b", x), x[2] }` -> `2 b\n`, status 0 (one byte: literal, never compiled).
- `RecordAssignmentTakesFsAndRs`:
  input `a:b c\n`, `{ FS = ":"; $0 = $0; print $1; print NF }` -> `a\n2\n`;
  input `p q r\n`, `{ RS = ""; FS = "x"; $0 = "p\nq r"; print NF; print $2 }` -> `2\nq r\n`;
  input `a\n`, `BEGIN { RS = "" } { RS = "\n"; FS = "x"; $0 = "p\nq r"; print NF }` -> `1\n`;
  input `a b\n`, `{ FS = "x"; sub(/ /, "x"); print NF, $1 }` -> `2 a\n`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
./output/linux/Awk.unittests --gtest_filter='AwkFunctionsTest.*:AwkRunTest.*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/awk/CLAUDE.md`: a "Functions" section -- local resolution
  (`localSlot`, `functionIndex`), `ActiveCall` and the binding chain (the scalar and
  array rules with their error texts), specials (NF included) passed by
  value, `FlowUnwind`, the depth limit, `m_globals` a deque (why); a
  "Built-in functions" section -- the argument-count table and its errors,
  `RegexOperand`, `split`'s separator rules (and how they differ from FS),
  the `sub`/`gsub` replacement and empty-match rules, `substr`'s rule for a
  start below 1; `AwkBuiltins.cpp` in the file list; `Awk.cpp`'s version
  1.3.0. Update what changed: "Running"'s hooks bullet (only
  `EvaluateGetline` and the not-yet built-ins left), "Values, fields and
  records" (`$0 = v` re-split with the FS and RS of the assignment, set by
  the interpreter through `SetRecord`), "Regexes" (a regex FS compiled and
  checked when FS is assigned, `-F`/`-v`/operands location-less); under
  "Documented exceptions" the depth limit and `split(s, a[i])`, and the
  "parts that do not run yet" bullet trimmed to printf, sprintf, the math
  functions, getline and output redirections.
- `src/components/BuiltinCommands/CLAUDE.md` (the awk row, version 1.3.0)
  and root `CLAUDE.md` (the awk row): user functions and the string
  functions added, plus the two exceptions; printf, sprintf, math, getline
  and redirections still listed as to come.

## Acceptance

- [ ] Every test above passes with the exact bytes; no expectation was
  changed to mawk's.
- [ ] Argument counts are refused at parse time with gawk's messages and
  caret at the `)`.
- [ ] Arrays by reference, untyped variables bound by name through any chain
  of calls, scalars (specials and NF included) by value, locals; every
  error text as specified.
- [ ] `next`/`nextfile`/`exit` inside functions act on the calling rule (a
  pattern's call too); calls are popped on every exit path.
- [ ] `split`, `sub`, `gsub`, `match` use `RegexOperand` (literal or cached
  dynamic regex), and `MatchRegex` uses it too; `split`'s separator rules
  and the `gsub` empty-match rule as specified.
- [ ] A regex FS that does not compile is gawk's fatal when assigned (or
  applied by `-F`, `-v`, an operand); `$0 = v` splits with the FS and RS of
  that moment; `NotYetAvailable` has no unused run.
- [ ] printf/sprintf/math still fail with their placeholder message; nothing
  reaches files or processes.
- [ ] New source and test files in their CMakeLists; version 1.3.0; all
  unit tests green; the three CLAUDE.md files updated.
- [ ] Clean room: no other awk's source read; no other program's internal
  names in code or comments.

## Out of scope

- `printf`, `sprintf`, `sin cos atan2 exp log sqrt int rand srand`
  (awk--printf-math).
- `getline`, output redirections, `close`, `fflush`, `system`, `ENVIRON`
  (awk--io).
- gawk extensions: indirect calls (`@f()`), `length(array)`, `split`'s
  fourth argument, `match`'s third, arrays of arrays (`split(s, a[i])`),
  `gensub`, `asort`, `IGNORECASE`.
