# Task hsh--lexer: The hsh tokenizer -- words with their quoting, operators, heredocs

- Rock: hsh
- Depends on: builtins--directories
- Size: ~1030 changed lines in ~9 files (at the limit: the nested-substitution scanner needs the whole tokenizer, so it is not split)
- Plan checked against: develop @ 8fb8324
- PR title: hsh: lexer for the shell language, with heredocs and nesting

## Goal

The first piece of `hsh`, the Haisos shell (a reimplementation of dash): a
lexer that turns shell source text into tokens, exactly as dash would split
it. Nothing runs yet and `hsh` is not a builtin yet (hsh--executor registers
it); this task is a library with unit tests.

Afterwards:

- `Haisos::Hsh::Lexer` reads a whole string (a `-c` command, a script, the
  text of a `$(...)`) token by token: words, operators, IO numbers, newlines,
  end of input.
- A word keeps its quoting as a tree of parts (unquoted text, quoted text,
  double-quoted groups, `$name` / `${...}`, `$(...)`, `` `...` ``, `$((...))`),
  so the later expansion knows what was quoted.
- The end of a `$(...)`, `${...}`, `$((...))` and `` `...` `` is found
  correctly however deeply they nest, including a `case` with `pat)` inside
  `$(...)`.
- Heredoc bodies are read after the newline that ends their line.
- An interactive shell can tell "this input is incomplete, read another line
  (PS2)" from a real error: every error says whether it happened because the
  input ended (`ShellError::Incomplete()`).
- Errors read as dash's, with line numbers: `Syntax error: Unterminated quoted string`.

## Context

