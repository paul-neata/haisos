# Regex

Haisos's own regex engine, on bytes: GNU BRE and ERE and a Perl subset
(PCRE2-style), parsed by a recursive-descent parser into a flat tree
(`RegexTree`). No `<regex>`, no PCRE2: `<regex>` has no leftmost-longest mode,
no BRE, and different error messages from GNU's; PCRE2 is a large C
dependency Haisos does not carry. The same engine is to be shared by the grep,
sed, find, awk, rg and jq builtins, whatever syntax each of them speaks --
free functions and one class, no interface in `interfaces/`, no `Create()`
beyond `Regex::Compile`. CMake target `Regex`.

## API (`Regex.h`)

- `RegexSyntax`: `Basic` (GNU BRE, sed's), `Extended` (GNU ERE, awk's), `Perl`
  (grep -P's subset).
- `RegexOptions`: `syntax`, `ignoreCase`, `multiline` (sed's M flag, and
  Perl's `(?m)` from the start: `^`/`$` also match after/before each `'\n'`).
  Case folding is ASCII-only.
- `Regex::Compile(pattern, options, error)` returns `nullptr` with `error`
  holding exactly what GNU prints for that mistake (glibc's message for
  BRE/ERE, PCRE2's for Perl) -- the wording callers such as grep echo to the
  user. Nesting past 250 parentheses (PCRE2's default limit) is refused with
  glibc's "Regular expression too big" (glibc itself has no limit; PCRE2's own
  message is used for Perl): deeper recursion would risk a 1 MB Windows stack.
- `Regex::Search(text, start, match, flags)` -- the matcher: the leftmost
  match at or after `start`, with every capture group, `match` untouched when
  nothing matches. Basic/Extended match leftmost-longest as glibc, Perl
  leftmost-first as PCRE2 (see "Matching"); `kRegexNotBol`/`kRegexNotEol` are
  regexec()'s REG_NOTBOL/REG_NOTEOL. An empty match is a match; a group that
  took no part is (-1, -1); the text before `start` is visible to assertions,
  as `re_search` sees it. `Search` is `const` and thread-safe.
- `Regex::GroupCount()`, `GroupNames()` (index 0 is the whole match, always
  `""`), `Options()`.

`RegexParser.h`'s `ParseRegex` is the parse without the `Regex` wrapper, and
`RegexTree.h`'s `DumpRegexTree` prints a tree in a small test notation:
`(cat ...)`, `(alt ...)`, `(rep min max X)` / `(lazy ...)`, `(group N X)`,
`(backref N)`, assertions as `(bol)` `(eol)` `(begbuf)` and friends, bytes as
`'b'` or `\xhh`, byte sets as maximal `[lo-hi]` runs.

## The three syntaxes

- **Basic (BRE)**: groups `\(`...`\)`, alternation `\|`, intervals `\{m,n\}`;
  `*`, `\+`, `\?` are ordinary at the start of a branch or after an anchor or
  `\(` (and `\{` there is an error); quantifiers stack (`a**` = `(a*)*`);
  `^` anchors only at a branch start, `$` only before a branch end or `\)`;
  `\1`-`\9` are back references, valid only once the group has closed
  (glibc's `re_compile_pattern` checks the same).
- **Extended (ERE)**: `( ) | {m,n} * + ?` are the operators; `\(` `\|` `\{`
  and friends are literals; `^`/`$` anchor anywhere; a quantifier with nothing
  to repeat is `Invalid preceding regular expression`; `{` always starts an
  interval.
- **Perl**: `(?:...)` non-capturing, `(?<name>)`/`(?P<name>)`/`(?'name')`
  named groups, `(?i) (?m) (?s)` flags applying to the rest of the enclosing
  group and `(?i:...)` scoped to one, lazy `*?`, `.` excluding `\n` unless
  dotall, `$` matching before any final newline, `\x{...}` `\cX` `\t` `\e`
  and `\1` back references checked against the whole pattern's groups.
  Unsupported PCRE2 features (lookaround, atomic groups, possessive
  quantifiers, `\p`, `\K`, `\Q`, named back references, ...) are refused with
  a Haisos-own message saying the feature is not supported -- a `{` that is
  no interval shape, as in `a{x}`, is a literal, as PCRE2 takes it.

BRE/ERE are to be matched leftmost-longest and Perl leftmost-first -- the
matcher's business, not the parser's.

## Matching

`Compile` compiles the tree one step further, into a program
(`RegexProgram.h`): `Save` instructions bracket the whole match and each
group (slot 2k the start of group k, 2k+1 its end), `Byte` consumes one byte
of a byte set, `Split`/`Jump` express choice, `Assert` tests an anchor, and
`BackRef` re-matches a group's bytes. `ProgressMark`/`ProgressCheck` guard the
body of a repetition that can match empty: one such iteration completes, its
groups recorded (the answer PCRE2 gives, and glibc's too once the loop has
consumed something), and the repetition then leaves instead of looping
forever on an iteration that consumes nothing. A program larger than
`kMaxRegexInstructions` (1,000,000) is refused: "Regular expression too big"
(glibc's message) for Basic/Extended, "regular expression is too large"
(PCRE2's) for Perl -- so `(a{1000}){1000}` fails at compile time, before any
memory is spent emitting it.

Two engines run the program (`Regex.h`'s `Search` picks):

- **The Pike VM** (`RegexPikeVM.cpp`) -- every program without
  back-references, always. All candidate threads advance in lockstep, one
  byte of text per step, at most one thread per program counter per list, so
  matching takes O(text x program) whatever the pattern: no catastrophic
  backtracking, `(a+)+$` included. Capture slots are carried per thread;
  position and priority decide which of two threads reaching the same
  instruction survives.
- **The backtracker** (`RegexBacktrack.cpp`) -- programs with
  back-references, the one case that cannot be simulated in parallel (as in
  GNU, such a pattern can take exponential time). A depth-first walk of the
  program in priority order at each possible start, on an explicit stack.

Neither engine recurses per input byte: the Pike VM's closure and the
backtracker's walk both use explicit stacks, so a million-byte line or a
`(a?){30000}`-sized program cannot overflow the call stack (the compiler
recurses over the tree only, bounded by the 250-parenthesis nesting limit).

The two modes:

- **Basic/Extended: leftmost-longest, as glibc.** Of all matches, those
  starting leftmost; of those, the longest; of several ways through the
  pattern giving that same span, the groups of the first way in priority
  order (alternatives left before right, a greedy repetition preferring one
  more iteration). That is what glibc's `regexec` reports, not always what
  strict POSIX subexpression rules would give: for `(a|ab)(c|bcd)(d*)` on
  `abcd` GNU reports `a`, `bcd` and the empty string (verified with
  `LC_ALL=C sed -E`), where POSIX would say `ab`, `c`, `d`. Haisos prints
  what GNU prints.
- **Perl: leftmost-first, as PCRE2.** The first match in priority order from
  the leftmost start: alternatives left to right, greedy quantifiers
  longest-first, lazy ones shortest-first. `a|ab` on `ab` matches `a`.

A group inside a repetition reports its last iteration; a back-reference
matches the exact bytes its group last captured (ASCII case-insensitively
when the pattern says so) and fails against a group that has not
participated, as glibc and PCRE2 both do.

## Documented differences from GNU

- Bytes, not characters: no collation, no locale, `.` matches one byte. A
  bracket expression ranges over bytes; `[[:alpha:]]` and friends use their
  ASCII members only. Case folding and `\w` `\s` `\d` likewise.
- GNU escapes such as `\w` `\b` `\<` `\>` exist in BRE/ERE as glibc has them,
  but `\n` in a pattern is the character `n`: callers such as sed and awk
  translate their own string escapes before compiling (the plan's callers
  note). The parser does not take escapes a shell-quoted pattern would.
- The 250-parenthesis nesting limit (see above); a tree nested deeper than
  1100 nodes -- say `a` and a thousand stacked GNU quantifiers, `a**...*` --
  is refused the same way, as the program compiler recurses over the tree.
- A repetition whose body can match empty completes one empty iteration and
  records its groups, as PCRE2 does. glibc agrees once the loop has consumed
  something, but reports the group as unset when a group whose subexpression
  is itself an unbounded repetition matches empty at the loop's entry:
  `\(a\*\)*\1b` on `b` GNU cannot match, Haisos can.
- Perl's messages come from PCRE2 (the version in the container); where
  Haisos does not implement a PCRE2 feature, the message is Haisos's own.

## Files

- `Regex.h`, `Regex.cpp` -- the public `Regex` class: `Compile` calls
  `ParseRegex` and then `CompileRegexProgram`; `Search` runs the Pike VM, or
  the backtracker when the pattern has back-references.
- `RegexParser.h`, `RegexParser.cpp` -- `ParseRegex`: the recursive-descent
  parser, one path per syntax, with the GNU/PCRE2 error wording inline at the
  point each mistake is met.
- `RegexTree.h`, `RegexTree.cpp` -- the tree `RegexTree` and `DumpRegexTree`.
- `RegexProgram.h`, `RegexProgram.cpp` -- the compiled program
  `RegexProgram` and `CompileRegexProgram` (the tree compiled by a Thompson
  construction), plus `RegexAssertionHolds`, the anchor semantics shared by
  the two engines.
- `RegexPikeVM.cpp`, `RegexBacktrack.cpp` -- the two engines,
  `PikeSearch`/`BacktrackSearch`, internal to the component.

Recursion is bounded by the nesting limit; concatenations and alternations
are flat `n`-ary nodes, so a 200000-character pattern parses in one level.

## Notes for callers

- awk (POSIX awk's ERE): use `Extended`; the awk task pre-processes awk's own
  escapes first -- `\/` to `/`, `\"` to `"`, `\n \t \r \a \b \f \v \ddd`
  to the byte (written inside brackets or escaped when it is a regex
  metacharacter), an escape inside brackets (`[a\]b]`) into a bracket that
  needs none (`[]ab]`, `\\` to `\`, `\-` placed last), any other `\c` with c
  not a regex metacharacter to `c` (gawk --posix has no `\w \s \< \> \y`).
  Nothing else is needed from Regex for awk.
- sed: `Basic`/`Extended`, `multiline` for the M flag, `ignoreCase` for I;
  sed translates `\n`, `\t` and its other escapes before compiling.
- find `-regex`: a whole-path match is `Search(text, 0, m)` with
  `m.groups[0] == {0, text.size()}` (exact for leftmost-longest).
- grep: `-w`/`-x` are the caller's business (check the match's surroundings,
  or wrap the pattern); so are grep's own error texts for things glibc
  accepts (`[:space:]` outside brackets).
- jq/rg: `Perl`; named groups via `GroupNames()`; jq's flags are mapped by
  tools--jq-text (its `p` flag is `(?s)` prepended; `s` changes nothing).
