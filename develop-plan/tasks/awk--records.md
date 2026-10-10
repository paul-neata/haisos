# Task awk--records: regexes in awk, regex field separators, paragraph mode

- Rock: awk
- Depends on: awk--interpreter, base--regex-match (`Regex`)
- Size: ~700 changed lines in ~6 files
- Plan checked against: develop @ c45c82e
- PR title: Add regexes to awk: patterns, ~ and !~, regex FS, RS = ""

## Goal

awk's regular expressions work as in gawk `--posix`: regex literals as
patterns (`/re/ { ... }`, `!/re/`, ranges `/a/,/b/`) and in expressions,
`~` and `!~` with a literal or a dynamic regex (any string value), regex
field separators (`-F '[0-9]+'`, `FS = ", *"`), and paragraph mode
(`RS = ""`: records separated by blank lines, newline a field separator
for a one-byte FS). POSIX ERE with intervals, through Haisos's `Regex`
component (`RegexSyntax::Extended`), after translating awk's own escapes.
Regex warnings and errors are gawk's.

## Context

**Clean room** (root `CLAUDE.md`, "Clean-room rule", above every other
rule): all code is written from scratch. Never read, copy, port, translate
or paraphrase another program's source (gawk, mawk, the one true awk,
busybox, ...), whatever its licence, and never name another program's
internal functions, variables, types or flags -- not in code, not in
comments. Behaviour is matched from documentation (POSIX awk, the gawk
manual, man pages) and from the observed output of real awks. Everything
below describes behaviour; the names are this project's own.

**Write in pieces**: never more than ~250 lines in one Write/Edit call;
build a file up with several Edits; commit after each file or step.

Read first: `src/components/BuiltinCommands/commands/awk/CLAUDE.md` (and
the files it lists, especially `AwkInterpreter.h/.cpp`, `AwkFields.h`,
`AwkInput.h/.cpp`, `AwkAst.h`, `AwkError.h`), `src/components/Regex/CLAUDE.md`
("Notes for callers": what awk must translate) and `Regex.h`.