Read first: the root `CLAUDE.md` (Builtin Commands; Automatic Development
Rules), `src/components/BuiltinCommands/CLAUDE.md`, `develop-plan/goal.md`
(the hsh clarifications), and the dash manual's "Lexical Structure", "Quoting",
"Reserved Words", "Here Documents" and "Word Expansions" sections
(https://man7.org/linux/man-pages/man1/dash.1.html). dash is installed on most
Linux machines (`/bin/sh` on Debian/Ubuntu): every behaviour below was checked
against dash 0.5.12, and you may check more with `dash -c '...'`.

What earlier tasks provide:

- builtins--directories: every builtin lives in
  `src/components/BuiltinCommands/commands/<name>/`; the `BuiltinCommands`
  static library's include root is still `src/components/BuiltinCommands`
  (and the repository root is on every target's include path, so
  `#include "src/components/BuiltinCommands/commands/hsh/HshLexer.h"` works
  from anywhere). There is no `commands/hsh/` directory yet: create it.

Nothing else is needed: this task uses only the C++17 standard library.

Later tasks build on what this one defines -- hsh--parser (the AST), hsh--expansion
(expands `Word`s), hsh--executor (runs commands, `$(...)` through the parser),
hsh--interactive (PS2 through `ShellError::Incomplete()`). The types and
functions below are their contract: name and shape them exactly as written.

## Changes

All new code is in namespace `Haisos::Hsh`, in
`src/components/BuiltinCommands/commands/hsh/`. Plain portable C++17 (it is
built for Linux, Windows/MSVC and WASM): no POSIX headers, no `<regex>`.

### `commands/hsh/HshError.h` (new, header-only)

```cpp
// Every error hsh reports: a syntax error, an expansion error, a runtime error.
// |message| is what dash prints after "<shell name>: <line>: ", e.g.
// "Syntax error: Unterminated quoted string" or "x: parameter not set".
class ShellError : public std::runtime_error {
public:
    explicit ShellError(const std::string& message, int line = 0, bool incomplete = false);
    // The line it happened on; 0 when the thrower does not know (expansion
    // errors: the executor adds the line of the command being run).
    int Line() const;
    // True when it happened only because the input ended where more was needed
    // (an unterminated quote, an unclosed $( or `if`, a trailing `&&` ...): an
    // interactive shell then shows PS2, reads another line and tries again.
    bool Incomplete() const;
};

// dash's shape for every diagnostic: "<shellName>: <line>: <message>\n",
// e.g. "hsh: 1: Syntax error: \"fi\" unexpected\n".
std::string FormatShellError(const std::string& shellName, int line, const std::string& message);
```

### `commands/hsh/HshWord.h` and `HshWord.cpp` (new)

```cpp
enum class WordPartKind {
    Literal,             // unquoted text, as written
    Quoted,              // text that was quoted: '...', \c outside quotes, the plain text inside "..." or a heredoc body
    DoubleQuoted,        // "...": its contents are |parts|
    Parameter,           // $name, ${...}
    CommandSubstitution, // $(...) or `...`
    Arithmetic,          // $((...)): the expression is |parts|
};

enum class ParameterOp {
    None,                 // $name, ${name}
    Length,               // ${#name}
    UseDefault,           // ${name:-word}
    UseDefaultIfUnset,    // ${name-word}
    AssignDefault,        // ${name:=word}
    AssignDefaultIfUnset, // ${name=word}
    ErrorIfNull,          // ${name:?word}
    ErrorIfUnset,         // ${name?word}
    UseAlternative,       // ${name:+word}
    UseAlternativeIfSet,  // ${name+word}
    RemoveSmallestSuffix, // ${name%word}
    RemoveLargestSuffix,  // ${name%%word}
    RemoveSmallestPrefix, // ${name#word}
    RemoveLargestPrefix,  // ${name##word}
    Bad,                  // a ${...} that makes no sense: expanding it fails with "Bad substitution"
};

struct WordPart {
    WordPartKind kind = WordPartKind::Literal;
    // Literal, Quoted: the characters. Parameter: the name -- an identifier,
    // digits (a positional parameter, "10" for ${10}), or one of @ * # ? - $ ! 0
    // (empty for Bad). CommandSubstitution: the source of the command inside,
    // exactly as written between "$(" and ")" (heredoc bodies included); for
    // backquotes the text after backquote processing (see below).
    std::string text;
    // DoubleQuoted: the contents. Parameter: the operand word (the "word" of
    // ${name:-word}), empty when none. Arithmetic: the expression.
    std::vector<WordPart> parts;
    ParameterOp op = ParameterOp::None;  // Parameter only
    bool backquoted = false;             // CommandSubstitution: `...` rather than $(...)
    int line = 0;                        // CommandSubstitution: the line its text starts on
};

struct Word {
    std::vector<WordPart> parts;
    std::string source;  // the word exactly as written, quotes included (for messages and debugging)
    int line = 0;        // the line it starts on
};

// What a heredoc redirection (<<, <<-) reads. Created by the lexer when it
// reads the delimiter word, filled at the next newline (see "Heredocs").
struct HereDocument {
    std::string delimiter;   // the delimiter word with its quotes removed
    bool quoted = false;     // some of the delimiter was quoted: the body is taken as it is, unexpanded
    bool stripTabs = false;  // <<-: leading tabs were removed from every body line and from the delimiter line
    std::string rawBody;     // the body lines, each with its '\n', the delimiter line excluded
    Word body;               // !quoted: rawBody lexed for $-expansions and `...` (Quoted text + expansion parts); quoted: one Quoted part holding rawBody (no part when rawBody is empty)
    bool complete = false;   // the body has been read (or the input ended first)
    bool terminated = false; // the delimiter line was found
    // Byte offsets in the lexer's source of the body region: from the first
    // body line through the delimiter line and its '\n' (end exclusive; up to
    // the end of input when not terminated). The parser copies this region to
    // rebuild a command's text (ListItem::sourceText).
    size_t sourceBegin = 0;
    size_t sourceEnd = 0;
};

// A name a shell variable or function may have: [A-Za-z_][A-Za-z0-9_]*.
bool IsValidShellName(std::string_view name);
// The word's text when it is made of unquoted literal text only (no quotes,
// no expansions), else nullopt -- what reserved words, for-loop variables,
// function names and assignment prefixes are recognized by.
std::optional<std::string> LiteralText(const Word& word);
// For tests and debugging; the format is in "Word and token descriptions".
std::string DescribeWord(const Word& word);
```

`std::vector<WordPart>` inside `WordPart` is legal C++17 (vector of an
incomplete type as a member).

### `commands/hsh/HshLexer.h` and `HshLexer.cpp` (new)

```cpp
enum class TokenKind {
    Word, IoNumber, Newline, EndOfInput,
    Semicolon,       // ;
    DoubleSemicolon, // ;;
    Ampersand,       // &
    AndIf,           // &&
    Pipe,            // |
    OrIf,            // ||
    LeftParen,       // (
    RightParen,      // )
    Less,            // <
    Great,           // >
    DoubleGreat,     // >>
    Clobber,         // >|
    LessGreat,       // <>
    LessAnd,         // <&
    GreatAnd,        // >&
    DoubleLess,      // <<
    DoubleLessDash,  // <<-
    TripleLess,      // <<<  (bash's here-string)
    AndGreat,        // &>   (bash's: stdout and stderr to a file)
};
// The operator's text (";", "&&", "<<-" ...); "" for Word, IoNumber, Newline, EndOfInput.
const char* OperatorText(TokenKind kind);
// Whether it is one of the redirection operators (Less ... AndGreat).
bool IsRedirectionOperator(TokenKind kind);

enum class ReservedWord { If, Then, Else, Elif, Fi, Do, Done, Case, Esac, While, Until, For, In, LeftBrace, RightBrace, Bang };
const char* ReservedWordText(ReservedWord word);  // "if", ..., "{", "}", "!"

struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    int line = 0;                           // the line the token starts on
    Word word;                              // Word
    int ioNumber = -1;                      // IoNumber: 0-9
    std::shared_ptr<HereDocument> hereDoc;  // the Word right after << or <<-: its heredoc, body filled at the next newline
    // Byte offsets in the Lexer's source (the string given to its constructor):
    // the token's first character and one past its last, as written (quotes,
    // escapes, line continuations inside it included; for a Newline the '\n'
    // itself; for EndOfInput both are the source's size). The parser cuts
    // ListItem::sourceText and FunctionDefinition::sourceText out of the
    // source with them.
    size_t begin = 0;
    size_t end = 0;
};

// The reserved word the token spells when it is a Word of exactly that
// unquoted text ("if", "{", "!" ...). Whether it IS a reserved word depends on
// where it stands, which only the parser knows (`echo if` is a plain word).
std::optional<ReservedWord> AsReservedWord(const Token& token);
// For tests and debugging; the format is in "Word and token descriptions".
std::string DescribeToken(const Token& token);

struct LexerOptions {
    int firstLine = 1;        // the line number of the first line of the source
    // An interactive shell's input: two endings a script accepts become
    // incomplete-input errors, so the shell reads more -- a heredoc whose
    // delimiter line has not come yet, and a source ending in a backslash-newline.
    bool interactive = false;
};

class Lexer {
public:
    explicit Lexer(std::string source, LexerOptions options = {});
    // The next token; EndOfInput forever once the source is used up. Throws
    // ShellError on a lexical error.
    Token Next();
    // The line the lexer is at: where the next token would start.
    int Line() const;
};
```

Private members are the implementer's choice. The source must be shared with
the sub-lexers used to scan `$(...)` (e.g. a `std::shared_ptr<const std::string>`
plus a position), so a private constructor taking (shared source, start
position, start line, options) is expected.

#### Reading characters

- Lines are counted from `firstLine`; every `'\n'` consumed (including one in a
  quote, a heredoc body or a line continuation) moves to the next line.
- **Line continuation**: a backslash immediately followed by a newline is
  removed, both characters, everywhere -- inside words, between tokens, inside
  operators (`&\<newline>&` is `&&`), inside double quotes, `$(...)`, `${...}`,
  `$((...))` and unquoted-delimiter heredoc bodies -- except inside single
  quotes, inside comments and in quoted-delimiter heredoc bodies, where both are
  kept as written. The simplest way: one "peek/get" layer that skips
  backslash-newline pairs, plus raw access for the three exceptions.
- When `options.interactive` is set and the source ends with a line
  continuation (the last two characters are a backslash and a newline that the
  layer above skipped), reaching the end of input throws
  `ShellError("Syntax error: end of file unexpected", line, true)`.

#### Tokens

- Blanks (space, tab) separate tokens and are skipped.
- `#` where a token would start begins a comment that runs to the end of the
  line; the newline itself is still a Newline token. `a#b` is one word; `\#a`
  is the word `#a`.
- Operators, longest match first: `&&` `&>` `&` `||` `|` `;;` `;` `(` `)`
  `<<<` `<<-` `<<` `<&` `<>` `<` `>>` `>&` `>|` `>`. A `'\n'` is a Newline
  token. `&>>` is not supported (it lexes as `&>` then `>`). `;&`, `;;&` are
  not operators (they lex as `;` `&` / `;;` `&`).
- **IoNumber**: a single digit that starts a token and is immediately followed
  by `<` or `>` (`2>x`, `0<&3`). As dash, only one digit: `12>x` is the word
  `12` then `>`; `a1>x` is the word `a1`.
- Everything else is a **Word**, which runs until an unquoted blank, newline or
  operator character (`; & | ( ) < >`). `{`, `}`, `!` are ordinary word
  characters: they are reserved words, not operators (`{echo` is one word, as
  in dash).
- Reserved words are not decided here: every word is a Word token; the parser
  uses `AsReservedWord` where reserved words are recognized.

#### Word parts

Adjacent `Literal` parts are always merged into one, and so are adjacent
`Quoted` parts (at every level: in a word, inside `DoubleQuoted`, in an
operand, in an arithmetic expression). The rules per context:

| Context | plain char | `'...'` | `"..."` | `\c` | `$`, `` ` `` | ends at |
|---|---|---|---|---|---|---|
| word (unquoted) | Literal | Quoted (contents, raw: no continuation removal) | DoubleQuoted | Quoted `c` (any c); a lone `\` at end of input is Quoted `\` | expansions | blank, newline, operator char |
| inside `"..."` | Quoted | the `'` is a Quoted char | -- (ends it) | `\$ \` \" \\` give Quoted `$ \` " \`; any other `\c` gives Quoted `\c` (both kept) | expansions | the closing `"` |
| operand of an unquoted `${...}` | Literal | Quoted | DoubleQuoted | Quoted `c` | expansions | the closing unquoted `}` |
| operand of a `${...}` inside `"..."` | Quoted | the `'` is a Quoted char | a nested DoubleQuoted | as inside `"..."`, plus `\}` gives Quoted `}` | expansions | the closing unquoted `}` |
| inside `$((...))` | Literal (quotes too: `'` and `"` are plain characters, as dash) | -- | -- | as inside `"..."` | expansions | see "Arithmetic" |
| unquoted-delimiter heredoc body | Quoted (`"` and `'` included) | -- | -- | `\$ \` \\` give Quoted `$ \` \`; any other `\c` both kept | expansions | the end of the body |

`$` expansions, in every context above:

- `$` followed by a letter or `_`: `Parameter`, the longest name
  `[A-Za-z0-9_]*` (`$ab_1c`).
- `$` followed by a digit: `Parameter` of that one digit (`$10` is `$1` then
  Literal `0`, as dash).
- `$` followed by one of `@ * # ? - $ ! 0`: `Parameter` of that character.
- `${`: a braced parameter, see below.
- `$((`: `Arithmetic` (always, as dash: `$((echo a) )` is an arithmetic error,
  `Missing '))'`).
- `$(` (not `$((`): `CommandSubstitution`.
- `$` followed by anything else, or at the end of input: a plain `$` character
  (Literal or Quoted per the context). `$'...'` and `$"..."` are a plain `$`
  followed by the quote (dash has no ANSI-C quoting).

A backquote starts a backquoted `CommandSubstitution` in every context but
single quotes.

#### `${...}`

After `${`:

1. `#` followed directly by `}`: the parameter `#`, op None (`${#}`).
2. `#` followed by a name (below): op `Length`; the next character must be
   `}`, otherwise the whole `${...}` is `Bad` (`${#x:-a}`).
3. A name: `[A-Za-z_][A-Za-z0-9_]*`, or one or more digits, or one of
   `@ * # ? - $ ! 0`. No name at all (`${}`, `${;}`) makes it `Bad`.
4. After the name: `}` ends it (op None); `:-` `:=` `:?` `:+` and `-` `=` `?`
   `+` select those ops; `%%` `%` `##` `#` (longest first) select the pattern
   ops; anything else (`${x!}`, `${x:2}`, `${x;}`) makes it `Bad`.
5. The operand: everything up to the matching `}`, lexed as an operand (see the
   table; nested `${...}`, `$(...)`, quotes are lexed recursively, so a `}`
   inside them does not end it). An empty operand is no part at all.
6. A `Bad` parameter is skipped to its matching `}` by lexing the rest as an
   operand and discarding it; it is not a syntax error -- expanding it fails
   later with `Bad substitution` (dash does the same). Note: dash says
   `Syntax error: Missing '}'` for `${x:}`; hsh says `Bad substitution` (a
   documented deviation).
7. End of input before the `}`: `ShellError("Syntax error: Missing '}'", line, true)`.

#### `$(...)` -- finding the end

A sub-lexer (a `Lexer` over the same source, starting right after `$(`, at the
current line, same options) reads tokens -- so quotes, comments, nested
substitutions and heredocs inside are handled by the ordinary rules -- until
the `)` that closes the substitution. Counting parentheses alone is not
enough: `$(case a in a) echo m;; esac)` has a `)` that ends a case pattern.
The scan keeps a stack of open constructs and a flag `commandPosition` (true at
the start):

- A Word in command position whose `LiteralText` is `case` pushes a *case*
  frame in state `Subject`. A case frame moves `Subject` -> (any word) ->
  `ExpectIn` -> (the word `in`; newlines are skipped) -> `Pattern`; in
  `Pattern`, an optional leading `(` is ignored, words and `|` are patterns,
  `)` moves to `Body` with `commandPosition` true, and the word `esac` pops the
  frame; in `Body`, `;;` moves back to `Pattern`, and the word `esac` in
  command position pops the frame.
- A `(` operator (outside a case `Pattern`) pushes a *paren* frame; a `)`
  pops a paren frame if one is on top; a `)` with an empty stack is the end.
- `commandPosition` becomes true after a Newline, `;`, `&`, `&&`, `||`, `|`,
  `(`, `;;`, and after a word in command position whose `LiteralText` is one of
  `if then else elif while until do ! { }`; false after any other word. A
  redirection operator leaves it unchanged and the word after it (its target)
  does not change it either.
- End of input before the end: `ShellError("Syntax error: end of file unexpected (expecting \")\")", line, true)`.
- Heredocs started inside are read by the sub-lexer at its own newlines; any
  still waiting when the closing `)` is found get an empty body (complete,
  not terminated), as dash (`$(cat <<E)` reads nothing).

The part's `text` is the source between `$(` and the closing `)` exactly; its
`line` is the line of the `$(`. Afterwards the outer lexer continues after the
`)` at the sub-lexer's line. Nothing is parsed here: hsh--parser checks the
text's syntax, the executor runs it.

#### Backquotes

From the opening backquote, characters are read up to the first unescaped
backquote. A backslash followed by `$`, a backquote or `\` is removed (the
character after it kept); inside double quotes, `\"` likewise becomes `"`;
every other backslash is kept. Backslash-newline is a line continuation as
everywhere. The result is the part's `text` (so `` `echo \`echo hi\`` `` has
the text ``echo `echo hi` ``). End of input first:
`ShellError("Syntax error: EOF in backquote substitution", line, true)`.

