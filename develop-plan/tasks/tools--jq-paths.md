# Task tools--jq-paths: jq paths and assignment -- path(), getpath, setpath, del, =, |=, +=

- Rock: tools
- Depends on: tools--jq-eval
- Size: ~1300 changed lines in ~16 files
- Plan checked against: develop @ 96ecdd0
- PR title: jq: path expressions, getpath/setpath/delpaths/del and assignment

## Goal

The fourth jq task. jq's path expressions work: `path(f)`, `paths`,
`paths(f)`, `getpath`, `setpath`, `delpaths`, `del`, `to_entries`-free
updates, and every assignment operator -- `=`, `|=`, `+=`, `-=`, `*=`,
`/=`, `%=`, `//=` -- with jq 1.7.1's results and messages (`Invalid path
expression with result 1`, `Out of bounds negative array index`, ...). So
`.a.b.c = 1`, `.[] |= . + 1`, `del(.[] | select(. == 2))`, `.. |= (if type
== "number" then .+1 else . end)` behave as on Linux.

It first closes the open review findings of `tools--jq-eval` (#87) --
small fixes to the arithmetic, the compile check, `?//` and the UTF-8
helpers ("Fixes first" below) -- and one evaluator bug found while
re-checking this plan (a `?` after an index covers that index alone).

Reference: jq 1.7.1 (`jq --version` prints `jq-1.7`; Ubuntu's 1.7.1
package, on the host and in the task container). Every expected output
below was taken from it with `jq -nc '<program>'` or `echo '<json>' | jq -c
'<program>'`; for anything else, run it there and copy the result.

## Context

**Clean room -- the user's rule, above every other rule** (root
`CLAUDE.md`, "Clean-room rule"). No code is copied from any other program
or project, whatever its licence: jq is MIT, and the rule applies all the
same; so are gojq and jaq. Everything here is written from scratch.
Behaviour is matched from the jq 1.7 manual and from the observed output
of the real jq 1.7.1 -- never from its source: no jq, gojq or jaq source
is read, ported, translated or paraphrased, and none of their internal
names appears in code, comments, tests or commit messages (no `jv_*`,
`block_*`, `gen_*`, no bytecode opcode names, no path-tracking
instructions, no stack-machine layout). No definition from jq's own
library (its jq-language `paths`, `del`, `to_entries`, `with_entries`,
`walk`, its assignment helpers) is used: `path`, `paths`, `del` and the
assignment operators are Haisos's own C++ over Haisos's own path mode,
specified below only by what they output. The path mode is Haisos's
design -- a second walk over the syntax tree `tools--jq-parse` builds,
next to the value walk of `tools--jq-eval` -- and every rule here is
observable behaviour, verified on jq 1.7.1.

**Write in pieces.** Never more than ~250 lines in one Write/Edit call:
create a file with its first part, then build it up with several Edits.
Commit after each file or step (each fix below, the internal header, each
part of `JqPaths.cpp`, a test file), so a failure loses little.

