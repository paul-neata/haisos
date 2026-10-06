# Task hsh--parser: The hsh AST and its recursive-descent parser

- Rock: hsh
- Depends on: hsh--lexer
- Size: ~1020 changed lines in ~7 files
- Plan checked against: develop @ 0d92271
- PR title: hsh: AST and parser for the full POSIX shell grammar

## Goal

The second piece of `hsh`: the syntax tree of a shell program and a parser
that builds it from the tokens of hsh--lexer, for the whole POSIX grammar as
dash accepts it -- simple commands, pipelines, `&&`/`||` lists, `;`/`&` lists,
`{ }`, `( )`, `if`, `while`, `until`, `for`, `case`, function definitions,
redirections anywhere -- with dash's syntax error messages and line numbers.
Like dash, it parses one *complete command* (a line's list, with every line a
compound command spans) at a time, so the executor can run line 1 of a script
before line 2 is parsed (`echo a<newline>fi` prints `a`, then the error), and
an interactive shell learns from `Incomplete()` that it must read more.

Still a library with unit tests: nothing runs, `hsh` is not registered.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (written by
hsh--lexer), `commands/hsh/HshWord.h`, `HshLexer.h`, `HshError.h`, the root
`CLAUDE.md` (Automatic Development Rules), and the dash manual's "Commands",
"Simple Commands", "Redirections", "Pipelines", "Lists", "Flow-Control
Constructs" and "Functions" sections
(https://man7.org/linux/man-pages/man1/dash.1.html). Every message below was
checked against dash 0.5.12; check more with `dash -c '...'`.

What hsh--lexer provides (namespace `Haisos::Hsh`, `src/components/BuiltinCommands/commands/hsh/`):

- `ShellError(message, line, incomplete)` with `Line()`, `Incomplete()`;
  `FormatShellError`.
- `Word { parts, source, line }`, `WordPart`, `WordPartKind`, `ParameterOp`,
  `HereDocument`, `IsValidShellName`, `LiteralText(const Word&)`, `DescribeWord`.
- `Lexer(std::string source, LexerOptions{firstLine, interactive})`,
  `Token Lexer::Next()` (throws `ShellError`), `int Lexer::Line()`;
  `Token { kind, line, word, ioNumber, hereDoc, begin, end }` (byte offsets in the source), `HereDocument::sourceBegin`/`sourceEnd` (the body region's offsets), `TokenKind`,
  `OperatorText`, `IsRedirectionOperator`, `ReservedWord`, `AsReservedWord`,
  `ReservedWordText`.
- Heredoc bodies are filled by the lexer at the newline ending their line, and
  `token.hereDoc` is set on the delimiter Word.
- The `Hsh.unittests` executable (`tests/unit/components/Hsh.unittests/`),
  filter `Hsh`.

hsh--expansion, hsh--executor and hsh--control-flow are planned on top of the
AST below: name and shape everything exactly as written.

## Changes

### `commands/hsh/HshAst.h` and `HshAst.cpp` (new)

```cpp
enum class RedirectionKind {
    Input,          // [n]<word    default fd 0
    Output,         // [n]>word    default 1
    OutputClobber,  // [n]>|word   default 1
    Append,         // [n]>>word   default 1
    ReadWrite,      // [n]<>word   default 0
    DupInput,       // [n]<&word   default 0; word: a digit, or "-" to close
    DupOutput,      // [n]>&word   default 1; word: a digit, or "-" to close
    HereDoc,        // [n]<<word, [n]<<-word   default 0
    HereString,     // [n]<<<word  default 0 (bash): the word and a newline as input
    OutputAndError, // &>word      (bash) stdout and stderr to the file; fd is 1
};

struct Redirection {
    RedirectionKind kind = RedirectionKind::Input;
    int fd = 0;                             // the IO number given, else the kind's default
    Word target;                            // the file, the dup word, the here-string, or the heredoc's delimiter word
    std::shared_ptr<HereDocument> hereDoc;  // HereDoc only (shared with the lexer, which filled it)
    int line = 0;
};

struct Assignment {
    std::string name;  // a valid name
    Word value;        // what follows "name=" (may have no parts); value.source is the text after '='
};

enum class CommandKind { Simple, BraceGroup, Subshell, If, While, Until, For, Case, FunctionDefinition };

struct CommandList;  // below

struct Command {
    explicit Command(CommandKind kind) : kind(kind) {}
    virtual ~Command() = default;
    CommandKind kind;
    int line = 0;  // the line of the command's first token
    // Simple: every redirection, in the order written (before, between or
    // after the words). Compound commands: those written after it
    // (`{ ...; } >f`). FunctionDefinition: always empty -- redirections after
    // the body belong to the body and apply on every call.
    std::vector<Redirection> redirections;
};
using CommandPtr = std::shared_ptr<Command>;

struct Pipeline {
    bool negated = false;              // ! in front
    std::vector<CommandPtr> commands;  // one or more, joined by |
    int line = 0;
};

enum class AndOrOperator { And, Or };  // &&, ||

struct AndOrList {
    std::vector<Pipeline> pipelines;        // one or more
    std::vector<AndOrOperator> operators;   // operators[i] joins pipelines[i] and pipelines[i + 1]
};

struct ListItem {
    AndOrList andOr;
    bool background = false;  // ended by &
    // The and-or list exactly as written, re-parsable on its own to the same
    // command (a background item needing the shell is run as `hsh -c
    // <sourceText>`): see "Source text" below.
    std::string sourceText;
};

struct CommandList {
    std::vector<ListItem> items;  // may be empty (a case item's body)
};

struct SimpleCommand : Command {
    SimpleCommand() : Command(CommandKind::Simple) {}
    std::vector<Assignment> assignments;  // the leading name=value words
    std::vector<Word> words;              // the command name and its arguments; may be empty (`x=1`, `>f`)
};
struct BraceGroup : Command { BraceGroup() : Command(CommandKind::BraceGroup) {} CommandList body; };
struct Subshell : Command { Subshell() : Command(CommandKind::Subshell) {} CommandList body; };

struct IfBranch { CommandList condition; CommandList body; };
struct IfCommand : Command {
    IfCommand() : Command(CommandKind::If) {}
    std::vector<IfBranch> branches;          // the if, then each elif
    std::optional<CommandList> elseBody;     // set when there is an else
};

// while and until (kind While or Until): repeat body while condition succeeds / fails.
struct LoopCommand : Command {
    explicit LoopCommand(CommandKind kind) : Command(kind) {}
    CommandList condition;
    CommandList body;
};

struct ForCommand : Command {
    ForCommand() : Command(CommandKind::For) {}
    std::string variable;
    bool hasIn = false;       // `for x in ...`; without `in` the loop runs over "$@"
    std::vector<Word> words;  // the words after `in` (possibly none)
    CommandList body;
};

struct CaseItem {
    std::vector<Word> patterns;  // pattern | pattern ...
    CommandList body;            // possibly empty
};
struct CaseCommand : Command {
    CaseCommand() : Command(CommandKind::Case) {}
    Word subject;
    std::vector<CaseItem> items;
};

struct FunctionDefinition : Command {
    FunctionDefinition() : Command(CommandKind::FunctionDefinition) {}
    std::string name;
    // Any command, as dash allows (`f() echo hi` too), with its redirections.
    // Shared so the function table can keep it after the tree is gone.
    CommandPtr body;
    // The whole definition exactly as written, `name() compound-command
    // [redirections]`, re-parsable on its own: see "Source text" below.
    std::string sourceText;
};

// The dump the tests compare against; format in "AST dump" below.
std::string DumpCommandList(const CommandList& list);
std::string DumpCommand(const Command& command);
```

The executor tells kinds apart with `command.kind` and a `static_cast` to the
struct. These are plain data types, not implementations of interfaces in
`interfaces/`, so the root `CLAUDE.md`'s private-constructor/`Create()` rule
does not apply; commands are held by `shared_ptr` (`CommandPtr`) anyway.

#### AST dump (byte for byte)

```
List      items joined by "; "; a background item is followed by " &"; an empty list is ""
AndOr     pipelines joined by " && " / " || "
Pipeline  ["! "] commands joined by " | "
Simple    "[" + the elements joined by " " + "]": first each assignment as name=<value.source>,
          then each word's source, then each redirection
Redir     <fd><op><target.source>, op one of < > >| >> <> <& >& << <<- <<<;
          OutputAndError is "&>" + <target.source> (no fd)
Brace     "{ " List " }"
Subshell  "( " List " )"
If        "if " List " then " List { " elif " List " then " List } [ " else " List ] " fi"
While     "while " List " do " List " done"     (until: "until ...")
For       "for " name [ " in" { " " word source } ] " do " List " done"
Case      "case " subject source " in" { " " item } " esac"
          item: patterns' sources joined by "|", then ")", then " " List if the body is
          not empty, then " ;;"
Function  name "() " + the body's dump
```

A compound command's redirections follow its dump, each preceded by one
space. Examples: `a | b && ! c &` is `[a] | [b] && ! [c] &`;
`x=1 echo $x >f 2>&1` is `[x=1 echo $x 1>f 2>&1]`;
`f() { echo; } >o` is `f() { [echo] } 1>o`;
`case $x in a|b) echo;; *) ;; esac` is `case $x in a|b) [echo] ;; *) ;; esac`.

### `commands/hsh/HshParser.h` and `HshParser.cpp` (new)

```cpp
struct ParserOptions {
    int firstLine = 1;
    bool interactive = false;   // passed to the lexer (see LexerOptions)
    // How the end of the input is named in messages: "end of file" for a
    // script or -c, "\")\"" when checking the text of a $(...) (see below).
    std::string endOfInputName = "end of file";
};