#### Arithmetic

After `$((`, the expression is lexed as in the table, with a parenthesis depth
starting at 0: `(` adds one, `)` at depth > 0 takes one away, and `)` at depth
0 must be followed directly by a second `)`, which ends the expression.
Otherwise -- a lone `)` at depth 0, or end of input -- it is
`ShellError("Syntax error: Missing '))'", line, incomplete)` with `incomplete`
true only when the end of input was reached. Newlines inside are allowed.

#### Heredocs

- After returning a `<<` or `<<-` token, the lexer remembers that the next
  token is a heredoc delimiter. If that token is a Word, it creates a
  `HereDocument`, attaches it to the token (`token.hereDoc`) and appends it to
  its list of pending heredocs. (If it is not a Word, the parser reports the
  error.)
- The delimiter: the word's `source` with quote removal done literally --
  `'...'` gives its contents, `"..."` its contents with `\$ \` \" \\` unescaped,
  `\c` gives `c`, everything else (a `$` included) as is. `quoted` is true when
  the source has any `'`, `"` or `\`. So `<<E"F"` has delimiter `EF`, quoted;
  `<<\E` has `E`, quoted; `<<$x` has `$x`, unquoted.
- When the lexer consumes a `'\n'` that makes a Newline token, before returning
  that token it reads the bodies of all pending heredocs, in order, from the
  character after the newline: line by line (each line up to and including its
  `'\n'`, the last one possibly without), with no continuation removal; for
  `<<-` every leading tab of the line is removed first; a line equal to the
  delimiter (without its `'\n'`) ends the body and is consumed but not part of
  it. The body lines go to `rawBody`.
- Then, unless `quoted`, `rawBody` is lexed in the heredoc-body context (a
  sub-lexer over that text, starting at the body's first line, so errors and
  `$(...)` lines are right) into `body`; quoted: `body` is one Quoted part
  with `rawBody`. `complete` is set, and `terminated` when the delimiter line
  was found.
- End of input with heredocs pending, or before a body's delimiter line: the
  body is what was read (complete, not terminated) -- dash accepts it in a
  script. With `options.interactive`, instead throw
  `ShellError("Syntax error: end of file unexpected", line, true)` so the
  shell reads more lines.
- Several heredocs on one line are read one after the other
  (`cat <<A; cat <<B` then the body of A, `A`, the body of B, `B`).

#### Errors

Every lexer error is a `ShellError` thrown from `Next()`, with the line where
it was detected (for an unterminated quote, the line at the end of input: dash
says `2:` for `echo "a<newline>b`). Exact messages:

| Message | When | Incomplete |
|---|---|---|
| `Syntax error: Unterminated quoted string` | `'` or `"` never closed | yes |
| `Syntax error: Missing '}'` | `${` never closed | yes |
| `Syntax error: Missing '))'` | see "Arithmetic" | only at end of input |
| `Syntax error: EOF in backquote substitution` | backquote never closed | yes |
| `Syntax error: end of file unexpected (expecting ")")` | `$(` never closed | yes |
| `Syntax error: end of file unexpected` | interactive only: pending heredoc or trailing continuation at end of input | yes |

#### Word and token descriptions (byte for byte; the tests rely on them)

`DescribeWord`: the parts, each described as below, joined by one space:

- Literal: `L'<text>'`; Quoted: `Q'<text>'` (text as is, no escaping)
- DoubleQuoted: `D[<parts joined by one space>]` (`D[]` when empty)
- Parameter, op None: `P(<name>)`; Length: `P(#<name>)`; Bad: `P(<bad>)`;
  any other op: `P(<name><op>[<operand parts joined by one space>])`, where
  `<op>` is `:-` `-` `:=` `=` `:?` `?` `:+` `+` `%` `%%` `#` `##`
- CommandSubstitution: `C'<text>'`, backquoted: `B'<text>'`
- Arithmetic: `A[<parts joined by one space>]`

