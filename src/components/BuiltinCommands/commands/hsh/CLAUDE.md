# hsh

hsh is the Haisos SHell: a reimplementation of POSIX `sh` modelled on dash
(Debian/Ubuntu's `/bin/sh`), to become a builtin command. All of it lives in
namespace `Haisos::Hsh`, in this directory, as plain portable C++17 (it is
built for Linux, Windows/MSVC and WASM: no POSIX headers, no `<regex>`).

The pipeline is lexer -> parser -> expansion -> executor; each stage is a
task of the hsh rock and adds its files here:

- `HshError.h` - `ShellError`, the one error type every stage throws: the
  message is what dash prints after `hsh: <line>: ` (see
  `FormatShellError`), and `Incomplete()` tells an interactive shell that the
  error happened only because the input ended where more was needed -- it
  shows PS2, reads another line and tries again.
- `HshWord.h/.cpp` - the word model: a `Word` is a tree of `WordPart`s that
  keeps its quoting: `Literal` (unquoted text), `Quoted` (text that was
  quoted: `'...'`, `\c`, the plain text inside `"..."` or a heredoc body),
  `DoubleQuoted` (a `"..."` group, its contents in `parts`), `Parameter`
  (`$name` / `${...}`, with `op` and operand `parts`), `CommandSubstitution`
  (`$(...)` or `` `...` ``, the source in `text`) and `Arithmetic`
  (`$((...))`). Adjacent Literal parts are merged into one, and so are
  adjacent Quoted parts, at every level. `LiteralText(word)` gives the word's
  text when it is unquoted literal only -- how reserved words, for-loop
  variables and assignment prefixes are recognized. Also `HereDocument`, the
  record a `<<`/`<<-` redirection fills in.
- `HshLexer.h/.cpp` - the `Lexer`: reads a source string token by token (words
  with their parts, operators, IO numbers, newlines, end of input), counts
  lines, removes `\`-newline continuations everywhere but single quotes,
  comments and quoted-delimiter heredoc bodies, and attaches a `HereDocument`
  to the word after `<<`/`<<-`, its body read at the newline that ends the
  line (all pending heredocs, in order). Inside an *unquoted*-delimiter
  heredoc body a `\`-newline joins with the next line before the delimiter is
  compared, just as in the rest of the source (so the joined body is stored);
  a quoted delimiter's body matches raw lines, unchanged. Each token carries
  `begin`/`end`
  byte offsets into the source so the parser can cut `ListItem::sourceText`
  and `FunctionDefinition::sourceText` out of it. `AsReservedWord` spells out
  which reserved word a word token spells; whether it IS one is the parser's
  business.
- `HshAst.h/.cpp` - the syntax tree. `CommandList` holds items of an
  `AndOrList` (`Pipeline`s joined by `&&`/`||`; a `Pipeline` is `Command`s
  joined by `|`, with `negated` for a leading `!`) plus a `background` flag
  and the item's `sourceText` (see below). A `Command` is a `SimpleCommand`
  (assignments, words, redirections), `BraceGroup`, `Subshell`, `IfCommand`
  (branches + optional else), `LoopCommand` (while/until), `ForCommand`,
  `CaseCommand` (subject + pattern/body items) or `FunctionDefinition`
  (name, body command, and the definition's own `sourceText`). Every
  command carries the line of its first token and its trailing
  redirections. `DumpCommandList`/`DumpCommand` write a tree in the
  one-line format the tests compare against.
- `HshParser.h/.cpp` - the `Parser`, a recursive-descent parser with one
  token of lookahead over the lexer, and dash's error messages and line
  numbers (`Syntax error: <found> unexpected [(expecting <what>)]` plus the
  fixed `Bad for loop variable` / `Bad function name` / `Bad fd number`; a
  found Newline is reported on the line it ends into, as dash does).
  `Parser::ParseNext()` returns one *complete command* at a time -- the
  and-or lists of one line, up to its newline (heredoc bodies included) --
  so a script's line 1 runs before line 2 is parsed, and an interactive
  shell learns from `incomplete` that it must read more; after an error,
  every later call returns it again. `ParseProgram()` parses the whole
  source into one list for `eval`, `.`, the text of a command substitution,
  and the tests.
- `HshPattern.h/.cpp` - the one shell-pattern matcher (`MatchPattern`), to be
  shared by pathname expansion, `case` and `${x#p}`/`${x%p}` (`RemovePattern`):
  `*`, `?`, bracket expressions with `!` negation (never `^`: `[^a]` is the
  set of `^` and `a`, as dash), ranges and ASCII `[:class:]`, and `\c`
  escapes, over bytes. The classic two-index loop with backtracking to the
  last `*` -- no recursion, no regex, no `fnmatch`.
- `HshGlob.h/.cpp` - pathname expansion (`ExpandPathname`): walks the
  pattern's components, reading directories only through `IPathnameSource`
  (the executor implements it over the process's `IFileIO`, so globbing goes
  through `ICurrentProcess` like every other file access; nothing here
  touches `IFileIO` or the host). Results sorted byte by byte; names starting
  with `.` only from a component with a literal leading `.` (`.*` also finds
  `.` and `..`); nothing found is an empty result, and the caller then keeps
  the word as it was.
- `HshArithmetic.h/.cpp` - `$((...))` evaluation (`EvaluateArithmetic`), as
  dash: `intmax_t` with wrapping two's-complement arithmetic (computed in
  `uintmax_t` -- no UB), shift counts modulo 64, `INTMAX_MIN / -1` giving
  `INTMAX_MIN` where dash crashes, dash's operators and precedence only (no
  `++`, `**` or `,`), assignments stored decimally through
  `IArithmeticVariables`, short-circuit of `&&`, `||` and `?:` (the branch not
  taken is parsed but never looked up, assigned or divided), and dash's error
  messages byte for byte, thrown as `ShellError`.
