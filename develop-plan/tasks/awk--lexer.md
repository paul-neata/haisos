# Task awk--lexer: the awk builtin's invocation and the awk lexer

- Rock: awk
- Depends on: coreutils--names-env (`stopAtFirstOperand` of `ParseBuiltinArgs`), coreutils--sort (`BuiltinText.h`: `OpenInputOperand`)
- Size: ~950 changed lines in ~13 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the awk builtin's invocation and the awk lexer

## Goal

`awk` is a builtin command (placed with `BUILTIN rootfs awk /bin/awk`) that
takes gawk's command line -- `-F fs`, `-v var=value`, `-f progfile` (any
number), `--`, the program text as the first operand, then files and
`var=value` operands -- with gawk's own long options (`--field-separator`,
`--assign`, `--file`) and every other gawk option accepted and reported as
not treated. `awk --help`, `awk --version` (and gawk's `-h`, `-V`) and
`man awk` work like every builtin's. It loads the program (the operand, or
every `-f` file, `-f -` meaning standard input) but runs nothing yet: it
reports `awk: running programs is not implemented yet` (the parser and the
interpreter come in the next tasks of the rock).

Under it, in `src/components/BuiltinCommands/commands/awk/` (namespace
`Haisos::Awk`, its own `CLAUDE.md`, built like hsh), the **lexer** of POSIX
awk as gawk `--posix` reads it: tokens, string escapes, comments,
backslash-newline continuations, newlines that are tokens except after
`{ && || , ; do else`, the regex-versus-division ambiguity resolved by the
parser asking the lexer to rescan a `/` as a regex, line and column of every
token, and gawk's diagnostics (`awk: cmd. line:1: ...` with the source line
and a caret).

The rock: awk--lexer (this), awk--expressions, awk--parser, awk--values,
awk--interpreter, awk--records, then awk--functions and awk--io. The
reference is **gawk 5.2 run with `--posix`** (POSIX awk, as gawk implements
it); every expected text below was checked with `gawk --posix` 5.2.1. The
task container has only mawk (`/usr/bin/awk`), whose messages differ: never
change an expected text to mawk's.

## Context

Read first: root `CLAUDE.md` ("Security", "Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`
(`ParseBuiltinArgs`, `BuiltinContext::Out/ErrorText/ReportNotTreated`,
`BuiltinHelpText`, `BuiltinVersionText`), `commands/hsh/Hsh.cpp` (a builtin
parsing its own invocation, `BuiltinHelp::basedOn`),
`commands/hsh/CLAUDE.md` (how a multi-file builtin is laid out and
documented), `commands/hsh/HshLexer.h/.cpp` and
`tests/unit/components/Hsh.unittests/` (the lexer/test style to follow).

What earlier tasks provide, as if on develop (their plans in
`develop-plan/tasks/` are the authority):
- coreutils--names-env: `ParseBuiltinArgs(args, options, bool stopAtFirstOperand = false)`
  -- with `true`, the first argument that is not an option (`-` alone
  included) and everything after it are operands, untouched; a `--` before
  any operand ends the options and is dropped. Error texts are the existing
  ones (`invalid option -- 'y'`, `option requires an argument -- 'f'`,
  `unrecognized option '--x'`, `option '--file' requires an argument`).
- coreutils--sort: `BuiltinText.h` -- `OpenInputOperand(context, name, failure)`
  with `InputOpenFailure { None, Missing, Directory, Denied, BadDescriptor }`
  (`-` is descriptor 0).

## Changes

### `src/components/BuiltinCommands/commands/awk/AwkError.h` / `AwkError.cpp` (new)

Namespace `Haisos::Awk`. The diagnostics every stage of awk shares:

```cpp
inline constexpr const char* kAwkName = "awk";
// How gawk names the program operand's text in messages.
inline constexpr const char* kCommandLineSourceName = "cmd. line";

// One piece of program text: the program operand ("cmd. line"), or one -f
// file, named as given on the command line ("-" for standard input).
struct AwkSource {
    std::string name;
    std::string text;
};

struct AwkWarning {
    std::string sourceName;
    int line = 0;
    std::string message;   // "escape sequence `\q' treated as plain `q'"
};

