# Task tools--jq-paths: jq paths and assignment -- path(), getpath, setpath, del, =, |=, +=

- Rock: tools
- Depends on: tools--jq-eval
- Size: ~650 changed lines in ~6 files
- Plan checked against: develop @ ccb9dbe
- PR title: jq: path expressions, getpath/setpath/delpaths/del and assignment

## Goal

The fourth jq task. jq's path expressions work: `path(f)`, `paths`,
`getpath`, `setpath`, `delpaths`, `del`, `to_entries`-free updates, and every
assignment operator -- `=`, `|=`, `+=`, `-=`, `*=`, `/=`, `%=`, `//=` -- with
jq 1.7.1's results and messages (`Invalid path expression with result 1`,
`Out of bounds negative array index`, ...). So `.a.b.c = 1`, `.[] |= . + 1`,
`del(.[] | select(. == 2))`, `.. |= (if type == "number" then .+1 else .
end)` behave as on Linux.

Reference: the task container's jq (`jq-1.7`); expected outputs below are
its `jq -nc` results; for anything else, run it there.

## Context

Read first: `commands/jq/CLAUDE.md`, `JqInterpreter.h`, `JqIndex.h`,
`JqNatives.h`, `JqPrelude.cpp`, `JqRuntime.h` (from `tools--jq-eval`:
`Interpreter::Eval`, `EvalClosure`, `Closure`, `EnvPtr`, `IndexValue`,
`SliceValue`, `IterateValue`, `NativeFunction::runPath`, `NativeRegistry`,
`RegisterCoreNatives`, `StandardPrelude`, `JqError`, the `Assign` node
placeholder) and `JqValue.h`/`JqJsonWriter.h` (`Value`, `DumpTruncated`).
The test runner `tests/unit/components/Jq.unittests/JqTestRunner.h`
(`RunJq`).

Rules that bite: portable C++17; nothing outside the process; no recursion
per element of a value (paths of `..` are produced iteratively, as `..` is).

## Changes

### `commands/jq/JqPaths.h` / `JqPaths.cpp` (new)

```cpp
struct PathValue {
    Value value;
    std::vector<Value> path;   // keys: strings, numbers, or {"start":..,"end":..} slices
    bool valid = true;         // false: produced by a non-path expression
};
using EmitPath = std::function<void(const PathValue&)>;

// In Interpreter (declared in JqInterpreter.h, defined here):
//   void EvalPaths(const Node& node, const PathValue& input, const EnvPtr& env, const EmitPath& emit);
//   void EvalClosurePaths(const Closure& closure, const PathValue& input, const EmitPath& emit);

Value GetPath(const Value& root, const Value& path);                 // getpath/1
Value SetPath(const Value& root, const Value& path, const Value& v);  // setpath/2
Value DeletePaths(const Value& root, const Value& paths);             // delpaths/1
```

**Path mode** (`EvalPaths`), as jq 1.7.1:
- `Identity`: the input. `RecurseDefault`: every path pre-order (`path(..)`
  on `{"a":[1,2]}` gives `[] ["a"] ["a",0] ["a",1]`), iteratively.
- `Index`/`Slice`/`Iterate` (with `?` through `Try`): on a valid input,
  the value as `IndexValue`/`SliceValue`/`IterateValue` give it (same
  errors), the path extended by the key -- a slice by
  `{"start":S,"end":E}` with `null` for an absent bound
  (`path(.a[1:])` is `["a",{"start":1,"end":null}]`). On an invalid input:
  `Invalid path expression near attempt to access element <key dump15> of
  <value dump30>` or `Invalid path expression near attempt to iterate
  through <value dump30>` (`path(1|.[])` -> `... iterate through 1`;
  `path(.a|1|.b)` -> `... access element "b" of 1`).
