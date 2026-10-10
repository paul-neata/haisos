# Task tools--jq-builtins: jq's builtin library -- length, keys, map, sort, strings, math, generators

- Rock: tools
- Depends on: tools--jq-command (and through it tools--jq-paths, tools--jq-eval, tools--jq-json, tools--jq-parse)
- Size: ~900 changed lines in ~10 files
- Plan checked against: develop @ ccb9dbe
- PR title: jq: the builtin library -- keys, map, sort_by, strings, math, generators

## Goal

The sixth jq task. jq's builtin library works with jq 1.7.1's results and
messages, except the regular expressions, the `@format`s beyond `@text`/
`@json` and the dates (the seventh and last jq task, `tools--jq-text`):
`length utf8bytelength keys keys_unsorted has in inside map map_values
to_entries from_entries with_entries add any all flatten range floor sqrt
pow log` (and the rest of jq's math functions), `tostring tonumber tojson
fromjson infinite nan isinfinite isnan isnormal isfinite`, the type filters
(`arrays objects iterables booleans numbers strings nulls values scalars
finites normals`), `sort sort_by group_by min max min_by max_by unique
unique_by reverse contains startswith endswith ltrimstr rtrimstr explode
implode split/1 join ascii_downcase ascii_upcase`, `recurse/0,1,2 limit
first last nth until while repeat isempty`, `indices index rindex walk
transpose combinations bsearch pick abs tostream fromstream
truncate_stream`, and `builtins`. So `jq -r '.dependencies | keys[]'
package.json` (acceptance scenario 4) runs, and so do the one-liners agents
write (`map(select(.x)) | sort_by(.n) | .[0]`, `to_entries[] | "\(.key)=\(.value)"`,
`[.[] | .size] | add`, `group_by(.k) | map({k: .[0].k, n: length})`).

How: most of jq's library **is jq code** (jq 1.7.1's `src/builtin.jq`); this
task copies those definitions verbatim into the prelude, and writes in C++
only what jq writes in C (its `f_*` natives) plus the generators that recurse
in jq (`recurse`, `while`, `until`, `repeat`), made iterative natives here
so that a 3000-step loop does not hit Haisos's call-depth limit of 512
(`tools--jq-eval`).

Reference: the task container's jq (Ubuntu 24.04's `jq 1.7.1-3ubuntu0.24.04.x`,
`jq --version` prints `jq-1.7`). Every expected value below was taken from it
(`jq -nc '<program>'`); for anything not written here, run it there and copy
what it prints.

## Context

Read first: `src/components/BuiltinCommands/commands/jq/CLAUDE.md`, then the
headers of the earlier jq tasks. What they provide, by exact name (the
merged code is the truth -- if a name differs, use the merged one and say so
in the PR summary):
- `tools--jq-json` (`JqValue.h`, `JqJsonWriter.h`, `JqJsonReader.h`):
  `Value` (`Null()`, `Boolean`, `Number` -- a computed number --,
  `NumberLiteral`, `String`, `Array`, `Object`, `GetKind`, `IsTruthy`,
  `AsNumber`, `Literal`, `AsString`, `AsArray`, `AsObject`, `Find`,
  `WithMember`, `WithoutMember`, `WithElement`), `Kind`, `KindName`,
  `Compare`, `Equal`, `WriteJson`, `WriteOptions` (`indent` 0 = compact),
  `FormatNumber`, `DumpTruncated`, `ParseSingleJson`, `RepairUtf8`.
- `tools--jq-eval` (`JqRuntime.h`, `JqInterpreter.h`, `JqNatives.h`,
  `JqPrelude.h`, `JqFormat.h`, `JqIndex.h`): `JqError`, `Emit`,
  `Interpreter` (`EvalClosure`, `CheckStop`, `Host`), `Closure`,
  `NativeFunction { name, arity, run, runPath }`, `NativeRegistry` (`Add`,
  `Find`, `Names`, `Standard()` -- which calls every `Register*`),
  `ForEachArgumentCombination` (the **last** argument outermost),
  `RegisterCoreNatives` (`empty`, `error/0,1`, `not`, `type`),
  `StandardPrelude()` (today: `select/1`, `recurse/1`), `ApplyFormat`
  (`text`, `json`), `IndexValue`, `SliceValue`, `IterateValue`. The test
  helper `tests/unit/components/Jq.unittests/JqTestRunner.h`:
  `RunJq(program, inputJson = "null")` -- outputs written compact, separated
  by single spaces; an uncaught error appends `error: <message>`; compile
  errors come back as jq's formatted text.
- `tools--jq-paths` (`JqPaths.h`): `PathValue { value, path, valid }`,
  `EmitPath`, `Interpreter::EvalClosurePaths(closure, pathValue, emitPath)`,
  `RegisterPathNatives` (`path`, `getpath`, `setpath`, `delpaths`), the
  assignment operators (`|=` keeps the first output and deletes the paths
  whose update is empty), and the prelude's `del/1`, `paths/0`, `paths/1`.
- `tools--jq-command` (`Jq.cpp`, `JqIoNatives.cpp`): the `jq` builtin at
  version `1.0.0`, `RegisterIoNatives` (`input inputs debug stderr
  input_filename input_line_number env halt halt_error`), and
  `tests/unit/components/BuiltinCommands.unittests/JqTest.cpp`.

Error messages here are jq's `type_error` shape wherever a value is shown:
`<KindName> (<DumpTruncated(value, 15)>) <text>` -- written "(dump15)" below.

Rules that bite: portable C++17 -- Linux, Windows/MSVC, WASM: no POSIX
headers, no `<regex>`, only `<cmath>` functions that C++17 names in `std::`
(see "Math"); nothing here reaches outside the process (no `JqHost` use at
all); no recursion per element of a value or per loop step (the generator
natives below are iterative). No class here implements an `interfaces/`
interface (the `Create()` rule does not apply). The builtin rules: bump the
jq builtin's version (behaviour changes), keep `--help` generated.

## Changes

All in `src/components/BuiltinCommands/commands/jq/`, namespace `Haisos::Jq`.

### `JqNatives.h` / `JqNatives.cpp`

Declare and call from `NativeRegistry::Standard()`, after the existing
`Register*` calls:

```cpp
void RegisterLibraryNatives(NativeRegistry& registry);    // JqLibraryNatives.cpp
void RegisterMathNatives(NativeRegistry& registry);       // JqMathNatives.cpp
void RegisterGeneratorNatives(NativeRegistry& registry);  // JqGeneratorNatives.cpp
```

### `JqLibraryNatives.cpp` (new) -- jq's C natives

Each is a `NativeFunction`; a native with value arguments evaluates them
with `ForEachArgumentCombination`. Results are computed numbers
(`Value::Number`) unless a value passes through unchanged. Behaviour and
messages (all verified):

- `length/0`: null 0; boolean `<kind> (dump15) has no length`; number its
  absolute value (computed: `-1.0|length` is `1`); string its code points
  (`"aé😀"` 3 -- decode with `Unicode::DecodeUtf8`, an invalid byte counting
  one); array/object their sizes.
- `utf8bytelength/0`: a string's bytes; else `(dump15) only strings have
  UTF-8 byte length`.
- `keys/0`, `keys_unsorted/0`: an object's keys (`keys` in `Compare` order
  -- bytes: `{"é":1,"z":2,"A":3}|keys` is `["A","z","é"]`); an array's
  indices `[0..n-1]`; else `(dump15) has no keys`.
- `has/1`: object with a string key: member present; array with a number
  key: `0 <= trunc(k) < size` (`[1,2]|has(0.5), has(-0.5), has(-1), has(2)`
  is `true true false false`); else `Cannot check whether <kind> has a
  <keykind> key` (no dumps: `Cannot check whether object has a null key`).
- `contains/1` (`a|contains(b)`): kinds differ (true and false are
  different kinds here, as `Kind` has them) -> `<k1> (dump15) and <k2>
  (dump15) cannot have their containment checked`; objects: every key of
  b is in a and `a[k]` contains `b[k]`; arrays: every element of b is
  contained by some element of a; strings: b is a byte substring of a (NUL
  bytes included); anything else: `Equal`. Recursive over nesting, as
  `Compare` is.
- `tostring/0`: `ApplyFormat("text", input)`. `tojson/0`: `WriteJson` with
  `indent` 0 (literals kept: `1.0|tojson` is `"1.0"`; NaN `"null"`).
- `fromjson/0`: string -> `ParseSingleJson`; its error as `<message> (while
  parsing '<the whole input string>')`; a non-string `(dump15) only strings
  can be parsed`.
- `tonumber/0`: a number as is (its literal kept); a string through
  `ParseSingleJson` (same "while parsing" error), and a result that is not a
  number -> `<kind of the input> (dump15 of the input) cannot be parsed as a
  number`; anything else the same message (`null (null) cannot be parsed as a
  number`). `"1.50"|tonumber` is `1.50`, `" 1 "` is `1`, `"nan"` NaN.
- `sort/0`: array, `std::stable_sort` with `Compare`; else `(dump15) cannot
  be sorted, as it is not an array`.
- `_sort_by_impl/1`, `_group_by_impl/1` (argument: the keys array built by
  the prelude's `map([f])`): input and keys both arrays, else `<k> (dump15)
  and <k> (dump15 of keys) cannot be sorted, as they are not both arrays`;
  stable sort of the elements by their keys; group_by: consecutive equal
  keys form a group, in sorted order.
- `_min_by_impl/1`, `_max_by_impl/1`: same check with the text `cannot be
  iterated over`; empty -> null; min keeps the **first** minimal element,
  max the **last** maximal one.
- `min/0`, `max/0`: an array (empty -> null; same first/last rule:
  `[1.0, 1]|max, min` is `1 1.0`); else `<k> (dump15) and <k> (dump15)
  cannot be iterated over` (the input twice).
- `explode/0`: a string's code points; else `explode input must be a string`.
- `implode/0`: not an array -> `implode input must be an array`; an element
  not a number -> `<kind> (dump15) can't be imploded, unicode codepoint needs
  to be numeric`; each code point truncated to an integer; negative, above
  0x10FFFF or a surrogate becomes U+FFFD.
- `split/1` (strings only; `split/2` is regex, `tools--jq-text`): both
  strings, else `split input and separator must be strings`; an empty input
  gives `[]`; an empty separator gives the code points as strings
  (`"aé"|split("")` is `["a","é"]`); else the pieces between occurrences.
- `startswith/1`, `endswith/1`: both strings, else `startswith() requires
  string inputs` / `endswith() requires string inputs`.
- `ltrimstr/1`, `rtrimstr/1`: when both are strings and the input starts
  (ends) with the argument, the input without it; otherwise the input
  unchanged, whatever the kinds.
- `_strindices/1`: the **byte** offsets of every occurrence of a string in
  a string, overlapping ones included (`"aaa"`/`"aa"` -> `[0,1]`), `[]` for
  an empty needle. jq 1.7.1 reports bytes (`"aé,b"|indices(",")` is `[3]`):
  keep it.
- `infinite/0`, `nan/0`; `isinfinite/0`, `isnan/0`, `isnormal/0`: of a
  number the C answer; of anything else `false` (jq 1.7.1 does not fail).
- `range/2`: **not** through `ForEachArgumentCombination` -- the first
  argument is outermost (`[range(0,1; 3,4)]` is
  `[0,1,2,0,1,2,3,1,2,1,2,3]`): for each output `from` of the first argument,
  for each output `upto` of the second, both numbers or `Range bounds must be
  numeric`; emit `from` itself (its literal kept: `[range(1.0;3)]` is
  `[1.0,2]`), then `from + 1`, ... (computed) while `< upto`; `CheckStop()`
  per value.
- `builtins/0`: an array of `"name/arity"` strings: `NativeRegistry::Names()`
  plus `PreludeFunctionNames()` (below), without names starting with `_`,
  each once. jq lists them in an internal order; Haisos's order is its own (a
  documented difference).

### `JqMathNatives.cpp` (new) -- jq's libm functions

A table, not one function per name. Input (0-arity) or each argument must be
a number, else `<kind> (dump15) number required` -- for several arguments
the first non-number in argument order (`pow(1; "a")` and `pow("b"; "a")`
report `"a"` and `"b"`). The arguments of 2- and 3-arity ones are value
arguments (`ForEachArgumentCombination`: `[pow(1,2; 3,4)]` is
`[1,8,1,16]`); the input is ignored by them. Results are computed numbers.
- 0-arity, `std::` of the same name: `acos acosh asin asinh atan atanh cbrt
  ceil cos cosh erf erfc exp exp2 expm1 fabs floor lgamma log log10 log1p
  log2 logb nearbyint rint round sin sinh sqrt tan tanh tgamma trunc`; and
  `exp10` (`std::pow(10, x)`), `gamma` (= `lgamma`), `significand`
  (`std::scalbn(x, -std::ilogb(x))` for finite non-zero x, else x).
- 0-arity returning arrays: `frexp` -> `[mantissa, exponent]` (`[x, 0]` for
  infinity and NaN, whatever the C library says); `modf` -> `[fraction,
  integer part]`; `lgamma_r` -> `[lgamma(x), sign of Γ(x)]`, the sign 1 for
  x > 0 or x a non-positive integer, else -1 when `floor(x)` is odd and 1
  when even (`-2.5` gives -1).
- 2-arity: `atan2 copysign fdim fmax fmin fmod hypot nextafter pow
  remainder` (`std::`), `drem` (= `remainder`), `nexttoward`, `ldexp` and
  `scalbln` with the exponent truncated to an integer (`ldexp(1; 2.7)` is
  4), `scalb` (NaN unless the exponent is an integer, else `scalbln`).
  3-arity: `fma`.
- Not provided: the Bessel functions `j0 j1 jn y0 y1 yn` (not in portable
  C++17: MSVC and WASM's libc++ lack them) -- calling one is the compile
  error `j0/0 is not defined`; a documented difference.

### `JqGeneratorNatives.cpp` (new) -- iterative `recurse`, `while`, `until`, `repeat`

jq defines these recursively; here each is a native with both `run` and
`runPath` (so `path(recurse)`, `.. |=`-like updates through `recurse(f)`,
and `path(until(...))` work as in jq). Write the loop **once**, as a
template over the item type (`Value` in value mode, `PathValue` in path
mode) with the "apply this closure" step passed in -- `EvalClosure` or
`EvalClosurePaths` -- and conditions always evaluated in value mode on the
item's value. An explicit stack (a `std::vector`) of work items replaces
jq's recursion; `CheckStop()` at every item. The outputs of one closure call
are collected into a vector before they are pushed (in reverse, so they are
visited in order); this is the one difference from jq -- for a step whose
update yields several values, an error raised by a later one surfaces before
the earlier one's subtree is emitted -- documented.

Semantics (jq 1.7.1's definitions, which the tests pin):
- `recurse(f)` = `def r: ., (f | r); r;` -- pre-order: emit the item, then
  recurse into each output of `f` in order (`[2|recurse(if . < 20 then . *
  . else empty end)]` is `[2,4,16,256]`; `[[1,[2]]|recurse]` is
  `[[1,[2]],1,[2],2]` through the prelude's `recurse/0`).
- `recurse(f; cond)` = `def r: ., (f | select(cond) | r); r;` -- an output
  `u` of `f` is visited once per truthy output of `cond` on `u`
  (`[2|recurse(. * .; . < 20)]` is `[2,4,16]`).
- `while(cond; update)` = `def _while: if cond then ., (update | _while)
  else empty end;` -- for each output of `cond` on the item, in order: if
  truthy, emit the item, then visit each output of `update`
  (`[1|while(. < 100; . * 2)]` is `[1,2,4,8,16,32,64]`).
- `until(cond; next)` = `def _until: if cond then . else (next|_until) end;`
  -- per output of `cond`: truthy emits the item, falsy visits each output of
  `next` (`[1|until(. > 100; . * 2)]` is `[128]`).
- `repeat(f)`: the container's jq 1.7.1 defines it as `def _repeat: f,
  _repeat;` -- `f`'s outputs on the **same** input, again and again, forever
  (`[limit(5; 1|repeat(. * 2))]` is `[2,2,2,2,2]`, not powers of two; that
  is also how `inputs` reads every input). A loop: run `f`, emit its
  outputs, `CheckStop()`, repeat (an `f` with no output loops until
  stopped, as in jq).

Register `recurse/1`, `recurse/2`, `while/2`, `until/2`, `repeat/1`.

### `JqPrelude.h` / `JqPrelude.cpp`

- **Remove** the `recurse/1` definition `tools--jq-eval` put there (a
  prelude definition shadows a native of the same name and arity).
- Add `std::vector<std::string> PreludeFunctionNames();` -- `"name/arity"`
  of every top-level definition of the parsed prelude (the definitions of
  its top-level `Defs` node), for `builtins`.
- Append jq 1.7.1's own definitions, **verbatim**, from the container's
  `builtin.jq`. Extract it in the container with
  `python3 -c "import glob;d=open(glob.glob('/usr/lib/*/libjq.so.1')[0],'rb').read();s=d.index(b'def halt_error: halt_error(5);');print(d[s:d.index(b'\0',s)].decode())"`
  (the file is stored in the library as one string). Copy exactly these
  definitions (and nothing else -- the others are written in C++ by an
  earlier task, by this one, or by `tools--jq-text`):
  `map/1`, `sort_by/1`, `group_by/1`, `unique/0`, `unique_by/1`, `max_by/1`,
  `min_by/1`, `add/0`, `abs/0`, `map_values/1`, `recurse/0`, `to_entries/0`,
  `from_entries/0`, `with_entries/1`, `reverse/0`, `indices/1`, `index/1`,
  `rindex/1`, `isfinite/0`, `arrays`, `objects`, `iterables`, `booleans`,
  `numbers`, `normals`, `finites`, `strings`, `nulls`, `values`, `scalars`,
  `join/1`, `_flatten/1`, `flatten/1`, `flatten/0`, `range/1`, `range/3`,
  `limit/2`, `first/1`, `isempty/1`, `all/2`, `any/2`, `all/1`, `any/1`,
  `all/0`, `any/0`, `last/1`, `nth/2`, `first/0`, `last/0`, `nth/1`,
  `combinations/0`, `combinations/1`, `transpose/0`, `in/1`, `inside/1`,
  `ascii_downcase/0`, `ascii_upcase/0`, `truncate_stream/1`, `fromstream/1`,
  `tostream/0`, `bsearch/1`, `walk/1`, `pick/1`, and the last one,
  `def pow10: "Error: pow10/0 not found at build time"|error;` (the
  container's build has no `pow10`; keep its message). Keep jq's comments
  out; one definition per line or as jq lays them out, either is fine.
  Above them, a comment: taken from jq 1.7.1's `src/builtin.jq`, MIT
  licence, Copyright (C) 2012 Stephen Dolan -- the notice the licence asks
  for.
- Not copied: `IN`, `INDEX`, `JOIN` (SQL-style, out of the develop's
  scope), `get_search_list` and the other module functions, `ascii` (jq
  1.7.1 has none: `ascii` is `ascii/0 is not defined`, as in jq).
- What jq's definitions give for free, and the tests pin: `limit`, `nth/2`,
  `last/1` are built on `foreach`/`reduce`, which `tools--jq-paths` does not
  make path expressions -- so `path(limit(1; .a))` fails here where jq
  prints `["a"]` (a documented difference; `first/1`, built on `label`,
  works: `del(first(.[] | select(. == 2)))`). `walk` recurses about three
  calls per nesting level, so values nested deeper than ~170 levels hit the
  512 call-depth limit (documented).

### `Jq.cpp`

Version `1.1.0` (behaviour changed). Nothing else.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add
`commands/jq/JqLibraryNatives.cpp`, `commands/jq/JqMathNatives.cpp`,
`commands/jq/JqGeneratorNatives.cpp`. `tests/unit/components/Jq.unittests/CMakeLists.txt`:
add `JqBuiltinsTest.cpp`.

## Tests

`tests/unit/components/Jq.unittests/JqBuiltinsTest.cpp` (new): table-driven
`EXPECT_EQ(RunJq(program, input), expected)` with `SCOPED_TRACE(program)`;
input `null` unless given; `U+FFFD` below means that character's UTF-8 bytes.

- `JqBuiltinsTest.LengthKeysHas`:
  `map(length)` on `[[1,[2]],"aé😀",{"a":1},null,-5,-1.0]` -> `[2,3,1,0,5,1]`;
  `true|length` -> `error: boolean (true) has no length`;
  `"aé😀"|utf8bytelength` -> `7`; `1|utf8bytelength` -> `error: number (1) only strings have UTF-8 byte length`;
  `keys, keys_unsorted` on `{"b":1,"a":2}` -> `["a","b"] ["b","a"]`;
  `{"é":1,"z":2,"A":3}|keys` -> `["A","z","é"]`; `[4,5]|keys` -> `[0,1]`;
  `1|keys` -> `error: number (1) has no keys`;
  `[1,2]|has(1), has(2), has(-1), has(0.5), has(-0.5)` -> `true false false true true`;
  `[1]|has("a")` -> `error: Cannot check whether array has a string key`;
  `{}|has(null)` -> `error: Cannot check whether object has a null key`;
  `"a"|in({"a":1})` -> `true`; `[1,2]|inside([1,2,3])` -> `true`.
- `JqBuiltinsTest.EntriesAndMaps`:
  `{"a":1,"b":2}|to_entries` -> `[{"key":"a","value":1},{"key":"b","value":2}]`;
  `[[1,2]]|to_entries` -> `[{"key":0,"value":[1,2]}]`;
  `[{"name":"a","value":1},{"Key":"b","Value":2},{"key":"c"}]|from_entries` -> `{"a":1,"b":2,"c":null}`;
  `[{"key":1,"value":2}]|from_entries` -> `error: Cannot use number (1) as object key`;
  `[{"value":1}]|from_entries` -> `error: Cannot use null (null) as object key`;
  `[]|from_entries` -> `{}`;
  `{"a":1,"b":2}|with_entries(select(.value > 1))` -> `{"b":2}`;
  `{"a":1}|with_entries(.value += 1)` -> `{"a":2}`;
  `[1,2]|map(.+1)` -> `[2,3]`; `{"a":1}|map(.+1)` -> `[2]`;
  `{"a":1,"b":2}|map_values(.+1)` -> `{"a":2,"b":3}`; `{"a":1,"b":2}|map_values(empty)` -> `{}`;
  `[1,2]|map_values(.,.)` -> `[1,2]`.
- `JqBuiltinsTest.SortingAndGrouping`:
  `[3,1,2]|sort` -> `[1,2,3]`; `{"a":1}|sort` -> `error: object ({"a":1}) cannot be sorted, as it is not an array`;
  `[{"a":1,"b":2},{"a":1,"b":1}]|sort_by(.a)` -> `[{"a":1,"b":2},{"a":1,"b":1}]` (stable);
  `[[1,2],[1,1]]|sort_by(.[0], .[1])` -> `[[1,1],[1,2]]`; `[3,1,2]|sort_by(-.)` -> `[3,2,1]`;
  `[3,1]|sort_by(empty)` -> `[3,1]`; `1|sort_by(.)` -> `error: Cannot iterate over number (1)`;
  `{"a":1}|sort_by(.)` -> `error: object ({"a":1}) and array ([[1]]) cannot be sorted, as they are not both arrays`;
  `[{"a":1,"b":1},{"a":2},{"a":1,"b":2}]|group_by(.a)` -> `[[{"a":1,"b":1},{"a":1,"b":2}],[{"a":2}]]`;
  `[3,1,2,1]|unique` -> `[1,2,3]`; `[{"a":1,"b":1},{"a":1,"b":2}]|unique_by(.a)` -> `[{"a":1,"b":1}]`;
  `[3,1,2]|min, max` -> `1 3`; `[]|min, max` -> `null null`; `[1.0, 1]|max, min` -> `1 1.0`;
  `[{"a":1,"b":3},{"a":2,"b":3}]|max_by(.b), min_by(.b)` -> `{"a":2,"b":3} {"a":1,"b":3}`;
  `[]|max_by(.a)` -> `null`; `{"a":1}|min` -> `error: object ({"a":1}) and object ({"a":1}) cannot be iterated over`;
  `{"a":1}|min_by(.)` -> `error: object ({"a":1}) and array ([[1]]) cannot be iterated over`;
  `[nan, 1]|min` -> `null`.
- `JqBuiltinsTest.Strings`:
  `"abc"|explode` -> `[97,98,99]`; `1|explode` -> `error: explode input must be a string`;
  `[97,233,128512]|implode` -> `"aé😀"`; `[1114112,55296,-1,97.7]|implode|explode` -> `[65533,65533,65533,97]`;
  `"a"|implode` -> `error: implode input must be an array`;
  `[null]|implode` -> `error: null (null) can't be imploded, unicode codepoint needs to be numeric`;
  `"a,b,,c"|split(",")` -> `["a","b","","c"]`; `"aé"|split("")` -> `["a","é"]`; `""|split(",")` -> `[]`;
  `1|split(",")` -> `error: split input and separator must be strings`;
  `["a",1,null,true,2.0]|join("-")` -> `"a-1--true-2.0"`; `[]|join(",")` -> `""`;
  `[[1]]|join(",")` -> `error: string ("") and array ([1]) cannot be added`;
  `"abc"|startswith("ab"), endswith("bc")` -> `true true`; `1|startswith("a")` -> `error: startswith() requires string inputs`;
  `"abc"|ltrimstr("a"), rtrimstr("c"), ltrimstr(1)` -> `"bc" "ab" "abc"`; `1|ltrimstr("a")` -> `1`;
  `"aBé"|ascii_downcase, ascii_upcase` -> `"abé" "ABé"`;
  `"a,b, cd, efg"|indices(", "), index(", "), rindex(", ")` -> `[3,7] 3 7`;
  `"aé,b"|indices(",")` -> `[3]`; `"aaa"|indices("aa")` -> `[0,1]`; `"x"|indices("")` -> `[]`;
  `[0,1,2,1,3,1,2]|indices(1), indices([1,2])` -> `[1,3,5] [1,5]`; `"abc"|index("z")` -> `null`.
- `JqBuiltinsTest.Conversions`:
  `1, "x", [1.0,{"a":"é"}] | tostring` -> `"1" "x" "[1.0,{\"a\":\"é\"}]"`; `0.1+0.2|tostring` -> `"0.30000000000000004"`;
  `1.0|tojson` -> `"1.0"`; `nan|tojson` -> `"null"`; `[infinite]|tojson` -> `"[1.7976931348623157e+308]"`;
  `" 1 ", "1.50", "nan" | tonumber` -> `1 1.50 null`;
  `"abc"|tonumber` -> `error: Invalid numeric literal at EOF at line 1, column 3 (while parsing 'abc')`;
  `"[1]"|tonumber` -> `error: string ("[1]") cannot be parsed as a number`;
  `null|tonumber` -> `error: null (null) cannot be parsed as a number`;
  `""|tonumber` -> `error: Expected JSON value (while parsing '')`;
  `"[1,2"|fromjson` -> `error: Unfinished JSON term at EOF at line 1, column 4 (while parsing '[1,2')`;
  `"1 2"|fromjson` -> `error: Unexpected extra JSON values (while parsing '1 2')`;
  `1|fromjson` -> `error: number (1) only strings can be parsed`; `"{\"a\":1.0}"|fromjson` -> `{"a":1.0}`.
- `JqBuiltinsTest.Math` (only values a correctly rounding libm gives everywhere):
  `[2.5,-2.5,0,1e300]` with `map(floor)` -> `[2,-3,0,1e+300]`, `map(ceil)` -> `[3,-2,0,1e+300]`,
  `map(round)` -> `[3,-3,0,1e+300]`, `map(rint)` -> `[2,-2,0,1e+300]`, `map(trunc)` -> `[2,-2,0,1e+300]`,
  `map(fabs)` -> `[2.5,2.5,0,1e+300]`, `map(sqrt)` -> `[1.5811388300841898,null,0,1e+150]`,
  `map(logb)` -> `[1,1,-1.7976931348623157e+308,996]`, `map(significand)` -> `[1.25,-1.25,0,1.4932217896051503]`,
  `map(frexp)` -> `[[0.625,2],[-0.625,2],[0,0],[0.7466108948025751,997]]`,
  `map(modf)` -> `[[0.5,2],[-0.5,-2],[0,0],[0,1e+300]]`, `map(abs)` -> `[2.5,2.5,0,1E+300]`;
  `[infinite,nan]|map(frexp), map(modf)` -> `[[1.7976931348623157e+308,0],[null,0]] [[0,1.7976931348623157e+308],[null,null]]`;
  `[2.5,-2.5]|map(lgamma_r|.[1])` -> `[1,-1]`;
  `[pow(5.5;2), fmod(5.5;2), fmin(5.5;2), fmax(5.5;2), fdim(5.5;2), copysign(5.5;2), drem(5.5;2), remainder(5.5;2), ldexp(5.5;2), scalb(5.5;2), scalbln(5.5;2), nextafter(5.5;2), ldexp(1;2.7)]`
  -> `[30.25,1.5,2,5.5,3.5,5.5,-0.5,-0.5,22,22,22,5.499999999999999,4]`;
  `[fma(2;3;4)]` -> `[10]`; `[pow(1,2; 3,4)]` -> `[1,8,1,16]`; `-1|sqrt` -> `null`;
  `"a"|floor` -> `error: string ("a") number required`; `pow(1; "a")` -> `error: string ("a") number required`;
  `pow("b"; "a")` -> `error: string ("b") number required`;
  `[infinite,-infinite,nan]|map(isinfinite, isnan, isnormal, isfinite)` -> `[true,false,false,false,true,false,false,false,false,true,false,true]`;
  `"a"|isinfinite, isnan, isnormal` -> `false false false`; `0, 1e-310 | isnormal` -> `false false`;
  `pow10` -> `error: Error: pow10/0 not found at build time`.
- `JqBuiltinsTest.Generators`:
  `[range(5)]` -> `[0,1,2,3,4]`; `[range(0,1; 3,4)]` -> `[0,1,2,0,1,2,3,1,2,1,2,3]`; `[range(0;10;3)]` -> `[0,3,6,9]`;
  `[range(5;0;-2)]` -> `[5,3,1]`; `[range(0;1;0)]` -> `[]`; `[range(1.5)]` -> `[0,1]`; `[range(1.0;3)]` -> `[1.0,2]`;
  `[range(0.5;3)]` -> `[0.5,1.5,2.5]`; `[range(-1)]` -> `[]`; `[range("a")]` -> `error: Range bounds must be numeric`;
  `[range(0;3;"a")]` -> `error: number (0) and string ("a") cannot be added`;
  `[limit(3; range(10))]` -> `[0,1,2]`; `[limit(0; 1,2)]` -> `[]`; `[limit(-1; 1,2)]` -> `[1,2]`;
  `[first(range(10))]` -> `[0]`; `[first(empty)]` -> `[]`; `last(range(5))` -> `4`; `[last(empty)]` -> `[null]`;
  `nth(2; range(10))` -> `2`; `[nth(5; range(3))]` -> `[]`; `nth(-1; 1)` -> `error: nth doesn't support negative indices`;
  `[1,2]|first, last, nth(1)` -> `1 2 2`; `[]|first` -> `null`;
  `[1|until(. > 100; . * 2)]` -> `[128]`; `[10|until(. < 3; . - 4)]` -> `[2]`;
  `[1|while(. < 100; . * 2)]` -> `[1,2,4,8,16,32,64]`; `[1|while(. < 0; .)]` -> `[]`;
  `[limit(5; 1|repeat(. * 2))]` -> `[2,2,2,2,2]`; `[limit(3; repeat(1))]` -> `[1,1,1]`;
  `[2|recurse(if . < 20 then . * . else empty end)]` -> `[2,4,16,256]`; `[2|recurse(. * .; . < 20)]` -> `[2,4,16]`;
  `[0|recurse(.+1; . < 3)]` -> `[0,1,2]`; `{"a":[1]}|[recurse]` -> `[{"a":[1]},[1],1]`;
  `"abc"|[recurse(if length > 1 then .[1:] else empty end)]` -> `["abc","bc","c"]`;
  `[isempty(empty), isempty(1, error("x"))]` -> `[true,false]`;
  `[true,false]|any, all` -> `true false`; `[]|any, all` -> `false true`; `any(1, error("x"); . == 1)` -> `true`;
  `[1,2]|any(. > 1), all(. > 1)` -> `true false`; `all(empty; .)` -> `true`.
- `JqBuiltinsTest.DeepLoopsDoNotHitTheCallLimit`:
  `[0|while(. < 3000; . + 1)]|length` -> `3000`; `[0|until(. >= 3000; . + 1)]` -> `[3000]`;
  `[2000|recurse(if . > 0 then . - 1 else empty end)]|length` -> `2001`;
  `[range(0; 3000; 1)]|length` -> `3000`; `[limit(3000; repeat(1))]|length` -> `3000`.
- `JqBuiltinsTest.GeneratorPaths`:
  `{"a":1}|[path(recurse)]` -> `[[],["a"]]`; `{"a":{"b":1}}|[path(recurse(.[]?; type == "object"))]` -> `[[],["a"]]`;
  `{"a":{"a":{"b":1}}}|path(until(has("b"); .a))` -> `["a","a"]`;
  `[limit(3; path(repeat(.a)))]` -> `[["a"],["a"],["a"]]`;
  `[1,2]|path(while(true; .[0]))` -> `[] [0] error: Cannot index number with number`;
  `[1,2]|path(first), path(last)` -> `[0] [-1]`; `{"a":[1,2]}|del(.a|last)` -> `{"a":[1]}`;
  `[[1,2]]|.[] |= last` -> `[2]`; `{"a":[1,2]}|(.a|first) = 9` -> `{"a":[9,2]}`;
  `[1,2,3]|del(first(.[] | select(. >= 2)))` -> `[1,3]`;
  `{"a":1}|path(to_entries)` -> `error: Invalid path expression with result [{"key":"a","value":1}]`.
- `JqBuiltinsTest.Structures`:
  `[1,[2,[3]]]|flatten, flatten(1), flatten(0)` -> `[1,2,3] [1,2,[3]] [1,[2,[3]]]`;
  `[1]|flatten(-1)` -> `error: flatten depth must not be negative`; `{"a":[1]}|flatten` -> `[1]`;
  `[[1,2],[3]]|flatten(1.5)` -> `[1,2,3]`;
  `[1,2]|add` -> `3`; `[]|add` -> `null`; `["a","b"]|add` -> `"ab"`; `{"a":1,"b":2}|add` -> `3`; `[3.0]|add` -> `3.0`;
  `[1,2,3]|reverse` -> `[3,2,1]`; `null|reverse` -> `[]`; `"abc"|reverse` -> `error: Cannot index string with number`;
  `{"a":1,"b":[1,2]}|contains({"b":[1]})` -> `true`; `"foobar"|contains("bar")` -> `true`;
  `[[1,2]]|contains([[1]])` -> `true`; `"a\u0000b"|contains("b")` -> `true`;
  `1|contains("a")` -> `error: number (1) and string ("a") cannot have their containment checked`;
  `true|contains(false)` -> `error: boolean (true) and boolean (false) cannot have their containment checked`;
  `[1,[2,{"a":3}]]|walk(if type == "number" then . + 1 else . end)` -> `[2,[3,{"a":4}]]`;
  `[[1,2],[3]]|walk(.,.)` -> `[[1,1,2,2],[1,1,2,2],[3,3],[3,3]] [[1,1,2,2],[1,1,2,2],[3,3],[3,3]]`;
  `[1,2]|walk(numbers |= empty)` -> `[null,null]`;
  `[[1,2],[3]]|transpose` -> `[[1,3],[2,null]]`; `[[1,2],[3,4]]|[combinations]` -> `[[1,3],[1,4],[2,3],[2,4]]`;
  `[0,1]|[combinations(2)]` -> `[[0,0],[0,1],[1,0],[1,1]]`; `[1,2,3]|bsearch(2), bsearch(0), bsearch(4)` -> `1 -1 -4`;
  `{"a":{"b":1,"c":2},"d":3}|pick(.a.b)` -> `{"a":{"b":1}}`; `[1,2,3]|pick(.[1])` -> `[null,2]`;
  `{"a":[1,{"b":2}]}|[tostream]` -> `[[["a",0],1],[["a",1,"b"],2],[["a",1,"b"]],[["a",1]],[["a"]]]`;
  `fromstream(([[0],1],[[1,0],2],[[1,0]],[[1]]))` -> `[1,[2]]`;
  `[1|truncate_stream([[0],1],[[1,0],2],[[1,0]],[[1]])]` -> `[[[0],2],[[0]]]`;
  `[1,null,"a",[],{},true]|[.[]|scalars], [.[]|iterables], [.[]|values], [.[]|nulls], [.[]|booleans]`
  -> `[1,null,"a",true] [[],{}] [1,"a",[],{},true] [null] [true]`;
  `[1,nan,infinite]|[.[]|finites], [.[]|normals]` -> `[1,null] [1]`.
- `JqBuiltinsTest.BuiltinsList`: `builtins|type` -> `"array"`;
  `builtins|map(select(startswith("_")))|length` -> `0`; `builtins|length == (unique|length)` -> `true`;
  `builtins|contains(["length/0","map/1","recurse/0","recurse/1","recurse/2","walk/1","sort_by/1","floor/0","pow/2","range/2","select/1","getpath/1"])` -> `true`;
  `ascii` -> `jq: error: ascii/0 is not defined at <top-level>, line 1:\nascii\njq: 1 compile error\n`;
  `j0` -> the same text for `j0/0`.

`tests/unit/components/BuiltinCommands.unittests/JqTest.cpp`:
- New `TEST_F(BuiltinCommandsTest, JqLibraryOnAPackageFile)`: `WriteFile("/w/package.json",
  R"({"name":"x","dependencies":{"zlib":"1","ajv":"2","lodash":"3"}})")`;
  `RunCaptured("jq", {"-r", ".dependencies | keys[]", "/w/package.json"})` ->
  out `ajv\nlodash\nzlib\n`, status 0; `{"-c", "[to_entries[] | select(.key != \"name\") | .key]", "/w/package.json"}`
  -> `["dependencies"]\n`; `{"-r", ".dependencies | to_entries[] | \"\\(.key)=\\(.value)\"", ...}` ->
  `zlib=1\najv=2\nlodash=3\n`.
- Update every expectation pinning version `1.0.0` (`JqHelpAndVersion`, the
  not-treated report line) to `1.1.0`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Jq
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Jq*:BuiltinCommandsTest.Every*:BuiltinCommandsTest.Lists*'
bash ./scripts/test_linux.sh L U BuiltinCommands
bash ./scripts/test_linux.sh L U
```

## Docs

- `commands/jq/CLAUDE.md`: a section "Library" -- which builtins are jq
  code copied from jq 1.7.1's `builtin.jq` (and the licence notice in
  `JqPrelude.cpp`), which are C++ natives and in which file, why
  `recurse`/`while`/`until`/`repeat` are iterative natives with a path mode
  (the 512 call-depth limit), the collected-outputs difference, and the
  documented differences: `builtins` order, no Bessel functions, `limit`/
  `nth`/`last(f)` not path expressions, `walk` limited to ~170 levels. File
  bullets for the three new `.cpp` files.
- `src/components/BuiltinCommands/CLAUDE.md`: the `jq` row -- version
  1.1.0, the library added, the exceptions above.
- Root `CLAUDE.md`: nothing (the `jq` row written by `tools--jq-command`
  already names the builtin library).

## Acceptance

- [ ] Every case of the tests gives the container's jq output, messages
      byte-exact.
- [ ] The listed jq definitions are copied verbatim from the container's
      `builtin.jq`, with the licence notice; the prelude's own `recurse/1`
      is gone.
- [ ] `recurse/1,2`, `while`, `until`, `repeat` are iterative natives with
      value and path modes from one template; 3000-step loops run.
- [ ] `range/2` evaluates its first argument outermost; other value-argument
      natives use `ForEachArgumentCombination`.
- [ ] Math uses only `std::` functions of portable C++17; no POSIX headers,
      no `<regex>`; nothing reaches outside the process.
- [ ] `builtins` hides `_` names; `ascii` and `j0` are "not defined".
- [ ] jq version `1.1.0`; `Jq.unittests`, `BuiltinCommands.unittests` and the
      whole unit suite pass.

## Out of scope

- `test match capture scan splits split/2 sub gsub`, `@csv @tsv @html @uri
  @sh @base64 @base64d`, `format/1`, `now mktime gmtime localtime strftime
  strflocaltime strptime todate fromdate todateiso8601 fromdateiso8601`:
  `tools--jq-text`.
- `IN`, `INDEX`, `JOIN`, modules and their functions, the Bessel functions,
  `getpath`/`paths`/`del` (done by `tools--jq-paths`), the I/O builtins (done
  by `tools--jq-command`).
- Making `foreach`/`reduce` path expressions (jq 1.7.1 lets them pass a
  path through in some cases; `tools--jq-paths` does not).