// "awk: <sourceName>:<line>: " -- the start of every diagnostic tied to a
// place in the program ("awk: cmd. line:1: ", "awk: prog.awk:3: ").
std::string AwkLocationPrefix(const std::string& sourceName, int line);

// A syntax error: gawk's two-line report (see FormatAwkSyntaxError).
class AwkSyntaxError : public std::runtime_error {
public:
    AwkSyntaxError(const std::string& message, std::string sourceName, int line,
                   std::string lineText, size_t column);
    const std::string& SourceName() const;
    int Line() const;
    const std::string& LineText() const;   // the source line shown, without its newline
    size_t Column() const;                 // byte offset of the caret in LineText()
};

// gawk's yyerror output, byte for byte:
//   <prefix><lineText>\n
//   <prefix><caret padding>^ <message>\n
// <prefix> is AwkLocationPrefix(sourceName, line); the caret padding has one
// byte per byte of lineText before Column(): a tab stays a tab, anything else
// is a space.
std::string FormatAwkSyntaxError(const AwkSyntaxError& error);
// "<prefix>warning: <message>\n"
std::string FormatAwkWarning(const AwkWarning& warning);
// "<prefix>error: <message>\n" -- gawk's non-syntax parse errors (awk--parser).
std::string FormatAwkError(const std::string& sourceName, int line, const std::string& message);
```

Example (verified): the error `syntax error` at column 13 of line 1 of
`"\tBEGIN {\tx = = 2 }"` formats as
`awk: cmd. line:1: \tBEGIN {\tx = = 2 }\nawk: cmd. line:1: \t       \t    ^ syntax error\n`.

### `src/components/BuiltinCommands/commands/awk/AwkLexer.h` / `AwkLexer.cpp` (new)

```cpp
enum class TokenKind {
    EndOfInput, Newline,
    Number, String, Regex, Name, FuncName, Builtin,
    // keywords
    Begin, End, Function, Getline, If, Else, While, For, Do, Break, Continue,
    Next, Nextfile, Exit, Return, Delete, In, Print, Printf,
    // punctuation
    LeftBrace, RightBrace, LeftParen, RightParen, LeftBracket, RightBracket,
    Semicolon, Comma, Plus, Minus, Star, Slash, Percent, Caret, Not,
    Greater, Less, Pipe, Question, Colon, Tilde, NoMatch, Dollar,
    Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign, PowAssign,
    Or, And, Equal, NotEqual, LessEqual, GreaterEqual, Append, Increment, Decrement,
};

// The spelling of a keyword or punctuation kind ("BEGIN", "getline", "{",
// "!~", ">>", "/="); "" for EndOfInput, Newline and the six value kinds.
const char* TokenKindText(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    // Number: the text as written ("1e3"); String: the value, escapes
    // decoded; Regex: the regex text (see ScanRegex); Name/FuncName/Builtin:
    // the name; keywords: the keyword as written ("func" or "function").
    std::string text;
    double number = 0;   // Number: its value
    int line = 1;        // the line the token starts on, from 1
    size_t column = 0;   // byte offset of its first byte within that line
    size_t begin = 0;    // byte offsets in the source: first byte, one past the last
    size_t end = 0;
};

// For tests: NUM(<text>), STR(<escaped value>), ERE(<text>), NAME(x),
// FUNC(f), BUILTIN(length), NL, EOF, and TokenKindText for the rest (a
// Function token is "function" whichever way it was spelled). STR escapes \\
// \" \n \t and every other byte below 0x20 or 0x7F as \ooo (3 octal digits).
std::string DescribeToken(const Token& token);