Examples: `a"b$x"c` is `L'a' D[Q'b' P(x)] L'c'`; `${x:-'a b'}` is
`P(x:-[Q'a b'])`; `$((1+$y))` is `A[L'1+' P(y)]`.

`DescribeToken`: a Word is `W(<DescribeWord>)`, an IoNumber `IO(<n>)`, a
Newline `NL`, the end `EOF`, an operator its `OperatorText`.

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `commands/hsh/HshWord.cpp` and `commands/hsh/HshLexer.cpp` to the
`add_library(BuiltinCommands STATIC ...)` sources (after the other commands).
No new link dependency.

### `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (new)

A short page for the agents of the next hsh tasks: what hsh is (dash
reimplemented as a builtin, namespace `Haisos::Hsh`), the pipeline
lexer -> parser -> expansion -> executor with one line per file of this
directory, that `ShellError` is the one error type (message after
`hsh: <line>: `, `Incomplete()` for PS2), the word-part model (Literal /
Quoted / DoubleQuoted / Parameter / CommandSubstitution / Arithmetic, and that
adjacent Literal and Quoted parts are merged), how the end of `$(...)` is found,
heredoc collection, the interactive protocol (below), and the documented
deviations from dash: `&>` and `<<<` are bash's operators (so `echo a &>f` is
not `echo a &` then `>f` as in dash), `${x:}` is `Bad substitution` rather
than a syntax error, `$'...'` is not ANSI-C quoting (as dash).

