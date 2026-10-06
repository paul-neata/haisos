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
  messages byte for byte, thrown as `ShellError`. A name directly before an
  assignment operator is not read there: a plain `=` never reads the old value
  and a compound op reads it only after the right-hand side has been
  evaluated, as dash.
- `HshVariables.h/.cpp` - the shell's state: `ShellVariables` (named
  variables with exported and read-only flags, `ImportFrom` seeding from an
  `IEnvironment`), `ShellOptions` (dash's options, `OptionLetters` for `$-`)
  and `ShellState` (options, `$0`, `$1...`, `$?`, `$$`, `$!`) -- the state
  the executor owns and a subshell copies.
- `HshExpansion.h/.cpp` - word expansion (`Expander`): turns the parser's
  `Word`s into strings, in dash's order (see "Expansion" below). Still a
  library: reading directories and running command substitutions go through
  `IExpansionHost`, which the executor implements over the process's
  `IFileIO` and a subshell.
- `HshInvocation.h/.cpp` - the invocation: `ShellOptionTable` (dash's options
  in `set -o` order, each with its letter, its `set -o` name and the
  `ShellOptions` field it drives or null when hsh does not act on it) and
  `ParseInvocation`, dash's `procargs`: options up to the first operand
  (`+x` turns an option off, `-o` takes the next argument, `--`/`-` end the
  options), then what to run -- a `-c` string, a script file, or standard
  input -- with `$0` and `$1...`. Untreated options come back in `notTreated`
  to be reported; invocation mistakes in dash's own words ("Illegal option
  -y", `-c requires an argument`, status 2).
- `HshShell.h/.cpp` - the executor (`Shell`), running one shell inside the
  builtin's process: see "Running" below.
- `HshRedirection.h/.cpp` - `RedirectionScope`, applying a command's
  redirections to the shell's own descriptor table and undoing them, and
  `PlaceDescriptor`: see "Redirections" below.
- `HshDescriptors.h/.cpp` - `ClosedDescriptor`, what a child gets in place of
  a closed slot.
- `HshBuiltins.h/.cpp` - the shell's own builtins (`:`, `exec`, `exit`,
  `false`, `true`): a sorted table of name, special/regular, function; later
  tasks add rows (and files of their own for the bigger ones).
- `HshBuiltinExec.cpp` - `exec`: with no command its redirections stay; with
  one, the command runs in the shell's place.
- `Hsh.cpp` - the `hsh` builtin itself: the option table (every dash option,
  treated or marked for the not-treated report), the dash-based `--help`
  (`BuiltinHelp::basedOn`), `ParseInvocation`, and `Shell` on the context.
- (later tasks: pipelines, the rest of the shell builtins,
  control flow, the interactive loop.)

## AST dump

`DumpCommandList`/`DumpCommand` (`HshAst.cpp`) write a tree on one line, for
the tests, which compare against it byte for byte. A command list is its
items joined by `; `, a background item followed by ` &`. An and-or list
joins its pipelines with ` && `/` || `; a pipeline is its commands joined by
` | `, prefixed `! ` when negated. A simple command is `[` then its
assignments (`name=<value source>`), words and redirections space-separated,
then `]`; a redirection is `<fd><op><target as written>` (no fd for `&>`; op
from the kind, `<<`/`<<-` by the heredoc's `stripTabs`). Compound commands
spell themselves out: `{ <list> }`, `( <list> )`, `if <list> then <list>
[elif ... ] [else <list> ] fi`, `while|until <list> do <list> done`,
`for v[ in <word>...] do <list> done`, `case <subject> in p1|p2)
[<list> ] ;; ... esac` (the body omitted when empty), `name() <body>` --
with words written from `Word::source`, exactly as typed -- and a
non-simple command's redirections appended after it with a space.

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

## Expansion

The `Expander` (`HshExpansion.h`) turns a `Word` into strings. One object is
built per shell state: the executor owns the one `ShellState` of its shell
(variables, options, `$0`, `$1...`, `$?`, `$$`, `$!`); a subshell works on a
copy of it. All file access and command running go through `IExpansionHost`,
never around it: `ReadDirectory`/`Exists` for globbing (the executor answers
them with the process's `IFileIO` -- `ICurrentProcess::IO()`, never a
filesystem of its own) and `RunCommandSubstitution`, whose text the executor
parses (`ParseProgram`) and runs in a subshell with its stdout on a pipe.
Expansion itself never sees an `IFileIO`, an `IFileSystem` or the host.

`ShellVariables` (`HshVariables.h`) is the variable store: names are valid
shell names (`IsValidShellName`), each entry has a value-or-unset plus
exported and read-only flags (`export`/`readonly` of an unset name set the
flag; `Set`/`Unset` of a read-only one fail). `ImportFrom` seeds it from the
environment the shell starts with: every variable with a valid shell name,
set and exported. `ExportedVariables` is what a command the shell starts
gets as its environment. `OptionLetters` spells `$-` in dash's order
"uaCvxsnife".

There is one `Expander` method per expansion context:

- `ExpandWords`/`ExpandWord` -- command words and `for`-loop words: every
  expansion, then field splitting by `IFS`, pathname expansion (unless
  `noglob`), quote removal. Zero or more fields per word.
- `ExpandAssignmentValue` -- an assignment's value (`x=...`, and the operand
  of `${x:=...}`): tilde at the start and after every unquoted `:`, the other
  expansions, quote removal; no splitting, no globbing. In a `${...}`
  operand a tilde at its start expands unless the `${...}` is in double
  quotes (`echo ${x:=~/b}` and `${q:+~}` expand, `"${x:-~}"` does not); in an
  assignment's value the after-`:` rule holds in the operands too, except in
  a `:=`/`=` operand (`z=${x:=a:~}` assigns `a:~`, as dash).
