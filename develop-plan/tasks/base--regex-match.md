# Task base--regex-match: the Regex matcher -- leftmost-longest and leftmost-first

- Rock: base
- Depends on: base--regex-syntax
- Size: ~850 changed lines in ~8 files
- Plan checked against: develop @ d2f11f2
- PR title: Regex: match GNU leftmost-longest and Perl leftmost-first

## Goal

`Regex::Search` works. A compiled pattern finds its leftmost match in a text
from a start offset, with every capture group, exactly as GNU sed/grep
(glibc) report it for Basic and Extended syntax, and as PCRE2 (`grep -P`)
does for the Perl subset. Long lines never overflow the stack (nothing
recurses per input byte), and a pattern without back-references always
matches in time proportional to text length times pattern size -- no
catastrophic backtracking, `(a+)+$` included. After this task grep, sed,
find, awk, rg and jq can all build on `Regex`.

## Context

Read first: `src/components/Regex/CLAUDE.md`, `src/components/Regex/Regex.h`,
`src/components/Regex/RegexTree.h` (all from `base--regex-syntax`), the root
`CLAUDE.md` "Automatic Development Rules".

What `base--regex-syntax` provides, by exact name:
- `Regex.h`: `RegexSyntax`, `RegexOptions { syntax, ignoreCase, multiline }`,
  `kRegexNotBol`, `kRegexNotEol`, `RegexMatch { groups }`, `class Regex` with
  `Compile`, `Search` (a stub returning false), `GroupCount`, `GroupNames`,
  `Options`, and `struct Regex::Compiled { RegexOptions options; RegexTree tree; }`
  defined in `Regex.cpp`.
- `RegexTree.h`: `RegexNodeType { Empty, Bytes, Concat, Alternate, Repeat,
  Group, Assert, BackRef }`, `RegexAssertion { LineStart, LineEnd,
  LineEndPerl, TextStart, TextEnd, TextEndBeforeNewline, WordBoundary,
  NotWordBoundary, WordStart, WordEnd }`, `RegexNode` (fields `bytes`,
  `children`, `min`, `max` (-1 unbounded), `greedy`, `group`, `assertion`,
  `multiline`, `ignoreCase`), `RegexTree { nodes, root, groupCount,
  groupNames, hasBackReferences }`. Every syntax rule and flag (`(?i)`,
  `(?s)`, `(?m)`, ignoreCase, multiline) is already resolved into the tree:
  this task never looks at the pattern text or at `RegexSyntax` except to
  choose the matching mode.
- `Regex.unittests` with `RegexSyntaxTest.cpp`; `BuiltinCommands` already
  links `Regex`.

## The semantics (what the tests pin)

Two modes, chosen by syntax:

- **Basic and Extended -- leftmost-longest, as glibc.** Of all matches, take
  those starting leftmost; of those, the longest. When several ways through
  the pattern give that same span, the groups are those of the *first way in
  priority order*: alternatives left before right, a greedy repetition
  preferring one more iteration over stopping. (This is what glibc's
  `regexec` does -- it finds the span first, then walks the pattern taking
  the first viable branch at every choice -- and it is not always what
  strict POSIX subexpression rules would give: for `(a|ab)(c|bcd)(d*)` on
  `abcd` glibc reports `a`, `bcd` and the empty string where POSIX would say
  `ab`, `c`, `d`.
  Haisos prints what GNU prints.) A group inside a repetition reports its
  last iteration; a group that took no part is (-1, -1).
- **Perl -- leftmost-first, as PCRE2.** The first match in priority order
  from the leftmost start: alternatives left to right, greedy quantifiers
  longest-first, lazy ones shortest-first.

Assertions, at position `p` of `text` (size `n`); a "word byte" is
`[0-9A-Za-z_]`, and outside the text counts as a non-word byte:
- `LineStart`: (`p == 0` and not `kRegexNotBol`) or (`node.multiline` and
  `p > 0` and `text[p-1] == '\n'`).