The interactive protocol to document (hsh--interactive implements it): keep a
buffer of the lines typed since the last complete command; after each line,
lex/parse the whole buffer again from the start with `interactive` set and
`firstLine` the number of that buffer's first line; on an error with
`Incomplete()` show PS2 and read another line; otherwise run (or report) and
empty the buffer. Re-reading the buffer each time keeps the lexer free of
suspended state.

### Root `CLAUDE.md`

In the directory tree, under `BuiltinCommands/`, nothing changes; no table
gets hsh yet (hsh--executor registers it).

## Tests

New test executable `Hsh.unittests`, shared by every hsh task:

- `tests/unit/components/Hsh.unittests/CMakeLists.txt` (new):
  `add_executable(Hsh.unittests HshLexerTest.cpp)`,
  `target_link_libraries(Hsh.unittests PRIVATE gtest_main BuiltinCommands)`,
  `target_compile_features(Hsh.unittests PRIVATE cxx_std_17)`.
- `tests/unit/CMakeLists.txt`: `add_subdirectory(components/Hsh.unittests)`
  after the BuiltinCommands line.

Every test suite name starts with `Hsh` (the test runner's filter must match
both the executable name and the gtest name, so `Hsh` is the filter for all
hsh tests).

`tests/unit/components/Hsh.unittests/HshLexerTest.cpp` -- a helper lexes a
source to the list of `DescribeToken` strings up to and including `EOF`; most
tests are tables of {source, expected descriptions}:

- `HshLexerTest.OperatorsAndNewlines`: every operator of `TokenKind`, longest
  match (`a&&b`, `a&>f`, `<<<w`, `<<-E` (with a body line), `>|`, `<>`, `;;`),
  and `&\<newline>&` lexing as `&&`.
- `HshLexerTest.IoNumbers`: `2>x` (IO(2) > W), `0<&3`, `12>x` (W(L'12') >),
  `a1>x`, `2 >x` (W(L'2')).
- `HshLexerTest.QuotingAndEscapes`: `'a b'`, `"a $x b"`, `\a`, `"\a\$"` gives
  `D[Q'\a$']`, `a"b"'c'` merging, `\` alone at the end, `'a\<newline>b'`
  keeping both characters, `"a\<newline>b"` dropping them.
- `HshLexerTest.CommentsAndBlanks`: `echo # c`, `a#b`, `\#a`, tabs.
- `HshLexerTest.Parameters`: `$x`, `$ab_1c`, `$10`, `$@ $* $# $? $- $$ $! $0`,
  `$`, `$'a'`, `${x}`, `${10}`, `${#x}`, `${#}`, every op with an operand,
  `${x-}`, `${x:-${y:-z}}`, `${x-\}}`, `"${x:-"a b"}"`, `"${x:-'a'}"`
  (`D[P(x:-[Q''a''])]`), and Bad: `${}`, `${x!}`, `${#x:-a}`, `${x:2:3}`.