- `Pipe`, `Comma`, `Try`, `Label`/`Break`, `Defs`: as in value mode, in
  path mode. `If`: the condition in value mode on the input's value, the
  branch in path mode. `Alternative`: the truthy outputs of the left in
  path mode, else the right's. `Bind`: the source in value mode, the body in
  path mode (`path(.a as $x | .b)` is `["b"]`).
- `Call`: a user function's body in path mode; a filter parameter's closure
  in path mode; a native's `runPath`, or -- when it has none -- its value
  mode, each output an invalid `PathValue`.
- Everything else (literals, arithmetic, objects, `reduce`, `foreach`,
  variables, `$__loc__`, strings): value mode, outputs invalid
  (`path(reduce 1 as $x (.; .a))` fails, as in jq 1.7.1).
- `path(f)`: `EvalPaths(f, {input, [], true})`; each valid output's path as
  an array; an invalid one fails with `Invalid path expression with result
  <dump30>` (`path(1)`, `path(.a|tostring)` -> `... with result "[1,2]"`,
  `path($__loc__)` -> `... with result {"file":"<top-level>","lin...`).

**getpath/setpath/delpaths** (natives, registered by a new
`RegisterPathNatives(NativeRegistry&)` called from `Standard()`; argument
combinations with `ForEachArgumentCombination`):
- `getpath(p)`: p not an array: `Path must be specified as an array`;
  walks with `IndexValue`/`SliceValue`, a `null` anywhere giving `null`
  (`getpath(["x","y"])` on `{}` is null; `[1,[2]] | getpath([1,0,0])` fails
  `Cannot index number with number`). Path mode: the input path plus p, the
  value as above (`path(getpath(["x","y"]))` is `["x","y"]`).
- `setpath(p; v)`: creates what is missing -- `null` with a string key
  becomes an object, with a number an array padded with nulls (`null |
  .[2] = 1` is `[null,null,1]`); a negative index counts from the end, and
  before the start fails `Out of bounds negative array index`; a slice key
  replaces that range of an array with v, which must be an array (`A slice
  of an array can only be assigned another array`; `[1,2,3] |
  setpath([{"start":1,"end":2}]; ["x","y"])` is `[1,"x","y",3]`; `null |
  .[1:3] = ["x"]` is `["x"]`); a string slice fails `Cannot update string
  slices`; a key of the wrong kind fails as indexing does (`Cannot index
  array with string "x"`); `setpath([]; 5)` is 5. Values are immutable:
  rebuild along the path with `WithMember`/`WithElement`.
- `delpaths(ps)`: ps not an array: `Paths must be specified as an array`;
  an element not an array: `Path must be specified as array, not number`;
  sort the paths with `Compare`, delete from the last to the first (so
  indices stay right: `[1,2,3,4] | delpaths([[0],[2]])` is `[2,4]`); a
  missing member or index is ignored; deleting inside a number/string/bool
  fails `Cannot delete fields from <kind>`; a slice deletes the range
  (`[1,2,3] | delpaths([[{"start":0,"end":2}]])` is `[3]`); `delpaths([[]])`
  gives null.

**Assignment** (the `Assign` node), exactly jq 1.7.1's definitions:
- `lhs = rhs`: for each output `$v` of `rhs` on `.` (outer): `reduce
  path(lhs) as $p (.; setpath($p; $v))` (`.a = (1,2)` gives two results;
  `(.a,.b) = (1,2)` gives `{"a":1,"b":1} {"a":2,"b":2}`).
- `lhs |= f`: `reduce path(lhs) as $p ([., []]; . as [$x, $del] | first(
  [$x | setpath($p; getpath($p) | f)), $del] ), or, when f gives nothing,
  [$x, $del + [$p]])` then `delpaths($del)` -- only f's **first** output
  is used (`{} | .a |= (.,.)` is `{"a":null}`) and paths where f gives
  nothing are deleted at the end (`[1,2,3] | .[] |= empty` is `[]`;
  `[1,2,3,4,5] | (.[] | select(. > 2)) |= empty` is `[1,2]`).
