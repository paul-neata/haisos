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
  line (all pending heredocs, in order). Each token carries `begin`/`end`
  byte offsets into the source so the parser can cut `ListItem::sourceText`
  and `FunctionDefinition::sourceText` out of it. `AsReservedWord` spells out
  which reserved word a word token spells; whether it IS one is the parser's
  business.
- (later tasks: the parser/AST, expansion, the executor that registers the
  `hsh` builtin, the interactive loop.)

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