// awk's string escapes, as gawk --posix decodes a string literal's body (and
// -v values, -F's value, var=value operands): \" \\ \a \b \f \n \r \t \v,
// \ooo (1 to 3 octal digits, \0 is a NUL byte), a backslash-newline removed
// (a continuation); any other \c is c, with the warning
// "escape sequence `\c' treated as plain `c'" appended to |warnings| --
// except \x, which is a plain x with no warning (gawk --posix has no \x
// escapes). A trailing lone backslash is kept as a backslash.
std::string DecodeAwkStringEscapes(std::string_view text, std::vector<std::string>& warnings);

class Lexer {
public:
    explicit Lexer(AwkSource source);

    // The next token. Throws AwkSyntaxError on a lexical error.
    Token Next();

    // Rescans as a regex the '/' (Slash) or '/=' (DivAssign) token that Next()
    // has just returned -- the parser calls it when such a token stands where
    // an operand is expected. Reads from right after the '/' (so for '/=' the
    // '=' is the regex's first byte) to the closing '/', and returns a Regex
    // token (line, column and begin of the '/'). Throws std::logic_error when
    // the last token returned was neither.
    Token ScanRegex(const Token& slash);

    // The warnings found so far (string escapes), in order; cleared.
    std::vector<AwkWarning> TakeWarnings();

    const AwkSource& Source() const;
    // The text of line |line| (from 1) without its newline; "" past the end.
    std::string LineText(int line) const;
};
```

A plain class (it implements no interface), held by value. Lexing rules,
as gawk `--posix`:

- **Blanks**: space, tab and `\r` separate tokens and are otherwise
  ignored (gawk takes a CR before a newline as a blank, so CRLF programs
  work).
- **Comments**: `#` to the end of the line; the newline is still a token.
- **Continuation**: a backslash followed by a newline (or by `\r\n`) is
  removed, anywhere outside a string, regex or comment. A backslash followed
  by anything else is the error `backslash not last character on line`,
  caret on the backslash.
- **Newlines** are `Newline` tokens (line = the line they end, column = the
  newline's offset in it), **except** that after a `{`, `&&`, `||`, `,`,
  `;`, `do` or `else` token every following newline (with blanks, comments
  and continuations between them) is skipped. A source that does not end
  with a newline gets one: a `Newline` token at its end (line and column of
  the end) -- so `BEGIN { print (1` reports its error on line 2, as gawk.
  `EndOfInput` (repeated forever) has the line of the last newline-ended
  line (the last line, for a source not ending in a newline) and the column
  of its end; an empty source gives `EndOfInput` at line 1, column 0.
- **Names**: `[A-Za-z_][A-Za-z0-9_]*`. A keyword (`BEGIN END function func
  getline if else while for do break continue next nextfile exit return
  delete in print printf`; `func` is a `Function`) is its keyword kind. One
  of the POSIX built-in functions (`atan2 close cos exp fflush gsub index int
  length log match rand sin split sprintf sqrt srand sub substr system
  tolower toupper`) is `Builtin`, whatever follows. Any other name is
  `FuncName` when the byte right after it is `(` (no blank between: POSIX's
  FUNC_NAME), else `Name`. gawk's extension functions (`gensub`,
  `strftime`, ...) are plain names under `--posix`.