- `ExpandToString` -- a redirection target, a here-string, the `case`
  subject: one string, no splitting, no globbing.
- A `#`/`##`/`%`/`%%` operand is lexed as an unquoted word even inside
  double quotes or a heredoc body, as dash does: `"${x%/*}"` is a pattern,
  `"${x%"$y"}"` is literal.
- `ExpandPattern` -- a `case` pattern: as `ExpandToString`, but every quoted
  character comes out escaped (`EscapeForPattern`) so `MatchPattern` takes it
  literally, while unquoted `* ? [` and the results of unquoted expansions
  keep their meaning.
- `ExpandHereDocument` -- a heredoc body: `rawBody` as it is when the
  delimiter was quoted; else the body's parameters, command substitutions and
  arithmetic expanded (never a tilde there).

The order of expansions is dash's: tilde (only `~`/HOME; `~user` is left as
it is), parameter, command substitution and arithmetic in one recursive walk
(a command substitution's status becomes `$?`), then field splitting,
pathname expansion and quote removal. The walk builds **fields under
construction**: a field is a sequence of elements, each a character flagged
`quoted` (from a quoted context: no splitting, literal for globbing) or
`splittable` (from an unquoted expansion: subject to IFS splitting), or a
*quote mark* -- an element with no character recording that a quoted,
possibly empty string stood here (that is why `"$x"` with x empty still gives
one empty field). A *hard break* ends the current field and starts a new one
-- between the values of `"$@"` (`"x$@y"` with `a b` gives `xa`, `by`), and
unquoted keeps them separate even when `IFS` is empty, an empty one
disappearing. In string contexts hard breaks join with the first character of
`IFS` (space when unset, nothing when empty). Splitting follows dash's IFS
rules exactly (white-space IFS characters merge delimiters, non-white ones
keep empty fields); a field with pattern characters whose `ExpandPathname`
result is non-empty is replaced by the matches, otherwise kept as written.

Expansion errors throw `ShellError` with dash's messages byte for byte
(`Bad substitution`, `<name>: parameter not set`, `<name>: parameter not set
or null`, `<name>: <text>`, `<name>: bad variable name`, `<name>: is read
only`, the arithmetic messages), with line 0 -- the executor knows the
command's line.

## Running

The executor is `Shell` (`HshShell.h`), built by the `hsh` builtin (`Hsh.cpp`)
on the run's `BuiltinContext` and `Invocation`. It owns the one `ShellState`
(startup seeds `IFS`, `OPTIND`, `PPID`, `PS1`/`PS2`/`PS4`, `PATH` and `PWD` as
dash does) and the `Expander` over it, and is itself the `IExpansionHost`:
globbing reads directories through the process's `IFileIO`, everything else
likewise -- the only door out is `ICurrentProcess`, the OS asked for at each
`Process().OS()` call and never stored.

`Shell::Run` takes the invocation's source (the `-c` string; a script read
whole through `IO().OpenFile`; standard input read to its end), then loops on
`Parser::ParseNext`: one complete command parsed, then run, so a script's
line 1 runs before line 2 is parsed. A parse error is reported
(`<arg0>: <line>: Syntax error: ...`) with status 2; a `ShellError` (an
expansion error or a builtin's `Fail`) is fatal to a non-interactive shell --
reported, status 2, over; a `ShellExit` (the `exit` builtin) sets the status;
a `ShellStopped` (TriggerStop seen between commands or from
`ThrowIfStopRequested`, a `kIOInterrupted` read/write, or a broken pipe on the
shell's own output at the top level, which also calls `StopForBrokenPipe`)
stops every live child and returns 143 -- the process records 143, or 141 when
a broken pipe came first. Every other command's status only sets `$?`.

A simple command runs in dash's order: the words expand first
(`ExpandWords`); with no fields the prefix assignments apply to the shell
(status 0, or the last command substitution's); a name in the shell-builtin
table runs in the shell -- a special builtin's prefix assignments stay
(`x=1 :` sets x), a regular one's are remembered and put back (`x=1 true`
leaves x unset) -- and anything else is a child: the assignments expand into
the child's environment (a clone of the process's with every variable dropped,
the exported shell variables and the prefix assignments set; secrets and LLM
identifiers kept), the command is looked up (a name with a `/` is `Stat`ed; a
plain name is searched in `PATH` -- unset finds nothing, an empty entry is the
working directory), then started through `OS()->StartProcess` with the shell's
working directory and its slots 0/1/2 as the child's standard streams.
`NotFound` reports `<name>: not found` (127), a directory or a refused start
`<name>: Permission denied` (126); `$?` is the child's `ExitCode()`. Waiting
polls in 50 ms slices so a stop of the shell reaches the child
(`TriggerStop`, 5 s grace) before the shell unwinds with `ShellStopped`.

What is not implemented yet reports `<what> is not supported yet` with status
2 through the `NotYet` helper: `&`, pipelines of more than one command and
`$(...)` (hsh--pipelines), every compound command and function definitions
(hsh--control-flow). Each later task removes its uses; hsh--control-flow
removes the helper.

## Redirections

A simple command's redirections change the shell's own descriptor table (the
process's `IFileIO` slots) for the length of the command. `RedirectionScope`
(`HshRedirection.h`): `Apply` applies them in dash's order, after the command
words have expanded, and the table is put back when the scope ends (after the
child has been waited for) unless `exec` with no command called
`Shell::KeepRedirections()`, which makes the scope `Keep()` the changes. Every
slot is saved (`GetDescriptor`, possibly null) before its first change in the
scope -- before an `AddDescriptor`, a `Dup2`, a `CloseDescriptor`, and before a
`CreatePipe` (whose two new slots may be the ones about to be changed) -- and
`Restore()` puts the saved content back, latest change undone first through
`PlaceDescriptor`, never writing or throwing. On the first failure `Apply`
undoes what that call did and returns the message in dash's words (`cannot
open /x: No such file`, `cannot create /x: File exists` / `Directory
nonexistent` / `Is a directory` / `Permission denied` from
`OpenFailureReason`, `<m>: Bad file descriptor` for a dup of an empty slot,
`<n>: Too many open files` when the table is full, `Pipe call failed`); the
caller reports it with status 2 -- unless the command is a special builtin or
the dup word expanded to something that is no digit, both fatal
(`Syntax error: Bad fd number`, thrown through `Shell::Fail`), as dash exits
on them.

The open flags per kind: `<` read-only; `>` and `&>` write-create-truncate,
refused with `File exists` when `-C`/`noclobber` is on and the target is an
existing regular file (an existing device such as `/dev/null` or a directory
is spared the check, as dash); `>|` the same without the check, `>>`
write-create-append, `<>` read-write-create (`kFileOpenReadWriteCreate`,
`FilesystemUtils.h`). `n>&m`/`n<&m` is `Dup2(m, n)` (`n` unchanged when
`m == n`); `n>&-`/`n<&-` closes the slot, an empty one closing fine
(`exec 9>&-` succeeds). `&>f` is `>f` then `2>&1`. A heredoc or here-string is
a pipe (`IFileIO::CreatePipe`) sized exactly to its text (at least 1), filled
whole and closed before anything is started -- a write of `capacity` bytes
into an empty pipe never blocks, so no thread is needed; the reader sees the
text, then end of file. Everything goes through `IFileIO` (`OpenFile`,
`CreatePipe`, the table operations): no `IFileSystem`, no host, as everywhere
else in the shell.

A child started by a command gets slots 0/1/2 as they are then; an empty
(closed) slot goes to it as `ClosedDescriptor` (`HshDescriptors.h`), whose
reads and writes fail with `kIOError` -- never as null, which
`StartProcessOptions` would read as "use the console".

`exec` (`HshBuiltinExec.cpp`, a special builtin): alone (a leading `--`
skipped) it returns 0 and its command's redirections stay. With a command it
looks it up with `LookUpCommand` -- PATH and paths only, never a shell
builtin, so `exec :` is `exec: :: not found` -- starts it (`not found` 127,
`Permission denied` 126, in its own name) and ends the shell with its status
(`throw ShellExit`), as dash's exec replaces the shell.

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

- `~user` is left as it is (dash expands it through the user database: there
  are no users yet); `~` alone is `HOME`, or the text itself when HOME is
  unset.
- `&>` and `<<<` are bash's operators (`echo a &>f` redirects stdout and
  stderr to `f`; in dash it would be `echo a &` then `>f`).
- `${x:}` is a `Bad substitution` (`ParameterOp::Bad`, failing at expansion)
  rather than dash's syntax error `Missing '}'`.
- `$'...'` is not ANSI-C quoting (as dash): a plain `$` followed by the quote.
- An error inside a `$(...)` is reported from parsing its text alone, so it
  names `")"` (the substitution's end) where dash names what its own parser
  happened to see: `echo $(if)` says `")" unexpected (expecting "then")`
  where dash says `")" unexpected`.
