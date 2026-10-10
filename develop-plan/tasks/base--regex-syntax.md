# Task base--regex-syntax: the Regex component -- parsing GNU BRE, ERE and a Perl subset

- Rock: base
- Depends on: none
- Size: ~750 changed lines in ~11 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the Regex component: GNU BRE/ERE and Perl-subset parser

## Goal

A new library, `src/components/Regex/` (CMake target `Regex`), portable C++17
without `<regex>`, that grep, sed, find, awk, rg and jq will share. This task
gives it its public API and its parser: `Regex::Compile` turns a pattern in
GNU BRE, GNU ERE or a Perl subset into a parse tree, or fails with the
message GNU (glibc) or PCRE2 prints for that mistake. `Search` exists but
finds nothing yet: the matcher is the next task, `base--regex-match`, which
compiles the tree into a program and runs it. Bytes, no locale (as GNU tools
under `LC_ALL=C`); case-insensitivity is ASCII.

## Context

Read first: the root `CLAUDE.md` (the "Creating things" section, "Automatic
Development Rules"), `src/components/Unicode/` as the model of a small
compiled utility library (its `CMakeLists.txt`, `CLAUDE.md`, and how
`UnicodeTables.h` is an internal header the unit tests include),
`tests/unit/components/Unicode.unittests/` as the model of its tests, and
`src/components/BuiltinCommands/commands/hsh/HshPattern.cpp` for the house
style of a hand-written matcher. Nothing in the tree parses regular
expressions today (hsh deliberately avoids `<regex>`: `std::regex` recurses
per character and overflows the stack on long lines, and its dialects are
not GNU's).

The reference behaviour is GNU's, which the task container has (Ubuntu 24.04):
`sed` (glibc regex, `RE_SYNTAX_POSIX_BASIC` / `RE_SYNTAX_POSIX_EXTENDED`),
`grep -P` (PCRE2 10.42). Check an error message with e.g.
`echo | LC_ALL=C sed 's/\(//'` (sed prints `sed: -e expression #1, char 7:
Unmatched ( or \(` -- the text after the last `: ` is the message) and
`echo | LC_ALL=C grep -P '('` (`grep: missing closing parenthesis`).

## Changes

### `src/components/Regex/Regex.h` -- the public API (contract 2, exact)

```cpp
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Haisos {

enum class RegexSyntax { Basic, Extended, Perl };

struct RegexOptions {
    RegexSyntax syntax = RegexSyntax::Basic;
    bool ignoreCase = false;  // ASCII letters only
    bool multiline = false;   // sed's M flag: ^ and $ also match after/before each '\n'
};

// Search flags, as regexec()'s REG_NOTBOL / REG_NOTEOL.
constexpr int kRegexNotBol = 1;  // ^ does not match at the start of the text (\` still does)
constexpr int kRegexNotEol = 2;  // $ does not match at the end of the text (\' still does)

struct RegexMatch {
    // groups[0] is the whole match, groups[n] capture group n, as byte
    // offsets [first, second) into the text searched; (-1, -1) for a group
    // that took no part. Size GroupCount() + 1 after a successful Search.
    std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> groups;
};

class Regex {
public:
    // Null on a bad pattern, |error| then holding the message GNU prints for
    // it (Basic/Extended: glibc's regerror text; Perl: PCRE2's), e.g.
    // "Unmatched [, [^, [:, [., or [=".
    static std::shared_ptr<const Regex> Compile(std::string_view pattern, const RegexOptions& options, std::string& error);

    // Finds the leftmost match starting at or after |start| (<= text.size());
    // ^, \b, \< ... look at text[start - 1] when start > 0, as re_search does.
    bool Search(std::string_view text, size_t start, RegexMatch& match, int flags = 0) const;

    // The number of capture groups, ( ... ) / \( ... \), not counting group 0.
    size_t GroupCount() const;
    // Size GroupCount() + 1; [n] is group n's name ((?<name>...) in Perl), "" if unnamed.
    const std::vector<std::string>& GroupNames() const;
    const RegexOptions& Options() const;

    ~Regex();

private:
    struct Compiled;
    explicit Regex(std::unique_ptr<Compiled> compiled);
    std::unique_ptr<Compiled> m_compiled;
};

}
```

Write each member's doc comment from the "Semantics" section below (that is
what the later builtin tasks read). `Regex` is not an interface of
`interfaces/`, but follows the same rule: private constructor, `Compile()`
returns a `shared_ptr` (to `const`: a compiled regex is immutable and
`Search` is safe to call from several threads at once).

### Semantics to document in `Regex.h` and implement in the parser

General (all three syntaxes):
- The pattern and the text are bytes. `ignoreCase` folds ASCII letters only.
- `.` and negated brackets `[^...]` match every byte, `'\n'` and NUL
  included -- except in Perl, where `.` excludes `'\n'` unless `(?s)` is in
  effect (Perl's `[^...]` does match `'\n'`).
- `^` matches at the start of the text (not when `kRegexNotBol`), `$` at its
  end (not when `kRegexNotEol`); with `multiline` (or Perl `(?m)`), also after
  / before every `'\n'`. Perl's `$` without `(?m)` also matches just before a
  `'\n'` that is the last byte of the text (PCRE2's default).

GNU Basic (BRE, as GNU sed and grep without `-E`; glibc `RE_SYNTAX_POSIX_BASIC`):
- Groups `\(` `\)`; alternation `\|`; repetition `*`, `\+`, `\?`,
  `\{m\}`, `\{m,\}`, `\{m,n\}`, `\{,n\}` (0 to n). `(` `)` `|` `{` `}` `+` `?`
  are ordinary characters.
- `*` (also `\+`, `\?`) is an ordinary character when it starts the pattern,
  or follows `\(`, `\|`, or an anchor (`^`, `$`, `\b`, `\B`, `\<`, `\>`, `` \` ``,
  `\'`); `\{` in those places is an error "Invalid preceding regular expression".
- `^` is an anchor only at the very start of the pattern, after `\(` or after
  `\|`; elsewhere it is ordinary. `$` is an anchor only at the very end of the
  pattern, before `\)` or before `\|`; elsewhere ordinary.
- Repetitions stack: `a**` is `(a*)*`.

GNU Extended (ERE, as `sed -E`, `grep -E`; glibc `RE_SYNTAX_POSIX_EXTENDED`):
- `( ) | { } + ?` are operators, `\(` `\)` `\|` `\{` `\}` `\+` `\?` ordinary
  characters. `^` and `$` are anchors everywhere (`a^b` never matches).
- `*`, `+`, `?`, `{` with nothing to repeat -- at the start, after `(`, `|`
  or an anchor -- is an error "Invalid preceding regular expression".
- `{` always starts an interval: `a{x}` is "Invalid content of \{\}".
- An unmatched `)` is an error "Unmatched ) or \)".
- `()` and empty alternatives (`a|`, `|b`, `a||b`) are allowed and match
  the empty string.

Both GNU syntaxes:
- Back-references `\1` to `\9`; one naming a group not yet closed at that
  point (or not existing) is "Invalid back reference". `\0` is the character `0`.
- GNU operators: `\w` = `[_[:alnum:]]`, `\W` = its complement, `\s` =
  `[[:space:]]`, `\S` its complement, `\b` word boundary, `\B` not a word
  boundary, `\<` start of word, `\>` end of word, `` \` `` start of text,
  `\'` end of text (both unaffected by `multiline` and the Not flags).
- Any other escaped character is that character (`\n` is `n`, `\.` is `.`,
  `\/` is `/`); callers that want `\n` or `\t` to mean newline or tab (sed)
  translate them before compiling. A trailing `\` is "Trailing backslash".
- Intervals: m and n decimal; n < m, a missing number where one is needed,
  or anything else inside is "Invalid content of \{\}"; the pattern ending
  inside an interval is "Unmatched \{"; m or n above 32767 (RE_DUP_MAX) is
  "Regular expression too big".
- Brackets: `[...]`, `[^...]`; `]` first (after an optional `^`) is literal;
  `-` first or last is literal; ranges by byte value (`[a-z]` is 0x61-0x7a);
  a range ending below its start is "Invalid range end"; a range with a class
  as an endpoint is "Invalid range end"; backslash is an ordinary character
  inside brackets (`[\n]` is `\` or `n`); POSIX classes `[:alnum:]`
  `[:alpha:]` `[:blank:]` `[:cntrl:]` `[:digit:]` `[:graph:]` `[:lower:]`
  `[:print:]` `[:punct:]` `[:space:]` `[:upper:]` `[:xdigit:]` (C locale,
  ASCII), any other name "Invalid character class name"; `[=c=]` and `[.c.]`
  are the single character c, more than one character there is "Invalid
  collation character"; an unterminated bracket, class, equivalence or
  collating element is "Unmatched [, [^, [:, [., or [=". With `ignoreCase` a
  set holding a letter also holds its other case.
- Unmatched `\(` / `(`: "Unmatched ( or \("; unmatched `\)` in BRE:
  "Unmatched ) or \)".

Perl subset (PCRE2's syntax, as `grep -P`; also what rg and jq will use):
- Operators `| ( ) * + ? {m} {m,} {m,n}`, lazy forms with a trailing `?`
  (`*? +? ?? {m,n}?`). A `{` that does not form a valid quantifier is a
  literal `{` (`a{`, `a{x}`, `a{,3}` -- PCRE2 10.42). A trailing `+`
  (possessive) is an error "possessive quantifiers are not supported"
  (Haisos's own message). A quantifier with nothing to repeat (at the start,
  after `(` or `|`, or after an assertion) is "quantifier does not follow a
  repeatable item"; m > n is "numbers out of order in {} quantifier"; a
  number above 65535 "number too big in {} quantifier".
- Groups: `(...)` capturing; `(?:...)` non-capturing; named capturing
  `(?<name>...)`, `(?P<name>...)`, `(?'name'...)` (name: a letter or `_`,
  then letters, digits, `_`); inline flags `(?i)`, `(?m)`, `(?s)`, any
  combination and with `-` to turn off (`(?i-s)`), applying to the rest of
  the enclosing group; scoped `(?i:...)`. Lookaround `(?=` `(?!` `(?<=`
  `(?<!` is "lookaround assertions are not supported", `(?>` "atomic groups
  are not supported" (Haisos's own messages); `(?x)` and any other `(?`
  form is "unrecognized character after (? or (?-". Missing `)`: "missing
  closing parenthesis"; extra `)`: "unmatched closing parenthesis".
- `^`, `$` anchors everywhere; `.` (see above).
- Escapes: `\d \D` (`[0-9]`), `\w \W` (`[0-9A-Za-z_]`), `\s \S`
  (`[\t\n\v\f\r ]`), `\h \H` (`[\t \xa0]`), `\v \V` (`[\n\v\f\r\x85]`),
  `\b \B`, `\A` (start of text), `\z` (end), `\Z` (end, or before a final
  `'\n'`), `\t \n \r \f \e \a` (0x07), `\0` followed by up to two octal
  digits, `\xhh` (up to two hex digits), `\x{h...}` (value above 0xff:
  "character code point value in \x{} or \o{} is too large"), `\cX`
  (control-X), back-references `\1` to `\9` (to a group that exists anywhere
  in the pattern; one that does not: "reference to non-existent
  subpattern"). Any non-alphanumeric escaped character is itself. An escaped
  letter or digit not listed is "unrecognized character follows \"; the
  PCRE2 escapes Haisos lacks (`\G \K \p \P \X \R \N \g \k \Q \E`) are
  "\<c> is not supported" (Haisos's own, e.g. `\p is not supported`). A
  trailing `\` is "\ at end of pattern".
- Brackets: as above but backslash escapes work inside (`\]`, `\\`, `\-`,
  `\n`, `\d`, `\w`, `\s` and their complements, `\xhh`, `\b` = backspace
  0x08), POSIX classes `[:name:]` and negated `[:^name:]` inside brackets
  only; unterminated: "missing terminating ] for character class"; a range
  ending below its start: "range out of order in character class"; a class
  as a range endpoint: "invalid range in character class"; unknown class:
  "unknown POSIX class name"; a POSIX class outside brackets (pattern
  `[:alpha:]`): "POSIX named classes are supported only within a class".
- Unicode properties, recursion, conditionals, `\Q...\E`, `(?x)`: not
  supported (Out of scope in the develop's goal).

Limits (all syntaxes): parentheses nested deeper than 1000 fail with
"Regular expression too big" (Basic/Extended) or "parentheses are too deeply
nested" (Perl), so the recursive-descent parser cannot overflow the stack.

Messages to verify in the container before writing a test that pins one: run
the pattern through `LC_ALL=C sed` (BRE), `LC_ALL=C sed -E` (ERE) and
`LC_ALL=C grep -P` (Perl). If GNU disagrees with this plan, GNU wins: follow
it, and say so in the PR summary. The Haisos-own messages ("... not
supported") have no GNU counterpart.

### `src/components/Regex/RegexTree.h` -- internal: the parse tree

What the parser produces and the matcher (next task) compiles. Exact:

```cpp
#pragma once
#include <bitset>
#include <string>
#include <vector>

namespace Haisos {

enum class RegexNodeType { Empty, Bytes, Concat, Alternate, Repeat, Group, Assert, BackRef };

enum class RegexAssertion {
    LineStart,          // ^ (honours kRegexNotBol; with node.multiline also after '\n')
    LineEnd,            // $ (honours kRegexNotEol; with node.multiline also before '\n')
    LineEndPerl,        // Perl $ without (?m): end, or before a final '\n' (honours kRegexNotEol)
    TextStart,          // \` and Perl \A
    TextEnd,            // \' and Perl \z
    TextEndBeforeNewline, // Perl \Z
    WordBoundary,       // \b
    NotWordBoundary,    // \B
    WordStart,          // \<
    WordEnd,            // \>
};

struct RegexNode {
    RegexNodeType type = RegexNodeType::Empty;
    std::bitset<256> bytes;          // Bytes: the bytes matched (ignoreCase already folded in)
    std::vector<int> children;       // Concat, Alternate (>= 2, in order); Repeat and Group: exactly 1
    int min = 0;                     // Repeat
    int max = -1;                    // Repeat; -1 = no upper bound
    bool greedy = true;              // Repeat; false for Perl's lazy forms
    int group = 0;                   // Group: capture number, 1-based; BackRef: the group referred to
    RegexAssertion assertion = RegexAssertion::LineStart;  // Assert
    bool multiline = false;          // Assert LineStart/LineEnd
    bool ignoreCase = false;         // BackRef: compare ASCII case-insensitively
};

struct RegexTree {
    std::vector<RegexNode> nodes;    // nodes refer to each other by index
    int root = -1;
    size_t groupCount = 0;
    std::vector<std::string> groupNames;  // size groupCount + 1
    bool hasBackReferences = false;
};

// The tree as one line, for tests and diagnostics -- format below.
std::string DumpRegexTree(const RegexTree& tree);

}
```

Shape rules the parser keeps (the matcher relies on them): `Concat` and
`Alternate` are n-ary and flat -- a sequence of 1000 characters is one
`Concat` with 1000 children, never a chain 1000 deep; a `Concat` or
`Alternate` with one child is replaced by that child; an empty sequence is
`Empty`; a non-capturing group is just its content (no `Group` node);
consecutive literal bytes stay separate `Bytes` nodes (one per character).

`DumpRegexTree` format (exact, pinned by the tests):
- `Empty` -> `(empty)`
- `Bytes` with exactly one byte b -> `'b'` when b is 0x21-0x7e and not `'`
  or `\`; otherwise `\xhh` (two lowercase hex digits). With more than one
  byte -> `[` + the set as maximal runs of consecutive byte values in
  increasing order + `]`: a run of one byte is that byte, a run of two is
  both bytes, a run of three or more is `lo-hi`; each byte written as itself
  when 0x21-0x7e and not one of `[ ] - \ '`, else `\xhh`. So `\w` is
  `[0-9A-Z_a-z]`, `.` in BRE `[\x00-\xff]`, Perl `.` `[\x00-\x09\x0b-\xff]`,
  `[ab]` `[ab]`, a case-folded `a` `[Aa]`.
- `Concat` -> `(cat X Y ...)`, `Alternate` -> `(alt X Y ...)`
- `Repeat` -> `(rep MIN MAX X)` with MAX `inf` when unbounded; lazy:
  `(lazy MIN MAX X)`
- `Group` -> `(group N X)`
- `Assert` -> `(bol)`, `(eol)`, `(eol-perl)`, `(bol-m)` / `(eol-m)` when
  multiline, `(begbuf)`, `(endbuf)`, `(endbuf-nl)`, `(wordb)`, `(notwordb)`,
  `(wordstart)`, `(wordend)`
- `BackRef` -> `(backref N)`, `(backref-i N)` with ignoreCase
- Children separated by one space; nothing else.

### `src/components/Regex/RegexParser.h/.cpp` -- internal

`bool ParseRegex(std::string_view pattern, const RegexOptions& options, RegexTree& tree, std::string& error);`
-- true and a complete tree, or false with `error` set (tree contents then
unspecified). One recursive-descent parser with a small per-syntax lexer
(reading the next token -- character, operator, escape, bracket -- by the
rules above); the recursion is over nesting only, with the depth limit
above. Context rules that depend on what came before (BRE's literal leading
`*`, `^` and `$` as anchors) are decided by the parser, which knows whether
an expression has started in the current branch and what the next token is.
Bracket expressions are parsed into a `std::bitset<256>` by one function per
syntax family (GNU / Perl); POSIX class membership by a table of
`<cctype>`-free ASCII predicates (do not call `isalpha` etc.: they depend on
the locale).

Group numbering is by the position of the opening parenthesis, left to
right, as in every regex flavour.

### `src/components/Regex/RegexTree.cpp`

`DumpRegexTree`, iterative or recursive over the tree (depth is bounded by
the nesting limit).

### `src/components/Regex/Regex.cpp`

`struct Regex::Compiled { RegexOptions options; RegexTree tree; };` --
`base--regex-match` adds the compiled program to it. `Compile` calls
`ParseRegex`; on success returns
`std::shared_ptr<const Regex>(new Regex(std::make_unique<Compiled>(...)))`.
`GroupCount`, `GroupNames`, `Options` read `m_compiled`. `Search` returns
false and leaves `match` untouched, with a comment saying
`base--regex-match` implements it. Defaulted destructor here (the
`Compiled` type is complete only in this file).

### `src/components/Regex/CMakeLists.txt`

As `src/components/Unicode/CMakeLists.txt`: `add_library(Regex STATIC
Regex.cpp RegexParser.cpp RegexTree.cpp)`, include directories
`${CMAKE_SOURCE_DIR}` and `${CMAKE_SOURCE_DIR}/src/components/Regex`,
`cxx_std_17`. No dependency (not even Logger).

### Root `CMakeLists.txt`

`add_subdirectory(src/components/Regex)` next to `src/components/Unicode`,
before `src/components/BuiltinCommands`.

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `Regex` to `target_link_libraries(BuiltinCommands PUBLIC ...)`, so the
builtin tasks only `#include "src/components/Regex/Regex.h"`.

### `src/components/Regex/CLAUDE.md`

New, in the style of `src/components/Unicode/CLAUDE.md`: what it is (one
regex engine of Haisos's own, why not `<regex>`); the API (`Regex.h`); the
three syntaxes and their rules (a condensed form of "Semantics" above,
including that callers translate `\n`/`\t` for BRE/ERE themselves and that
`.` matches `'\n'` outside Perl); the files (`RegexTree.h`,
`RegexParser.*`, and that the matcher arrives with `base--regex-match`);
the documented differences from GNU: bytes not characters (no UTF-8
awareness), ASCII-only case folding and classes, the Perl features not
supported. And a "Notes for callers" list:
- awk (POSIX awk's ERE): use `Extended`; the awk task pre-processes awk's
  own escapes first -- `\/` to `/`, `\"` to `"`, `\n \t \r \a \b \f \v
  \ddd` to the byte (written inside brackets or escaped when it is a regex
  metacharacter), an escape inside brackets (`[a\]b]`) into a bracket that
  needs none (`[]ab]`, `\\` to `\`, `\-` placed last), any other `\c` with c
  not a regex metacharacter to `c` (gawk --posix has no `\w \s \< \> \y`).
  Nothing else is needed from Regex for awk.
- sed: `Basic`/`Extended`, `multiline` for the M flag, `ignoreCase` for I;
  sed translates `\n`, `\t` and its other escapes before compiling.
- find `-regex`: a whole-path match is `Search(text, 0, m)` with
  `m.groups[0] == {0, text.size()}` (exact for leftmost-longest).
- grep: `-w`/`-x` are the caller's business (check the match's
  surroundings, or wrap the pattern); grep's own error texts for things glibc
  accepts (`[:space:]` outside brackets) too.
- jq/rg: `Perl`; named groups via `GroupNames()`; jq's flags are mapped by
  tools--jq-text (its `p` flag is `(?s)` prepended; `s` changes nothing).

### Root `CLAUDE.md`

- Directory structure: add `│   │   ├── Regex/` (keep the list alphabetical
  as it is) with a few words: "The regex engine: GNU BRE/ERE and a Perl subset".
- Architecture table: a row `| **Regex** | src/components/Regex/ | Haisos's
  own regex engine, shared by grep, sed, find, awk, rg and jq: GNU BRE and ERE
  (leftmost-longest) and a Perl subset (leftmost-first), on bytes |`.

## Tests

New directory `tests/unit/components/Regex.unittests/`:
- `CMakeLists.txt` as Unicode's: `add_executable(Regex.unittests
  RegexSyntaxTest.cpp)`, `target_link_libraries(... gtest_main Regex)`,
  `cxx_std_17`. (`base--regex-match` adds `RegexMatchTest.cpp`.)
- `tests/unit/CMakeLists.txt`: `add_subdirectory(components/Regex.unittests)`.

`RegexSyntaxTest.cpp` -- suite names begin with `Regex`. Use a helper
`std::string Dump(std::string_view pattern, RegexOptions options)` that calls
`ParseRegex` (include `src/components/Regex/RegexParser.h`) and returns
`DumpRegexTree(tree)` or `"error: " + message`. Table-driven tests (a
`struct { RegexSyntax syntax; const char* pattern; const char* expected; }`
array, each case reported with `SCOPED_TRACE(pattern)`):

`RegexSyntaxBasicTest.Trees` -- at least these (expected exactly as written):
- `abc` -> `(cat 'a' 'b' 'c')`
- `a*` -> `(rep 0 inf 'a')`
- `*a` -> `(cat '*' 'a')`
- `\(*a\)` -> `(group 1 (cat '*' 'a'))`
- `^*` -> `(cat (bol) '*')`
- `a^b$c` -> `(cat 'a' '^' 'b' '$' 'c')`
- `^a$` -> `(cat (bol) 'a' (eol))`
- `\(^a$\)` -> `(group 1 (cat (bol) 'a' (eol)))`
- `a\|^b` -> `(alt 'a' (cat (bol) 'b'))`
- `a\{2,3\}` -> `(rep 2 3 'a')`; `a\{,2\}` -> `(rep 0 2 'a')`; `a\{2,\}` -> `(rep 2 inf 'a')`; `a\{2\}` -> `(rep 2 2 'a')`
- `a\+\?` -> `(rep 0 1 (rep 1 inf 'a'))`
- `a+?(){}|` -> `(cat 'a' '+' '?' '(' ')' '{' '}' '|')`
- `\(a\)\(b\)\2\1` -> `(cat (group 1 'a') (group 2 'b') (backref 2) (backref 1))`
- ``\w\W\s\S`` -> ``(cat [0-9A-Z_a-z] [\x00-/:-@\x5b-^`{-\xff] [\x09-\x0d\x20] [\x00-\x08\x0e-\x1f!-\xff])`` (the second set holds a backtick)
- `\b\B\<\>\`\'` -> `(cat (wordb) (notwordb) (wordstart) (wordend) (begbuf) (endbuf))`
- `.` -> `[\x00-\xff]`
- `[]a]` -> `[\x5da]`; ``[^]a]`` -> ``[\x00-\x5c^-`b-\xff]``; `[a-]` -> `[\x2da]`;
  `[\n]` -> `[\x5cn]`; `[[:digit:]x]` -> `[0-9x]`; `[[.-.]a]` -> `[\x2da]`; `[[=a=]]` -> `'a'`
- `\n\.` -> `(cat 'n' '.')`
- `\0` -> `'0'`
- `\(\)` -> `(group 1 (empty))`

`RegexSyntaxExtendedTest.Trees`:
- `a|b|` -> `(alt 'a' 'b' (empty))`
- `(?:b)` -> `error: Invalid preceding regular expression` (in ERE `(?` is `(` then a `?` with nothing to repeat)
- `a+?` -> `(rep 0 1 (rep 1 inf 'a'))`
- `a{2}b{1,}` -> `(cat (rep 2 2 'a') (rep 1 inf 'b'))`
- `a^b` -> `(cat 'a' (bol) 'b')`
- `\(\|\{` -> `(cat '(' '|' '{')`
- `()` -> `(group 1 (empty))`
- `(a)\1` -> `(cat (group 1 'a') (backref 1))`

`RegexSyntaxPerlTest.Trees`:
- `a+?b*?c??` -> `(cat (lazy 1 inf 'a') (lazy 0 inf 'b') (lazy 0 1 'c'))`
- `(?:ab)+` -> `(rep 1 inf (cat 'a' 'b'))`
- `(?<y>\d{4})-(\d\d)` -> `(cat (group 1 (rep 4 4 [0-9])) '-' (group 2 (cat [0-9] [0-9])))`, and group names `["", "y", ""]`
- `a(?i)b` -> `(cat 'a' [Bb])`; `(?i:a)b` -> `(cat [Aa] 'b')`; `(?i)a(?-i)b` -> `(cat [Aa] 'b')`
- `.` -> `[\x00-\x09\x0b-\xff]`; `(?s).` -> `[\x00-\xff]`
- `^a$` -> `(cat (bol) 'a' (eol-perl))`; `(?m)^a$` -> `(cat (bol-m) 'a' (eol-m))`
- `\Aa\z\Z` -> `(cat (begbuf) 'a' (endbuf) (endbuf-nl))`
- `a{` -> `(cat 'a' '{')`; `a{x}` -> `(cat 'a' '{' 'x' '}')`; `a{,3}` -> `(cat 'a' '{' ',' '3' '}')`
- `[\]\\\-a]` -> `[\x2d\x5c\x5da]` (the set `-`, `\`, `]`, `a`: `\` and `]` are
  consecutive bytes 0x5c, 0x5d, a run of two)
- `[[:^digit:]]` -> `[\x00-/:-\xff]`
- `\x41\x{42}\cA\t\e` -> `(cat 'A' 'B' \x01 \x09 \x1b)`
- `(a)\1` -> `(cat (group 1 'a') (backref 1))`
- `\.\$` -> `(cat '.' '$')`

The expected strings above were derived from the dump rules; if one
disagrees with the rules, the rules win (fix the test, say so in the PR).
`ignoreCase` cases (a separate small table with
`options.ignoreCase = true`): `a` -> `[Aa]`, `[a-c]` -> `[A-Ca-c]`, `\(a\)\1`
-> `(cat (group 1 [Aa]) (backref-i 1))`.

`RegexSyntaxErrorTest.GnuMessages` -- each `Regex::Compile` returns null with
exactly this `error` (verify each with sed in the container first):
- BRE `a[b` -> `Unmatched [, [^, [:, [., or [=`; `[[:foo:]]` -> `Invalid character class name`;
  `[z-a]` -> `Invalid range end`; `[[.ab.]]` -> `Invalid collation character`;
  `\(a` -> `Unmatched ( or \(`; `a\)` -> `Unmatched ) or \)`;
  `a\{1` -> `Unmatched \{`; `a\{x\}` -> `Invalid content of \{\}`;
  `a\{2,1\}` -> `Invalid content of \{\}`; `a\{32768\}` -> `Regular expression too big`;
  `\{1\}a` -> `Invalid preceding regular expression`; `\1` -> `Invalid back reference`;
  `\(a\1\)` -> `Invalid back reference`; `a\` -> `Trailing backslash`
- ERE `*a` -> `Invalid preceding regular expression`; `a|*b` -> same;
  `(a` -> `Unmatched ( or \(`; `a)` -> `Unmatched ) or \)`; `a{1` -> `Unmatched \{`
- 1001 nested `\(` ... `\)` -> `Regular expression too big`

`RegexSyntaxErrorTest.PerlMessages` (verify each with `grep -P`):
- `(` -> `missing closing parenthesis`; `)` -> `unmatched closing parenthesis`;
  `[a` -> `missing terminating ] for character class`; `*a` -> `quantifier does not follow a repeatable item`;
  `[z-a]` -> `range out of order in character class`; `a{3,2}` -> `numbers out of order in {} quantifier`;
  `a{70000}` -> `number too big in {} quantifier`; `\` -> `\ at end of pattern`;
  `\i` -> `unrecognized character follows \`; `(a)\2` -> `reference to non-existent subpattern`;
  `[[:foo:]]` -> `unknown POSIX class name`; `(?=a)` -> `lookaround assertions are not supported`;
  `a++` -> `possessive quantifiers are not supported`; `\p{L}` -> `\p is not supported`;
  `(?x)a` -> `unrecognized character after (? or (?-`

`RegexSyntaxTest.GroupCountAndNames` -- through `Regex::Compile`: `\(a\)\(b\(c\)\)` has 3
groups; Perl `(?:a)(b)` has 1; `GroupNames().size() == GroupCount() + 1`;
`Options()` returns what was given.

`RegexSyntaxTest.SearchFindsNothingYet` -- **do not write**: `Search` is
replaced in the next task; no test pins the stub.

`RegexSyntaxTest.LongPatternsDoNotRecurse` -- a pattern of 200000 `a`
characters, and one of 200000 alternatives `a|a|...`, compile (ERE) and
dump without crashing (a flat tree).

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Regex
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/Regex/CLAUDE.md` (new), root `CLAUDE.md` (directory list,
Architecture table) -- as described under Changes.

## Acceptance

- [ ] `Regex.h` matches the contract exactly (names, signatures, constants),
      plus `GroupNames()` and `Options()`.
- [ ] `Regex` has a private constructor; `Compile` returns `shared_ptr<const Regex>`.
- [ ] No `<regex>`, no `<cctype>` classification, no POSIX headers;
      compiles with MSVC (no GCC extensions, no VLAs).
- [ ] Parser recursion bounded by the nesting limit; `Concat`/`Alternate` flat.
- [ ] Every error message in the tests checked against GNU sed / grep -P in
      the container (or noted in the PR summary where GNU differs from the plan).
- [ ] `Regex` built by the root `CMakeLists.txt`, linked into `BuiltinCommands`;
      `Regex.unittests` built and passing; the full unit suite passes.
- [ ] `src/components/Regex/CLAUDE.md` written; root `CLAUDE.md` updated.

## Out of scope

- Matching (`Search`), the program, the VMs: `base--regex-match`.
- grep/sed/awk/find/rg/jq and their pre-processing of patterns.
- Unicode/UTF-8-aware matching, locales, collation beyond single bytes.