- **Numbers**: digits with an optional `.` and digits, or `.` and digits;
  then an exponent `e`/`E`, optional sign, digits -- only if digits follow
  (`1e` is the number `1` then the name `e`). `1.2.3` is `1.2` then `.3`;
  `0x11` is `0` then the name `x11` (no hex constants in POSIX mode). The
  value is `std::strtod` of the token text (Haisos never calls `setlocale`,
  so the C locale's `.` applies).
- **Strings**: `"..."`, the body decoded by `DecodeAwkStringEscapes` (a
  backslash-newline inside is a continuation: a documented exception, gawk
  `--posix` refuses it as `POSIX does not allow physical newlines in string
  values`). A raw newline or the end of the source before the closing quote
  is `unterminated string`, caret on the opening quote. Warnings carry the
  string's line.
- **Regexes** (`ScanRegex` only): the text up to the first `/` that is
  neither escaped nor inside a bracket expression. A bracket starts at an
  unescaped `[`; a `]` right after `[` or `[^` is a member; `[:`...`:]`,
  `[.`...`.]`, `[=`...`=]` inside it are skipped whole; the bracket ends at
  the next `]`. A backslash and the byte after it are taken as a pair: `\/`
  is stored as `/`, every other pair byte for byte (`\.` stays `\.`, `\\`
  stays `\\`); a backslash-newline is removed. A newline or the end of the
  source first is `unterminated regexp`, caret on the byte right after the
  opening `/`. Nothing else is decoded here: translating awk's regex escapes
  for the regex engine is awk--records' job.
- **Punctuation**: longest match among `{ } ( ) [ ] ; , + - * / % ^ ! > < |
  ? : ~ $ = += -= *= /= %= ^= || && == != <= >= >> ++ -- !~`. gawk's `**`,
  `**=` and `|&` are not POSIX: `**` is two `*` tokens (a syntax error later).
- **Anything else** (a byte that starts no token: `` ` ``, `@`, `'`, `\0`,
  a byte >= 0x80 outside a string, regex or comment) is
  ``invalid char '<c>' in expression``, caret on it (gawk reports `@`, its
  indirect-call operator, as a syntax error at the next token instead: a
  documented exception).
- Errors are `AwkSyntaxError` with the source's name, the line, that line's
  text (`LineText`) and the column.

### `src/components/BuiltinCommands/commands/awk/AwkInvocation.h` / `AwkInvocation.cpp` (new)

The command line, as gawk takes it. Namespace `Haisos::Awk`.

```cpp
// Option ids of the treated options (the rest are kBuiltinNotTreated).
enum AwkOptionId {
    kAwkOptionFieldSeparator = 1, kAwkOptionFile, kAwkOptionAssign,
    kAwkOptionHelp, kAwkOptionVersion, kAwkOptionPosix, kAwkOptionReInterval,
};
// Every option of gawk 5.3, treated or not (see below).
const std::vector<BuiltinOption>& AwkOptionTable();

// -F and -v, in command-line order: gawk applies them in that order before BEGIN.
struct AwkPreAssignment {
    bool fieldSeparator = false;  // -F: |text| is FS's raw value; -v: |text| is "name=value", raw
    std::string text;
};

struct AwkInvocation {
    std::vector<AwkPreAssignment> preAssignments;
    std::vector<std::string> programFiles;    // -f operands, in order ("-": standard input)
    std::optional<std::string> programText;   // the first operand, when no -f was given
    std::vector<std::string> operands;        // the rest: files and var=value (ARGV[1] on)
};

// gawk's two usage lines, naming awk:
//   "Usage: awk [POSIX or GNU style options] -f progfile [--] file ...\n"
//   "Usage: awk [POSIX or GNU style options] [--] 'program' file ...\n"
std::string AwkUsageText();

// Parses context.Args() with ParseBuiltinArgs(args, AwkOptionTable(),
// /*stopAtFirstOperand=*/true). Returns the invocation, or nullopt with
// |exitStatus| set when the command is already done.
std::optional<AwkInvocation> ParseAwkInvocation(BuiltinContext& context, const IBuiltinCommand& command, int& exitStatus);

// The program's sources: one {"cmd. line", programText}, or one per -f file
// in order. Nullopt with |exitStatus| set (and the message written) on failure.
std::optional<std::vector<AwkSource>> LoadAwkSources(BuiltinContext& context, const AwkInvocation& invocation, int& exitStatus);
```

`AwkOptionTable()` -- treated:
- `{'F', "field-separator", kAwkOptionFieldSeparator, Required, "FS", "use FS for the input field separator"}`
- `{'f', "file", kAwkOptionFile, Required, "PROGFILE", "read the program from PROGFILE (repeatable; - is standard input)"}`
- `{'v', "assign", kAwkOptionAssign, Required, "VAR=VAL", "assign VAL to VAR before the program starts"}`
- `{'h', "", kAwkOptionHelp, None, "", "show this help (as --help)"}`
- `{'V', "", kAwkOptionVersion, None, "", "show the version (as --version)"}`
- `{'P', "posix", kAwkOptionPosix, None, "", "POSIX awk (always on)"}`
- `{'r', "re-interval", kAwkOptionReInterval, None, "", "interval expressions in regexes (always on)"}`

Not treated (`kBuiltinNotTreated`), gawk's extensions: `-b
--characters-as-bytes`, `-c --traditional`, `-C --copyright`, `-d
--dump-variables` (OptionalAttached, `FILE`), `-D --debug`
(OptionalAttached), `-e --source` (Required), `-E --exec` (Required), `-g
--gen-pot`, `-i --include` (Required), `-I --trace`, `-k --csv`, `-l
--load` (Required), `-L --lint` (OptionalAttached), `-M --bignum`, `-N
--use-lc-numeric`, `-n --non-decimal-data`, `-o --pretty-print`
(OptionalAttached), `-O --optimize`, `-p --profile` (OptionalAttached), `-s
--no-optimize`, `-S --sandbox`, `-t --lint-old`, and POSIX's `-W`
(Required, no long name).

`ParseAwkInvocation`, step by step:
1. `parsed.error` set: write `awk: <error>\n` then `AwkUsageText()` to
   stderr (`ErrorText`), status 1. (gawk prints its whole option summary
   after the two lines, and words a missing argument without quotes, or
   prints nothing for an unknown option: a documented exception -- the
   summary is `awk --help`.)
2. The first `--help`/`-h` or `--version`/`-V` given wins:
   `BuiltinHelpText`/`BuiltinVersionText` on stdout, status 0.
3. `context.ReportNotTreated(parsed)`.
4. `-F` and `-v` go to `preAssignments` in order. A `-v` value without `=`:
   stderr gets ``awk: `x' argument to `-v' not in `var=value' form\n\n``
   followed by `AwkUsageText()`, status 1 (the same text for `--assign`).
   Whether the name is a legal variable name is checked when the program
   runs (awk--interpreter).
5. With no `-f`, the first operand is `programText`; no operand at all:
   `AwkUsageText()` on stderr, status 1. The other operands are `operands`.

`LoadAwkSources`: for each `-f` name, `-` reads descriptor 0 to its end;
otherwise `OpenInputOperand`: `Missing` (or `Denied`, `BadDescriptor`) ->
``awk: fatal: cannot open source file `<name>' for reading: No such file or
directory\n`` (`Permission denied` for `Denied`), status 2; `Directory` ->
``awk: <name>:1: error: cannot read source file `<name>': Is a directory\n``,
status 1. The file is read whole (stop promptly on `StopRequested()` or
`kIOInterrupted`: return nullopt with status 143).

### `src/components/BuiltinCommands/commands/awk/Awk.cpp` (new)

`class AwkCommand : public IBuiltinCommand` in an anonymous namespace, and
`std::shared_ptr<IBuiltinCommand> CreateAwkCommand()` (as `Hsh.cpp`):
- `Name()` "awk", `Version()` "1.0.0" (the version stays 1.0.0 through the
  rock: the command is first released with the develop), `Options()`
  `AwkOptionTable()`.
- `Help()`: summary `pattern scanning and processing language`; usage
  `awk [-F fs] [-v var=value] [--] 'program' [file ...]` and
  `awk [-F fs] [-v var=value] -f progfile [-f progfile]... [--] [file ...]`;
  `basedOn = "gawk"` (so the help's second line is `Based on Linux gawk:
  https://man7.org/linux/man-pages/man1/gawk.1.html` -- the man-pages
  project has no `awk.1` page, and gawk `--posix` is the reference); notes:
  `POSIX awk, as gawk --posix runs it.` plus one line per documented
  exception so far (the usage-error output, `@`, backslash-newline in
  strings). Later tasks append theirs.