Read first, as they are on develop (#85-#87 merged):
`src/components/BuiltinCommands/commands/jq/CLAUDE.md`, then
- `JqInterpreter.h/.cpp`: `Interpreter` (`Eval`, `EvalClosure`,
  `CheckStop`, `Host`, the per-kind `Eval*` helpers, `m_depth`,
  `m_nextTryId`, `m_nextLabel`), `Closure { body, env }`, `EnvPtr`,
  `MakeVariableEnv`/`MakeFunctionEnv`/`MakeLabelEnv`, `LiteralValueOf`,
  `kJqMaxEvalDepth`; and in the `.cpp`'s anonymous namespace `Env`,
  `DepthGuard`, `TryPassThrough`, `FindFunction`/`FindVariable`/
  `FindLabel`, `MakeFilterParamEnv`, `CollectPatternVariables`,
  `MatchPattern`, `DestructureAlternatives`, `BinaryResult`, `CallUser`.
  `NodeType::Assign` currently throws `assignment is not implemented yet`.
- `JqRuntime.h`: `JqError { value }`, `JqStopped`, `JqBreak`, `JqHost`,
  `Emit`.
- `JqNatives.h/.cpp`: `NativeFunction { name, arity, run, runPath }` (the
  `RunPath` type is a placeholder, reshaped below), `NativeRegistry`
  (`Add`, `Find`, `Names`, `Standard()`), `ForEachArgumentCombination`,
  `RegisterCoreNatives` (`empty`, `error/0,1`, `not`, `type`).
- `JqPrelude.cpp`: `StandardPrelude()` -- `select/1` and `recurse/1` so
  far (no `first`, `limit`, `length`, `map`: they come with
  `tools--jq-builtins`; tests here must not use them).
- `JqIndex.h`: `IndexValue`, `SliceValue`, `IterateValue` (jq's index
  rules and messages). `JqArithmetic.h`: `Add`, `Subtract`, `Multiply`,
  `Divide`, `Modulo`.
- `JqValue.h`: `Value` (immutable, payloads shared by `shared_ptr`;
  `WithMember`, `WithoutMember`, `WithElement`, `Find`, `Literal`),
  `Kind`, `KindName`, `Compare`, `Equal`. `JqJsonWriter.h`:
  `DumpTruncated(value, bufferSize)` -- the dump kept to `bufferSize - 1`
  bytes, past that its first `bufferSize - 4` bytes and `...`.
- `JqAst.h`: `Node` (children per kind in its comment; `Assign`'s `text`
  is the operator, children `[lhs, rhs]`), `Pattern`. `JqParser.cpp`:
  `ParsePostfix` (its `afterSuffix` flag, the `?` wrapping).
  `JqProgram.cpp`: the compile check (`PushPatterns`, `CheckPatternKeys`).
  `JqUtf8.h/.cpp`: `Utf8Length`, `Utf8ByteOffset`.
- Tests: `tests/unit/components/Jq.unittests/JqTestRunner.h` (`RunJq(
  program, inputJson)`: outputs compact and space-separated, an uncaught
  error as `error: <message>`, compile errors as jq prints them).

Rules that bite: portable C++17 (Linux, Windows/MSVC, WASM); nothing
outside the process (the host is `JqHost`); no recursion per element of a
value or per step of a path -- `..` in path mode walks an explicit stack as
`EvalRecurseDefault` does, and `SetPath`/`DeletePaths` walk a path with a
loop and a vector of the containers passed (a path can be as long as a
value is deep, and `reduce` can build values deeper than any stack);
recursion per construct of the tree is fine (the depth guard bounds it --
every path-mode step takes a `DepthGuard` as `Eval` does); values are
never changed in place.

## Changes

### Fixes first: the open findings of #87

Each with its test (in `JqInterpreterTest.cpp`, or `JqParserTest.cpp` for
the compile check) and its own commit.

1. **String division** (`JqArithmetic.cpp`, `SplitString`): an empty left
   gives `[]` whatever the separator (`"" / ","` and `"" / ""` are `[]`);
   an empty separator splits by code points, not bytes (`"aé" / ""` is
   `["a","é"]`, `"é😀b" / ""` is `["é","😀","b"]`, `"x" / ""` is
   `["x"]`) -- step through the string with the shared UTF-8 step of
   item 7. Unchanged: `"a,b" / ","` is `["a","b"]`, `"a" / "a"` and
   `"," / ","` are `["",""]`.
2. **String repetition is bounded and stoppable** (`Multiply`): a result
   longer than 268435456 bytes (256 MiB) fails with jq's message `Repeat
   string result too long` before anything is built (`"ab" * 1e18`, `1e18
   * "ab"`; `try ("ab" * 1e18) catch .` is `"Repeat string result too
   long"`), and the building checks for a stop at least every 1 MiB
   appended. `Multiply` (and `BinaryResult`) take an optional
   `const std::function<void()>& checkStop` that `EvalBinary` -- and the
   assignment operators below -- pass as `[this] { CheckStop(); }`.
   Unchanged: `"ab" * 0` and `"ab" * 0.5` are `""`, `"ab" * 1.5` is
   `"ab"`, a negative or NaN count `null`, `"" * 1e18` is `""` (an empty
   string never fails). Documented difference: jq's own limit is about
   2^31 bytes.
3. **An object pattern's keys do not see that pattern's variables**
   (`JqProgram.cpp`, `PushPatterns`): check each alternative's keys
   (`CheckPatternKeys`) before pushing its own names, so `. as {a: $x,
   ($x): $y} | $y` and `. as {$a, ($a): $y} | $y` fail `$x is not defined`
   / `$a is not defined` (jq's compile error, its location block and
   count line). A later `?//` alternative's keys still see the earlier
   alternatives' names: `{"null":5} | . as [$a] ?// {($a|tostring): $b} |
   $b` is `5`; and a key sees the variables of enclosing bindings
   (`{"a":"k","k":3} | . as {a:$x} | . as {($x):$y} | $y` is `3`).
4. **`?//` retries on an error raised after an inner try**
   (`DestructureAlternatives`): a downstream error crossing an inner
   `try`'s body travels as a `TryPassThrough`, which today skips the
   alternatives. Treat a `TryPassThrough` as an error of the current
   alternative too: move on to the next one, and after the last rethrow
   the `TryPassThrough` itself (so the owning try still recognises it).
   `[[1]] | try (try (.[] as [$a] ?// $a | $a) | error) catch .` is `[1]`
   (not `1`).
5. **Object merges look keys up** (low; `MergeObjects`, `MergeDeep`): no
   scan of the members per key -- look each right-hand key up in a local
   `std::unordered_map<std::string, size_t>` of the left's positions (or
   `Value::Find` for presence), keeping the order rules: a replaced member
   stays in place, a new one is appended (`{"a":1,"b":2} + {"a":3,"c":4}`
   is `{"a":3,"b":2,"c":4}`; `{"a":1,"b":{"c":1}} * {"b":{"d":2},"a":3,
   "e":4}` is `{"a":3,"b":{"c":1,"d":2},"e":4}`).
6. **The jq `CLAUDE.md`'s nesting note** (low): "an `as` binding about
   two" levels -- a chained binding costs one tree level; say so.
7. **One UTF-8 step** (low; `JqUtf8.h/.cpp`): `Utf8ByteOffset` does not
   check continuation bytes, unlike `Utf8Length`. Add `size_t
   Utf8StepAt(const std::string& utf8, size_t i)` -- the byte count of the
   code point starting at `i`, exactly as `Utf8Length` counts one (a lead
   byte, then the continuation bytes it calls for while they are there) --
   and build `Utf8Length`, `Utf8ByteOffset` and item 1's split on it.
   Test: `Utf8ByteOffset` over a lead byte followed by a non-continuation
   byte counts the lead alone (`"\xC3A"`: offset of 1 code point is 1).
