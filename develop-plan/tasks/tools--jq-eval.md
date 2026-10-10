# Task tools--jq-eval: the jq evaluator -- generators, variables, functions, errors

- Rock: tools
- Depends on: tools--jq-parse, tools--jq-json
- Size: ~1500 changed lines in ~20 files
- Plan checked against: develop @ 58c42cc
- PR title: jq: evaluate programs -- generators, bindings, functions, try/catch

## Goal

The third jq task. A parsed jq program runs on a `Jq::Value` and produces
its outputs in jq 1.7.1's order, with jq's runtime error messages: every
construct of the language except paths and assignment (`path()`, `=`, `|=`,
`+=` ..., `del` -- the next task, `tools--jq-paths`) -- pipes and commas,
literals, string interpolation, arrays and objects, arithmetic and
comparison, `and`/`or`/`//`, `if`, `try`/`catch`/`?`, `error`, `reduce`,
`foreach`, `as` bindings with destructuring and `?//`, `def` with filter
and `$value` parameters and closures, recursion, `label`/`break`,
`$__loc__` -- plus the compile-time checks jq makes before running
(`foo/0 is not defined`, `$x is not defined`). The builtin library is a
registry of native C++ functions and a prelude written in jq, both filled by
later tasks; this one registers only `empty`, `error/0,1`, `not`, `type`.

Still a library (namespace `Haisos::Jq`), tested with a small runner;
`tools--jq-command` makes it the `jq` builtin.