- `Run`: `ParseAwkInvocation`, `LoadAwkSources`, then, for now,
  `ErrorText("awk: running programs is not implemented yet\n")` and status 2
  (awk--parser replaces this).

`ManPage()` is not overridden (the page is the `--help` text).

### Registration and build

- `BuiltinCommandList.h`: declare `CreateAwkCommand()`; add it first in
  `CreateStandardBuiltinCommands()` (the list is alphabetical). That alone
  puts `# BUILTIN rootfs awk /bin/awk` in the `haisos --init` template
  (root `CLAUDE.md` rule 9; checked by `TheInitTemplatesBuiltinsAllApplyOnceUncommented`).
- `src/components/BuiltinCommands/CMakeLists.txt`: add
  `commands/awk/AwkError.cpp`, `commands/awk/AwkLexer.cpp`,
  `commands/awk/AwkInvocation.cpp`, `commands/awk/Awk.cpp`.

### Rules that apply (root `CLAUDE.md`)

- `ICurrentProcess` is the only door out: `-f` files and standard input are
  read through `context.IO()` (`OpenInputOperand`, `GetDescriptor`), never an
  `IFileSystem` or `IHaisosOS`.
- The builtin rules: every gawk option in `Options()`, untreated ones
  reported by `ReportNotTreated`; `--help` only from `BuiltinHelpText`;
  `--version` as every builtin; portable C++17 (no POSIX headers, no
  `<regex>`; `std::strtod` is fine).