8. **A `?` after an index covers that index alone** (found on re-check;
   value mode): in jq a `?` written directly after an index, slice or
   iterate suffix suppresses only the error of that one indexing step --
   not the errors of its target, nor of its key or bound expressions:
   `{"a":1} | [.a.b.c?]` fails `Cannot index number with string "b"`,
   `{"a":1} | [.a.b[]?]` too, `{"a":1} | [.[error("k")]?]` fails `k`,
   `[1] | [.[0:error("x")]?]` fails `x`; while `{"a":1} | [.a.b?]`,
   `[.a.b?.c?]` and `[(.a.b)?]` are `[]` and `{"a":{"b":1}} | [.a.b.c?]`
   is `[]`. A `?` on anything else -- a parenthesised term, a call,
   `if ... end`, another `?` -- stays a try over the whole of it.
   In the parser, mark the Try a `?` makes when it directly follows an
   index, slice or iterate (the `.a`, `."a"`, `.[...]`, `.[]` forms,
   primary or suffix: `afterSuffix` is set and the wrapped node is an
   Index, Slice or Iterate): a new `bool Node::optionalStep = false;`
   (`DumpNode` unchanged). In `EvalTry`, such a try evaluates its child's
   target and key/bounds as usual and runs only the final
   `IndexValue`/`SliceValue`/`IterateValue` call under the error
   suppression (an `IterateValue` target that is not an array or object
   gives nothing); downstream errors pass as before. Path mode keeps the
   same split (below).

### Path mode -- what it means

`path(f)` runs `f` in **path mode** on its input. In path mode every
output carries, besides its value, the path it was reached by and the
value that path leads to in `path(f)`'s input. Path-mode outputs are
**bound** when their value *is* the value their path leads to, and
**loose** otherwise:

- the path constructs -- `.`, `..`, `.[k]`/`.k`, `.[a:b]`, `.[]`,
  `getpath` -- on a bound input give bound outputs, their path extended by
  the key (`.a` by `"a"`, `.[1.7]` by `1.7`, `.[-1]` by `-1` -- the key as
  written, never normalised; a slice by an object `{"start":S,"end":E}`,
  `null` for an absent bound, `.[1.5:-1]` by `{"start":1.5,"end":-1}`;
  `.[]` by each index or member name; `getpath(p)` by every element of
  `p`);
- the other constructs (literals, strings, formats, arrays, objects,
  arithmetic, comparisons, `and`/`or`, unary minus, `$variables`,
  `$__loc__`, an assignment, a native without a path form) run in value
  mode on the input's value, and each output keeps the input's path and
  the value it leads to: bound when the output is that very value, loose
  otherwise;
- **the very value**, `Value::IsSameAs` (new, `JqValue.h`): the same
  kind, and for `null`/`false`/`true` nothing more; for a number the same
  literal instance (both carry the same shared literal), or neither has a
  literal and the doubles are equal; for a string, array or object the
  same shared payload. So a value passed through unchanged stays bound and
  a value built afresh does not, even when equal: on `1`, `path(1)` fails
  (the program's literal is another instance than the input's) while
  `path(. as $x | 2 | $x)` is `[]`; `path(null)` on `null` is `[]`;
  `true | path(true)` is `[]`, `false | path(true)` fails;
  `[1+1] | path(.[0] | 1+1)` is `[0]` (two computed 2s), `[1+1] |
  path(.[0] | 2)` fails; `{"a":1} | path(.a | . + 0)` fails;
  `{"a":[1]} | path(.a | [.[]])` fails.
- Identity-keeping arithmetic, to match: `x + null` and `null + x` are
  already `x` itself; also make `a + b` with an array `a` and an empty
  array `b`, `a + b` with an object `a` and an empty object `b`, and `a *
  b` with an object `a` and an empty object `b` return `a` itself
  (`[1] | path(. + [])`, `{"a":1} | path(. + {})` and `path(. * {})` are
  `[]`; `[1] | path([] + .)`, `path(. - [])`, `"a" | path(. + "")` fail).

Messages (each a `JqError` holding the string; dumps by `DumpTruncated`,
15 for a key, 30 for a value):
- a path construct on a loose input: `Invalid path expression near
  attempt to access element <key> of <value>` for an index or slice
  (`null | path(.a|1|.b)` -> `... access element "b" of 1`; a slice's key
  is its `{"start":..,"end":..}` object: `path(1|.[1:2])` -> `... access
  element {"start":1,... of 1`; a long key `"abcdefghij...`) and
  `Invalid path expression near attempt to iterate through <value>` for
  `.[]` (`path(1|.[])`; `path({"abcdefghijklmnopqrstuvwxyz0123456789":1}
  | .a)` -> `... access element "a" of {"abcdefghijklmnopqrstuvwx...`;
  `path(1 | .["abcdefghijklmnopqrstuvwxyz"])` -> `... access element
  "abcdefghij... of 1`);
- `path(f)` meeting a loose output: `Invalid path expression with result
  <value>` (`path(1)`; `[1,2] | path(tostring)` -> `... with result
  "[1,2]"`; `path($__loc__)` -> `... with result {"file":"<top-level>",
  "lin...`; `path([0,1,2,3,4,5,6,7,8,9,10,11,12])` -> `... with result
  [0,1,2,3,4,5,6,7,8,9,10,11...`).