What is on develop already:
- base--regex-syntax / base--regex-match: `Regex::Compile(pattern,
  RegexOptions{RegexSyntax::Extended}, error)` -> `shared_ptr<const Regex>`
  or null with glibc's message (`Unmatched ( or \(`, `Trailing backslash`,
  `Invalid content of \{\}`); `Search(text, start, match, flags = 0)`
  (leftmost-longest; `.` and `[^...]` match `\n`, `^`/`$` only at the
  text's ends -- what awk wants); `RegexMatch::groups[0]` the whole match
  as byte offsets. `Regex` is in namespace `Haisos` and already linked into
  `BuiltinCommands`.
- awk--lexer (#76): a `Regex` token keeps the regex text with `\/` already
  turned into `/` (inside a bracket expression too), every other backslash
  pair as written. `DecodeAwkStringEscapes(text, warnings)` decodes awk's
  string escapes.
- awk--parser (#77, #78): `ExprKind::Regex` (`Expr::text` the token's
  text), `ExprOp::Match`/`NoMatch` binaries, `Expr::parenthesized`,
  `Expr::position` (`SourcePosition{source, line}`).
- awk--values (#79): `RecordReader::Next(rs, record)` (RS's first byte; an
  empty `rs` currently falls back to `\n`), `SplitAwkFields(text, fs,
  fields)` (`" "` blanks, `""` one field, one byte literally; false for a
  longer fs), `FieldStore` with its `Splitter` and `SetRecord(record, fs,
  paragraphMode)` (the flag saved with the record and handed to the
  splitter), `kAwkMaxFields` (1000000: a cap on assignments only -- a
  split makes as many fields as the record has, as `SplitAwkFields` does),
  `AwkIntegerOf`, `IsAwkIdentifier`.
- awk--interpreter (#80): `Interpreter` (`AwkInterpreter.h`): `Prepare`
  (names to slots through `ResolveExpr`/`ResolveStmt`, the specials, then
  `-F`/`-v` through `ApplyPreAssignment`), `Run` (Prepare, BEGIN, main
  loop, END; `AwkFatal` -> `ReportFatal`, status 2), `ValueOf`,
  `RunStatement`, `MatchesPattern`, `NextMainRecord`, `SplitRecord` (the
  FieldStore's splitter), `GlobalVariable`, `ScalarRef`, `ArrayRef`,
  `SpecialString(kSlotFS/kSlotRS/kSlotCONVFMT)`, `m_position` (fatal
  errors report it, with `(FILENAME=<f> FNR=<n>) ` once FNR > 0). The
  placeholder fatals this task removes: `regular expressions are not
  implemented yet` (`ValueOf`'s `ExprKind::Regex` and `Match`/`NoMatch`),
  `regular expression field separators are not implemented yet`
  (`SplitRecord`), `RS = "" (paragraph mode) is not implemented yet`
  (`NextMainRecord`).
- `AwkError.h`: `FormatAwkWarning(AwkWarning{sourceName, line, message})`
  -> `awk: <src>:<line>: warning: <message>\n`; `FormatAwkError(sourceName,
  line, message)` -> `awk: <src>:<line>: error: <message>\n`.

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
// is compiled once. Bounded: at 256 entries it is cleared.
class AwkRegexCache {
public:
    // Null with |error| set (Regex's message) when it does not compile;
    // |warnings| filled only when the text is compiled (not on a hit).
    std::shared_ptr<const Regex> Get(const std::string& awkRegex,
                                     std::vector<std::string>& warnings, std::string& error);
};

// |text| cut at every match of |regex|: the fields are the text between
// matches; a match at the very start gives an empty first field, one at
// the end an empty last field; an empty match never separates; an empty
// text has no fields. (awk--functions' split() reuses it.)
void SplitByRegex(std::string_view text, const Regex& regex, std::vector<std::string>& fields);
```

`TranslateAwkRegex`, outside a bracket expression, for a backslash and the
byte after it:
- `\/` -> `/`; `\"` -> `"` with the warning (gawk warns for it and matches
  a quote);
- `\n \t \r \f \v \a \b` -> that control byte (`\b` is a backspace in awk);
- `\` and 1-3 octal digits -> that byte, emitted raw: a decoded
  metacharacter is a metacharacter (`/a\056b/` matches `axb`, `/a\052b/`
  matches `aab`), as gawk does -- the Regex CLAUDE.md note saying such a
  byte is escaped is wrong; correct it;
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
Regex literal's, set before BEGIN; the dump ignores it), with a forward
declaration `namespace Haisos { class Regex; }` above the awk namespace.

### `commands/awk/AwkInterpreter.h` / `.cpp`

New members: `AwkRegexCache m_regexCache;` and the set of regex warning
messages already written (`std::unordered_set<std::string>
m_regexWarningsGiven`). New methods, private like the rest:

- `void RegexWarnings(const std::vector<std::string>& messages, bool atRuntime)`
  -- gawk warns about an unknown regex escape once per run: the first
  time that escape is met, in a literal or a dynamic regex, never again
  (`/b\w/; /d\w/` warns once; a dynamic `"d\\w" NR` over two records
  warns once). A message already in the set is dropped; a new one goes
  through `RuntimeWarning` when `atRuntime`, else as a `FormatAwkWarning`
  line at `m_position`'s source and line (no FILENAME part).
- `void RuntimeWarning(const std::string& message)` -- the one place a
  run-time warning is written: `awk: <src>:<line>: [(FILENAME=<f> FNR=<n>)
  ]warning: <message>\n`, the prefix `ReportFatal` builds from
  `m_position` (factor that prefix into one helper both use).
  awk--functions reuses it.
- `bool CompileLiteralRegexes()` -- called by `Run` right after `Prepare`
  (so a bad `-v` name is still reported first, as gawk's) and before
  BEGIN. Walks the program in source order -- `Program::items` in order,
  a Function item's body (`functions[functionIndex].body`) in its place;
  within an item its pattern, range end, action; within a statement its
  parts as written (a `for`'s init, condition, update, body; a `do`'s body
  before its condition) -- and compiles every Regex node into its
  `compiledRegex` (`TranslateAwkRegex`, then `Regex::Compile` with
  `RegexSyntax::Extended`), `m_position` set to each Regex node's
  position. Warnings come out as met, through `RegexWarnings(..., false)`.
  The first regex that does not compile is reported --
  `FormatAwkError(<src>, <line>, "<Regex error>: /<text>/")`, e.g.
  `awk: cmd. line:1: error: Unmatched ( or \(: /a(/` -- and the walk
  stops there (gawk reports only the first: `/a(/; /b(/` gives one line;
  warnings of regexes after it are never written); `Run` then returns 1
  with nothing run. `<text>` is the regex as written: the token's text
  with every `/` outside a bracket expression shown `\/` (it can only
  have been written so: `/a\/(/` is reported `/a\/(/`).
- `bool MatchRegex(const Expr& regexOperand, const std::string& text)` --
  the one place both forms go through (awk--functions' `match`, `sub`,
  `gsub`, `split` use it or its dynamic half): a Regex node that is not
  `parenthesized` uses its `compiledRegex`; anything else -- a
  parenthesized regex included, which is first `$0 ~ /re/`'s 0 or 1 (gawk:
  on input `a`, `$0 ~ (/a/)` is 0 and `"1" ~ (/a/)` is 1) -- is a dynamic
  regex: its value's `ToString(CONVFMT)` through `m_regexCache`, warnings
  through `RegexWarnings(..., true)`, one that does not compile
  `AwkFatal("invalid regexp: <error>: /<text>/")` with the string as is.
  A match is `Search(text, 0, m)` succeeding anywhere.

`ValueOf`:
- `ExprKind::Regex` alone is `$0` (`m_fields.Record(CONVFMT)`) matched
  against its `compiledRegex`: 1 or 0 (in BEGIN `$0` is empty).
- `Match`/`NoMatch`: the left's `ToString(CONVFMT)` through
  `MatchRegex(*operands[1], ...)`, 1 or 0, inverted for `!~`. The left is
  evaluated first; the right (when dynamic) inside `MatchRegex`.

`SplitRecord(record, fs, paragraphMode, fields)`:
- FS `" "`: `SplitAwkFields` (its blanks already include the newline),
  paragraph mode or not.
- FS one byte or `""`: outside paragraph mode `SplitAwkFields`; in
  paragraph mode the record is first cut at every `\n` and each piece
  split by `SplitAwkFields`, the fields appended in order (so `RS = "";
  FS = "x"` on `p1 l1\np1 l2` gives 2 fields, and `FS = ""` makes each
  line one field).
- FS of two or more bytes: a regex (dynamic, through `m_regexCache`, its
  warnings through `RegexWarnings(..., true)`, a bad one
  `AwkFatal("invalid regexp: <error>: /<fs>/")`), the record split by
  `SplitByRegex` -- in paragraph mode too, where a newline then separates
  only if the regex matches it (gawk: `RS = ""; FS = ":+"` on `a:b\nc`
  gives the 2 fields `a` and `b\nc`).

`NextMainRecord`: drop the paragraph-mode fatal; pass RS as it is (`""`
included) to the reader, and `paragraphMode = RS.empty()` to `SetRecord`.

### `commands/awk/AwkInput.h` / `.cpp`

`RecordReader::Next` with `rs == ""` (paragraph mode): newlines at the
start are skipped; a record ends at a run of two or more newlines (the run
is the separator, however long; only empty lines count -- a line of blanks
is part of the record); at the end of the input the trailing newlines are
not part of the record and no empty record follows. A run may straddle two
reads: the search must look back over a newline at the end of what was
already searched, not resume past it. The stop and erase rules of the
one-byte separator stay. Update the header comment (no more fallback to
`\n`).

### `Awk.cpp`

`Version` 1.2.0. `Help().notes`: "Regular expressions, functions, ..."
becomes "Functions, printf, getline and output redirections are not
available yet."; add that regexes are POSIX EREs as gawk `--posix` takes
them (no `\y \w \s \< \>`).

### Follow-ups from #80's review (lows)

- `ApplyPreAssignment` (AwkInterpreter.cpp, the `-v` branch): split the
  raw text at its first `=`, check the raw name with `IsAwkIdentifier`,
  then decode only the value (as the `var=value` operands already do) --
  gawk refuses `-v 'a\x41=1'` with ``awk: fatal: `a\x41' is not a legal
  variable name`` (status 2) where the decoded name (`ax41`: `\x` is a
  plain x under `--posix`) is taken now.
  `-F` still decodes its whole text.
- `ValueOf`, `ExprKind::In`: type the array through `ArrayRef(expr.slot,
  expr.text)` -- `k in x` makes an untyped `x` an array, as gawk's, so a
  later `x = 1` is ``attempt to use array `x' in a scalar context``; a
  scalar `x` keeps ArrayRef's ``attempt to use scalar `x' as an array``.
- `AwkInterpreterTest.cpp`, `StopsPromptly`: stop only once the program is
  running. The input case: feed the pipe 3000 lines `y\n` (6000 bytes), so
  `{ print }` flushes its 4096-byte stdout buffer to `/out`; poll `/out`
  (`ReadWholeFile`, up to `kWaitMs`) until it is not empty, then
  `TriggerStop` while awk waits on the pipe. The loop case:
  `BEGIN { for (i = 0; i < 3000; i++) print "y"; while (1) x++ }`, the
  same poll, then the stop. Both still expect `WaitToFinish(1000)` and 143.

### Build

`src/components/BuiltinCommands/CMakeLists.txt`: add `commands/awk/AwkRegex.cpp`.
`tests/unit/components/Awk.unittests/CMakeLists.txt`: add `AwkRegexTest.cpp`.

## Tests

`tests/unit/components/Awk.unittests/AwkRegexTest.cpp` (new):
- `AwkRegexTest.Translate` (plain `TEST`): `a\/b` -> `a/b`; `a\.b` ->
  `a\.b`; `a\tb` -> `a` TAB `b`; `\056` -> `.`; `a\wb` -> `awb` + one
  warning ``regexp escape sequence `\w' is not a known regexp operator``;
  `a\"b` -> `a"b` + a warning; `[a\]b]` -> `[]ab]`; `[\\]` -> `[\]`;
  `[a\-z]` -> `[az-]`; `[[:alpha:]\/]` -> `[[:alpha:]/]`; `a{2}b` unchanged;
  `a\` -> `a\`.
- `AwkRegexTest.SplitByRegex`: `[0-9]+` on `a1b22c` -> `a b c`; `[ ]` on
  `a  b` -> `a`, ``, `b`; ` +` on ` a b ` -> ``, `a`, `b`, ``; `x*` on `abc`
  -> `abc`; `x*` on `axxbc` -> `a`, `bc`; anything on `` -> no fields.

`AwkValueTest.cpp` (the `ReaderHarness`):
- `RecordReaderParagraphMode`: `\n\na\nb\n\n\nc\n\n` -> `a\nb`, `c`, End;
  `a\n \nb\n\nc` -> `a\n \nb`, `c`, End; 65535 `x` + `\n\ny\n` (the run
  straddles the first 64 KiB read) -> the 65535 `x`, `y`, End; 65535 `x` +
  `\nc\n` -> the 65535 `x` + `\nc`, End.

In `AwkInterpreterTest.cpp` (`AwkRunTest`, the fixture's `/abc.txt`),
outputs exactly:
- `RegexPatterns`: input `abc\n`,
  `'/b/ { print "lit" } !/z/ { print "neg" } $0 ~ /c$/ { print "end" } { x = /a/; print x + 1 }'`
  -> `lit\nneg\nend\n2\n`; `'/b/,/e/ { print "r", $1 } $1 == "d", 0' /abc.txt`
  -> `r a\nr d\nd e f\n`; input `a/b\n`, `'/\// { print "slash" }'` -> `slash\n`;
  input `a/b\n`, `'/a[/]b/'` -> `a/b\n`; input `abc\n`,
  `'BEGIN { print /a/ } { print /a/ }'` -> `0\n1\n`; input `a\n`,
  `'{ print ($0 ~ (/a/)), ("1" ~ (/a/)) }'` -> `0 1\n`.
- `RegexEscapes`: input `axb\n`, `'/a\056b/ { print "dot" }'` -> `dot\n`;
  input `awb\n`, `'/a\wb/ { print "w" }'` -> out `w\n`, err
  ``awk: cmd. line:1: warning: regexp escape sequence `\w' is not a known regexp operator\n``;
  input `a]b\n`, `'/a[\]]b/ { print "b" }'` -> `b\n`; input `aab\n`,
  `'/a{2}b/ { print "i" }'` -> `i\n`; input `a{2}b\n` with the same -> nothing;
  input `a\tb\n` (a real tab), `'/a\tb/ { print "t" }'` -> `t\n`;
  `'/b\w/; /d\w/'` (no input) -> one warning line only.
- `DynamicRegexes`: input `awb\n`, `'{ if ($0 ~ "a\\wb") print "m" }'` -> out
  `m\n`, err ``awk: cmd. line:1: (FILENAME=- FNR=1) warning: regexp escape sequence `\w' is not a known regexp operator\n``;
  input `ab\nab\n`, `'BEGIN { r = "^a" } $0 ~ r { n++ } END { print n }'` -> `2\n`;
  input `awb\nawb\n`, `'{ if ($0 ~ "a\\wb") n++ } END { print n }'` -> out `2\n`
  and exactly one warning line; input `x\ny\n`,
  `'/b\w/ { } { r = "d\\w" NR; if ($0 ~ r) n++ } END { print n + 0 }'` ->
  out `0\n`, err only the literal's
  ``awk: cmd. line:1: warning: regexp escape sequence `\w' is not a known regexp operator\n``;
  input `x\n`, `'{ print ("a+" ~ "a+"), ("aa" ~ "a+"), ("b" !~ "a"), ("+" ~ "a+") }'` -> `1 1 1 0\n`.
- `RegexErrors`: input `ab\n`, `'$0 ~ "a("'` -> err
  `awk: cmd. line:1: (FILENAME=- FNR=1) fatal: invalid regexp: Unmatched ( or \(: /a(/\n`, 2;
  `'/a(/'` -> err `awk: cmd. line:1: error: Unmatched ( or \(: /a(/\n`, 1, nothing read;
  `'/a(/; /b(/'` -> that one line only, 1; `'/a\/(/'` -> err
  `awk: cmd. line:1: error: Unmatched ( or \(: /a\/(/\n`, 1;
  `'BEGIN { print "x" } /a(/'` -> no output, the error, 1; input `a\n`,
  `'{ r = "a\\"; print ($0 ~ r) }'` -> err
  `awk: cmd. line:1: (FILENAME=- FNR=1) fatal: invalid regexp: Trailing backslash: /a\/\n`, 2.
- `RegexFieldSeparators`: input `a1b22c\n`, `-F '[0-9]+' '{ print NF, $3 }'` -> `3 c\n`;
  input `a  b\n`, `-F '[ ]' '{ print NF }'` -> `3\n`; input `x, y,z\n`,
  `'BEGIN { FS = ", *" } { print $2 "|" $3 }'` -> `y|z\n`; input `:a\n`,
  `-F ':+' '{ print NF }'` -> `2\n`; input ` a b \n`,
  `-F ' +' '{ print NF, "[" $1 "]" }'` -> `4 []\n`; input `abc\n`,
  `-F 'x*' '{ print NF }'` -> `1\n`.
- `ParagraphMode`: `/para.txt` written as `p1 l1\np1 l2\n\n\n\np2 l1\n\n`:
  `'BEGIN { RS = "" } { print NR ": " $1 "|" $NF "|" NF }' /para.txt` ->
  `1: p1|l2|4\n2: p2|l1|2\n`; `'BEGIN { RS = ""; FS = "x" } { print NF; print $2 }' /para.txt`
  -> `2\np1 l2\n1\n\n`; `'BEGIN { RS = ""; FS = "" } { print NF ": " $1 }' /para.txt`
  -> `2: p1 l1\n1: p2 l1\n`; input `a:b\nc\n\nd\n`,
  `'BEGIN { RS = ""; FS = ":+" } { print NF "|" $2 }'` -> `2|b\nc\n1|\n`;
  input `\n\na\nb\n\n\nc` with `'BEGIN { RS = "" } { print NR ": " $0 }'`
  -> `1: a\nb\n2: c\n`; input `a\n \nb\n\nc\n`, the same program ->
  `1: a\n \nb\n2: c\n`.
- The #80 lows: in `DashV`, `-v 'a\x41=1' 'BEGIN { }'` -> err
  ``awk: fatal: `a\x41' is not a legal variable name\n``, 2; in
  `RuntimeErrors`, `'BEGIN { if (1 in x) ; x = 1 }'` -> err
  ``awk: cmd. line:1: fatal: attempt to use array `x' in a scalar context\n``, 2;
  `StopsPromptly` as above.
- Remove `AwkRunTest.NotYetAvailable`'s regex case.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U
```

## Docs

`commands/awk/CLAUDE.md`: list `AwkRegex.h/.cpp`; a "Regexes" section --
the translation table, literal vs dynamic regexes (compiled before BEGIN,
only the first bad literal reported, status 1; dynamic ones cached, a bad
one a fatal), the once-per-run escape warnings, regex FS and its
empty-field rules, paragraph mode (the reader's rule, which FS make the
newline a separator); update "Values, fields and records" and "Running"
(no more regex/paragraph placeholders, `RuntimeWarning`), the version
1.2.0, and the documented exceptions: a literal regex's warnings come after
the program's string-escape warnings (gawk interleaves them in source
order); inside a bracket expression a `\/` is reported as `/`; gawk cuts
the regex text of its error message after a decoded escape (`/a\t(/` shown
`/a\t/`), Haisos shows it whole.
`src/components/BuiltinCommands/CLAUDE.md` and root `CLAUDE.md`: the awk
rows gain regexes, regex FS and `RS = ""` (out of the not-implemented
list), version 1.2.0, and the new exceptions.
`src/components/Regex/CLAUDE.md` ("Notes for callers", awk): octal
escapes are emitted raw (a decoded metacharacter stays one).

## Acceptance

- [ ] `TranslateAwkRegex` follows the table; nothing but it prepares awk
  regexes for `Regex::Compile`, always with `RegexSyntax::Extended`.
- [ ] Literal regexes are compiled once before BEGIN; the first bad one
  stops the program with status 1; dynamic ones are cached; each unknown
  escape is warned about once per run.
- [ ] `RS = ""` reads paragraphs; the newline separates fields for FS
  `" "`, one byte and `""`, and only by the regex for a longer FS.
- [ ] #80's three lows fixed, with their tests.
- [ ] Every output, warning and error above exactly.
- [ ] All unit tests green.

## Out of scope

- `match`, `sub`, `gsub`, `split` with a regex (awk--functions, on
  `MatchRegex`, `AwkRegexCache`, `SplitByRegex` and `RuntimeWarning`).
- gawk's regex extensions (`\y`, `\<`, `IGNORECASE`, `RS` as a regex).