## Tests

New test executable `tests/unit/components/Awk.unittests/` (as
`Hsh.unittests`): `CMakeLists.txt` with `add_executable(Awk.unittests
AwkLexerTest.cpp AwkCommandTest.cpp)`, the same include directories
(`src/components/Factory`, `tests/unit/components/BuiltinCommands.unittests`)
and libraries (`gtest_main BuiltinCommands Environment Factory`),
`cxx_std_17`; `tests/unit/CMakeLists.txt` gets
`add_subdirectory(components/Awk.unittests)`. Every test suite name starts
with `Awk`.

`AwkLexerTest.cpp` -- plain `TEST`s, helpers as in `HshLexerTest.cpp`:
`Lex(source)` returns the `DescribeToken` of every token through the first
`EOF`; `LexRegex(source)` does the same but calls `ScanRegex` on every
`Slash`/`DivAssign` returned; `ExpectError(source, message, line, column)`.
Sources are `AwkSource{"cmd. line", text}`.
- `AwkLexerTest.Punctuation`: `{ } ( ) [ ] ; , + - * / % ^ ! > < | ? : ~ $ = += -= *= /= %= ^= || && == != <= >= >> ++ -- !~`
  -> each spelling in order, then `NL`, `EOF`; `a**b` -> `NAME(a) * * NAME(b) NL EOF`; `a!==b` -> `NAME(a) != = NAME(b) NL EOF`.
- `AwkLexerTest.KeywordsNamesBuiltins`: `BEGIN END function func getline if else while for do break continue next nextfile exit return delete in print printf`
  -> the same words with `func` as `function`, `NL`, `EOF`;
  `length substr(x) foo foo(1) foo (1) _a1 gensub(x)` -> `BUILTIN(length) BUILTIN(substr) ( NAME(x) ) NAME(foo) FUNC(foo) ( NUM(1) ) NAME(foo) ( NUM(1) ) NAME(_a1) FUNC(gensub) ( NAME(x) ) NL EOF`.
- `AwkLexerTest.Numbers`: `1 1.5 .5 1. 1e3 1E-2 1e 1.2.3 0x11 007` ->
  `NUM(1) NUM(1.5) NUM(.5) NUM(1.) NUM(1e3) NUM(1E-2) NUM(1) NAME(e) NUM(1.2) NUM(.3) NUM(0) NAME(x11) NUM(007) NL EOF`;
  the values of `1e3`, `.5`, `007`, `1E-2` are 1000, 0.5, 7, 0.01.
- `AwkLexerTest.Strings`: `"a\"b\\c\n\t\101\0612\x41"` (as awk source) ->
  `STR(a\"b\\c\n\tA12x41)`, no warning; `"a\/b" "\q"` -> `STR(a/b) STR(q)`
  with the warnings ``escape sequence `\/' treated as plain `/'`` and
  ``escape sequence `\q' treated as plain `q'`` on line 1; `"\0"` -> a
  one-byte NUL value (`STR(\000)`); a backslash-newline inside a string is
  removed and the next token is on line 2.