- `HshLexerTest.CommandSubstitutionEnds`: `$(echo ")")`, `$( (echo a) )`,
  `$(case a in a) echo m;; esac)`, `$(case a in (a) echo;; esac) x`,
  `$(echo a # c )<newline>)`, `x=$(echo "a)"; echo 'b)' \))` -- each checks the
  part's text and that the next token is right.
- `HshLexerTest.Backquotes`: `` `echo \`echo hi\`` `` text, `"`echo \"q\"`"`,
  `` `echo \$x` ``.
- `HshLexerTest.Arithmetic`: `$((1+2))`, `$(( (3) + (4) ))`, `$((1 + $(echo 2)))`,
  `$((x))a`, newline inside.
- `HshLexerTest.HereDocuments`: `cat <<E` + body + `E` (rawBody, body parts,
  terminated); `<<-` stripping tabs from body and delimiter; quoted delimiters
  `'E'`, `"E"`, `\E`, `E"F"` (no expansion: one Quoted part); `$x`, `\$x`,
  `a\<newline>b` and `$(echo cs)` in an unquoted body; two heredocs on one
  line; a heredoc inside `$(cat <<E<newline>in<newline>E<newline>)`; the body
  not terminated at end of input (complete, not terminated, no error).
- `HshLexerTest.TokenOffsets`: `begin`/`end` of every token of
  `  echo "a b"$(x) 2>&1 &\<newline>& y` (the substring of each is its text as
  written, the continuation inside `&\<newline>&` included); EndOfInput at the
  source's size; a heredoc's `sourceBegin`/`sourceEnd` covering
  `body<newline>E<newline>` in `cat <<E; echo<newline>body<newline>E<newline>next`,
  and up to the end when not terminated.
- `HshLexerTest.LineNumbers`: token lines across newlines, continuations and
  heredoc bodies; `firstLine` 5 shifting them.
- `HshLexerTest.Errors`: each message of the table, byte for byte, with its
  line and `Incomplete()` (`echo "a<newline>b` is line 2; `$((echo a) )` is
  not incomplete).
- `HshLexerTest.InteractiveIncomplete`: with `interactive`, `cat <<E<newline>x<newline>`
  and `echo a\<newline>` throw incomplete errors; without it, both lex fine.
- `HshLexerTest.ReservedWordsAndNames`: `AsReservedWord` for each reserved
  word, none for `"if"`, `\if`, `ifx`; `IsValidShellName`, `LiteralText`.
- `HshLexerTest.FormatShellError`: `FormatShellError("hsh", 3, "x")` is `"hsh: 3: x\n"`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

- New `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` as above.
- `src/components/BuiltinCommands/CLAUDE.md`: one line under "Key Classes"
  saying `commands/hsh/` holds the shell, in progress, with its own CLAUDE.md.

## Acceptance

- [ ] Every type and function above exists with exactly these names and
      signatures, in namespace `Haisos::Hsh`, in `commands/hsh/`.
- [ ] Adjacent Literal and adjacent Quoted parts are merged at every level.
- [ ] `$(...)` ends are found with the case-aware scan, not by counting characters.
- [ ] Heredoc bodies are read at the newline, before the Newline token is returned.
- [ ] Every error message matches the table byte for byte, with its line and
      incomplete flag; the interactive-only errors appear only with `interactive`.
- [ ] No POSIX-only header; builds on Linux; `Hsh.unittests` passes; all
      other unit tests still pass.
- [ ] `hsh` is not registered as a builtin and nothing outside
      `BuiltinCommands` (and the test tree) changes.

## Out of scope

- The AST and the parser (hsh--parser); expansions (hsh--expansion);
  executing anything, registering `hsh`, its options and `--help`
  (hsh--executor); the interactive loop and prompts (hsh--interactive).
- Aliases, `$'...'` ANSI-C quoting, `&>>`, `;&`, brace expansion `{a,b}`,
  multi-digit IO numbers, `[[`, `function` -- none are dash's.