- (later tasks: expansion, the executor that registers the
  `hsh` builtin, the interactive loop.)

## Reserved words and source text

A word token is taken as a reserved word only where one may stand: at the
start of a command (including after `!` and at the start of every list
item), where `in` may follow `case <word>` or `for <name>`, where `do` may
follow a `for` header, and where a case pattern list may start or end
(`esac`). Everywhere else (`echo if then`, `for x in do done`, patterns
after `|`, the case subject) every word is a word.

`ListItem::sourceText` and `FunctionDefinition::sourceText` are cut from the
source by token offsets, never rebuilt from the tree: from the first token's
`begin` to the last token's `end` (so comments, blanks and the `;`/`&` that
follows stay out), plus, when a heredoc's body follows that range, one
newline and each such body region in order. Parsing a sourceText again gives
the same command (checked by the tests). A background item needing the shell
at run time is run as `hsh -c <sourceText>`.

While a word is kept in the tree, every `CommandSubstitution` part in it (at
any depth, heredoc bodies included) is checked at parse time by parsing its
text with `ParseProgram`; an error there is the error of the whole parse, as
dash checks substitutions when it parses them.

## How the end of a `$(...)` is found

Counting parentheses is not enough -- `$(case a in a) echo m;; esac)` has a
`)` that ends a case pattern. The lexer instead starts a sub-lexer over the
same source (shared string plus a position) right after `$(` and reads tokens,
so quotes, comments, nested substitutions and heredocs inside follow the
ordinary rules, until the `)` that closes the substitution. The scan keeps a
stack of open constructs: a word `case` in command position pushes a case
frame that tracks `Subject`/`ExpectIn`/`Pattern`/`Body` (a `)` in `Pattern`
moves to `Body`, `;;` back to `Pattern`, the word `esac` pops); a `(` pushes
a paren frame and a `)` pops one; a `)` with an empty stack is the end.
Heredocs still waiting at the closing `)` get an empty body, as dash. The
part's `text` is the source between `$(` and `)` exactly, heredoc bodies
included; nothing is parsed here.

## The interactive protocol

(hsh--interactive implements it.) Keep a buffer of the lines typed since the
last complete command; after each line, lex/parse the whole buffer again from
the start with `LexerOptions::interactive` set and `firstLine` the number of
that buffer's first line; on an error with `Incomplete()` show PS2 and read
another line; otherwise run (or report) and empty the buffer. Re-reading the
buffer each time keeps the lexer free of suspended state. With `interactive`,
two endings a script accepts become incomplete-input errors: a heredoc whose
delimiter line has not come yet, and a source ending in a backslash-newline.

## Documented deviations from dash

- `&>` and `<<<` are bash's operators (`echo a &>f` redirects stdout and
  stderr to `f`; in dash it would be `echo a &` then `>f`).
- `${x:}` is a `Bad substitution` (`ParameterOp::Bad`, failing at expansion)
  rather than dash's syntax error `Missing '}'`.
- `$'...'` is not ANSI-C quoting (as dash): a plain `$` followed by the quote.
- An error inside a `$(...)` is reported from parsing its text alone, so it
  names `")"` (the substitution's end) where dash names what its own parser
  happened to see: `echo $(if)` says `")" unexpected (expecting "then")`
  where dash says `")" unexpected`.
