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
- `Regex::Search(text, start, flags)` -- the matcher, by the next task
  (base--regex-match); a stub returning `false` for now.
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

## Documented differences from GNU

- Bytes, not characters: no collation, no locale, `.` matches one byte. A
  bracket expression ranges over bytes; `[[:alpha:]]` and friends use their
  ASCII members only. Case folding and `\w` `\s` `\d` likewise.
- GNU escapes such as `\w` `\b` `\<` `\>` exist in BRE/ERE as glibc has them,
  but `\n` in a pattern is the character `n`: callers such as sed and awk
  translate their own string escapes before compiling (the plan's callers
  note). The parser does not take escapes a shell-quoted pattern would.
- The 250-parenthesis nesting limit (see above).
- Perl's messages come from PCRE2 (the version in the container); where
  Haisos does not implement a PCRE2 feature, the message is Haisos's own.

## Files

- `Regex.h`, `Regex.cpp` -- the public `Regex` class: `Compile` calls
  `ParseRegex` and keeps the tree; `Search` is the next task's.
- `RegexParser.h`, `RegexParser.cpp` -- `ParseRegex`: the recursive-descent
  parser, one path per syntax, with the GNU/PCRE2 error wording inline at the
  point each mistake is met.
- `RegexTree.h`, `RegexTree.cpp` -- the tree `RegexTree` and `DumpRegexTree`.

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
