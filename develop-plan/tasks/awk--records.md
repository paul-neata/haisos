# Task awk--records: regexes in awk, regex field separators, paragraph mode

- Rock: awk
- Depends on: awk--interpreter, base--regex-match (`Regex`)
- Size: ~700 changed lines in ~6 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add regexes to awk: patterns, ~ and !~, regex FS, RS = ""

## Goal

awk's regular expressions work as in gawk `--posix`: regex literals as
patterns (`/re/ { ... }`, `!/re/`, ranges `/a/,/b/`) and in expressions,
`~` and `!~` with a literal or a dynamic regex (any string value), regex
field separators (`-F '[0-9]+'`, `FS = ", *"`), and paragraph mode
(`RS = ""`: records separated by blank lines, newline always a field
separator). POSIX ERE with intervals, through Haisos's `Regex` component
(`RegexSyntax::Extended`), after translating awk's own escapes. Regex
warnings and errors are gawk's.

## Context

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` (and
the files it lists, especially `AwkInterpreter.h/.cpp`, `AwkFields.h`,
`AwkInput.h`), `src/components/Regex/CLAUDE.md` ("Notes for callers": what
awk must translate) and `Regex.h`.

What earlier tasks provide (on develop):
- base--regex-syntax / base--regex-match: `Regex::Compile(pattern,
  RegexOptions{RegexSyntax::Extended}, error)` -> `shared_ptr<const Regex>`
  or null with glibc's message (`Unmatched ( or \(`); `Search(text, start,
  match, flags = 0)` (leftmost-longest; `.` and `[^...]` match `\n`, `^`/`$`
  only at the text's ends -- what awk wants); `RegexMatch::groups[0]`.
  Regex is linked into `BuiltinCommands`.
- awk--interpreter: `Interpreter` with `Evaluate`, `SplitRecord` (the
  FieldStore's splitter), `NextMainRecord`, `m_position`, `AwkFatal`, the
  fatal and `(FILENAME=... FNR=...)` formats; the placeholder fatals
  `regular expressions are not implemented yet`, `regular expression field
  separators are not implemented yet`, `RS = "" (paragraph mode) is not
  implemented yet`, which this task removes.
- awk--values: `RecordReader::Next(rs, record)` (paragraph mode left to
  this task), `SplitAwkFields`, `FieldStore::SetRecord(record, fs,
  paragraphMode)`.
- awk--lexer: Regex tokens keep the regex text with `\/` already turned
  into `/`, everything else as written.

Every expected text below was checked with gawk 5.2.1 `--posix`; the task
container has only mawk -- never change an expectation to mawk's.

## Changes

### `commands/awk/AwkRegex.h` / `AwkRegex.cpp` (new)

```cpp
// awk's regex text (a literal's, or a dynamic regex's string value) as the
// ERE Regex::Compile takes. |warnings| gets one message per unknown escape:
// "regexp escape sequence `\w' is not a known regexp operator".
std::string TranslateAwkRegex(std::string_view awkRegex, std::vector<std::string>& warnings);

// Compiled dynamic regexes by their awk text, so a regex held in a variable
// is compiled (and warned about) once. Bounded: at 256 entries it is cleared.
class AwkRegexCache {
public:
    // Null with |error| set (Regex's message) when it does not compile.
    std::shared_ptr<const Regex> Get(const std::string& awkRegex,
                                     std::vector<std::string>& warnings, std::string& error);
};
```

`TranslateAwkRegex`, outside a bracket expression, for a backslash and the
byte after it:
- `\/` -> `/`; `\"` -> `"` with the warning (gawk warns for it and matches
  a quote);
- `\n \t \r \f \v \a \b` -> that control byte (`\b` is a backspace in awk);
- `\` and 1-3 octal digits -> that byte, emitted raw (so `\056` is a `.`
  that matches any byte, as gawk does);
- `\` and one of `. [ ] ( ) * + ? { } | ^ $ \` -> kept as the escape pair;
- `\` and any other byte c -- `y B w W s S < > `` ` `` `'` included: gawk
  `--posix` has none of GNU's regex operators -- -> `c` alone, with the
  warning;
- a final lone `\` -> kept (Regex then reports `Trailing backslash`).
Inside a bracket expression (from an unescaped `[`; a `]` first, after
`[` or `[^`, is a member; `[:class:]`, `[.x.]`, `[=x=]` copied whole): an
escape is decoded to its byte as above (`\]` is `]`, `\\` is `\`, `\/` is
`/`, `\n` a newline) and the bracket is re-emitted so POSIX reads it: a
decoded `]` goes first (after `^`), a decoded `-` last, a decoded `^` not
first; every other member in place. So `[a\]b]` becomes `[]ab]`. Everything
else is copied byte for byte (intervals `{n,m}` are ERE).

### `commands/awk/AwkAst.h`

`Expr` gains `mutable std::shared_ptr<const Regex> compiledRegex;` (a
Regex literal's, set before the program runs).

### `commands/awk/AwkInterpreter.h` / `.cpp`

- **Literal regexes** are compiled in `Prepare`, before BEGIN, walking the
  whole program: warnings written as
  `awk: <src>:<line>: warning: <message>\n`; a regex that does not compile
  is ``awk: <src>:<line>: error: <Regex error>: /<awk text>/\n`` (e.g.
  `awk: cmd. line:1: error: Unmatched ( or \(: /a(/`), every one reported,
  then status 1 and nothing runs. (Report these from `Run`, before BEGIN.)
- **Evaluation**: a Regex expression alone is `$0` matched against it (1 or
  0). `a ~ b` / `a !~ b`: the left's `ToString(CONVFMT)` searched with the
  right's regex -- the literal's `compiledRegex` when the right is a Regex
  node, else the right's `ToString(CONVFMT)` through `AwkRegexCache`
  (dynamic). A dynamic regex's warnings are written with the runtime
  prefix (as the fatal one: `awk: cmd. line:1: (FILENAME=- FNR=1)
  warning: ...`); a dynamic regex that does not compile is
  `AwkFatal("invalid regexp: <error>: /<text>/")`. A match is
  `Search(text, 0, m)` succeeding anywhere.
- Add `bool MatchRegex(const Expr& regexOperand, const std::string& text)`
  (the one place both forms go through; awk--functions' `match`, `sub`,
  `gsub`, `split` use it or its dynamic half).
- **SplitRecord**: when `SplitAwkFields` declines (FS of two or more
  bytes), FS is a regex (dynamic, through the cache; a bad one is
  `AwkFatal("invalid regexp: ...")`): fields are the text between matches;
  a match at the very start gives an empty first field, one at the end an
  empty last field (`-F '[ ]'` on `a  b` gives 3 fields); an empty match
  never separates. In paragraph mode the record is first cut at every
  `\n` and each piece split by FS -- unless FS is `" "`, whose blanks
  already include the newline (so `RS = ""; FS = "x"` on `p1 l1\np1 l2`
  gives 2 fields).
- **NextMainRecord** passes RS `""` to the reader and `paragraphMode =
  true` to `SetRecord`.

### `commands/awk/AwkInput.cpp`

`RecordReader::Next` with `rs == ""`: skip newlines at the start of the
input; a record ends at a run of two or more newlines (the run is the
separator); at the end of the input trailing newlines are not part of the
record and no empty record follows.

### `Awk.cpp`

`Help().notes`: remove "regexes ... not available yet"; add that regexes
are POSIX EREs as gawk `--posix` takes them (no `\y \w \s \< \>`).

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkRegex.cpp`.

## Tests

`tests/unit/components/Awk.unittests/AwkRegexTest.cpp` (new, in the CMakeLists):
- `AwkRegexTest.Translate` (plain `TEST`): `a\/b` -> `a/b`; `a\.b` ->
  `a\.b`; `a\tb` -> `a` TAB `b`; `\056` -> `.`; `a\wb` -> `awb` + one
  warning ``regexp escape sequence `\w' is not a known regexp operator``;
  `a\"b` -> `a"b` + a warning; `[a\]b]` -> `[]ab]`; `[\\]` -> `[\]`;
  `[a\-z]` -> `[az-]`; `[[:alpha:]\/]` -> `[[:alpha:]/]`; `a{2}b` unchanged;
  `a\` -> `a\`.

In `AwkInterpreterTest.cpp` (`AwkRunTest`, the fixture's `/abc.txt`),
outputs exactly:
- `RegexPatterns`: input `abc\n`,
  `'/b/ { print "lit" } !/z/ { print "neg" } $0 ~ /c$/ { print "end" } { x = /a/; print x + 1 }'`
  -> `lit\nneg\nend\n2\n`; `'/b/,/e/ { print "r", $1 } $1 == "d", 0' /abc.txt`
  -> `r a\nr d\nd e f\n`; input `a/b\n`, `'/\// { print "slash" }'` -> `slash\n`;
  input `a/b\n`, `'/a[/]b/'` -> `a/b\n`.
- `RegexEscapes`: input `axb\n`, `'/a\056b/ { print "dot" }'` -> `dot\n`;
  input `awb\n`, `'/a\wb/ { print "w" }'` -> out `w\n`, err
  ``awk: cmd. line:1: warning: regexp escape sequence `\w' is not a known regexp operator\n``;
  input `a]b\n`, `'/a[\]]b/ { print "b" }'` -> `b\n`; input `aab\n`,
  `'/a{2}b/ { print "i" }'` -> `i\n`; input `a{2}b\n` with the same -> nothing;
  input `a\tb\n` (a real tab), `'/a\tb/ { print "t" }'` -> `t\n`.
- `DynamicRegexes`: input `awb\n`, `'{ if ($0 ~ "a\\wb") print "m" }'` -> out
  `m\n`, err ``awk: cmd. line:1: (FILENAME=- FNR=1) warning: regexp escape sequence `\w' is not a known regexp operator\n``;
  input `ab\nab\n`, `'BEGIN { r = "^a" } $0 ~ r { n++ } END { print n }'` -> `2\n`;
  input `awb\nawb\n`, `'{ if ($0 ~ "a\\wb") n++ } END { print n }'` -> out `2\n`
  and exactly one warning line (the regex is compiled once, from the cache);
  input `x\n`, `'{ print ("a+" ~ "a+"), ("aa" ~ "a+"), ("b" !~ "a"), ("+" ~ "a+") }'` -> `1 1 1 0\n`.
- `RegexErrors`: input `ab\n`, `'$0 ~ "a("'` -> err
  `awk: cmd. line:1: (FILENAME=- FNR=1) fatal: invalid regexp: Unmatched ( or \(: /a(/\n`, 2;
  `'/a(/'` -> err `awk: cmd. line:1: error: Unmatched ( or \(: /a(/\n`, 1, nothing read.
- `RegexFieldSeparators`: input `a1b22c\n`, `-F '[0-9]+' '{ print NF, $3 }'` -> `3 c\n`;
  input `a  b\n`, `-F '[ ]' '{ print NF }'` -> `3\n`; input `x, y,z\n`,
  `'BEGIN { FS = ", *" } { print $2 "|" $3 }'` -> `y|z\n`; input `:a\n`,
  `-F ':+' '{ print NF }'` -> `2\n`.
- `ParagraphMode`: `/para.txt` written as `p1 l1\np1 l2\n\n\n\np2 l1\n\n`:
  `'BEGIN { RS = "" } { print NR ": " $1 "|" $NF "|" NF }' /para.txt` ->
  `1: p1|l2|4\n2: p2|l1|2\n`; `'BEGIN { RS = ""; FS = "x" } { print NF; print $2 }' /para.txt`
  -> `2\np1 l2\n1\n\n`; input `\n\na\nb\n\n\nc` with `'BEGIN { RS = "" } { print NR ": " $0 }'`
  -> `1: a\nb\n2: c\n`.
- Remove `AwkRunTest.NotYetAvailable`'s regex case.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/awk/CLAUDE.md`: a "Regexes" section -- the translation table,
literal vs dynamic regexes (compile time and warnings, the cache, the error
texts), regex FS and its empty-field rules, paragraph mode.
`src/components/BuiltinCommands/CLAUDE.md` and root `CLAUDE.md`: the awk
rows gain regexes, regex FS and `RS = ""`.

## Acceptance

- [ ] `TranslateAwkRegex` follows the table; nothing but it prepares awk
  regexes for `Regex::Compile`, always with `RegexSyntax::Extended`.
- [ ] Literal regexes are compiled once before BEGIN; their errors stop the
  program with status 1; dynamic ones are cached.
- [ ] Every output, warning and error above exactly.
- [ ] All unit tests green.

## Out of scope

- `match`, `sub`, `gsub`, `split` with a regex (awk--functions, on
  `MatchRegex` and `AwkRegexCache`).
- gawk's regex extensions (`\y`, `\<`, `IGNORECASE`, `RS` as a regex).