- `AwkLexerTest.StringEscapeDecoder`: `DecodeAwkStringEscapes` on `a\tb`,
  `\101`, `\1234` (`S` then `4`), `x\` (kept), `\e` (warning).
- `AwkLexerTest.NewlinesAndComments`: `a # c\nb\n\n` -> `NAME(a) NL NAME(b) NL NL EOF`
  with lines 1 1 2 2 3; `{\n\n x &&\n y ||\n z ,\n w ;\n v do\n u else\n t\n}`
  -> no `NL` but the one before `}` and the final one; `a ;# c\n b` -> `NAME(a) ; NAME(b) NL EOF`.
- `AwkLexerTest.Continuations`: `a \\\n b` (a backslash-newline) -> `NAME(a) NAME(b) NL EOF`, b on line 2;
  the same with `\\\r\n`; `a\r\n` -> `NAME(a) NL EOF`.
- `AwkLexerTest.ImplicitFinalNewline`: `a` -> `NAME(a) NL EOF` with the `NL` and `EOF` at line 1, column 1;
  `a\n` -> `EOF` at line 1, column 1; `` -> `EOF` only, line 1, column 0;
  `a;` -> `NAME(a) ; EOF`.
- `AwkLexerTest.Regexes` (via `LexRegex`): `/a\/b[/]c\.d/` -> `ERE(a/b[/]c\.d) NL EOF`;
  `/=x/` -> `ERE(=x)`; `/a\\/ x` -> `ERE(a\\) NAME(x)`; `/[]/]/` -> `ERE([]/])`;
  `/[^]/]x/` -> `ERE([^]/]x)`; `/[[:alpha:]/]/` -> `ERE([[:alpha:]/])`; `/#x/` -> `ERE(#x)`;
  `ScanRegex` after a `Name` token throws `std::logic_error`.
- `AwkLexerTest.Errors`: `x = "abc` -> `unterminated string`, line 1, column 4;
  `"ab\ncd"` -> `unterminated string`, column 0; `/abc` (LexRegex) and `/ab\nc/`
  -> `unterminated regexp`, column 1; `a \\ b` -> `backslash not last character on line`, column 2;
  `\n\nx = ` + backtick -> ``invalid char '`' in expression``, line 3, column 4.
- `AwkLexerTest.PositionsAndLineText`: `BEGIN {\n  x = 1 }`: `x` at line 2,
  column 2, `begin` 10; `LineText(2)` is `  x = 1 }`, `LineText(3)` is "".
- `AwkLexerTest.FormatsDiagnostics`: the `FormatAwkSyntaxError` example
  above (tabs kept); `FormatAwkWarning({"prog.awk", 3, "m"})` ->
  `awk: prog.awk:3: warning: m\n`; `FormatAwkError("cmd. line", 1, "m")` ->
  `awk: cmd. line:1: error: m\n`.

`AwkCommandTest.cpp` -- `class AwkCommandTest : public BuiltinCommandsTest {}`,
`TEST_F`s on `RunCaptured("awk", ...)`:
- `AwkCommandTest.VersionAndHelp`: `--version` and `-V` -> `awk (HaisosOS builtin) 1.0.0\n`, status 0;
  `--help` and `-h` print `BuiltinHelpText`, whose first two lines are
  `HaisosOS awk version 1.0.0 - pattern scanning and processing language` and
  `Based on Linux gawk: https://man7.org/linux/man-pages/man1/gawk.1.html`.
- `AwkCommandTest.UsageErrors` (stdout empty, status 1 each): no argument ->
  stderr exactly `AwkUsageText()`; `-y` -> `awk: invalid option -- 'y'\n` + usage;
  `-f` -> `awk: option requires an argument -- 'f'\n` + usage;
  `-v x BEGIN{}` -> ``awk: `x' argument to `-v' not in `var=value' form\n\n`` + usage.
