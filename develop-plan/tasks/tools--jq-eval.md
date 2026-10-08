# Task tools--jq-eval: the jq evaluator -- generators, variables, functions, errors

- Rock: tools
- Depends on: tools--jq-parse, tools--jq-json
- Size: ~1000 changed lines in ~10 files
- Plan checked against: develop @ ccb9dbe
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

Reference: the task container's jq (`jq-1.7`, Ubuntu's 1.7.1 package).
Every expected output below was taken from it with `jq -nc '<program>'`;
for anything else, run it there and copy the result.

## Context

Read first: `src/components/BuiltinCommands/commands/jq/CLAUDE.md`, the
headers `JqAst.h`, `JqParser.h` (from `tools--jq-parse`: `Node`,
`NodeType`, `Pattern`, `FunctionDefinition`, `ParseProgram`,
`CompileError`, `FormatCompileError`) and `JqValue.h`, `JqJsonWriter.h`
(from `tools--jq-json`: `Value`, `Kind`, `KindName`, `Compare`, `Equal`,
`WriteJson`, `FormatNumber`, `DumpTruncated`, `CanonicalNumberLiteral`).

Rules that bite: portable C++17, no `<regex>`, no POSIX headers. Nothing
here reaches outside the process: whatever a program reads or writes
(inputs, `debug`, `stderr`) goes through the `JqHost` interface below, which
the jq builtin implements over its `BuiltinContext` -- so `ICurrentProcess`
stays the only door. These classes implement no `interfaces/` interface;
`Program` is created by a static `Compile` returning `std::shared_ptr`
anyway (it is shared by the command and its natives).

## Changes

New files in `src/components/BuiltinCommands/commands/jq/`.

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

Evaluation is **callback-based**: `Eval` calls `emit` once per output, in
order, and a generator nested in another simply calls its own callback
inside the outer one. jq's backtracking is that recursion; stopping early
(`label`/`break`, `limit`, `first`) is an exception (`JqBreak`) caught by
whoever started it. Errors are `JqError` exceptions.

Semantics per node (orders verified in the container; "A outer, B inner"
means: for each output of A, every output of B):
- `Identity`: the input. `RecurseDefault` (`..`): the input, then
  recursively every element/member value, pre-order, objects in insertion
  order (`{"a":[1,{"b":2}]}` gives the whole, `[1,{"b":2}]`, `1`, `{"b":2}`,
  `2`) -- iteratively, with an explicit stack (no C++ recursion per level).
- `Index`: key outer, target inner (`[([1,2],[3,4])[0,1]]` is
  `[1,3,2,4]`). `Slice`: from outer, then to, then target
  (`[([1,2,3],[4,5,6])[(0,1):(2,3)]]` is
  `[[1,2],[4,5],[1,2,3],[4,5,6],[2],[5],[2,3],[5,6]]`). `Iterate`: arrays'
  elements, objects' values in order. The indexing rules and messages are
  in "Indexing" below (put them in `JqIndex.h/.cpp`: `tools--jq-paths`
  reuses them).
- `Literal`: `true`/`false`/`null`, or `Value::NumberLiteral` of the
  canonical literal (`1.0` prints `1.0`). Build each Literal's value once.
- `String`: interpolations, the **last** outermost (`"\(1,2)-\(3,4)"` gives
  `"1-3" "2-3" "1-4" "2-4"`); each interpolated value is converted by the
  format (`ApplyFormat(name, value)`) or, with none, by `tostring` (a
  string as is, anything else its compact JSON). `Format` alone applies to
  the input. `ApplyFormat` lives in `JqFormat.h/.cpp`: here only `text`
  (= tostring) and `json` (= compact JSON); any other name fails with
  `<name> is not a valid format` (`tools--jq-text` adds the rest).
- `Array`: all outputs of the body collected (an error inside propagates).
- `Object`: entries left to right, the first outermost; within an entry the
  key outer, the value inner (`{("a","b"): (1,2)}` gives `{"a":1}
  {"a":2} {"b":1} {"b":2}`; `{a:(1,2), b:(3,4)}` a outer). A key that is
  not a string: `Cannot use <kind> (<DumpTruncated 15>) as object key`.