struct ParseResult {
    enum class Status { Command, EndOfInput, Error };
    Status status = Status::EndOfInput;
    CommandList commands;       // Command: the next complete command
    std::string errorMessage;   // Error: what follows "hsh: <line>: ", e.g. "Syntax error: \"fi\" unexpected"
    int errorLine = 0;          // Error
    bool incomplete = false;    // Error: the input ended where more was needed
};

class Parser {
public:
    explicit Parser(std::string source, ParserOptions options = {});
    // Parses the next complete command: an and-or list, or several joined by
    // ; or &, up to the newline that ends it (heredoc bodies included) or the
    // end of input -- what dash reads before running anything. Empty lines and
    // comment-only lines are skipped. EndOfInput once the source is used up;
    // after an Error, every later call returns the same Error.
    ParseResult ParseNext();
};

// Every complete command of |source| in one list (Status Command, an empty
// list for an empty source), or the first error. For `eval`, `.`, the text of
// a command substitution, and the tests.
ParseResult ParseProgram(const std::string& source, ParserOptions options = {});
```

`ShellError` thrown by the lexer or by the parser itself is caught in
`ParseNext` and turned into the Error result (`what()`, `Line()`,
`Incomplete()`).

#### Grammar and where reserved words count

The parser keeps a one-token lookahead over `Lexer::Next()`. A Word token is
taken as a reserved word (`AsReservedWord`) only where one may stand: at the
start of a command (the first token `command` reads, which includes the start
of every list item and the token after `!`), where `in` may follow
`case <word>` or `for <name>`, where `do` may follow a `for` header, and
where a case pattern list may start or end (`esac`). Everywhere else --
arguments, words after the command name, `for` word lists, patterns after
`|`, the case subject -- every Word is a word (`echo if then`, `for x in do done`).

Newlines are skipped (the *linebreak* of POSIX): after `&&`, `||`, `|`; at the
start of every item of a compound list; before `in`/`do` in `for`; before
`in` in `case`; before each case pattern and before `esac`; between `f()` and
the function body.

- **complete command** (`ParseNext`): skip Newline tokens; at EndOfInput
  return EndOfInput. Then items: an *and-or*, optionally `&` (background) or
  `;`; after `;`/`&`, a Newline or EndOfInput ends the command, anything else
  starts the next item; after an and-or with no separator, a Newline or
  EndOfInput ends it and any other token is an error `<it> unexpected`
  (`(echo) foo` is `word unexpected`). An item's first token that cannot start
  a command (`fi`, `;`, `)` ...) is `<it> unexpected` (`echo a; fi` is
  `"fi" unexpected`).
- **compound list** (the body of `{ }`, `( )`, `if`, loops, case items):
  items separated by `;`, `&` or newlines. The first item is mandatory -- a
  token that cannot start a command is `<it> unexpected` (`{ }` is
  `"}" unexpected`, `if; then` is `";" unexpected`) -- except that end of
  input there ends the list empty (the caller then complains with its
  expectation: `until false` at the end is
  `end of file unexpected (expecting "do")`). After the first item, the list
  ends before EndOfInput, `)`, `;;`, or (in reserved-word position) `then`
  `else` `elif` `fi` `do` `done` `esac` `}`; a case item's body may also be
  empty (`a) ;;`). After an and-or not followed by a separator, the list ends
  at whatever comes; the caller checks it.
- **and-or**: pipeline { (`&&`|`||`) linebreak pipeline }.
- **pipeline**: [`!`] command { `|` linebreak command }. A second `!`
  (`! ! true`) is `"!" unexpected`, as is `!` after `|`.
- **command**, by its first token: `if`, `while`, `until`, `for`, `case`,
  `{`, `(` start compound commands; a Word or a redirection operator/IoNumber
  starts a simple command; anything else is `<it> unexpected`. A compound
  command is followed by any number of redirections, stored on it.
- **if**: `if` list `then` list { `elif` list `then` list } [ `else` list ]
  `fi`. A missing `then`/`fi`: `<it> unexpected (expecting "then")` /
  `(expecting "fi")`.
- **while / until**: list `do` list `done`.
- **for**: the next token must be a Word whose `LiteralText` is a valid name,
  else `Syntax error: Bad for loop variable` (`for 1 in a`, a bare `for`).
  Then linebreak; `in` followed by Words up to a `;` or Newline (any other
  token: `<it> unexpected`), `hasIn` true; or, without `in`, an optional `;`.
  Then linebreak, `do` (else `<it> unexpected (expecting "do")`: `for x y` is
  `word unexpected (expecting "do")`), list, `done`.
- **case**: a Word subject (else `<it> unexpected (expecting word)`),
  linebreak, the word `in` (else `<it> unexpected (expecting "in")`), then
  items until `esac`: linebreak; `esac` ends; else an optional `(`, then
  Words joined by `|` (a non-Word where a pattern must be:
  `<it> unexpected (expecting ")")`), then `)` (else
  `<it> unexpected (expecting ")")`: `case x in a b)` is
  `word unexpected (expecting ")")`), the body (a compound list that may be
  empty), then linebreak and `;;` (next item) or `esac` (end), else
  `<it> unexpected (expecting ";;")`. `case x in esac` is valid, with no items.
- **brace group / subshell**: `{` list `}` / `(` list `)`; missing closer:
  `<it> unexpected (expecting "}")` / `(expecting ")")`.
- **simple command**: a loop over Words and redirections. While no word has
  been taken yet, a Word whose first part is a Literal starting with
  `<valid name>=` is an assignment (name, and the rest of the word as the
  value: the Literal's text after `=` if any, then the remaining parts; the
  value's `source` is the source after the first `=`); every other Word is a
  word, and from then on even `a=b` words are words. A redirection operator
  (optionally preceded by an IoNumber) takes the next token, which must be a
  Word (else `<it> unexpected`: `echo >` at the end is
  `end of file unexpected`, `echo > > x` is `redirection unexpected`). The
  loop ends at any other token.
- **function definition**: inside the simple-command loop, a `(` that comes
  when exactly one word and no assignment or redirection has been taken makes
  a function: `)` must follow (else `<it> unexpected (expecting ")")`), the
  word's `LiteralText` must be a valid name (else
  `Syntax error: Bad function name`: `"f"() {...}`, `f-g() {...}`), then
  linebreak and any command (compound or simple) as the body, which takes its
  own trailing redirections. A `(` anywhere else in a simple command ends it
  (`echo a (b)` is `"(" unexpected`).
- **redirections**: kind from the operator (`<` Input, `>` Output, `>|`
  OutputClobber, `>>` Append, `<>` ReadWrite, `<&` DupInput, `>&` DupOutput,
  `<<`/`<<-` HereDoc, `<<<` HereString, `&>` OutputAndError); `fd` from the
  IoNumber or the default; `hereDoc` from the delimiter token. For DupInput
  and DupOutput whose target has no expansion parts (only Literal/Quoted, at
  any depth), its text after quote removal must be one digit or `-`, else
  `Syntax error: Bad fd number` (`>&a`, `>&12`, `>&1x`; `>&"1"` is fine); a
  target with expansions is checked by the executor after expansion, with the
  same message.
- **source text**: the parser keeps the `end` offset of the last token it
  consumed and, while an item or a function definition is being parsed, the
  `HereDocument`s of the delimiter tokens it consumes. `ListItem::sourceText`
  is the source from the `begin` of the item's first token to the `end` of
  its last token (so without the `;` or `&` after it, and without a comment
  or blanks after it); `FunctionDefinition::sourceText` likewise from the
  name's `begin` to the `end` of the body's last token (its redirections
  included). Heredoc bodies inside that range are already in it; for every
  collected heredoc whose `sourceBegin` is at or after the range's end (its
  body follows the line the text ends on), append `"\n"` once, then each such
  heredoc's region `source[sourceBegin, sourceEnd)` in order (they are
  consecutive in the source). Line continuations inside the range stay as
  written. So `cat <<E && echo b; echo c` + body gives the first item the text
  `cat <<E && echo b` + `"\n"` + `body<newline>E<newline>`. The text of an item
  inside a compound list (`{ a & }`) is filled the same way. Parsing a
  sourceText again with `ParseProgram` gives one item whose
  `DumpCommandList` equals the original item's (and, for a function, one
  FunctionDefinition with the same dump).
- **command substitutions**: every `CommandSubstitution` part in every word
  the parser keeps (at any depth: inside double quotes, operands, arithmetic,
  heredoc bodies) is checked by parsing its text with `ParseProgram(text,
  {part.line, false, "\")\""})` for `$(...)` and `{part.line, false, "end of file"}`
  for backquotes; its error, if any, is the error of the whole parse (dash
  checks them at parse time too). The trees are not kept: the executor parses
  the text again when it runs it.

#### Error messages (byte for byte)

`Syntax error: <found> unexpected` or
`Syntax error: <found> unexpected (expecting <expected>)`, where `<found>` is:

- `end of file` at EndOfInput -- `options.endOfInputName` -- and then the
  error is `incomplete`;
- `newline` for a Newline;
- `redirection` for a redirection operator or an IoNumber;
- `"<text>"` for `;` `;;` `&` `&&` `|` `||` `(` `)`;
- `"<word>"` for a Word taken as a reserved word in a reserved-word position
  (`"fi"`, `"}"`, `"!"`), `word` for every other Word;

and `<expected>` is `"then"`, `"fi"`, `"do"`, `"done"`, `"in"`, `"esac"`,
`")"`, `"}"`, `";;"` or `word`. Plus the three fixed ones:
`Syntax error: Bad for loop variable`, `Syntax error: Bad function name`,
`Syntax error: Bad fd number`. The line is the line of the token found
(for end of input, `Lexer::Line()`).

Known deviation (document it in the hsh CLAUDE.md): an error inside `$(...)`
is reported from parsing its text alone, so `echo $(if)` says
`")" unexpected (expecting "then")` where dash says `")" unexpected`.

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `commands/hsh/HshAst.cpp` and `commands/hsh/HshParser.cpp` to the
`BuiltinCommands` sources.

## Tests

`tests/unit/components/Hsh.unittests/HshParserTest.cpp` (new); add it to
`add_executable(Hsh.unittests ...)` in that directory's `CMakeLists.txt`.
Most tests are tables of {source, expected `DumpCommandList` of
`ParseProgram(source)`} or {source, expected message, line, incomplete}:

- `HshParserTest.SimpleCommands`: words, assignments before words, `a=b`
  after the command name as a word, `x=1` alone, `>f` alone,
  redirections before/between/after words, every redirection kind with and
  without an IO number, `2>&1`, `>&-`, `&>f`, `<<<w`.
- `HshParserTest.PipelinesAndLists`: `a | b | c`, `! a | b`, `a && b || c`,
  `a & b; c`, `a;`, newlines after `&&`/`||`/`|`, `a &` (background).
- `HshParserTest.CompoundCommands`: brace group, subshell, nested; if with
  elif/else across lines; while; until; for with `in`, with an empty `in`,
  without `in` (`for x; do`, `for x<newline>do`); case with `(a)`, `a|b)`,
  empty bodies, last item without `;;`, `case x in esac`, `case in in in) ...`;
  redirections after compound commands.
- `HshParserTest.Functions`: `f() { echo; }`, `f()<newline>{ ...; }`,
  `f() echo hi`, `f() ( echo )`, `f() { :; } >o` (redirection on the body),
  the FunctionDefinition's `name` and `body` kind.
- `HshParserTest.ReservedWordPositions`: `echo if then fi`, `for x in do done; do :; done`,
  `{echo a; }` (an error: `"}" unexpected`), `if true then echo; fi`
  (`"fi" unexpected (expecting "then")`).
- `HshParserTest.HereDocuments`: `cat <<E` with a body then another command
  on the next line; the Redirection's `hereDoc` filled; two heredocs on one line.
- `HshParserTest.SyntaxErrors`: each of these, byte for byte with line and
  incomplete flag (all checked against dash):
  `if true; fi` -> `Syntax error: "fi" unexpected (expecting "then")`;
  `while true; done` -> `"done" unexpected (expecting "do")`;
  `until false; do echo` -> `end of file unexpected (expecting "done")` (incomplete);
  `{ echo; ` -> `end of file unexpected (expecting "}")`;
  `( echo` -> `end of file unexpected (expecting ")")`;
  `(<newline>)` -> line 2, `")" unexpected`;
  `echo |<newline>|` -> line 2, `"|" unexpected`;
  `echo ; ;` -> `";" unexpected`; `echo ;;` -> `";;" unexpected`;
  `echo a & & echo b` -> `"&" unexpected`; `echo a &; echo` -> `";" unexpected`;
  `| x` -> `"|" unexpected`; `echo )` -> `")" unexpected`;
  `echo &&`, `echo |`, `echo >`, `cat <<` -> `end of file unexpected` (incomplete);
  `echo > ;` -> `";" unexpected`; `echo > > x` -> `redirection unexpected`;
  `then`, `esac`, `{ }`, `( )`, `! ! true` -> `"then"`/`"esac"`/`"}"`/`")"`/`"!" unexpected`;
  `case x foo` -> `word unexpected (expecting "in")`;
  `case x in a b) ;; esac` -> `word unexpected (expecting ")")`;
  `case x in a) ;; b` -> `end of file unexpected (expecting ")")`;
  `case x in x) echo y esac` -> `end of file unexpected (expecting ";;")`;
  `for x in a b c ; echo; done` and `for x y` -> `word unexpected (expecting "do")`;
  `for 1 in a; do :; done`, `for` -> `Bad for loop variable`;
  `f-g() { :; }`, `"f"() { :; }` -> `Bad function name`;
  `f()` -> `end of file unexpected`; `f() ;` -> `";" unexpected`;
  `echo 2>&a`, `echo 1>&12`, `echo >&1x` -> `Bad fd number`;
  `if true; then echo; fi foo`, `(echo) foo` -> `word unexpected`;
  `echo "abc` -> the lexer's `Unterminated quoted string` coming through.