- `lhs op= rhs` for `+ - * / %`: for each output `$x` of `rhs` on `.`
  (outer): `lhs |= . op $x` (`{} | .a += 1` is `{"a":1}`; `.a += 1` on
  `{"a":[1,2]}` fails `array ([1,2]) and number (1) cannot be added`).
- `lhs //= rhs`: for each `$x` of rhs: `lhs |= (. // $x)`.
Implement them in C++ over `EvalPaths`, `GetPath`, `SetPath`,
`DeletePaths` (not by text substitution).

**Prelude** additions (`JqPrelude.cpp`): `def del(f): delpaths([path(f)]);`,
`def paths: path(..) | select(length > 0);` (needs `length` from
`tools--jq-builtins`; until then define `paths` as `path(..) | select(. !=
[])`, which is the same), `def paths(node_filter): . as $dot | paths |
select(. as $p | $dot | getpath($p) | node_filter);`. `leaf_paths` is not
defined in the reference jq: do not add it.

Also give `select/1` (prelude, already path-capable through `if`) and the
`recurse/1` prelude definition their path behaviour by nothing more than
being jq definitions -- check with tests.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/jq/JqPaths.cpp`.

## Tests

`tests/unit/components/Jq.unittests/JqPathsTest.cpp` (added to the
directory's CMakeLists), `RunJq` with the expected values of the container:
- `JqPathsTest.PathOf`: `path(..)`, `[paths]` and `[paths(type ==
  "number")]` on `{"a":[1,{"b":2}]}`, `path(.a[1:])`, `path(if .a then .b
  else .c end)`, `path(.a // .b)`, `path(first(.a,.b))` once `first` exists
  -- else `path(label $f | .a, break $f)` (`["a"]`), `path(.a as $x | .b)`,
  `path(getpath(["x","y"]))`, `path(empty)`, `path(.[]?)` on `{"a":1}`,
  `path(.. | select(type == "number"))`, `path(def f: .a; f)`.
- `JqPathsTest.InvalidPaths`: each of the four messages with its truncation.
- `JqPathsTest.GetSetDelete`: every getpath/setpath/delpaths example and message above.
- `JqPathsTest.Assignment`: every assignment example above plus
  `.a.b.c = 1` on `{}`, `{"a":null} | .a.b.c |= 1`, `[1,2] | .[0] |= . +
  10`, `{"a":[{"b":1},{"b":2}]} | .a[].b |= . * 10`, `[1,2,3] | .[1:] =
  ["x","y"]` (`[1,"x","y"]`), `"abc" | .[1:] |= "Z"` (`Cannot update string
  slices`), `{} | .["a","b"] = 1`, `.a //= 3` on `{"a":[1,2]}` (unchanged),
  `{"a":5} | .a %= 2 | .b //= 3` (`{"a":1,"b":3}`), `[1,2,3] |
  del(.[] | select(. == 2))` (`[1,3]`), `del(.[0,2])` and `del(.[1:3])` on
  `[1,2,3,4]` (`[2,4]`, `[1,4]`), `.. |= (if type=="number" then .+1 else .
  end)` on `[1,2,3]` (`[2,3,4]`).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/jq/CLAUDE.md`: a section "Paths" -- `PathValue`, which nodes are
path expressions (and that `reduce`/`foreach` are not, as in jq 1.7.1), the
`|=` definition with deferred deletion, the file bullet for `JqPaths`.

## Acceptance

- [ ] Every example above gives the container's output; every message is byte-exact.
- [ ] `|=` uses the first output and deletes empty-update paths at the end.
- [ ] No recursion per element in `path(..)`; values never mutated in place.
- [ ] `Jq.unittests` passes; the whole unit suite passes.

## Out of scope

- `to_entries`, `with_entries`, `map_values`, `walk`, `pick`, `tostream`
  and the rest of the library (`tools--jq-builtins`); the command
  (`tools--jq-command`).