- `AwkCommandTest.OptionsStopAtTheProgram`: `BEGIN{} -x --lint` -> no
  `invalid option` and no not-treated report (both are operands), the
  not-implemented message, status 2; `-- BEGIN{}` the same.
- `AwkCommandTest.NotTreatedOptionsAreReported`: `--lint BEGIN{}` -> stderr
  holds `Parameter --lint is not treated by HaisosOS awk v. 1.0.0`;
  `-P BEGIN{}` and `--re-interval BEGIN{}` -> no report.
- `AwkCommandTest.ProgramSources`: `-f /nope.awk` ->
  ``awk: fatal: cannot open source file `/nope.awk' for reading: No such file or directory\n``, 2;
  `-f /docs` -> ``awk: /docs:1: error: cannot read source file `/docs': Is a directory\n``, 1;
  `-f /notes.txt` and `-f -` (stdin `BEGIN{}`) -> the not-implemented message, 2.

Existing tests to update: `ListsEveryBuiltinSortedWithAVersion` in
`tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`
gets `"awk"` (first). The generic tests (`EveryBuiltinsHelpHasTheSameShape`,
`EveryUntreatedOptionIsAcceptedAndReported`, `EveryBuiltinHasAVersion`,
`EveryBuiltinsManPageIsItsHelp`) then cover awk without change; check they
pass (the untreated-option test runs `awk <option> [x] /docs`).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Awk
bash ./scripts/test_linux.sh L U BuiltinCommands
bash ./scripts/test_linux.sh L U
```

## Docs

- New `src/components/BuiltinCommands/commands/awk/CLAUDE.md`, shaped like
  hsh's: what awk is (POSIX awk as gawk `--posix` runs it, namespace
  `Haisos::Awk`, portable C++17, no `<regex>`), the pipeline (lexer ->
  parser -> interpreter, one task each, adding their files here), a list of
  this task's files with a paragraph each (`AwkError` with the diagnostic
  shapes, `AwkLexer` with every rule above -- especially the regex rescan
  protocol and the newline rules, `AwkInvocation`, `Awk.cpp`), and a
  "Documented exceptions" list (usage errors, `@`, backslash-newline in
  strings).
- `src/components/BuiltinCommands/CLAUDE.md`: the opening list of commands
  and a table row: `awk` | 1.0.0 | `-F -f -v --`, gawk's
  `--field-separator --file --assign`, `-h -V -P -r` (the program is not run
  yet) | the exceptions above; and a `commands/awk/` line next to
  `commands/hsh/` under "Key Classes".
- Root `CLAUDE.md`: `awk` in the Builtin Commands sentence and table
  (`POSIX awk, as gawk --posix: options so far; the program is not run yet`
  -- later tasks update the row), and `BuiltinCommands/ - The builtin
  commands (...)` list in the directory structure.

## Acceptance

- [ ] `awk --version`, `-V`, `--help`, `-h`, `man awk` as specified; the help's
  second line links gawk's man page.
- [ ] Options stop at the first operand; everything after the program is an operand.
- [ ] Every gawk option is in `AwkOptionTable()`; untreated ones are
  reported, never refused; usage errors exactly as specified, status 1.
- [ ] `-f` files (and `-f -`) are read through `context.IO()`; their errors
  exactly as specified.
- [ ] Every lexer rule above is implemented and tested; `ScanRegex` is the
  only way a Regex token is made.
- [ ] `awk` is registered in `CreateStandardBuiltinCommands()` (first) and
  appears in the `haisos --init` template; `ListsEveryBuiltinSortedWithAVersion` updated.
- [ ] New sources in `src/components/BuiltinCommands/CMakeLists.txt`; the
  `Awk.unittests` executable is built and green; all unit tests green.
- [ ] The three CLAUDE.md files updated.

## Out of scope

- Parsing (awk--expressions, awk--parser), running programs
  (awk--values, awk--interpreter, awk--records), built-in and user functions, `printf`
  (awk--functions), `getline`, redirections, `system`, `ENVIRON`
  (awk--io).
- Translating regex escapes for the regex engine (awk--records).