- `LineEnd`: (`p == n` and not `kRegexNotEol`) or (`node.multiline` and
  `p < n` and `text[p] == '\n'`).
- `LineEndPerl`: not `kRegexNotEol`, and `p == n` or (`p == n-1` and
  `text[p] == '\n'`).
- `TextStart`: `p == 0`. `TextEnd`: `p == n`.
  `TextEndBeforeNewline`: `p == n` or (`p == n-1` and `text[p] == '\n'`).
- `WordBoundary`: word(`text[p-1]`) != word(`text[p]`); `NotWordBoundary`
  its negation; `WordStart`: not word(before) and word(after); `WordEnd`:
  word(before) and not word(after).

The text before `start` is visible to assertions (`text[start-1]`), as
`re_search` sees it: `Search("ab b", 1, ...)` for `\bb` finds (3,4), not (1,2).

A back-reference matches the exact bytes its group last captured (ASCII
case-insensitively when the node says so); to a group that has not
participated it fails (glibc and PCRE2 both).

`Search(text, start, match, flags)`: false (and `match` untouched) if
`start > text.size()` or nothing matches; otherwise true with
`match.groups` resized to `GroupCount() + 1` and filled with absolute offsets.
An empty match is a match (`a*` on `baaa` is (0,0)); callers iterating
(sed's `g`, grep `-o`) advance past an empty match themselves.

## Changes

### `src/components/Regex/RegexProgram.h/.cpp` (new, internal)

The tree compiled into a program for the two virtual machines below
(Thompson construction, as in Russ Cox's "Regular Expression Matching: the
Virtual Machine Approach" -- the implementer may know it as the Pike VM).

```cpp
enum class RegexOp { Byte, Split, Jump, Save, Assert, BackRef, ProgressMark, ProgressCheck, Match };

struct RegexInstruction {
    RegexOp op = RegexOp::Match;
    int x = 0;  // Byte: index into RegexProgram::byteSets; Split: preferred target; Jump: target;
                // Save: capture slot; BackRef: group; ProgressMark/ProgressCheck: mark slot
    int y = 0;  // Split: the other target
    RegexAssertion assertion = RegexAssertion::LineStart;  // Assert
    bool multiline = false;   // Assert
    bool ignoreCase = false;  // BackRef
};

struct RegexProgram {
    std::vector<RegexInstruction> instructions;  // starts at 0
    std::vector<std::bitset<256>> byteSets;
    size_t slotCount = 0;   // 2 * (groupCount + 1): slot 2k start, 2k+1 end of group k
    size_t markCount = 0;   // ProgressMark slots
    bool longest = true;    // Basic/Extended: leftmost-longest; Perl: leftmost-first
    bool hasBackReferences = false;
    std::bitset<256> firstBytes;   // bytes that can be consumed first
    bool canSkip = false;          // true when every match consumes one of firstBytes first
    bool anchoredAtTextStart = false;  // the pattern begins with \` or a non-multiline ^
};

// Compiles |tree|; false with |error| set when the program would exceed
// kMaxRegexInstructions.
bool CompileRegexProgram(const RegexTree& tree, RegexSyntax syntax, RegexProgram& program, std::string& error);
constexpr size_t kMaxRegexInstructions = 1000000;
```

Construction (program = `Save 0`, root, `Save 1`, `Match`):
- `Empty`: nothing. `Bytes`: `Byte` (deduplicate identical sets into
  `byteSets` with a map, or not -- either is fine).
- `Concat`: children in order. `Alternate` of k children:
  `Split(L1, next1)`, L1: child 1, `Jump end`; next1: `Split(L2, next2)` ...;
  the last child without a Split; the preferred target is always the earlier
  alternative.
- `Group n`: `Save 2n`, child, `Save 2n+1`.
- `Assert`: one `Assert` instruction. `BackRef`: one `BackRef`.
- `Repeat {min, max, greedy}` of X: `min` copies of X, then
  - `max == -1`: a loop `L: Split(body, out); body: [ProgressMark k] X
    [ProgressCheck k] Jump L; out:` -- the Mark/Check pair only when X can
    match the empty string (compute "nullable" on the tree: Empty, Assert,
    Repeat with min 0, Group/Concat of nullables, Alternate with a nullable
    child; a BackRef counts as nullable);
  - otherwise `max - min` optional copies nested so that each one is tried
    only if the previous matched: `Split(c1, out) c1: X Split(c2, out) c2: X ... out:`;
  - lazy (`greedy == false`): the same with each `Split`'s two targets swapped.
  Every copy of X saves to the same capture slots, so the last iteration wins.
- Before emitting, compute each node's size bottom-up (saturating at
  `kMaxRegexInstructions + 1`); over the limit, fail with
  `"Regular expression too big"` (Basic/Extended) or
  `"regular expression is too large"` (Perl, PCRE2's text) -- so
  `(a{1000}){1000}` fails quickly instead of eating memory.
- The compile walks the tree recursively (depth bounded by the parser's
  nesting limit of 250 -- not 1000: the Windows stack is 1 MB, so keep each
  recursive frame small, and the matcher must mind recursion depth too) but iterates over `Concat`/`Alternate` children.

`firstBytes`/`canSkip`: walk from instruction 0 through `Save`, `Split`,
`Jump`, `ProgressMark/Check` and `Assert` (an assertion is treated as
passing: a superset is safe), with a visited set; the union of every `Byte`
set reached is `firstBytes`; reaching `Match` or `BackRef` makes `canSkip`
false. `anchoredAtTextStart`: the root (or the first child of a root
`Concat`) is an `Assert` `TextStart`, or `LineStart` without `multiline`.

### `src/components/Regex/RegexPikeVM.cpp`, `RegexBacktrack.cpp` (new, internal; declared in `RegexProgram.h`)

```cpp
// Both: the leftmost match at or after |start|, in the program's mode; on
// success |slots| (size slotCount) holds the offsets, -1 for unset.
bool PikeSearch(const RegexProgram& program, std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& slots);
bool BacktrackSearch(const RegexProgram& program, std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& slots);
```

`Regex::Search` uses `PikeSearch` unless `program.hasBackReferences`, then
`BacktrackSearch` (back-references cannot be simulated in parallel; that is
the one exponential case, as in GNU).

**PikeSearch** -- threads in two lists (`current`, `next`) kept in priority
order, each thread a program counter plus its own copy of the capture slots;
at most one thread per program counter per list (a "sparse set" of pcs marks
which are already there):
1. At each position `p` from `start` to `n` (inclusive):
   a. If no match has been recorded yet and `p` is a possible start, add a
      new thread at pc 0 with all slots -1 to the **end** of `current` (it
      has the lowest priority). "Possible start": `anchoredAtTextStart`
      allows only `p == 0`; `canSkip` allows only a `p < n` with
      `firstBytes[text[p]]`. When `current` is empty and no match is
      recorded, jump `p` straight to the next possible start (stop if none).
   b. Walk `current` in order. A `Match` thread: in **first** mode (Perl)
      record its slots as the result and stop walking `current` (everything
      after it is lower priority); in **longest** mode record it if there is
      no result yet, or its start (`slots[0]`) is smaller than the result's,
      or the start is equal and `p` is larger -- never on a tie -- and go on.
      A `Byte` thread whose set holds `text[p]` (`p < n`) adds the closure of
      `pc + 1` to `next` at position `p + 1`. In longest mode, skip threads
      whose start is greater than the recorded result's start.
   c. Stop when `next` is empty and a result exists, or at `p == n`; else
      swap the lists and continue.
2. The **closure** (adding a thread at a position) follows `Jump`, `Split`
   (preferred target first), `Save` (writes `p` into the slot),
   `ProgressMark`/`ProgressCheck` (no-ops here: the one-thread-per-pc rule
   already stops empty loops) and `Assert` (continues only if it holds at
   that position, with `flags`), until it reaches a `Byte` or `Match`, which
   it appends to the list -- unless that pc is already there. Every pc it
   passes through is marked for this position too, so a second, lower-
   priority way to the same instruction is dropped. It must be **iterative**:
   an explicit stack (a `std::vector`) of entries "explore pc" and "restore
   slot s to value v", one working slot array, pushing a restore before each
   `Save` and the `y` target before following `x`. (A recursive closure
   would overflow on `(a?){32767}`.)

Why longest mode is right: two threads at the same pc and position have the
same future, so the one with the earlier start, or with the same start and
higher priority, can only do better -- exactly what keeping the first
arrival does, given that earlier starts are always earlier in the list.

Allocate the lists and slot storage once per `Search` call (or reuse
`thread_local` buffers); `Search` is `const` and called concurrently, so
nothing mutable lives in the `Regex` object.

**BacktrackSearch** -- for each possible start `s` (same rules as above, in
increasing order): a depth-first search over the program in priority order
with an explicit stack of frames "try pc at position p" and "restore slot /
mark to value", one working slot array and one mark array:
`Byte` advances or fails; `Split` pushes `y` and continues with `x`;
`Save` and `ProgressMark` push their restore then write; `ProgressCheck k`
fails when `mark[k] == p` (an iteration that consumed nothing);
`Assert` as above; `BackRef g`: fails if group g is unset, else compares
`text.substr(slots[2g], len)` with `text.substr(p, len)` (ASCII-folded if
`ignoreCase`) and advances `len`. On `Match`: first mode -> that is the
result, return; longest mode -> record if `p` is greater than the best end
so far (ties keep the first found), stop the search early if `p == n`, else
keep popping. When the stack is empty and a result exists for `s`, return
it; otherwise try the next `s`. No recursion.

### `src/components/Regex/Regex.h`

The review of #44 found the "Semantics" doc comments on the `Regex.h` members
still missing; this task touches `Regex.h` and adds them: above `Search`, the
two modes (leftmost-longest with glibc's tie rule for Basic/Extended,
leftmost-first for Perl), `start`, the flags, `match` untouched on failure,
empty matches, unset groups (-1, -1), and that `Search` is const and
thread-safe; and short ones on `Compile`'s limits (nesting 250, program size).

### `src/components/Regex/Regex.cpp`

`Regex::Compiled` gains a `RegexProgram program`; `Compile` calls
`CompileRegexProgram` after `ParseRegex` and returns null with its error on
failure. `Search` checks `start <= text.size()`, runs the right engine,
converts the slots into `match.groups` (pairs `(slots[2k], slots[2k+1])`,
both -1 when either is -1), returns true or false. Replace the stub's
comment.

### `src/components/Regex/CMakeLists.txt`

Add `RegexProgram.cpp RegexPikeVM.cpp RegexBacktrack.cpp` to the existing
`add_library(Regex STATIC Regex.cpp RegexParser.cpp RegexTree.cpp)`.

### `src/components/Regex/CLAUDE.md`

Add a "Matching" section: the program and the two engines, when each runs,
the two modes and the glibc tie rule above (with the `(a|ab)(c|bcd)(d*)`
example), the complexity (O(text x program) without back-references;
exponential possible with them, as in GNU), the instruction limit and its
message, and that nothing recurses per input byte. Replace the Search bullet's "by the next task (base--regex-match); a stub
returning `false` for now" and any other mention of the stub. Note that the
nesting limit is 250 (see Regex.cpp above).

## Tests

`tests/unit/components/Regex.unittests/RegexMatchTest.cpp` (new), added to
`add_executable(Regex.unittests ...)` in that directory's `CMakeLists.txt`.
A helper `std::string Find(RegexSyntax syntax, std::string_view pattern,
std::string_view text, size_t start = 0, int flags = 0, bool ignoreCase =
false, bool multiline = false)` returns `"error: <message>"`, `"nomatch"`, or
the groups as `(a,b)(c,d)...` (all `GroupCount() + 1` of them). Table-driven,
`SCOPED_TRACE` per case. B = Basic, E = Extended, P = Perl; `\n` below is a
real newline in the C++ string.

Each case below is written `syntax  pattern  text  [options]  =>  expected`,
with pattern and text between `«` and `»` (they may hold `|` and spaces).

`RegexMatchTest.GnuLeftmostLongest`:

- B  «abc»  «xabcx»  =>  (1,4)
- B  «a*»  «baaa»  =>  (0,0)
- B  «a\{2,3\}»  «aaaa»  =>  (0,3)
- B  «\(a*\)\(b*\)»  «aabbb»  =>  (0,5)(0,2)(2,5)
- B  «\(a\)*»  «aaa»  =>  (0,3)(2,3)
- B  «x*»  «aaxx»  [start 2]  =>  (2,4)
- B  «a\|b»  «xb»  =>  (1,2)
- E  «a|ab»  «ab»  =>  (0,2)
- E  «(wee|week)(knights|night)»  «weeknights»  =>  (0,10)(0,3)(3,10)
- E  «(a)|b»  «b»  =>  (0,1)(-1,-1)
- E  «(a|b)*»  «abab»  =>  (0,4)(3,4)
- E  «(a|ab)(c|bcd)(d*)»  «abcd»  =>  (0,4)(0,1)(1,4)(4,4)  (verify)
- E  «(a|ab)(bc|c)»  «abc»  =>  (0,3)(0,1)(1,3)  (verify)
- E  «[a-c]*»  «(empty text)»  =>  (0,0)
- B  «ABC»  «xabc»  [ignoreCase]  =>  (1,4)
- B  «[a-c]\+»  «ABCD»  [ignoreCase]  =>  (0,3)

`RegexMatchTest.AnchorsAndFlags`:

- B  «^a»  «ba»  =>  nomatch
- B  «^b»  «a\nb»  =>  nomatch
- B  «^b»  «a\nb»  [multiline]  =>  (2,3)
- E  «a$»  «a\nb»  =>  nomatch
- E  «a$»  «a\nb»  [multiline]  =>  (0,1)
- B  «$»  «ab»  =>  (2,2)
- B  «$»  «ab»  [kRegexNotEol]  =>  nomatch
- B  «^»  «ab»  [kRegexNotBol]  =>  nomatch
- B  «^»  «a\nb»  [multiline, kRegexNotBol]  =>  (2,2)
- B  «\`a»  «ab»  [kRegexNotBol]  =>  (0,1)
- E  «\`b»  «a\nb»  [multiline]  =>  nomatch
- B  «^»  «ab»  [start 1]  =>  nomatch
- B  «\bb»  «ab b»  [start 1]  =>  (3,4)
- B  «\<the\>»  «other the»  =>  (6,9)
- B  «\bx»  «ax x»  =>  (3,4)
- E  «\Bb»  «abc»  =>  (1,2)
- B  «\w\+»  «  foo_1 bar»  =>  (2,7)
- B  «[[:digit:]]\+»  «ab123c»  =>  (2,5)
- B  «a.c»  «a\nc»  =>  (0,3)
- B  «[^a]»  «\n»  =>  (0,1)
- B  «a»  «abc»  [start 4 (past the end)]  =>  nomatch

`RegexMatchTest.BackReferences`:

- B  «\(.\)\1»  «abccd»  =>  (2,4)(2,3)
- B  «\(a*\)\1»  «aaaa»  =>  (0,4)(0,2)
- E  «(a)|b\1»  «b»  =>  nomatch (the group is unset)
- B  «\(a\)\1»  «aA»  [ignoreCase]  =>  (0,2)(0,1)
- P  «(\w)\1»  «hello»  =>  (2,4)(2,3)

`RegexMatchTest.PerlLeftmostFirst`:

- P  «a|ab»  «ab»  =>  (0,1)
- P  «a+?»  «aaa»  =>  (0,1)
- P  «a*?b»  «aab»  =>  (0,3)
- P  «(a|ab)c»  «abc»  =>  (0,3)(0,2)
- P  «(a|ab)(c|bcd)(d*)»  «abcd»  =>  (0,4)(0,1)(1,4)(4,4)
- P  «\d+»  «ab12»  =>  (2,4)
- P  «(?i)abc»  «ABC»  =>  (0,3)
- P  «a(?i)b»  «aB»  =>  (0,2)
- P  «a(?i)b»  «AB»  =>  nomatch
- P  «a.b»  «a\nb»  =>  nomatch
- P  «(?s)a.b»  «a\nb»  =>  (0,3)
- P  «a$»  «a\n»  =>  (0,1)
- P  «(?<y>\d{4})-(\d\d)»  «on 2024-01»  =>  (3,10)(3,7)(8,10)
- P  «\bfoo\b»  «a foo.»  =>  (2,5)
- P  «(?:ab)+»  «ababx»  =>  (0,4)

"(verify)" rows: check in the container first, e.g.
`echo abcd | LC_ALL=C sed -E 's/(a|ab)(c|bcd)(d*)/[\1][\2][\3]/'` (expected
`[a][bcd][]`) and `echo abc | LC_ALL=C sed -E 's/(a|ab)(bc|c)/[\1][\2]/'`
(expected `[a][bc]`); Perl rows with `grep -oP` or `python3 -c` (Python's
`re` agrees with PCRE2 on all of these). Every other row may be checked the
same way. If GNU disagrees with a row, make the test expect GNU's output when
the implementation gives it; otherwise keep the test as `DISABLED_` with a
comment quoting GNU's output, and say so in the PR summary.

`RegexMatchTest.GroupsArePresentForEveryGroup` -- after a successful
`Search`, `groups.size() == GroupCount() + 1` for patterns with 0, 1 and 10
groups.

`RegexMatchTest.TooBigPatternsFail` -- E `(a{1000}){1000}` -> null with
`Regular expression too big`; P same pattern -> `regular expression is too large`.

`RegexMatchPerformanceTest` (each case also asserts it took under 10 s of
wall time, measured with `std::chrono::steady_clock`; in a release build
they take well under a second):
- `LongLinesDoNotOverflowTheStack` -- a text of 1,000,000 `a` then `b`:
  E `(a|b)*c` -> `nomatch`; B `\(.*\)b` -> `(0,1000001)(0,1000000)`; P `.*?b`
  -> `(0,1000001)`; B `\(a\)\1` (back-reference engine) -> `(0,2)(0,1)`;
  E `(a?){30000}` compiles and runs on the text without crashing.
- `NoCatastrophicBacktrackingWithoutBackReferences` -- 5000 `a` then `!`:
  E `(a*)*b`, E `(a|aa)*c`, P `(a+)+$`, P `(a|a)*b` -> all `nomatch`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Regex
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/Regex/CLAUDE.md` -- the "Matching" section above. The root
`CLAUDE.md` row for Regex (written by `base--regex-syntax`) stays.

## Acceptance

- [ ] `Regex::Search` implemented with the exact contract signature; flags
      `kRegexNotBol`/`kRegexNotEol` honoured as specified.
- [ ] Basic/Extended: leftmost-longest with glibc's tie rule; Perl:
      leftmost-first; both engines agree with the tables.
- [ ] No recursion over input bytes anywhere (closure and backtracking use
      explicit stacks); the performance tests pass.
- [ ] Without back-references the Pike VM always runs: no backtracking path
      for such patterns.
- [ ] `Search` is const and thread-safe (no mutable state in `Regex`).
- [ ] Program size limit enforced with the specified messages.
- [ ] No `<regex>`; portable C++17 (MSVC: no VLAs, no GCC builtins).
- [ ] Every "(verify)" row checked against GNU in the container; deviations
      reported in the PR summary.
- [ ] `Regex.unittests` and the full unit suite pass.

## Out of scope

- The parser and its error messages (`base--regex-syntax`).
- Any builtin (grep, sed, find, awk, rg, jq) and their match loops.
- A DFA, literal-string fast paths beyond `firstBytes`, UTF-8 awareness.
- Strict POSIX subexpression rules where glibc does not follow them.