- `Negate`: numbers (a computed result); else `<kind> (<dump15>) cannot be negated`.
- `Pipe`: right on each output of left. `Comma`: left's outputs, then right's.
- `Alternative` (`a // b`): the truthy outputs of `a`; if there was none, the
  outputs of `b`. Errors in `a` propagate (`[(1, error("x"), 2) // 3]`
  fails with `x` in jq 1.7.1).
- `Binary` arithmetic and comparison: the **right** operand outer, the
  left inner (`[(1,2) + (10,20)]` is `[11,12,21,22]`). See "Arithmetic".
- `And`/`Or`: the left operand outer; `and`: a falsy left gives `false`,
  a truthy one gives the truthiness of each right output; `or` the mirror
  (`[(true,false) and (true,false)]` is `[true,false,false]`,
  `[(true,false) or (true,false)]` is `[true,true,false]`).
- `If`: for each output of the condition, the matching branch on the input;
  no `else` means `.`.
- `Try`: the body's outputs; when the body raises a `JqError`, the
  catch body runs on the error's value (or nothing, with no catch) and the
  body is **not resumed** (`try (1, error("x"), 3) catch .` gives `1 "x"`).
  Only errors raised *inside the body* are caught -- not those raised by
  whatever consumes its outputs downstream: the emit callback given to the
  body wraps `JqError`s thrown by the downstream `emit` in a private
  `TryPassThrough { uint64_t tryId; JqError error; }`, and this `Try`
  rethrows its own pass-through as the plain `JqError` once outside the
  body. `JqBreak` and `JqStopped` are never caught. `error(null)` caught
  gives `null`.
- `Reduce`: init outer (each init output starts its own reduction:
  `[reduce empty as $x (1,2; .)]` is `[1,2]`); the state becomes the
  **last** output of the update, or `null` when it produced none
  (`[reduce (1,2) as $x (0; empty)]` is `[null]`).
- `Foreach`: init outer; per item, every output of the update is emitted
  (through the extract, if given) and the last becomes the state; no output:
  nothing emitted and the state becomes `null` (`[foreach (1,2,3) as $x (0;
  if $x == 2 then empty else . + $x end; .)]` is `[1,3]`;
  `[foreach (1,2) as $x (0; (. + $x), 7)]` is `[1,7,9,7]`).