- the indexing itself fails as in value mode (`IndexValue`'s words):
  `1 | path(.a)` -> `Cannot index number with string "a"`.

Per construct (each mirrors its value-mode generator order exactly --
the key outer and the target inner for an index, from then to then target
for a slice):
- `.`: the input. `..` on a bound input: the input, then every value
  below it, pre-order, iteratively (`{"a":[1,2]} | path(..)` is `[]
  ["a"] ["a",0] ["a",1]`; scalars have nothing below). `..` on a loose
  input: the input itself (loose), then the failure `... iterate through
  <value>` -- for any kind (`[path(1 | .. | empty)]` and `[path([] | .. |
  empty)]` fail so; `[path(null | ..)]` is `[[]]`, the null being bound).
- `.[k]`, `.[a:b]`, `.[]`: as above. With `?` (item 8's marked try): the
  indexing step's own *value* errors are suppressed, but **not** the
  invalid-path failure: `null | path(1|.a?)`, `path(1|.[0]?)`,
  `path(1|.[]?)`, `path(1|.[1:]?)` and `path(1|.a?.b?)` fail `Invalid path
  expression near attempt to ...`, while `1 | path(.a?)` gives nothing
  (bound input, value error suppressed).
- `try B` / `try B catch C` / a `?` on anything else: `B` in path mode;
  its errors -- the invalid-path ones included -- are caught as in value
  mode (`path(1|try .a)`, `path(1|(.a)?)`, `path(1|.a??)` give nothing);
  `C` runs in path mode on the error's value as a loose output with the
  try's input path (`1 | path(try .a catch .)` fails `... with result
  "Cannot index number with ...`).
- `|`, `,`, `label`/`break`, `def`: as in value mode, in path mode.
- `if`: the conditions in value mode on the input's value, the chosen
  branch (or the missing `else`'s `.`) in path mode (`path(if .a then .b
  else .c end)` is `["c"]`).
- `A // B`: `A` in path mode, its outputs with a truthy value emitted;
  when there are none (or `A` fails, as in value mode), `B` in path mode
  (`path(.a // .b)` is `["b"]` on `null`, `["a"]` on `{"a":1}`).
- `E as $x | B` (and patterns, `?//`): `E` in value mode on the input's
  value, `B` in path mode on the input (`path(.a as $x | .b)` is `["b"]`;
  `{"a":[1]} | path(.a | . as $x | $x | .[0])` is `["a",0]`; `{"a":[1]} |
  path(.a as $x | $x)` fails `... with result [1]`). Destructuring adds
  nothing to the path (a documented difference below).
- `reduce SRC as $x (INIT; UPDATE)`: `INIT` in path mode; for each of its
  outputs `s0`, the accumulator starts as `s0`'s value, and for each
  output of `SRC` (value mode, on the input's value) `UPDATE` runs in path
  mode on an output with `s0`'s path and target value and the accumulator
  as its value -- its last output's value becomes the accumulator (null
  when it gave none); the reduce outputs the final accumulator with `s0`'s
  path and target value (bound or loose by the rule above). So `null |
  path(reduce (1,2) as $x (.; .a))` is `[]`; `{"a":{"a":1}} | path(reduce
  (1,2) as $x (.; .a))` fails `... access element "a" of {"a":1}`;
  `{"a":[1]} | path(reduce 1 as $x (.; .a))` fails `... with result [1]`;
  `{"a":[1]} | path(reduce 1 as $x (.a; .))` is `["a"]`; `null |
  path(reduce 1 as $x (.a; .b))` is `["a"]`.
- `foreach SRC as $x (INIT; UPDATE[; EXTRACT])`: the same, but every
  `UPDATE` output is emitted as it is (through `EXTRACT` in path mode when
  given), and its value becomes the accumulator: `{"a":[1]} |
  path(foreach 1 as $x (.a; .[0]))` is `["a",0]`; `path(foreach (1,2) as
  $x (.a; .; .[0]))` is `["a",0] ["a",0]`; `{"a":{"a":1}} | path(foreach
  (1,2) as $x (.; .a))` is `["a"]` then fails `... access element "a" of
  {"a":1}`.
- A call: a filter parameter's closure in path mode; a user function's
  body in path mode (its `$value` parameters bound in value mode, on the
  input's value, as `CallUser` binds them); a native: its path form when it
  has one, else its value form on the input's value, each output kept as
  above (`path(def f: .a; f)` is `["a"]`; `{"a":[1]} | path(.a | def
  f(g): g; f(.[0]))` is `["a",0]`; `path(.a | select(true))` is `["a"]`,
  `select` and `recurse` being prelude definitions).

### `JqInterpreterInternal.h` (new) -- what both walks share

Move out of `JqInterpreter.cpp`'s anonymous namespace into a header used
only by `JqInterpreter.cpp` and `JqPaths.cpp`, in a nested namespace
`Haisos::Jq::detail`, unchanged: `Env`, `DepthGuard`, `TryPassThrough`,
`FindFunction`, `FindVariable`, `FindLabel`, `MakeFilterParamEnv`,
`CollectPatternVariables`, `MatchPattern`, `DestructureAlternatives`.
Split `CallUser` into `BindUserCall(Interpreter&, const Env& binding,
const Node& call, const Value& input, const EnvPtr& callerEnv, const
std::function<void(const EnvPtr& bodyEnv)>& run)` -- the parameter
binding as today, every combination handed to `run` -- and the value
walk's `run` that evaluates the body; path mode passes its own. No
behaviour changes; the existing tests must pass untouched before the next
step (commit it alone).

### `JqPaths.h` / `JqPaths.cpp` (new)

```cpp
// One output of a path-mode walk: its value, the path it was reached by
// (keys: strings, numbers, or {"start":..,"end":..} slice objects; any
// value an index was given), and the value that path leads to in the
// walk's root. Bound when value.IsSameAs(target).
struct PathValue {
    Value value;
    std::vector<Value> path;
    Value target;
    bool Bound() const { return value.IsSameAs(target); }
    static PathValue Root(const Value& v) { return {v, {}, v}; }
};
using EmitPath = std::function<void(const PathValue&)>;

// The input |root| with every path of |paths|... (see below).
Value GetPath(const Value& root, const Value& path);                 // getpath/1
Value SetPath(const Value& root, const Value& path, const Value& v);  // setpath/2
Value DeletePaths(const Value& root, const Value& paths);             // delpaths/1

void RegisterPathNatives(NativeRegistry& registry);  // called by Standard()
```

`Interpreter` gains (declared in `JqInterpreter.h`, defined in
`JqPaths.cpp`): `void EvalPaths(const Node&, const PathValue& input, const
EnvPtr&, const EmitPath&)` (the path walk above, one helper per kind as
`Eval` has, each with its `DepthGuard`), `void EvalClosurePaths(const
Closure&, const PathValue& input, const EmitPath&)`, and the private
`EvalAssign` (below), which `Eval`'s `NodeType::Assign` case calls.

`NativeFunction::RunPath` becomes `std::function<void(Interpreter&, const
PathValue& input, const std::vector<Closure>&, const EmitPath&)>` (fix the
`JqNatives.h` comment too).

**Natives** (`RegisterPathNatives`; `path`, `paths/1` and `del` take their
argument as a closure, the others take values through
`ForEachArgumentCombination` -- one call per combination, the last
argument outermost):
- `path/1`: `EvalClosurePaths(f, PathValue::Root(input))`, each bound
  output's path emitted as an array, a loose one failing `Invalid path
  expression with result <dump 30>`. No path form (`path(path(.a))`
  fails `... with result ["a"]`).
- `getpath/1`: `GetPath(input, p)`: `p` not an array fails `Path must be
  specified as an array`; otherwise `IndexValue` step by step, so a
  `null` met along the way gives `null` for the string, number and object
  keys after it (`{} | getpath(["x","y"])` and `null | getpath([1,"a"])`
  are null) and fails for the others (`null | getpath([true])` -> `Cannot
  index null with boolean`; `[1,[2]] | getpath([1,0,0])` -> `Cannot index
  number with number`; `{"a":1} | getpath(["a","b"])` -> `Cannot index
  number with string "b"`; `[1,2,3] | getpath([{"start":1,"end":null}])`
  is `[2,3]`; `getpath([{"start":1}])` fails `Array/string slice indices
  must be integers`). Path form: the value computed the same way (same
  errors); bound input -> bound output, path extended by every element of
  `p`; loose input -> loose output (`path(getpath(["x","y"]))` is
  `["x","y"]`; `null | path(.a | getpath(["b","c"]))` is `["a","b","c"]`;
  `{"a":{"b":1}} | path(.a | {"b":1} | getpath(["b"]))` fails `... with
  result 1`; `null | path(1|getpath(["a"]))` fails `Cannot index number
  with string "a"`).
- `setpath/2`, `delpaths/1`: `SetPath`, `DeletePaths` below. No path form.
- `paths/0`: every path of `..` on the input but the empty one, as arrays,
  pre-order (`{"a":[1,{"b":2}]} | [paths]` is
  `[["a"],["a",0],["a",1],["a",1,"b"]]`; `1 | [paths]` is `[]`).
- `paths/1` (`paths(f)`): for each path `p` of `paths/0`, `f` evaluated on
  the value at `p`, and `p` emitted once per truthy output (`{"a":[1,
  {"b":2}]} | [paths(type == "number")]` is `[["a",0],["a",1,"b"]]`;
  `{"a":1} | [paths(true,true)]` is `[["a"],["a"]]`; `f`'s errors
  propagate).
- `del/1` (`del(f)`): every path of `f` in path mode on the input
  (each must be bound: `del(1)` fails `Invalid path expression with
  result 1`), collected, then deleted together as `DeletePaths` does
  (`[1,2,3] | del(.[] | select(. == 2))` is `[1,3]`; `[1,2,3,4] |
  del(.[0,2])` is `[2,4]`, `del(.[1:3])` `[1,4]`; `[1,2,3] | del(.[0],
  .[0])` is `[2,3]`; `{"a":1} | del(.)` is `null`, `del(empty)` the
  input; `{"a":1} | del(.a.b)` fails `Cannot index number with string
  "b"`; `{"a":[1,2,3]} | del(.a[0:2][0])` is `{"a":[2,3]}`).

**SetPath** (`setpath(p; v)`; `p` not an array fails `Path must be
specified as an array`; `setpath([]; v)` is `v`): walk `p` from the root
keeping the containers met in a vector, then rebuild upwards with
`WithMember`/`WithElement`/a new array (never in place). At each step,
with container `c` and key `k`:
- `c` null: a string `k` makes an object, a number or slice `k` an array
  (`null | .[2] = 1` is `[null,null,1]`; `null | .[1:3] = ["x"]` is
  `["x"]`), any other key fails as indexing null does (`Cannot index null
  with boolean`, `... with null`).
- `c` an object: `k` a string, else `Cannot index object with <keykind>`
  (`{"a":1} | setpath([0]; 1)`, `setpath([{"start":0,"end":1}]; [1])` ->
  `... with object`).
- `c` an array: a number `k` floored (`[1,2,3] | setpath([1.5]; "x")` is
  `[1,"x",3]`); a negative counts from the end, and before the start
  fails `Out of bounds negative array index` (`[1] | .[-5] = 1`; also on
  `null` and `[]`: `.[-1] = 1`); past the end pads with nulls; an index of
  536870912 or more fails `Array index too large` (`[1] | .[1e10] = 1`),
  and so -- Haisos's own limit, documented -- does an index more than
  1048576 beyond the array's current length (`null | .[1048577] = 1`). A slice `k` (an object whose `start` and `end` are
  each a number or null; anything else, a missing member included, fails
  `Array/string slice indices must be integers`) is resolved as
  `SliceValue` resolves its bounds (negatives from the end, clamped, start
  floored, end ceiled, an end before the start taken as the start); the
  new value at that step must be an array (`A slice of an array can only
  be assigned another array`) and replaces the range: `[1,2,3] |
  setpath([{"start":1,"end":2}]; ["x","y"])` is `[1,"x","y",3]`,
  `setpath([{"start":5,"end":7}]; ["x"])` `[1,2,3,"x"]`,
  `setpath([{"start":2,"end":1}]; ["x"])` `[1,2,"x",3]`,
  `setpath([{"start":1.5,"end":2.2}]; ["x"])` `[1,"x"]`; a path may
  continue below a slice (`[[1,2]] | setpath([0,{"start":0,"end":1},0];
  9)` is `[[9,2]]`; `[1,2,3] | .[1:][0] = 9` is `[1,9,3]`). An array key
  fails `Cannot update field at array index of array`; a string `Cannot
  index array with string "x"` (named while shorter than 30 bytes, as
  `IndexValue` does); others `Cannot index array with <keykind>`.
- `c` a string with a slice `k`: `Cannot update string slices`; any other
  kind/key: `Cannot index <kind> with <keykind>` as `IndexValue` words it
  (`1 | setpath(["a"]; 1)` -> `Cannot index number with string "a"`).

**DeletePaths** (`delpaths(ps)`): `ps` not an array fails `Paths must be
specified as an array`; an element not an array fails `Path must be
specified as array, not <kind>`. Every path refers to the input as it was
before any deletion -- positions never shift under each other:
`[1,2,3,4] | delpaths([[0],[2]])` is `[2,4]`; `[1,2,3,4,5] |
delpaths([[{"start":1,"end":3}],[3]])` is `[1,5]`, with `[1]` instead of
`[3]` `[1,4,5]`; `delpaths([[{"start":3,"end":null}],[{"start":0,
"end":1}]])` `[2,3]`; `[1,[2]] | delpaths([[1,0],[0]])` is `[[]]`. A path
listed twice counts once, and a path below another listed path is dropped
(`{"a":1} | delpaths([["a"],["a","x"]])` is `{}`; `[1,2,3] |
delpaths([[0],[0,"a"]])` is `[2,3]`); the empty path deletes everything:
`delpaths([[]])` (or with others) is `null`. One way: sort and deduplicate
with `Compare`, drop the paths with a listed prefix, group the rest by
parent path, and apply the groups from the longest parent to the
shortest, each group's deletions computed against its parent as it stands
then (deeper groups change contents, never the positions of a shallower
group) -- all by loops, no recursion per path step. Per path:
- the parent is reached by `IndexValue` steps (a slice key through
  `SliceValue`-style resolution, so a path may go below a slice); a
  missing member, an index out of range or a `null` met on the way means
  nothing to delete (`{"a":{"x":1}} | delpaths([["a","z","y"]])`, `{"a":
  [1]} | delpaths([["a",5,"y"]])`, `null | delpaths([["a","b"]])`
  unchanged); a kind mismatch fails as indexing does (`[1,2] |
  delpaths([["a","b"]])` -> `Cannot index array with string "a"`).
- the last key, with the parent: null -> nothing; an object -> a string
  key's member removed (absent: nothing), another kind fails `Cannot
  delete <keykind> field of object` (`{"a":1} | delpaths([[0]])` ->
  `Cannot delete number field of object`, a slice -> `... object field
  of object`); an array -> a number floored, negative from the end, out of
  range ignored (`[1,2,3] | delpaths([[-1]])` is `[1,2]`, `[[5]]` and
  `[[-5]]` change nothing, `[[1.5]]` is `[1,3]`), or a slice's resolved
  range (`[1,2,3] | delpaths([[{"start":0,"end":2}]])` is `[3]`; a bad
  slice fails `Array/string slice indices must be integers`), another
  key kind fails `Cannot delete <keykind> element of array` (`[[null]]`,
  `[["a"]]`, `[[[1]]]`); a number, string or boolean parent fails `Cannot
  delete fields from <kind>` (`1 | delpaths([["a"]])`, `"x" |
  delpaths([[0]])`, `"abc" | delpaths([[{"start":0,"end":1}]])`, `{"a":1}
  | delpaths([["a","x"]])`).
Which failure is reported when several paths fail is not pinned.

### Assignment (`Interpreter::EvalAssign`)

Implemented in C++ over `EvalPaths`, `GetPath`, `SetPath` and
`DeletePaths` -- never by rewriting the program text. The paths of `lhs`
are always taken from the assignment's original input (in path mode from
`PathValue::Root(input)`, each loose one failing `Invalid path expression
with result <dump 30>`: `1 = 2`, `1 |= 2`, `path(.a) |= 1` -> `... with
result ["a"]`; their errors propagate: `{"a":1} | (.a, error("p")) |= 5`
fails `p`), never from the value being updated
(`{"a":1} | .. |= (if type=="object" then {"b":{"c":1}} else 2 end)` is
`{"b":{"c":1},"a":2}` -- the second path, `["a"]`, is the original's).

- `lhs = rhs`: for each output `v` of `rhs` on the original input (rhs
  outer): the input with every path of `lhs` set to `v`, in order, one
  output (`null | .a = (1,2)` is `{"a":1} {"a":2}`; `(.a,.b) = (1,2)`
  is `{"a":1,"b":1} {"a":2,"b":2}`; `{"a":1,"b":2} | (.a,.b) = (.b,.a)`
  is `{"a":2,"b":2} {"a":1,"b":1}`; `{"a":1} | .a = .b` is `{"a":null}`;
  `.a = empty` gives nothing).
- `lhs |= f`: one output. For each path `p` of `lhs`, in order: `f` runs
  on the value at `p` in the result so far; its **first** output replaces
  that value (`f` is not run further: `{} | .a |= (.,.)` is `{"a":null}`,
  `{"a":1} | .a |= (., error("x"))` is `{"a":1}`, `.a |= (empty, 5)` is
  `{"a":5}`); when `f` gives nothing, `p` is set aside, and after the
  last path all set-aside paths are deleted together (`DeletePaths`):
  `[1,2,3] | .[] |= empty` is `[]`; `[1,2,3,4,5] | (.[] | select(. > 2))
  |= empty` is `[1,2]`; `[1,2,3] | (.[0],.[1]) |= empty` is `[3]`;
  `[[1,2],[3]] | .[][] |= empty` is `[[],[]]`; `{"a":1} | .a |= empty` is
  `{}`. `f`'s errors propagate (`[1,2,3] | .[0] |= error("x")` fails
  `x`).
- `lhs op= rhs` for `+ - * / %`: for each output `x` of `rhs` on the
  original input (rhs outer): `lhs |= . op x` (`{} | .a += 1` is
  `{"a":1}`; `{"a":1,"b":2} | .a += (1,2)` is `{"a":2,"b":2}
  {"a":3,"b":2}`; `{"a":[1,2]} | .a[] += .a[0]` is `{"a":[2,3]}`;
  `{"a":[1,2]} | .a += 1` fails `array ([1,2]) and number (1) cannot be
  added`; `null | .a -= 1` fails `null (null) and number (1) cannot be
  subtracted`; `.a += empty` gives nothing). The operator through
  `BinaryResult` with the stop check.
- `lhs //= rhs`: for each output `x` of `rhs` on the original input:
  `lhs |= (. // x)` (`{"a":[1,2]} | .a //= 3` unchanged; `{"a":null,
  "b":false} | .[] //= 3` is `{"a":3,"b":3}`; `{"a":null} | .a //= (1,2)`
  is `{"a":1} {"a":2}`; `.a //= empty` gives nothing).
- An assignment in path mode is a value-mode construct (`path(.a = 1)`
  fails `... with result {"a":1}`).

### Prelude and registry

`NativeRegistry::Standard()` calls `RegisterPathNatives` after
`RegisterCoreNatives`. Nothing is added to the prelude: `path`, `paths`,
`del` are natives. Give no native but `getpath` a path form (`empty`'s
and `error`'s value forms already behave right in path mode).

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add
`commands/jq/JqPaths.cpp`. `tests/unit/components/Jq.unittests/
CMakeLists.txt`: add `JqPathsTest.cpp`.

## Tests

Fixes (in `JqInterpreterTest.cpp` unless said): every example of items
1-4 and 8 above, each in its own `TEST`; item 3's two failures in
`JqParserTest.cpp` or wherever the compile errors are tested now (the
whole jq output, location block and count line); item 5's two merges;
item 7's `Utf8ByteOffset` case (a `JqUtf8` test in `JqValueTest.cpp` or a
new `JqUtf8Test.cpp`); for item 2 a stop: a `TestJqHost` whose
`StopRequested` turns true makes `"ab" * 1e8` (200 MB, under the limit)
end in `JqStopped`, not a full build.

`tests/unit/components/Jq.unittests/JqPathsTest.cpp`, `RunJq` with the
expected values above, all verified on jq 1.7.1:
- `JqPathsTest.PathOf`: `path(..)`, `path(.a[1:])`, `path(.[1.5:-1])`,
  `path(.[:2])`, `path(.[-1])`, `path(.[1.7])`, `path(.[1,2])`,
  `path(.["a","b"])`, `path(.[1,2:3])`, `path(if ...)`, both `path(.a //
  .b)`, `path(label $f | .a, break $f)` (`["a"]`), `path(.a as $x | .b)`,
  `path(getpath(["x","y"]))`, `path(.a | getpath(["b","c"]))`,
  `path(empty)`, `{"a":1} | path(.[]?)` (`["a"]`), `1 | path(.a?)`
  (nothing), `path(.. | select(type == "number"))` on `{"a":[1,{"b":2}]}`
  (`["a",0] ["a",1,"b"]`), `path(def f: .a; f)`, the `def f(g)` case,
  `[[1]] | path(.[0] | ..)` (`[0] [0,0]`), `path(.[1:2] | .[0])`
  (`[{"start":1,"end":2},0]`).
- `JqPathsTest.BoundAndLoose`: every `IsSameAs` example of "Path mode"
  (`path(null)`, `true | path(true)`, `false | path(true)`, `1 |
  path(1)`, `path(. as $x | 2 | $x)`, the `1+1` pair, `. + 0`, `[.[]]`,
  `. as $x | $x | .[0]`, `{"a":null} | path(.a | null | .b)` (`["a","b"]`),
  the identity-keeping `+`/`*` cases and their failing neighbours).
- `JqPathsTest.InvalidPaths`: each message with its truncation (the
  long key, the long object, the long array, `$__loc__`, the slice key),
  `..` on a loose value (both the `1` and the `[]` cases), the `?` cases
  that do not suppress it and the `try`/`(...)?`/`??` ones that do,
  `try .a catch .`'s loose result, `path(path(.a))`.
- `JqPathsTest.ReduceForeach`: the five `reduce` and three `foreach`
  examples.
- `JqPathsTest.GetSetDelete`: every `getpath`/`setpath`/`delpaths`
  example and message above, the `Array index too large` cases (jq's
  `1e10`, and Haisos's `null | .[1048577] = 1`), `paths`, `paths(f)` and
  every `del` example.
- `JqPathsTest.Assignment`: every assignment example above plus `{} |
  .a.b.c = 1` (`{"a":{"b":{"c":1}}}`), `{"a":null} | .a.b.c |= 1`
  (same), `[1,2] | .[0] |= . + 10` (`[11,2]`), `{"a":[{"b":1},{"b":2}]} |
  .a[].b |= . * 10` (`{"a":[{"b":10},{"b":20}]}`), `[1,2,3] | .[1:] =
  ["x","y"]` (`[1,"x","y"]`), `"abc" | .[1:] |= "Z"` (`Cannot update
  string slices`), `{"a":"abc"} | .a[1:] += "x"` (same), `{"a":[1]} |
  .a[1:] += ["x"]` (`{"a":[1,"x"]}`), `[1,2,3] | .[-1:] = ["z"]`
  (`[1,2,"z"]`), `[1,2,3] | .[:-1] |= []` (`[3]`), `{} | .["a","b"] = 1`
  (`{"a":1,"b":1}`), `{"a":5} | .a %= 2 | .b //= 3` (`{"a":1,"b":3}`),
  `[1,2,3] | .. |= (if type=="number" then .+1 else . end)` (`[2,3,4]`),
  `[3,1] | .[] += 1` (`[4,2]`), `[1] | .[0] = (.[0] | . + 1)` (`[2]`),
  `{"a":[1]} | .a[0] as $x | .a[0] |= $x+1` (`{"a":[2]}`).
- `JqPathsTest.LongPaths`: a `Value` nested 2000 arrays deep, built in
  C++, and a 2000-element path of zeros: `GetPath` gives the innermost
  value, `SetPath` replaces it (and `GetPath` reads the new one back),
  `DeletePaths` empties the innermost array; `path(..)` over it through
  the interpreter (outputs counted in the emitter) gives 2001 paths. Not
  deeper: destroying a `Value` is itself recursive per level, so a much
  deeper value cannot be tested yet (the loops are checked in review).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/jq/CLAUDE.md`:
- a section "Paths" -- path mode, bound and loose outputs and
  `Value::IsSameAs`, which constructs are path constructs, `reduce` and
  `foreach` in path mode, a `?` after an index keeping the invalid-path
  failure, getpath/setpath/delpaths and their rules (deletions against the
  original positions, prefix paths), assignment (`|=`'s first output and
  deferred deletion, `op=` and `//=` over `|=`, paths from the original
  input);
- file bullets for `JqPaths.h/.cpp` and `JqInterpreterInternal.h`; the
  `JqNatives` bullet lists the path natives, the `JqArithmetic` one the
  identity-keeping cases, the repetition limit and the code-point split,
  the `JqUtf8` one `Utf8StepAt`; the `?` rule of item 8 in the evaluation
  section;
- item 6's correction; "Documented differences": drop "The `Invalid
  path expression` family belongs to paths, a later task"; add the
  string repetition limit (256 MiB, jq's about 2^31), the array padding
  limit (`Array index too large` past 1048576 beyond the end; jq's only
  from index 536870912), and destructuring in a path expression: a pattern
  adds nothing to the path, where jq 1.7.1 appends the pattern's own
  indexing (`null | path(. as [$a] | .b)` is `["b"]` here, `[0,"b"]` in
  jq; `{"a":{"b":1}} | path(. as {a:$x} | .a)` is `["a"]` here, a failure
  in jq).

`src/components/BuiltinCommands/CLAUDE.md`, the `commands/jq/` bullet:
the evaluator, paths and assignment are there; the `jq` builtin is a
later task.

## Acceptance

- [ ] Every example above gives jq 1.7.1's output; every message is
      byte-exact (the documented differences aside).
- [ ] #87's seven findings and item 8 fixed, each with its test.
- [ ] Bound/loose follows `Value::IsSameAs`; `reduce`/`foreach` behave
      as listed in path mode.
- [ ] `|=` uses the first output and deletes empty-update paths at the
      end; `delpaths` works against the original positions.
- [ ] No recursion per element of a value or per path step (loops and an
      explicit stack; `LongPaths` passes); values never changed in place.
- [ ] No jq library definition and no jq-internal name anywhere.
- [ ] `Jq.unittests` passes; the whole unit suite passes.

## Out of scope

- `to_entries`, `from_entries`, `with_entries`, `map`, `map_values`,
  `walk`, `pick`, `tostream`, `leaf_paths`, `first`, `limit`, `length`
  and the rest of the library (`tools--jq-builtins`); the command
  (`tools--jq-command`).
- Windows' stack depth for `Jq.unittests` (`final--windows-fix`).
