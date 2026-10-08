# Task search--rg-regex: rg -- Rust-regex syntax and ripgrep's regex errors

- Rock: search
- Depends on: search--rg-search
- Size: ~500 changed lines in ~5 files
- Plan checked against: develop @ ccb9dbe
- PR title: rg: Rust regex syntax onto Regex Perl, rg's parse errors

## Goal

`rg` reads its patterns as ripgrep does -- Rust's `regex` crate syntax --
instead of handing them to `Regex`'s Perl syntax as written: what Rust
accepts and the Perl subset lacks is translated (`\v`, `\<`, `\>`,
`\b{start}`, `[[:word:]]`, `\u{...}`, `(?x)`, `(?U)`, stacked
repetitions, spaces in `{ 2 }`), what Rust refuses is refused with
ripgrep 14's exact message frame and caret (`(a)\1`, `(?=x)`, `\Z`, `\e`,
`a{,3}`, unclosed groups and classes, ...), and smart case (`-S`) looks at
the parsed literals. So an agent's `rg 'foo\(' src` or `rg '(?<=x)y'` gets
the error text it knows from ripgrep, and `rg '\bCreate\b'` works.

## Context

Read first: `develop-plan/tasks/search--rg-search.md` and
`src/components/BuiltinCommands/commands/rg/` (`Rg.cpp`: where patterns
are joined, escaped for `-F`, checked for newlines, compiled through
`GrepMatcher` with `GrepSyntax::Perl`, and where the interim error frame
`rg: regex parse error:` ... is printed), `src/components/Regex/CLAUDE.md`
(what the Perl subset accepts: `\d \D \s \S \w \W \b \B \A \z`, `\xhh`,
`\x{h..}`, classes with `[:name:]`/`[:^name:]`, `(?:)`, named groups
`(?<n>)`/`(?P<n>)`, `(?i)`/`(?s)`/`(?m)` and scoped `(?i:...)`, lazy
quantifiers; a `{` that is not a quantifier is literal; no `\<`, no
lookaround).

Reference: ripgrep 14.1.1 (see `search--rg-search.md` for running it in the
container: `(exec -a rg "$(npm root -g)/@anthropic-ai/claude-code/bin/claude.exe" ARGS)`),
`LC_ALL=C`. All frames below were produced by it.

## Changes

### `commands/rg/RgRegex.h` / `RgRegex.cpp` (new)

```cpp
struct RgRegexError {
    size_t position = 0;      // byte offset in the wrapped pattern
    size_t length = 1;        // carets
    std::string message;      // after "error: "
    bool pcre2Hint = false;   // backreferences and look-around
};
// |patterns| as given (after -F escaping). Builds the wrapped pattern rg
// shows -- each pattern as "(?:" p ")", joined by "|" -- parses it as Rust
// regex syntax and returns the equivalent Regex Perl-subset pattern, or
// nullopt with |error| set. |hasUppercaseLiteral| for -S.
std::optional<std::string> TranslateRgPattern(const std::vector<std::string>& patterns,
                                              std::string& wrapped, RgRegexError& error,
                                              bool& hasUppercaseLiteral);
// The frame rg prints, byte for byte (see below).
std::string FormatRgRegexError(const std::string& wrapped, const RgRegexError& error);
```

One left-to-right pass over `wrapped` with a stack of open groups (each:
position of its `(`, its flags `i s m x U` as in effect inside it) and the
current flags; nothing recursive per byte. Emit Perl text as you go.

Accepted and emitted as is: literals (an ASCII punctuation byte that is a
Perl metacharacter is emitted escaped), `.`, `^`, `$`, `|`, `(`, `(?:`,
`(?<name>`, `(?P<name>`, `)`, `*`, `+`, `?`, `{m}`, `{m,}`, `{m,n}` and
their lazy `?` forms, `\d \D \s \S \w \W \b \B \A \z`, `\t \n \r \a \f`
(`\n` and anything else matching a newline byte is refused earlier by
rg-search's newline rule: extend that rule here to `\n`, `\x0a`,
`\x{a}`, `\u000a` -> the same "literal \"\\n\" is not allowed" message),
`\xhh`, `\x{h..}` (above 0x7f: its UTF-8 bytes as `\xhh` each, outside
classes), escaped ASCII punctuation and space (`\.`, `\-`, `\ `, `\_`,
`\#`, `\&`, `\~`, `\@`, `\%`: the byte), classes `[...]`, `[^...]` with
`]` first literal, ranges, `[:name:]` and `[:^name:]`.

Translated:
- `\v` -> `\x0b` (Rust: vertical tab; Perl's `\v` is a class).
- `\u` + 4 hex, `\U` + 8 hex, `\u{h..}`, `\U{h..}`: the code point's UTF-8
  bytes (outside classes); a value above 0x10FFFF or a surrogate ->
  `hexadecimal literal is not a Unicode scalar value` (span: the escape).
- `\<`, `\>`, `\b{start}`, `\b{end}`, `\b{start-half}`, `\b{end-half}` ->
  `\b` (an approximation, documented: Haisos's Perl subset has no
  one-sided word boundary; exact whenever the pattern next to it is a word
  character, the usual case).
- `[:word:]` -> `\w`, `[:^word:]` -> `\W` inside a class.
- `{ m , n }`: spaces inside braces dropped.
- A quantifier right after a quantifier (`a**`, `x{2}{3}`): wrap the
  previous quantified item in `(?:` `)` first.
- `(?flags)` / `(?flags:`: `i s m` passed on; `u` and `R` dropped; `x`
  handled here -- while in effect, unescaped whitespace and `#`-to-end-of-
  line outside classes are skipped; `U` handled here -- while in effect,
  greedy and lazy forms are swapped when emitted. Flags end with their
  group, as Rust scopes them.

Refused, with these messages and spans (positions in `wrapped`; the frame
below adds 4 spaces): 
- `unclosed group` -- at the end, the innermost group still open (span 1
  at its `(`): `a(b` -> under the wrapper's `(` (column 0), `a(b(c` ->
  under the first user `(`; a trailing `\` makes the wrapper's `)` an
  escaped literal, so `a\` is `unclosed group` at column 0 too.
- `unopened group` -- a `)` with nothing open (span 1).
- `unclosed character class` -- the `[` of the outermost open class (span
  1); `[[a]`, `a[b[c]` (Rust nests classes) the outer `[`.
- `repetition operator missing expression` -- `*`, `+`, `?` or `{` with
  nothing to repeat (at a group's or alternative's start, after `(?`):
  span 1 at the operator (`(?` alone gives this, at its `?`).
- `repetition quantifier expects a valid decimal` -- where a number is
  required and something else is (`a{,3}` at the `,`; `a{` at the byte
  after `{`): span 1.
- `unclosed counted repetition` -- digits read but no `}` (`xy{2` -> span
  `{2`, 2 bytes: from `{` to before the byte that stopped it).
- `invalid repetition count range, the start must be <= the end` -- span
  the whole `{2,1}`.
- `unrecognized escape sequence` -- a backslash and a letter Rust does not
  know (`\h \e \Z \Q \E \c \k \g \G \K \R \N \X \H \V \o`): span 2.
- `backreferences are not supported` -- `\` and a digit (`\1`, `\0`,
  `\141`): span 2, PCRE2 hint.
- `look-around, including look-ahead and look-behind, is not supported` --
  `(?=`, `(?!` span 3, `(?<=`, `(?<!` span 4, PCRE2 hint.
- `unrecognized flag` -- span 1 at the flag byte (`(?z)`).
- `invalid escape sequence found in character class` -- `[\b]`: span 2.
- Haisos's own refusals (documented, same frame): `\p`/`\P` classes ->
  `Unicode classes are not supported by HaisosOS rg` (span: `\pL` 3 bytes,
  `\p{...}` through its `}`); `&&`, `--`, `~~` inside a class, or a class
  inside a class -> `class set operations and nested classes are not
  supported by HaisosOS rg` (span 2 / 1); a non-ASCII byte inside a class
  -> `non-ASCII characters in a class are not supported by HaisosOS rg`.
- Any `Regex::Compile` failure of the translated pattern (a size limit):
  the frame with span 1 at column 0 and `Regex`'s message.

`hasUppercaseLiteral`: an ASCII uppercase letter as a literal or a class
member/range end (not the letter of an escape like `\S`, `\W`, `\D`, `\B`,
`\A`, not a `\x41`-style escape -- rg ignores escapes there too).

`FormatRgRegexError` -- byte-exact:
```
rg: regex parse error:
    <wrapped>
    <position spaces><length carets>
error: <message>
```
each line ending in `\n`; with `pcre2Hint` two more lines follow after an
empty one:
```

Consider enabling PCRE2 with the --pcre2 flag, which can handle backreferences
and look-around.
```
(The `rg: ` belongs to the first line only.)

### `commands/rg/Rg.cpp`

Replace the interim path: build `wrapped` and the Perl pattern with
`TranslateRgPattern`; on error write `FormatRgRegexError` to stderr, exit
2; compile the result with `GrepMatcher` (`GrepSyntax::Perl`, one
pattern -- the translation of the whole `wrapped`). `-S` uses
`hasUppercaseLiteral` instead of rg-search's byte scan. `-F` patterns are
escaped before, so they never fail. Bump `rg` to `1.1.0`; `Help()` notes:
Rust syntax over Haisos's byte-wise Perl subset -- `\w`, `.`, `-i` are
ASCII/byte-wise; `\<`/`\>`/`\b{...}` are `\b`; `\p{...}` refused.

### `CMakeLists.txt`

`commands/rg/RgRegex.cpp`.

## Tests

`tests/unit/components/BuiltinCommands.unittests/RgRegexTest.cpp` (new, in
the CMakeLists): plain `TEST(RgRegexTest, ...)` on `TranslateRgPattern` +
`FormatRgRegexError`, and a few `TEST_F(BuiltinCommandsTest, RgRegex...)`
through `rg` itself. Exact frames, verified with ripgrep 14.1.1 (each line ends in `\n`; the lines are
written `/`-separated below):

- `RgRegexTest.Frames` (single pattern; the frame then `error: ...`):
  `(` -> `    (?:()` / `    ^` / `unclosed group`;
  `a(b(c` -> `    (?:a(b(c)` / `        ^` / `unclosed group`;
  `ab)` -> `    (?:ab))` / `          ^` / `unopened group`;
  `x[ab` -> `    (?:x[ab)` / `        ^` / `unclosed character class`;
  `xy{2` -> `    (?:xy{2)` / `         ^^` / `unclosed counted repetition`;
  `a{,3}` -> `    (?:a{,3})` / `         ^` / `repetition quantifier expects a valid decimal`;
  `a{` -> `    (?:a{)` / `         ^` / the same message;
  `a{2,1}` -> `    (?:a{2,1})` / `        ^^^^^` / `invalid repetition count range, the start must be <= the end`;
  `*a` -> `    (?:*a)` / `       ^` / `repetition operator missing expression`;
  `ab\Qx` -> `    (?:ab\Qx)` / `         ^^` / `unrecognized escape sequence`;
  `(?z)a` -> `    (?:(?z)a)` / `         ^` / `unrecognized flag`;
  `[\b]` -> `    (?:[\b])` / `        ^^` / `invalid escape sequence found in character class`;
  `a\` -> `    (?:a\)` / `    ^` / `unclosed group`;
  `(?` -> `    (?:(?)` / `        ^` / `repetition operator missing expression`.
- `RgRegexTest.Pcre2Hint`: `(a)b\1` -> `    (?:(a)b\1)` / `           ^^` /
  `error: backreferences are not supported` + the hint; `ab(?<=x)` ->
  carets `         ^^^^`, `error: look-around, including look-ahead and
  look-behind, is not supported` + the hint; `\0` -> backreferences, span 2.
- `RgRegexTest.SeveralPatterns`: `-e b -e 'a('` -> wrapped `(?:b)|(?:a()`,
  caret `          ^` (column 6), `unclosed group`.
- `RgRegexTest.Translations` (the Perl text is free, so assert behaviour:
  compile the result and search): `\v` matches `\x0b`; `\<word` and
  `word\>` find `word` in `<word> x`; `[[:word:]]+` matches `x_y`;
  `a**`, `x{2}{3}`, `a{ 2 }` compile; `(?x) a b` matches `ab`; `(?U)a+`
  on `aaa` matches `a`; `(?P<n>a)` and `(?<n>a)` compile;
  `\x{41}` matches `A`; `\u{e9}` matches the bytes `\xc3\xa9`.
- `RgRegexTest.HaisosRefusals`: `\pL`, `[a-z&&[^aeiou]]`, `[é]` -> the
  Haisos messages with their spans.
- `RgRegexTest.SmartCase`: `todo` -> no uppercase; `Todo` -> yes; `\S\W` ->
  no; `[A-Z]` -> yes.
- `BuiltinCommandsTest.RgRegexErrorExitsTwo`: `rg '(' t1` -> err the
  frame, status 2, out empty; `rg -S '\btodo' t1` (t1 = `TODO\n`) prints
  `TODO\n` (no literal uppercase: case-insensitive).
- `BuiltinCommandsTest.RgRegexNewlineEscape`: `rg 'a\nb' t1` -> the
  multiline message of rg-search, 2.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='*RgRegex*:*Rg*'
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/BuiltinCommands/CLAUDE.md`: the `rg` row (1.1.0; Rust
syntax; the documented approximations: `\<`/`\>`/`\b{...}` as `\b`,
byte-wise `.`/`\w`/`-i`, no `\p` classes or class set operations).

## Acceptance

- [ ] Every frame in the tests byte-exact (wrapped pattern, carets,
      message, hint).
- [ ] One pass, explicit stack, no recursion per byte; a 100000-byte
      pattern translates without trouble.
- [ ] Translations behave as listed; refusals use the Haisos messages.
- [ ] `-S` from the parse; `-F` unaffected.
- [ ] Full unit suite passes; CLAUDE.md row updated.

## Out of scope

- PCRE2 (`-P`), multiline (`-U`), Unicode-aware classes and case folding.
- The search, output and ignore rules (`search--rg-search`, `search--rg-ignore`).