- `Bind` (`S as P | B`): for each output of `S`, destructure, then `B` on
  the original input. Destructuring: `$x` binds; `[P0, P1]` binds `Pi`
  to `.[i]` of the value; `{key: P}` to `.[key]` (the key an expression;
  each of its outputs, outer to inner); `{$a}` binds `$a` to `.a`; `{$a: P}`
  both. Indexing errors are the usual ones (`1 as [$a] | $a` fails with
  `Cannot index number with number`). With `?//` alternatives: every
  variable of every alternative is bound, `null` unless the alternative in
  use sets it; alternative 1 is tried first; an error raised while
  destructuring **or by the body** under alternative i moves on to i+1 (with
  all variables back to null; outputs already emitted stay); the last
  alternative's error propagates (`[[1,2]] | [.[] as [$a] ?// $a | if $a == 1
  then error("x") else $a end]` is `[[1,2]]`).
- `Defs`: the definitions are added to the scope in order, each seeing
  itself (recursion) and the earlier ones; then the scope expression.
- `Call`: a user function (innermost definition with that name and arity),
  else a filter parameter (arity 0), else a native. A user function with
  parameters: each `$name` parameter is evaluated on the caller's input, the
  **first** such parameter outermost (`[range(0,1; 3,4)]` order), and bound
  both as `$name` and as the filter `name`; each filter parameter is bound
  as a `Closure` over the caller's env; the body runs on the caller's input
  in the definition's env plus those bindings. A filter parameter call runs
  its closure body in the closure's env, on the current input.
- `Variable`: lookup; `$ENV` and the command's named arguments are globals
  given to `Program::Run`. `Loc`: `{"file":"<top-level>","line":N}`.
- `Label`: a fresh instance id bound under the name; a `JqBreak` with that
  id ends the label's outputs quietly. `Break`: throws `JqBreak`.

`CheckStop()` at every function call, every `Iterate` element, every
`reduce`/`foreach` item and every `..` step.

**Depth.** jq grows its own stack in heap memory; Haisos recurses in C++.
Count nested function calls (user, parameter and native) and fail beyond
`kJqMaxCallDepth = 512` with the `JqError` string `Maximum call depth (512)
exceeded` -- a documented exception (jq runs `def f: if . < 100000 then
.+1|f else . end; 0|f`; Haisos refuses it). Keep `Eval`'s frames small
(no large locals; helpers for the big cases), so 512 levels fit a 1 MB
(Windows) thread stack with room to spare; a test runs 500 levels.

### Arithmetic (`JqArithmetic.h/.cpp`)

```cpp
Value Add(const Value& a, const Value& b);   // and Subtract, Multiply, Divide, Modulo
```
Messages: `<k1> (<d1>) and <k2> (<d2>) cannot be added` (`subtracted`,
`multiplied`, `divided`, `divided (remainder)`), dumps `DumpTruncated(v, 15)`
(`string ("abcdefghij...) and number (1) cannot be subtracted`).
- `+`: `null + x` and `x + null` are `x` itself (a literal survives:
  `[3.0] | add` is `3.0`); numbers; strings concatenated; arrays
  concatenated; objects merged, right wins, in place (`{"a":1,"b":2} +
  {"c":0,"a":3}` is `{"a":3,"b":2,"c":0}`).
- `-`: numbers; arrays: the left without every element equal to one of the
  right's.
- `*`: numbers; a string and a number (either order): `n < 0` (or NaN)
  gives `null`, else the string repeated `floor(n)` times (`"ab" * 0` is
  `""`, `* 0.5` `""`, `* 1.5` `"ab"`, `* 2.5` `"abab"`, `* -1` `null`);
  two objects merged recursively (`{"a":{"b":1}} * {"a":{"c":2}}` is
  `{"a":{"b":1,"c":2}}`).
- `/`: numbers, a zero divisor failing with `... cannot be divided because
  the divisor is zero`; two strings: the left split on the right (`"a,b" /
  ","` is `["a","b"]`; splitting on `""` gives the characters -- verify).
- `%`: both numbers converted to 64-bit integers by truncation, saturating
  at the int64 range (NaN -> 0); a zero divisor (after truncation: `5 %
  0.5`) fails with `... cannot be divided (remainder) because the divisor is
  zero`; C's `%` (`5 % -2` is 1, `-5 % 3` is -2, `5.9 % 2.1` is 1, `1e30 %
  7` is 0), `INT64_MIN % -1` is 0.
- Comparisons with `Compare` (`1 < "a"` is true). Results of arithmetic
  are computed numbers (`Value::Number`): `1.0 + 0` prints `1`.

### Indexing (`JqIndex.h/.cpp`)

```cpp
Value IndexValue(const Value& target, const Value& key);           // .[key]
Value SliceValue(const Value& target, const Value& from, const Value& to);
void IterateValue(const Value& target, const std::function<void(const Value&)>& each);
```
- `null` indexed or sliced by anything valid is `null`.
- Object by string: the member or null. Object by anything else: `Cannot
  index object with <kind>` (`.[0]` -> `Cannot index object with number`).
- Array by number: `floor`; negative counts from the end; out of range
  null (`[1,2,3] | .[1.7]` is 2). Array by array: the indices where the
  sub-array starts (`[1,2,1] | .[[1]]` is `[0,2]`; `[1,2,1,2] | .[[1,2]]`
  is `[0,2]`). Array by object: a slice when it has `start` and `end`
  members (numbers or null), else `Array/string slice indices must be
  integers`. Array by null: `Cannot index array with null`.
- Anything (array, number, string, boolean) by a string:
  `Cannot index <kind> with string "<key>"` when the key is shorter than 30
  bytes, `Cannot index <kind> with string` otherwise (jq's buffer). Any
  other pair: `Cannot index <kind> with <keykind>` (`"abc" | .[0]` ->
  `Cannot index string with number`, `true | .[0]` -> `Cannot index
  boolean with number`).
- Slices of arrays and strings (strings by code points: `"aéb" | .[1:2]` is
  `"é"`): `from` floored, `to` ceiled (`[1,2,3] | .[1.2:2.8]` is `[2,3]`),
  null meaning the start/end, negatives from the end, clamped. A bound that
  is neither number nor null: `Start and end indices of an array slice must
  be numbers` (verify). Slicing an object: `Cannot index object with
  object`; others: `Cannot index <kind> with object`.
- Iterating: arrays and objects; anything else `Cannot iterate over <kind>
  (<dump15>)` (`Cannot iterate over null (null)`, `Cannot iterate over
  string ("abcdefghij...)`).

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

// For natives taking value arguments (C functions in jq): calls |each| with
// one value per argument for every combination, the LAST argument
// outermost (jq: [pow(1,2; 3,4)] is [1,8,1,16]).
void ForEachArgumentCombination(Interpreter& interp, const Value& input, const std::vector<Closure>& args,
                                const std::function<void(const std::vector<Value>&)>& each);

void RegisterCoreNatives(NativeRegistry& registry);   // this task: empty/0, error/0, error/1, not/0, type/0
```

`PathValue` is declared (forward) here and defined by `tools--jq-paths`;
leave `runPath` empty for now. `Standard()` builds the registry once
(function-local static, thread-safe in C++11) calling every `Register*`.
`error/0` is `error(.)`; `error/1` throws `JqError` with each argument
value; `not` is the input's falsiness; `type` the `KindName`.

### `JqPrelude.h` / `JqPrelude.cpp`

```cpp
// The builtins written in jq, parsed once and placed in scope before the
// user's program (a user definition of the same name/arity shadows them).
std::string_view StandardPrelude();
```
This task's prelude: `def select(f): if f then . else empty end;` and
`def recurse(f): def r: ., (f | r); r;` (the iterative native
`recurse/1` of `tools--jq-builtins` replaces it later). Later tasks append
their definitions. The parsed prelude is a static `ParseResult`; a parse
error in it is a programming error (assert in debug, and a test checks it
parses).

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

The check walks the tree with the scope (prelude definitions, the user's
definitions in force, parameters, `as` variables, labels, globals) and
reports, in source order, every:
- call with no definition, parameter or native of that name and arity:
  `<name>/<arity> is not defined` at the call's offset;
- variable not in scope: `$<name> is not defined`;
- `break $name` with no enclosing `label $name`: `$*label-<name> is not defined`;
- object key written as a parenthesised number/boolean/null literal:
  `Cannot use <kind> (<literal>) as object key` at the key's offset (the
  `(`; if `tools--jq-parse` gives a parenthesised expression the inner
  offset, use the offset of the `(` before it).
Expected (container): `foo` -> `jq: error: foo/0 is not defined at
<top-level>, line 1:\nfoo\njq: 1 compile error\n`; `foo | bar` -> two
errors, the second line `foo | bar······` (6 spaces), `jq: 2 compile
errors`; `def f: 1; f(2)` -> `f/1 is not defined` with 10 spaces;
`1 as $x | $y` -> `$y is not defined` with 10 spaces; `label $f | break
$g` -> `$*label-g is not defined` with 11 spaces.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `JqInterpreter.cpp`,
`JqArithmetic.cpp`, `JqIndex.cpp`, `JqFormat.cpp`, `JqNatives.cpp`,
`JqPrelude.cpp`, `JqProgram.cpp` (all under `commands/jq/`).

## Tests

`tests/unit/components/Jq.unittests/JqTestRunner.h` (new, header-only):
`std::string RunJq(const std::string& program, const std::string& inputJson = "null")`
-- compiles (compile errors returned as the formatted text), reads the
input with `ParseSingleJson`, runs with a stub `JqHost` (no inputs, stderr
collected, never stopping), and returns the outputs written compact,
separated by single spaces, or, on an uncaught error, the outputs so far
followed by `error: <message>` (`<message>` the string, or `(not a string):
<compact JSON>`). Later jq tasks reuse it.

`tests/unit/components/Jq.unittests/JqInterpreterTest.cpp` -- each case
`EXPECT_EQ(RunJq(program, input), expected)`, expected values from the
container:
- `JqInterpreterTest.PathsAndIteration`: `.a.b`, `.[1:]`, `.[-1:]`,
  `"abcdef" | .[2:4]` (`"cd"`), `.[]?` on a number (nothing), `..`, the
  Index/Slice order cases above, `[1,2,1] | .[[1]]`.
- `JqInterpreterTest.IndexErrors`: each message of "Indexing", including
  the 29- and 30-byte key cases (`1 | .["kkk...29"]` with the key,
  30 without).
- `JqInterpreterTest.Arithmetic`: every example of "Arithmetic", the
  division-by-zero and remainder messages, `{} * 2`, `[] - 1`, the
  truncated dumps.
- `JqInterpreterTest.Generators`: `[(1,2) + (10,20)]`, `"\(1,2)-\(3,4)"`,
  `[{a:(1,2), b:(3,4)}]`, `and`/`or` orders, `//` cases
  (`[(null, false) // (3, 4)]` is `[3,4]`; `[false, null] | .[] // 4` is 4).
- `JqInterpreterTest.TryCatch`: `try error("x") catch .`, `try error({})
  catch .`, `try error(null) catch .` (null), `[.[] | try if . == 2 then
  error("e") else . end catch "c"]` on `[1,2,3]` (`[1,"c",3]`), the
  not-resumed case, `.a?` on a number (nothing), and `try (1,2) catch . |
  error("down")` -- the downstream error is **not** caught (result
  `error: down`).
- `JqInterpreterTest.ReduceForeach`: every example of those bullets.
- `JqInterpreterTest.Destructuring`: `. as [$a, {b: $c, $d}] | [$a,$c,$d]`
  on `[1,{"b":2,"d":3}]`, the `?//` examples, `{"a":1} as {$a, b: $b} ?//
  [$c] | [$a, $b, $c]` (`[1,null,null]`), `1 as [$a] | $a` (error).
- `JqInterpreterTest.Functions`: `def f(g): [g, g]; f(1, 2)`
  (`[1,2,1,2]`), `def f($x; y): $x + y; f(1; 2)`, `def f(x): x as $v | $v;
  [f(1,2)]`, `def f: def g: 3; g; f`, factorial of 10 (`3628800`),
  closures seeing their definition scope (`1 as $x | def f: $x; 2 as $x |
  f` is 1), shadowing a prelude name.
- `JqInterpreterTest.LabelsAndLoc`: `[label $out | 1, 2, break $out, 3]`
  (`[1,2]`), nested labels, `$__loc__`, `{$__loc__}`.
- `JqInterpreterTest.LiteralsSurvive`: `{"a":1.0} | .a` (`1.0`), `1.0 + 0`
  (`1`), `[1.0, 100000000000000000001]`.
- `JqInterpreterTest.CompileErrors`: the five expected texts above.
- `JqInterpreterTest.DepthLimit`: 500 nested calls work; `def f: .+1|f;
  0|f` gives `error: Maximum call depth (512) exceeded`; no crash.
- `JqInterpreterTest.PreludeParses`: `StandardPrelude()` parses with no error.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/jq/CLAUDE.md`: a section "Evaluation" -- callback generators,
the orders (right operand outer, key outer, last native argument outer,
first `$param` outer), how `try` tells its own errors from downstream ones,
`Env`/`Closure`, the registry and prelude (and that each later task
registers there), the call-depth limit as a documented difference, and
`JqHost` as the program's only way out (the `ICurrentProcess` rule).

## Acceptance

- [ ] Every construct listed evaluates with jq's outputs and order; every
      message listed is byte-exact.
- [ ] `try` catches only errors from its body; `break` and stop are never caught.
- [ ] Compile checks report every undefined name in source order with jq's text.
- [ ] Depth limit enforced without a crash; no recursion per array element
      in `..`.
- [ ] Nothing reaches files or processes; only `JqHost`.
- [ ] `Jq.unittests` passes; the whole unit suite passes.

## Out of scope

- `path()`, `getpath`/`setpath`/`delpaths`/`del`/`paths`, and every
  assignment operator: `tools--jq-paths` (an `Assign` node may throw a
  `JqError` "assignment is not implemented yet" until then).
- The `jq` command, inputs, `$ENV`, `input`/`debug`/`stderr` natives:
  `tools--jq-command`. The builtin library: `tools--jq-builtins`,
  `tools--jq-text`. Formats other than `@text`/`@json`: `tools--jq-text`.