It first closes the open review findings of `tools--jq-parse` (#85) and
`tools--jq-json` (#86) -- small fixes to the tree, the parser, the values
and the reader this task stands on ("Fixes first" below).

Reference: jq 1.7.1 (`jq --version` prints `jq-1.7`; Ubuntu's 1.7.1
package, on the host and in the task container). Every expected output
below was taken from it with `jq -nc '<program>'` (or `echo '<json>' | jq
-c '<program>'`); for anything else, run it there and copy the result.

## Context

**Clean room -- the user's rule, above every other rule** (root
`CLAUDE.md`, "Clean-room rule"). No code is copied from any other program
or project, whatever its licence: jq is MIT, and the rule applies all the
same; so are gojq and jaq. Everything here is written from scratch.
Behaviour is matched from the jq 1.7 manual and from the observed output
of the real jq 1.7.1 -- never from its source: no jq, gojq or jaq source
is read, ported, translated or paraphrased, and none of their internal
names appears in code, comments, tests or commit messages (no `jv_*`,
`block_*`, `gen_*`, no bytecode opcode names, no stack-machine layout, no
iterator type names). The evaluator below is Haisos's own design --
callbacks over the syntax tree `tools--jq-parse` builds -- and every rule
in this plan is stated as observable behaviour: what a program outputs, in
what order, with what message. The prelude's jq definitions are written by
Haisos from the manual's description of each builtin, not taken from jq's
own library.

**Write in pieces.** Never more than ~250 lines in one Write/Edit call:
create a file with its first part, then build it up with several Edits.
Commit after each file or step (each fix below, a header and its `.cpp`,
a test file), so a failure loses little.

Read first: `src/components/BuiltinCommands/commands/jq/CLAUDE.md`, then
these headers as they are on develop (#85 and #86 merged; the real
signatures, which differ in places from those tasks' plans):
- `JqAst.h`: `Node` (`type`, `begin`/`end`, `line`, `text`, `children`,
  `stringParts`, `entries`, `patterns`, `definitions`, `hasCatch`,
  `hasElse`; the children of each kind are in the comment above it),
  `NodeType`, `Pattern`/`PatternType`/`Pattern::Entry` (`variable`, `key`,
  `value`), `FunctionDefinition` (`name`, `params` -- `"f"` for a filter
  parameter, `"$x"` for a value one --, `body`, `begin`), `DumpNode`. A
  Literal's `text` is the number as written, or `true`/`false`/`null`; a
  String's `text` is its format name without `@` (`""` for none), and so
  is a Format's.
- `JqParser.h`: `ParseProgram(std::string_view)` returning
  `ParseResult { root, errors }`, `CompileError { message, offset }`
  (offset `SIZE_MAX`: printed without a location),
  `FormatCompileError(program, error)`, `FormatCompileErrorCount(count)`.
- `JqValue.h`: `Value` (`Null`, `Boolean`, `Number` -- a computed number
  --, `NumberLiteral(d, canonical)`, `String`, `Array`,
  `Object(std::vector<ObjectEntry>)`; `GetKind`, `IsTruthy`, `AsNumber`,
  `Literal`, `AsString`, `AsArray`, `AsObject`, `Find`, `WithMember`,
  `WithoutMember`, `WithElement`), `Kind`, `KindName`, `ObjectEntry`,
  `Compare`, `Equal`, `CompareDecimalLiterals`,
  `bool CanonicalNumberLiteral(text, std::string& canonical, double& value)`.
- `JqJsonWriter.h`: `void WriteJson(const Value&, const WriteOptions&,
  std::string& out)` -- it **appends** to an out-parameter (compact output
  is `WriteOptions` with `indent = 0`) --; `FormatNumber(value)`;
  `QuoteJsonString(const std::string& utf8, bool ascii)`;
  `DumpTruncated(value, bufferSize)` (the dumps in jq's messages are
  `DumpTruncated(v, 15)`: past 14 bytes, the first 11 then `...`).
- `JqJsonReader.h`: `JsonReader` binds its output vector in its
  constructor -- `std::vector<Value> values; JsonReader reader(values);
  reader.Feed(bytes); reader.Finish(); reader.Error()`, `Feed`/`Finish`
  returning false on error --, and `bool ParseSingleJson(std::string_view
  bytes, Value& out, std::string& error)`.
- `JqUtf8.h`: `AppendUtf8`, `RepairUtf8` (string slices count code points:
  add a small decoder there if one is missing).
- Tests: `tests/unit/components/Jq.unittests/` -- `JqParserTest.cpp` has
  the `Dump(...)` helper and the syntax-error tables the parser fixes
  extend.

Rules that bite: portable C++17, no `<regex>`, no POSIX headers. Nothing
here reaches outside the process: whatever a program reads or writes
(inputs, `debug`, `stderr`) goes through the `JqHost` interface below, which
the jq builtin implements over its `BuiltinContext` -- so `ICurrentProcess`
stays the only door. These classes implement no `interfaces/` interface;
`Program` is created by a static `Compile` returning `std::shared_ptr`
anyway (it is shared by the command and its natives).

## Changes

New files in `src/components/BuiltinCommands/commands/jq/`, after the
fixes to the existing ones.

### Fixes first: the open findings of #85 and #86

Each is a small commit of its own, with its test.

#### The tree and the parser (tools--jq-parse, #85)

1. **Bound the tree's height** (medium). Left-folded chains --
   `1,1,1,...`, `1+1+...`, `.a.a.a...`, `1???...` -- add a tree level per
   link while the parser itself loops, so the tree can be as deep as the
   program is long, and `~Node`, `DumpNode`, `CloneNode` and the evaluator
   recurse over it (measured on develop: 100000 × `1,` parses, then
   overflows a 1 MB stack when the tree is destroyed). **Chosen: bound the
   tree's height after parsing**, and make only the destructor iterative.
   Not the 256-level nesting limit (it would refuse a 300-element array
   literal `[1,2,...]`, ordinary jq, and counting links as they are parsed
   does not even bound a left fold, whose first element ends deepest); not
   iterative walkers everywhere (the evaluator's callbacks nest once per
   pipe stage by design).
   - `JqAst.h/.cpp`: `constexpr size_t kMaxTreeDepth = 512;` and
     `size_t TreeHeight(const Node& root, size_t* deepestBegin = nullptr)`
     -- no recursion: an explicit stack of (node, level), visiting
     children, object entries (key and value), the key nodes inside
     patterns (nested patterns are few: the nesting limit bounds them) and
     definition bodies; `*deepestBegin` gets the `begin` of the first node
     found at the greatest level.
   - `ParseProgram`: a parsed tree taller than `kMaxTreeDepth` is refused
     with one compile error, Haisos's own wording: `program nested too
     deeply (more than 512 levels)` at `deepestBegin`. A documented
     difference (jq has no such limit).
   - `Node::~Node()`, declared in `JqAst.h`, defined in `JqAst.cpp`,
     without recursion: it moves every subtree it owns -- children,
     entries' keys and values, the keys of its patterns' entries (at any
     depth of the patterns), definition bodies -- into a local
     `std::vector<std::unique_ptr<Node>>`, then pops them one at a time,
     each first emptied the same way into that vector. A refused tree, and
     a half-built one dropped by a syntax error, can be any height.
   - `CloneNode` copies a shorthand key into its value (`{"\(...)"}`,
     `{@base64 "..."}`) during parsing, before the check: check
     `TreeHeight(*key)` first and fail with the same error past the limit.
   - `DumpNode` and the evaluator stay recursive: only accepted trees reach
     them, and the evaluator has its own guard ("Depth" below).
   - Tests (`JqParserTest.TallTrees`): `[` + 400 × `1,` + `1]` parses and
     dumps; 600 × `1,`, `.a` × 600, `1` + 600 × `?` and `1` + 600 × `+1`
     are refused with that error, formatted; 200000 × `1,` too, without a
     crash.
2. **`as` bindings hold no nesting level over their body** (low; jq runs
   300 chained `. as $x |` -- the program `. as $x | ` × 300 + `$x` gives
   `null`). In `ParseUnary` the `Depth` of a binding covers its patterns
   only. The body must not recurse in the parser either: measured on
   develop (Linux, `-O2`), the parser spends about 2.5 KB of stack per
   nesting level, so its 256 levels already take some 700 KB of a 1 MB
   Windows stack, and a body per binding costs as much. So bindings are
   closed without recursion:
   - On `as PATTERNS |`, `ParseUnary` returns the Bind with its body still
     empty and pushes it on a parser member `m_openBindings` (raw
     pointers; the nodes are owned by the tree being built).
   - A *pipe context* closes the bindings opened inside it: `ParsePipe`
     (its non-`def` branch), and `ParseTry` for its body, which must stop
     before `catch`. Each remembers `m_openBindings.size()` when it starts;
     whenever a part it parsed comes back with more bindings open, it keeps
     parsing -- the rest of its pipe, up to the token that ends it, which
     is the token that ended such a body before -- as a new segment. At the
     end it folds the segments last first, each folded pipe becoming the
     body of the binding opened in the segment before it, and pops them.
     Share the loop: `ParsePipe` and `ParseTry` call one helper.
   - Every operator loop between `ParseUnary` and the pipe context (`* /
     %`, `+ -`, comparisons, `and`, `or`, assignments, `//`, `,`) stops
     while a binding is open: `. as $x | -1` is `-1`, not a subtraction.
   - The tree is the one built before: the existing `Bindings` dumps do not
     change. New cases in `JqParserTest.Bindings`: 300 chained bindings
     parse; `. as $x | -1`, `1 + . as $x | $x * 2`, `try . as $x | $x catch
     5` (`(try (as . $x $x) 5)`), `try error("x") catch . as $y | $y, 1`
     dump as the recursive parser built them (compare with a dump taken
     before the change); 2000 chained bindings are refused by the height
     check, without a crash.
3. **Object-pattern keys written as a name or keyword are String nodes**
   (medium): `. as {b: $c}` and `{null: $a}` build `String` (one
   `stringParts` entry, the name), as object constructions do, never a
   `Literal` (whose text means a number or `true`/`false`/`null`). Dump
   `("b" $c)`; update `JqParserTest.Bindings`. jq: `{"b":5,"null":6} | .
   as {b: $c, null: $a, true: $t} | [$c,$a,$t]` is `[5,6,null]`.
4. **Three "expecting" lists as jq 1.7.1 has them** (medium), in
   `JqParserTest.SyntaxErrors`:
   - after `reduce`/`foreach` and its source, a token that is not `as`:
     no list when the source is a lone `.` (`reduce . $x` -> `syntax
     error, unexpected BINDING (Unix shell quoting issues?)`, `foreach .
     1` -> `unexpected LITERAL`), else `expecting FIELD or as or '.' or
     '['` (`reduce .a 1`, `reduce 1 1`, `reduce $x 1`, `foreach .a 1`).
     jq adds a second error note after `reduce . $x`; only the first is
     reported here, as documented.
   - `foreach . as $x (0 1)`: `syntax error, unexpected LITERAL (Unix
     shell quoting issues?)`, no list (not `expecting ';'`).
   - call arguments: `f(1 2)`, `f(1; 2 3)`, `f(1, 2 3)` -> `syntax
     error, unexpected LITERAL, expecting ';' or ')' (Unix shell quoting
     issues?)`.
5. **A parenthesised object key starts at its `(`** (for the compile check
   below): in `ParseObject` and `ParsePatternEntry`, the key node parsed
   between `(` and `)` gets `begin` = the `(` offset.
6. `tests/unit/components/Jq.unittests/CMakeLists.txt` already ends with a
   newline since #86; keep it so when adding `JqInterpreterTest.cpp`.

#### Values and the reader (tools--jq-json, #86)

1. **The Windows CI failure**: `JqJsonWriterTest.cpp` (~line 55)
   `Num(0.0 / 0.0)` does not compile on MSVC (C2124, a constant division
   by zero): use `std::numeric_limits<double>::quiet_NaN()` (`<limits>`);
   likewise `1e300 * 1e300` there becomes
   `std::numeric_limits<double>::infinity()` (MSVC warns on constant
   overflow). Search the Jq tests for any other such constant.
2. **A key index for objects** (`JqValue.cpp` ~94-125): `Find`,
   `WithMember` and `WithoutMember` scan the members, so building a large
   object is quadratic. The object payload becomes a small struct -- the
   ordered `std::vector<ObjectEntry>` plus, from 16 members up, a
   `std::unordered_map<std::string, size_t>` from key to position, built
   by `Value::Object` -- and the three use it. `AsObject()` still returns
   the vector. The evaluator never grows an object one `WithMember` at a
   time: it collects entries with a local index, then calls `Value::Object`
   once.
3. **The reader's duplicate-key check** (`JqJsonReader.cpp` ~121) scans
   every member for each new one: give `Frame` a key-to-position map, used
   from 16 members up. Test (`JqJsonReaderTest`): an object of 100000
   distinct keys reads, with a repeated key at its end keeping its first
   place and the last value; `Find` gets every key (quadratic code takes
   minutes here, the indexed one milliseconds; no timing assertion).
4. Lows: drop the dead size comparison in `CompareLiteralMagnitude`
   (`JqValue.cpp` ~208: `std::string::compare` already orders a prefix
   first) and its comment; `JqValue.h` ~81's "the plan's number layout"
   becomes a description of the layout (or a pointer to the jq
   `CLAUDE.md`); rename the test suites `JqValue`, `JqJsonWriter`,
   `JqJsonReader` to `JqValueTest`, `JqJsonWriterTest`,
   `JqJsonReaderTest` (as `JqLexerTest`, `JqParserTest`); in the jq
   `CLAUDE.md`, the punctuation is "bold in the default colour (`1;39`)",
   not "bold white".

### `JqRuntime.h` -- what evaluation throws and needs

```cpp
// A jq error: error/1's value, or a runtime error's message as a string.
struct JqError { Value value; };
// Thrown when the host asks to stop (the process was stopped).
struct JqStopped {};
// break $label: the label instance it unwinds to.
struct JqBreak { uint64_t label; };

// What a running program reaches outside itself; implemented by the jq
// builtin (tools--jq-command) and by a stub in the tests.
class JqHost {
public:
    virtual ~JqHost() = default;
    virtual std::optional<Value> NextInput() = 0;        // input/inputs; nullopt at the end
    virtual void WriteStderr(const std::string& bytes) = 0;
    virtual Value InputFilename() const = 0;             // a string, or null
    virtual int InputLineNumber() const = 0;
    virtual bool StopRequested() const = 0;
};

using Emit = std::function<void(const Value&)>;
```

### `JqInterpreter.h` / `JqInterpreter.cpp` -- evaluation

```cpp
struct Env;                                  // an immutable linked scope
using EnvPtr = std::shared_ptr<const Env>;
struct Closure { const Node* body = nullptr; EnvPtr env; };   // a filter argument

constexpr int kJqMaxEvalDepth = 1024;

class Interpreter {
public:
    Interpreter(const NativeRegistry& natives, JqHost& host);
    // Every output of |node| on |input|, in jq's order.
    void Eval(const Node& node, const Value& input, const EnvPtr& env, const Emit& emit);
    void EvalClosure(const Closure& closure, const Value& input, const Emit& emit);
    JqHost& Host();
    void CheckStop();                        // throws JqStopped when the host says so
};
```

`Env` holds one binding per node, with a parent: a variable (`name`,
`Value`), a function (`name`, arity, `const FunctionDefinition*`, and the
`EnvPtr` it was defined in), a filter parameter (`name`, a `Closure`), or
a label (`name`, a `uint64_t` instance id). Lookups walk up the chain by
name (and arity for functions); the compile check below guarantees they
succeed.

Evaluation is **callback-based** (Haisos's own design): `Eval` calls
`emit` once per output, in order, and a construct with several
sub-expressions evaluates the inner one inside the callback of the outer
one -- "for each output of A, every output of B" is a callback nested in a
callback. Ending early (`label`/`break` here; `limit`, `first` later)
throws `JqBreak`, caught by whoever started it. Errors are `JqError`
exceptions.

Semantics per node (orders observed on jq 1.7.1; "A outer, B inner" means:
for each output of A, every output of B):
- `Identity`: the input. `RecurseDefault` (`..`): the input, then
  recursively every element/member value, pre-order, objects in insertion
  order (`{"a":[1,{"b":2}]} | [..]` is
  `[{"a":[1,{"b":2}]},[1,{"b":2}],1,{"b":2},2]`) -- iteratively, with an
  explicit stack (no C++ recursion per level).
- `Index`: key outer, target inner (`[([1,2],[3,4])[0,1]]` is
  `[1,3,2,4]`). `Slice`: from outer, then to, then target
  (`[([1,2,3],[4,5,6])[(0,1):(2,3)]]` is
  `[[1,2],[4,5],[1,2,3],[4,5,6],[2],[5],[2,3],[5,6]]`). `Iterate`: arrays'
  elements, objects' values in order. The indexing rules and messages are
  in "Indexing" below (put them in `JqIndex.h/.cpp`: `tools--jq-paths`
  reuses them).
- `Literal`: `true`/`false`/`null`, or `Value::NumberLiteral` of the
  canonical literal (`CanonicalNumberLiteral(text, canonical, value)`:
  `1.0` prints `1.0`). Build each Literal's value once (a cache keyed by
  the node, filled at compile time).
- `String`: interpolations, the **last** outermost (`"\(1,2)-\(3,4)"` gives
  `"1-3" "2-3" "1-4" "2-4"`); each interpolated value is converted by the
  format (`ApplyFormat(name, value)`) or, with none, by `tostring` (a
  string as is, anything else its compact JSON). A string with a format
  and no interpolation is its text, the format unused (`@foo "a"` is
  `"a"`). `Format` alone applies to the input. `ApplyFormat` lives in
  `JqFormat.h/.cpp`: here only `text` (= tostring) and `json` (= compact
  JSON); any other name fails with `<name> is not a valid format` (`"a" |
  @foo` -> `foo is not a valid format`; `tools--jq-text` adds the rest).
- `Array`: all outputs of the body collected (an error inside propagates).
- `Object`: entries left to right, the first outermost; within an entry the
  key outer, the value inner (`{("a","b"): (1,2)}` gives `{"a":1}
  {"a":2} {"b":1} {"b":2}`; `[{a:(1,2), b:(3,4)}]` is
  `[{"a":1,"b":3},{"a":1,"b":4},{"a":2,"b":3},{"a":2,"b":4}]`). A key that
  is not a string: `Cannot use <kind> (<DumpTruncated 15>) as object key`
  (`[1] as $x | {($x):1}` -> `Cannot use array ([1]) as object key`).
  Members are collected with a local key index, then one `Value::Object`.
- `Negate`: numbers (a computed result); else `<kind> (<dump15>) cannot be
  negated` (`[1,2,3,4,5,6,7,8,9] | -.` -> `array ([1,2,3,4,5,...) cannot
  be negated`).
- `Pipe`: right on each output of left. `Comma`: left's outputs, then right's.
- `Alternative` (`a // b`): the truthy outputs of `a`; if there was none, the
  outputs of `b`. Errors in `a` propagate (`[(1, error("x"), 2) // 3]` and
  `[error("x") // 3]` fail with `x`; `[(1 | .a) // 3]` with `Cannot index
  number with string "a"`; `[.a.b // 3]` on null is `[3]`).
- `Binary` arithmetic and comparison: the **right** operand outer, the
  left inner (`[(1,2) + (10,20)]` is `[11,12,21,22]`). See "Arithmetic".
- `And`/`Or`: the left operand outer; `and`: a falsy left gives `false`,
  a truthy one gives the truthiness of each right output; `or` the mirror
  (`[(true,false) and (true,false)]` is `[true,false,false]`,
  `[(true,false) or (true,false)]` is `[true,true,false]`).
- `If`: for each output of the condition, the matching branch on the input;
  no `else` means `.`.
- `Try`: the body's outputs; when the body raises a `JqError`, the catch
  body runs on the error's value (or nothing, with no catch) and the body
  is **not resumed** (`try (1, error("x"), 3) catch .` gives `1 "x"`).
  Only errors raised *inside the body* are caught -- not those raised by
  whatever consumes its outputs downstream (`try ((try (1,2) catch
  "inner") | error("down")) catch "outer: \(.)"` is `"outer: down"`).
  Haisos's way to tell them apart: the emit callback given to the body
  wraps a `JqError` thrown by the downstream `emit` in a private
  `TryPassThrough { uint64_t tryId; JqError error; }`, and this `Try`
  rethrows its own pass-through as the plain `JqError` once outside the
  body. `JqBreak` and `JqStopped` are never caught. `try error(null)
  catch .` gives `null`.
- `Reduce`: init outer (each init output starts its own reduction:
  `[reduce empty as $x (1,2; .)]` is `[1,2]`); the state becomes the
  **last** output of the update, or `null` when it produced none
  (`[reduce (1,2) as $x (0; empty)]` is `[null]`).
- `Foreach`: init outer (`[foreach (3,4) as $x (0,10; . + $x)]` is
  `[3,7,13,17]`); per item, every output of the update is emitted (through
  the extract, if given: `[foreach (1,2) as $x (0; . + $x; [$x, .])]` is
  `[[1,1],[2,3]]`) and the last becomes the state; no output: nothing
  emitted and the state becomes `null` (`[foreach (1,2,3) as $x (0; if $x
  == 2 then empty else . + $x end; .)]` is `[1,3]`; `[foreach (1,2) as $x
  (0; (. + $x), 7)]` is `[1,7,9,7]`).
- `Reduce` and `Foreach` destructure each item as `Bind` does, `?//`
  included: an error while destructuring or in the update under
  alternative i retries the item under alternative i+1 (`reduce ([1],2)
  as [$a] ?// $a (0; . + $a)` is `3`; `reduce ([1]) as [$a] ?// $a (0; if
  $a == 1 then error("x") else 5 end)` is `5`).
- `Bind` (`S as P | B`): for each output of `S`, destructure, then `B` on
  the original input. Destructuring: `$x` binds; `[P0, P1]` binds `Pi`
  to `.[i]` of the value; `{key: P}` to `.[key]` (the key a String node,
  or an expression -- each of its outputs, outer to inner: `. as
  {("a","b"): $x} | $x` gives `null null`); `{$a}` binds `$a` to `.a`;
  `{$a: P}` both. Indexing errors are the usual ones (`1 as [$a] | $a`
  fails with `Cannot index number with number`). With `?//` alternatives:
  every variable of every alternative is bound, `null` unless the
  alternative in use sets it; alternative 1 is tried first; an error
  raised while destructuring **or by the body** under alternative i moves
  on to i+1 (with all variables back to null; outputs already emitted
  stay); the last alternative's error propagates (`[[1,2]] | [.[] as [$a]
  ?// $a | if $a == 1 then error("x") else $a end]` is `[[1,2]]`; `"x" as
  [$a] ?// $b | [$a,$b]` is `[null,"x"]`).
- `Defs`: the definitions are added to the scope in order, each seeing
  itself (recursion) and the earlier ones -- not the later ones (`def f:
  g; def g: 1; f` is `g/0 is not defined`) --; then the scope expression.
- `Call`: a user function (innermost definition with that name and arity),
  else a filter parameter (arity 0), else a native. A user function with
  parameters: each `$name` parameter is evaluated on the caller's input, the
  **first** such parameter outermost (`[range(0,1; 3,4)]` is
  `[0,1,2,0,1,2,3,1,2,1,2,3]` in jq; `range` comes later, so test it with
  `def f($a; $b): [$a, $b]; [f(1,2; 3,4)]`, which is
  `[[1,3],[1,4],[2,3],[2,4]]`), and bound both as `$name` and as the
  filter `name`; each filter parameter is bound as a `Closure` over the
  caller's env; the body runs on the caller's input in the definition's
  env plus those bindings. A filter parameter call runs its closure body
  in the closure's env, on the current input.
- `Variable`: lookup; `$ENV` and the command's named arguments are globals
  given to `Program::Run`. `Loc`: `{"file":"<top-level>","line":N}` (`1 as
  $x |\n$__loc__` gives line 2).
- `Label`: a fresh instance id bound under the name; a `JqBreak` with that
  id ends the label's outputs quietly (`[label $out | 1, 2, break $out,
  3]` is `[1,2]`; `[label $a | 1, (label $b | 2, break $a, 3), 4]` is
  `[1,2]`). `Break`: throws `JqBreak`.

`CheckStop()` at every function call, every `Iterate` element, every
`reduce`/`foreach` item and every `..` step.

**Depth.** jq 1.7.1 recurses as deep as memory allows (`def f: if . <
100000 then .+1|f else . end; 0|f` prints `100000`); Haisos evaluates by
C++ recursion on a thread whose stack is 1 MB on Windows, so it bounds the
recursion -- a documented difference. `Eval` counts its own nesting with an
RAII guard (one level per active `Eval` call, released on exceptions too)
and past `kJqMaxEvalDepth = 1024` throws the `JqError` string `Maximum
evaluation depth (1024) exceeded`. Every construct counts -- a call, a pipe
stage, a binding, a closure -- so one guard bounds recursion through user
functions, tall trees (at most 512 levels after the parser's check) and
both together. Keep each level cheap: `Eval` only dispatches on the node
kind to one helper per kind (so the locals of the big cases do not swell
every frame), and the emit lambdas capture by reference. A recursive
function takes several levels per call, so recursion about a hundred calls
deep fits. `JqInterpreterTest.DepthLimit` reaches the limit on the test's
main thread -- 1 MB on Windows -- so the Windows CI proves the stack holds;
if it overflows there, lower `kJqMaxEvalDepth` (and the test) rather than
removing the test.

### Arithmetic (`JqArithmetic.h/.cpp`)

```cpp
Value Add(const Value& a, const Value& b);   // and Subtract, Multiply, Divide, Modulo
```
Messages: `<k1> (<d1>) and <k2> (<d2>) cannot be added` (`subtracted`,
`multiplied`, `divided`, `divided (remainder)`), dumps `DumpTruncated(v, 15)`
(`"abcdefghijklmnopq" - 1` -> `string ("abcdefghij...) and number (1)
cannot be subtracted`; `null - null` -> `null (null) and null (null)
cannot be subtracted`).
- `+`: `null + x` and `x + null` are `x` itself (a literal survives:
  `[3.0] | add` is `3.0` -- `add` comes later; test `null + 3.0`); numbers;
  strings concatenated; arrays concatenated; objects merged, right wins, in
  place (`{"a":1,"b":2} + {"c":0,"a":3}` is `{"a":3,"b":2,"c":0}`).
- `-`: numbers; arrays: the left without every element equal to one of the
  right's (`[1,2,3,1] - [1]` is `[2,3]`, `[1,[2]] - [[2]]` is `[1]`).
- `*`: numbers; a string and a number (either order): `n < 0` (or NaN)
  gives `null`, else the string repeated `floor(n)` times (`"ab" * 0` is
  `""`, `* 0.5` `""`, `* 1.5` `"ab"`, `* 2.5` `"abab"`, `* -1` `null`, `2 *
  "ab"` `"abab"`); two objects merged recursively (`{"a":{"b":1}} *
  {"a":{"c":2}}` is `{"a":{"b":1,"c":2}}`, `{"a":{"b":1}} * {"a":2}` is
  `{"a":2}`); anything else fails (`"a" * "b"`, `[1] * 2`).
- `/`: numbers, a zero divisor failing with `number (1) and number (0)
  cannot be divided because the divisor is zero`; two strings: the left
  split on the right (`"a,b" / ","` is `["a","b"]`, `"abc" / ""` is
  `["a","b","c"]`); else `cannot be divided` (`"a" / 0`).
- `%` (observed on jq 1.7.1): a NaN operand gives NaN (printed `null`);
  else both magnitudes are truncated to integers, saturating at 2^63-1
  (an infinity too); a zero truncated divisor fails with `number (5) and
  number (0.5) cannot be divided (remainder) because the divisor is zero`;
  the result is the remainder of the magnitudes carrying the **dividend's**
  sign, `-0` included, as a computed number. `[5 % -2, -5 % 3, 5.9 % 2.1,
  1e30 % 7, -4 % 2, -5 % -3, 1e19 % 10, -1e19 % 10, -0 % 3, 0 % -3]` is
  `[1,-2,1,0,-0,-2,7,-7,-0,0]`; with `nan` read as the input, `[. % 3, 5 %
  .]` is `[null,null]`; `[1e1000 % 7, 7 % 1e1000, -1e1000 % 7]` is
  `[0,7,-0]`. A documented difference: jq gives `-8`/`8` for `±2^63 % 10`
  exactly (`9223372036854775807 % 10`, a double equal to 2^63); Haisos
  saturates, as it does for every larger magnitude.
- Comparisons with `Compare` (`1 < "a"` is true). Results of arithmetic
  are computed numbers (`Value::Number`): `1.0 + 0` prints `1`.

### Indexing (`JqIndex.h/.cpp`)

```cpp
Value IndexValue(const Value& target, const Value& key);           // .[key]
Value SliceValue(const Value& target, const Value& from, const Value& to);
void IterateValue(const Value& target, const std::function<void(const Value&)>& each);
```
- `null` indexed by a string or a number, or sliced (any bounds), is
  `null`; by an object (a slice form) too. By anything else: `Cannot index
  null with <kind>` (`.[true]` -> `Cannot index null with boolean`,
  `.[null]`, `.[[1]]` likewise).
- Object by string: the member or null. Object by anything else: `Cannot
  index object with <kind>` (`.[0]` -> `Cannot index object with number`,
  `.[null]` -> `with null`).
- Array by number: `floor`; negative counts from the end; out of range
  null (`[1,2,3] | .[1.7]` is 2, `.[-4]` null; NaN null). Array by array:
  the indices where the sub-array starts (`[1,2,1] | .[[1]]` is `[0,2]`;
  `[1,2,1,2] | .[[1,2]]` is `[0,2]`). Array by object: a slice when it has
  `start` and `end` members (numbers or null: `[1,2,3] |
  .[{"start":1,"end":null}]` is `[2,3]`), else `Array/string slice indices
  must be integers`. Array by null or boolean: `Cannot index array with
  null` / `boolean`.
- Anything (array, number, string, boolean) by a string: `Cannot index
  <kind> with string "<key>"` when the key is shorter than 30 bytes,
  `Cannot index <kind> with string` otherwise (observed: `1 |
  .["kkk..."]` names a 29-byte key, not a 30-byte one). Any other pair:
  `Cannot index <kind> with <keykind>` (`"abc" | .[0]` -> `Cannot index
  string with number`, `true | .[0]` -> `Cannot index boolean with
  number`, `"abc" | .[null]`, `.[[1]]` likewise).
- Slices of arrays and strings (strings by code points: `"aéb" | .[1:2]` is
  `"é"`): `from` floored, `to` ceiled (`[1,2,3] | .[1.2:2.8]` is `[2,3]`),
  null meaning the start/end, negatives from the end, clamped (`.[5:]` and
  `.[1:0]` are `[]`, `.[-5:-4]` `[]`). A bound that is neither number nor
  null: `Array/string slice indices must be integers` (`[1,2] | .[1:"x"]`).
  Slicing an object: `Cannot index object with object`; others: `Cannot
  index <kind> with object` (`1 | .[1:2]`).
- Iterating: arrays and objects; anything else `Cannot iterate over <kind>
  (<dump15>)` (`Cannot iterate over null (null)`, `Cannot iterate over
  string ("abcdefghij...)`); but `.[]?` and `.a?` on a number give
  nothing.

### `JqNatives.h` / `JqNatives.cpp` -- the builtin registry

```cpp
struct NativeFunction {
    std::string name;
    int arity = 0;
    std::function<void(Interpreter&, const Value& input, const std::vector<Closure>& args, const Emit& emit)> run;
    // Path mode (tools--jq-paths); empty for a function that is not a path expression.
    std::function<void(Interpreter&, const struct PathValue& input, const std::vector<Closure>& args,
                       const std::function<void(const PathValue&)>& emit)> runPath;
};

class NativeRegistry {
public:
    void Add(NativeFunction function);                    // a later Add of the same name/arity replaces
    const NativeFunction* Find(const std::string& name, int arity) const;
    std::vector<std::string> Names() const;               // "name/arity", for builtins/0
    // Every native Haisos has: each jq task adds its Register* call here.
    static const NativeRegistry& Standard();
};

// For natives whose arguments are values: calls |each| with one value per
// argument for every combination, the LAST argument outermost (jq 1.7.1:
// [pow(1,2; 3,4)] is [1,8,1,16]).
void ForEachArgumentCombination(Interpreter& interp, const Value& input, const std::vector<Closure>& args,
                                const std::function<void(const std::vector<Value>&)>& each);

void RegisterCoreNatives(NativeRegistry& registry);   // this task: empty/0, error/0, error/1, not/0, type/0
```

`PathValue` is declared (forward) here and defined by `tools--jq-paths`;
leave `runPath` empty for now. `Standard()` builds the registry once
(function-local static, thread-safe in C++11) calling every `Register*`.
`error/0` raises its input as the error; `error/1` raises each output of
its argument; `not` is the input's falsiness (`[1,2] | [.[] | not]` is
`[false,false]`); `type` the `KindName`. A user definition of the same
name and arity shadows a native (`def empty: 3; [empty]` is `[3]`, `def
type: 1; type` is `1`).

### `JqPrelude.h` / `JqPrelude.cpp`

```cpp
// The builtins written in jq, parsed once and placed in scope before the
// user's program (a user definition of the same name/arity shadows them).
std::string_view StandardPrelude();
```
This task's prelude, written by Haisos from the manual's descriptions:
`select(f)` -- the input, once for each truthy output of `f` (`[1 |
select(true, false, true)]` is `[1,1]`) --, and `recurse(f)` -- the input,
then `recurse(f)` of each output of `f`, depth first (`[0 | recurse(if . <
3 then .+1 else empty end)]` is `[0,1,2,3]`; `[[1,[2]] | recurse(.[]?)]`
is `[[1,[2]],1,[2],2]`); `tools--jq-builtins` replaces `recurse/1` with an
iterative native later. Later tasks append their definitions. A definition
chain alone is a parse error (`Top-level program not given`), so the text
ends with `.`; the parsed prelude is a static `ParseResult` whose root is
that `Defs` node. A parse error in it is a programming error (assert in
debug; a test checks it parses).

### `JqProgram.h` / `JqProgram.cpp` -- compiling and running

```cpp
class Program {
public:
    // Parses |text|, then checks names; |globals| are the variables the
    // command defines ("ENV", "ARGS", --arg names ...), without '$'.
    static std::shared_ptr<Program> Compile(std::string_view text, const std::vector<std::string>& globals,
                                            std::vector<CompileError>& errors);
    // Runs on one input; throws JqError (uncaught error), JqStopped.
    void Run(const Value& input, const std::map<std::string, Value>& globals, JqHost& host, const Emit& emit) const;
};
```

The check walks the tree (at most 512 levels: recursion is fine, frames
small) with the scope -- prelude definitions, the user's definitions in
force, parameters, `as` variables, labels, globals, natives -- and reports,
in source order, every:
- call with no definition, parameter or native of that name and arity:
  `<name>/<arity> is not defined` at the call's offset;
- variable not in scope: `$<name> is not defined`;
- `break $name` with no enclosing `label $name`: `$*label-<name> is not
  defined` at the `break`;
- object key -- in a construction or an object pattern -- that is a lone
  number, boolean or null literal in parentheses (`(1)`, `( 1 )`,
  `((1))`): `Cannot use <kind> (<DumpTruncated(literal, 15)>) as object
  key` at the key's `(` (fix 5 above). Its dump is the canonical literal
  (`{(1.0):2}` -> `number (1.0)`, `{(1e2):2}` -> `number (1E+2)`,
  `{(100000000000000000001):2}` -> `number (10000000000...)`). jq also
  refuses keys it computes from constants (`{(1+1):2}`, `{(1|.):2}`:
  `Cannot use number (2) as object key` at compile time); here those fail
  at run time with the runtime message -- documented. `{(-1):2}` fails at
  run time in jq too.

Expected (jq 1.7.1, the `FormatCompileError` texts joined, then
`FormatCompileErrorCount`): `foo` -> `jq: error: foo/0 is not defined at
<top-level>, line 1:\nfoo\njq: 1 compile error\n`; `foo | bar` -> two
errors, the second's line `foo | bar······` (6 spaces), `jq: 2 compile
errors`; `def f: 1; f(2)` -> `f/1 is not defined` with 10 spaces; `1 as $x
| $y` -> `$y is not defined` with 10 spaces; `label $f | break $g` ->
`$*label-g is not defined` with 11 spaces; `{(1):2}` -> `Cannot use number
(1) as object key` with 1 space; `{(true):2, (null):3}` -> two errors
(`boolean (true)` with 1 space, `null (null)` with 11); `[.[] as {(1): $x}
| $x]` -> `number (1)` with 9 spaces.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `JqInterpreter.cpp`,
`JqArithmetic.cpp`, `JqIndex.cpp`, `JqFormat.cpp`, `JqNatives.cpp`,
`JqPrelude.cpp`, `JqProgram.cpp` (all under `commands/jq/`, next to the
existing `JqAst.cpp` ... `JqValue.cpp`).
`tests/unit/components/Jq.unittests/CMakeLists.txt`: add
`JqInterpreterTest.cpp`.

## Tests

`tests/unit/components/Jq.unittests/JqTestRunner.h` (new, header-only):
`std::string RunJq(const std::string& program, const std::string& inputJson = "null")`
-- compiles (compile errors returned as the formatted text:
`FormatCompileError` of each, then `FormatCompileErrorCount`), reads the
input with `ParseSingleJson(inputJson, value, error)`, runs with a stub
`JqHost` (no inputs, stderr collected, never stopping), and returns the
outputs written compact (`WriteJson` with `WriteOptions` `indent = 0`,
appended to a string), separated by single spaces, or, on an uncaught
error, the outputs so far followed by `error: <message>` (`<message>` the
string, or `(not a string): <compact JSON>`). Later jq tasks reuse it.

`tests/unit/components/Jq.unittests/JqInterpreterTest.cpp` -- each case
`EXPECT_EQ(RunJq(program, input), expected)`, expected values from jq
1.7.1:
- `JqInterpreterTest.PathsAndIteration`: `.a.b`, `.[1:]`, `.[-1:]`,
  `"abcdef" | .[2:4]` (`"cd"`), `.[]?` on a number (nothing), `..`, the
  Index/Slice order cases above, `[1,2,1] | .[[1]]`.
- `JqInterpreterTest.IndexErrors`: each message of "Indexing", including
  the 29- and 30-byte key cases (`1 | .["kkk...29"]` with the key, 30
  without) and the null cases.
- `JqInterpreterTest.Arithmetic`: every example of "Arithmetic", the
  division-by-zero and remainder messages, `{} * 2`, `[] - 1`, the
  truncated dumps, the remainder table and its NaN case (input `nan`).
- `JqInterpreterTest.Generators`: `[(1,2) + (10,20)]`, `"\(1,2)-\(3,4)"`,
  `[{a:(1,2), b:(3,4)}]`, `{("a","b"): (1,2)}`, `and`/`or` orders, `//`
  cases (`[(null, false) // (3, 4)]` is `[3,4]`; `[false, null] | .[] //
  4` is 4; the error cases), the formats (`"\(1,2)" | @text "x\(.)",
  @json "y\("q")"` is `"x1" "y\"q\"" "x2" "y\"q\""`; `"a" | @foo` fails).
- `JqInterpreterTest.TryCatch`: `try error("x") catch .`, `try error({})
  catch .` (`{}`), `try error(null) catch .` (`null`), `[.[] | try if . ==
  2 then error("e") else . end catch "c"]` on `[1,2,3]` (`[1,"c",3]`), the
  not-resumed case, `.a?` on a number (nothing), `try (1,2) catch . |
  error("down")` (`error: down`), and the nested inner/outer case (`"outer:
  down"`).
- `JqInterpreterTest.ReduceForeach`: every example of those bullets, the
  `?//` ones included.
- `JqInterpreterTest.Destructuring`: `. as [$a, {b: $c, $d}] | [$a,$c,$d]`
  on `[1,{"b":2,"d":3}]` (`[1,2,3]`), the `?//` examples, `{"a":1} as {$a,
  b: $b} ?// [$c] | [$a, $b, $c]` (`[1,null,null]`), `. as {b: $c, null:
  $a, true: $t} | [$c,$a,$t]` on `{"b":5,"null":6}` (`[5,6,null]`), the
  expression-key case, `1 as [$a] | $a` (error).
- `JqInterpreterTest.Functions`: `def f(g): [g, g]; f(1, 2)`
  (`[1,2,1,2]`), `def f($x; y): $x + y; f(1; 2)` (`3`), the two-`$param`
  order case, `def f(x): x as $v | $v; [f(1,2)]` (`[1,2]`), `def f: def g:
  3; g; f` (`3`), factorial of 10 (`def fac: if . <= 1 then 1 else . *
  (. - 1 | fac) end; 10 | fac` is `3628800`), closures seeing their
  definition scope (`1 as $x | def f: $x; 2 as $x | f` is `1`), shadowing
  a prelude name (`def select(f): 5; 1 | select(.)` is `5`) and a native
  (`def empty: 3; [empty]`), the prelude's `select` and `recurse` cases.
- `JqInterpreterTest.LabelsAndLoc`: the two label cases, `$__loc__`,
  `{$__loc__}` (`{"__loc__":{"file":"<top-level>","line":1}}`), the
  line-2 case.
- `JqInterpreterTest.LiteralsSurvive`: `{"a":1.0} | .a` (`1.0`), `1.0 + 0`
  (`1`), `null + 3.0` (`3.0`), `[1.0, 100000000000000000001]`.
- `JqInterpreterTest.CompileErrors`: every expected text of the check.
- `JqInterpreterTest.DepthLimit`: `def f: if . < 100 then .+1|f else .
  end; 0|f` is `100`; `def f: .+1|f; 0|f` gives `error: Maximum evaluation
  depth (1024) exceeded`; `[` + 400 × `1,` + `1] | .[400]` is `1`; the
  program `. as $x | ` × 300 + `$x` is `null`; no crash.
- `JqInterpreterTest.PreludeParses`: `StandardPrelude()` parses with no error.

Plus the fixes' tests: `JqParserTest.TallTrees`, the new
`JqParserTest.Bindings` and `JqParserTest.SyntaxErrors` cases,
`JqJsonReaderTest`'s 100000-key object, and the renamed suites passing.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/jq/CLAUDE.md`:
- The opening paragraph: the evaluator is here now (the `jq` builtin
  itself is still `tools--jq-command`'s); add the new files to the
  pipeline list.
- A section "Evaluation" -- callback generators, the orders (right operand
  outer, key outer, last native argument outer, first `$param` outer), how
  `try` tells its own errors from downstream ones, `Env`/`Closure`, the
  registry and prelude (and that each later task registers there), and
  `JqHost` as the program's only way out (the `ICurrentProcess` rule).
- "Documented differences": the semantic-errors bullet becomes what is
  still missing (keys jq folds from constants are caught at run time;
  `a::b/1`, modules); add the tree-height limit (512 levels: a chain of
  about 500 `,` `|` operators or suffixes at one level, or as many chained
  bindings), the evaluation-depth limit (1024 levels), and `±2^63 % n`.
- The colours: punctuation "bold in the default colour (`1;39`)".

## Acceptance

- [ ] Every construct listed evaluates with jq's outputs and order; every
      message listed is byte-exact.
- [ ] `try` catches only errors from its body; `break` and stop are never caught.
- [ ] Compile checks report every undefined name and literal object key in
      source order with jq's text.
- [ ] Depth bounded without a crash: trees past 512 levels refused at
      compile time, evaluation past 1024 levels a `JqError`, no destructor
      recursion, no recursion per element in `..`; 300 chained bindings
      parse and run.
- [ ] The #85 and #86 findings above are fixed, each with its test; the
      Windows build compiles `JqJsonWriterTest.cpp`.
- [ ] Nothing reaches files or processes; only `JqHost`.
- [ ] No code or internal name from jq, gojq or jaq (clean room).
- [ ] `Jq.unittests` passes; the whole unit suite passes.

## Out of scope

- `path()`, `getpath`/`setpath`/`delpaths`/`del`/`paths`, and every
  assignment operator: `tools--jq-paths` (an `Assign` node may throw a
  `JqError` "assignment is not implemented yet" until then).
- The `jq` command, inputs, `$ENV`, `input`/`debug`/`stderr` natives:
  `tools--jq-command`. The builtin library: `tools--jq-builtins`,
  `tools--jq-text`. Formats other than `@text`/`@json`: `tools--jq-text`.
- Values nested deeper than the evaluation limit can still be built by a
  long `reduce` (`reduce .[] as $x (null; [.])` over a long array); their
  recursive destruction, comparison and printing belong to `JqValue` and
  the writer -- a follow-up for `tools--jq-builtins` or a fixes task.