- `HshParserTest.SourceTextRoundTrips`: for each source, every ListItem's
  `sourceText` (top level and inside compound commands) and every
  FunctionDefinition's `sourceText` is checked: its exact expected string
  for a few rows, and for all rows that `ParseProgram(sourceText)` gives one
  item with the same `DumpCommandList` (same dump of the function). Rows:
  `a | b && c &`, `x=1 echo "a b" 2>&1; d` (`x=1 echo "a b" 2>&1`, no `;`),
  `echo a \<newline>b` (continuation kept), `echo a # c` (comment dropped),
  `{ sleep 1; echo $(echo ")"); } >o &`, `if a; then b; fi & c`,
  `cat <<E && echo b; echo c` + `<newline>x $y<newline>E<newline>` (body
  appended after `"\n"`), `cat <<A & cat <<B` + both bodies (each item gets
  only its own), `cat <<-E` with tab-indented body, a heredoc in the middle
  of a multi-line `while` (already inside the range),
  `f() { echo f; } >o` and `g()<newline>(echo g)` (function texts),
  `{ a & b; }` (inner item `a`).
- `HshParserTest.CommandSubstitutionChecked`: `echo $(if)` is an error
  (the deviation's wording), `echo $(echo ok)` and
  `x=$(case a in a) echo;; esac)` are fine, `echo "$(fi)"` is an error.
- `HshParserTest.ParseNextOneLineAtATime`: `echo a<newline>fi` gives a
  Command (`[echo a]`), then an Error at line 2; `<newline><newline>echo b`
  skips the empty lines; `if true<newline>then echo t<newline>fi<newline>echo c`
  gives the whole `if` as the first command and `echo c` as the second;
  after an Error, `ParseNext` repeats it.
- `HshParserTest.LinesAndFirstLine`: `Command::line` of each command of a
  multi-line script, `ParserOptions::firstLine` shifting lines and error
  lines.
- `HshParserTest.InteractiveIncomplete`: with `interactive`, `cat <<E<newline>x<newline>`,
  `echo a \<newline>`, `if true<newline>`, `echo "a<newline>` are incomplete;
  `echo a<newline>` is complete; `fi<newline>` is an error, not incomplete.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: add the AST
(one line per node type), `ParseNext` versus `ParseProgram` and when each is
used (scripts, `-c` and interactive input one complete command at a time;
`eval`, `.` and command substitutions whole), the reserved-word positions,
and the `$(...)` error-wording deviation.

## Acceptance

- [ ] Every type and function above exists with exactly these names, fields
      and signatures, in namespace `Haisos::Hsh`.
- [ ] `ParseNext` returns one complete command at a time; a syntax error on a
      later line does not prevent the earlier commands from being returned.
- [ ] Reserved words are recognized only in the positions listed.
- [ ] Every error in the test list matches dash byte for byte (except the
      documented `$(...)` deviation), with its line and incomplete flag.
- [ ] `DumpCommandList` follows the format byte for byte.
- [ ] Every `ListItem::sourceText` and `FunctionDefinition::sourceText` is
      cut from the source by token offsets (never rebuilt from the AST) and
      re-parses to the same dump.
- [ ] Builds on Linux; `Hsh.unittests` passes; all other unit tests still pass.

## Out of scope

- Expanding words (hsh--expansion); running anything, `hsh` as a builtin,
  its options (hsh--executor); control flow and functions at run time
  (hsh--control-flow); the interactive loop (hsh--interactive).
- Aliases, `time`, `function f`, `[[ ]]`, `select`, `;&`/`;;&`, arithmetic
  commands `(( ))` -- none are dash's.
