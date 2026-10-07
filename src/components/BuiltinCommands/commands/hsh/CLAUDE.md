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
  builtin's process: see "Running" and "Pipelines, subshells, jobs" below.
- `HshRedirection.h/.cpp` - `RedirectionScope`, applying a command's
  redirections to the shell's own descriptor table and undoing them, and
  `PlaceDescriptor`: see "Redirections" below.
- `HshDescriptors.h/.cpp` - `ClosedDescriptor`, what a child gets in place of
  a closed slot, and `NullInputDescriptor`, the empty stdin of a background
  command.
- `HshUnboundedPipe.h/.cpp` - `CreateUnboundedPipe`: a pipe without a
  capacity (writes never block), for the pairs of reader/writer where the
  reader runs only after the writer (two in-shell pipeline stages, `$(...)`).
- `HshSubshell.h/.cpp` - `SubshellScope`, an in-process subshell's save and
  restore of everything it may change: see "Pipelines, subshells, jobs".
- `HshBuiltins.h/.cpp` - the shell's own builtins (`.`, `:`, `[`, `break`,
  `cd`, `continue`, `eval`, `exec`, `exit`, `export`, `false`, `read`,
  `readonly`, `return`, `set`, `shift`, `test`, `true`, `unset`, `wait`): a
  sorted table of name, special/regular, function. Also
  `HshVersion`, for `set`'s not-treated reports inside the shell, where a
  `BuiltinContext`'s version is not at hand.
- `HshBuiltinExec.cpp` - `exec`: with no command its redirections stay; with
  one, the command runs in the shell's place.
- `HshBuiltinWait.cpp` - `wait`: waits for the background jobs, all of them
  or by pid, with dash's statuses and messages.
- `HshBuiltinCd.cpp` - `cd`: `-L`/`-P` (the same: no symlinks), HOME and
  OLDPWD defaults, CDPATH, and dash's print rules (`cd -`, a non-empty CDPATH
  entry that found it).
- `HshBuiltinVariables.cpp` - `export`, `readonly`, `unset`, `shift`: the
  sorted `-p` listings, name/value operands, `-f`/`-v` (a function or a
  variable), dash's messages.
- `HshBuiltinSet.cpp` - `set`: the sorted variable listing, option clusters
  and `-o` names, positional parameters, and the `set -o`/`set +o` tables.
- `HshBuiltinFlow.cpp` - the control-flow builtins: `break`/`continue`
  (a `LoopControl` unwinds to the loops), `return` (a `FunctionReturn`
  unwinds to the calling function or dot script), `.` and `eval` (the text
  parsed one complete command at a time and run in this shell): see
  "Control flow" below.
- `HshBuiltinRead.cpp` - `read`: one input line (a byte at a time, so a pipe
  is never over-read), backslash processing unless `-r`, `-p` prompting when
  stdin is a terminal, and dash's IFS splitting into the named variables.
- `HshBuiltinTest.cpp` - `test` and `[` (one builtin, two names): dash's
  POSIX reductions and expression descent byte for byte (see "test" below).
- `HshQuote.h/.cpp` - `ShellSingleQuote`: dash's single_quote -- a value as
  `'plain'`, `''`, or `'it'"'"'s'` -- for the `export -p`/`set` listings.
- `HshNumber.h/.cpp` - `Atomax10`: dash's atomax10 -- blanks, a sign,
  decimal digits, overflow-rejected -- for `shift`'s count and `test`'s
  numbers and `-t` fd.
- `Hsh.cpp` - the `hsh` builtin itself: the option table (every dash option,
  treated or marked for the not-treated report), the dash-based `--help`
  (`BuiltinHelp::basedOn`), `ParseInvocation`, the decision whether the shell
  runs interactively (see "The interactive protocol"), and `Shell` on the
  context. Its `ManPage()` override returns the full manual page.
- `HshManPage.h/.cpp` - `HshManPage()`: the shell's full manual page, plain
  text. The page is several raw string literals concatenated, one per
  section, because MSVC refuses a string literal longer than 16 KB.

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

A simple command runs in dash's order (dash's `evalcommand`): the words
expand first (`ExpandWords`); then `-n` (noexec) ends the command's whole
list from here -- dash re-checks it at every `evaltree`, so a `set -n`
stops what follows even mid-list, mid-`&&`/`||`-chain and per pipeline
stage, and once set it cannot be turned off again (the `set +n` itself is
skipped); an interactive shell is unaffected; then the redirections apply
(a failure: the command does not run and its prefix assignments do not even
expand; fatal for a special builtin); then the prefix assignments' values
expand and are made, through `Shell::AssignVariable` -- the one place every
shell assignment goes: a read-only variable is fatal (`<name>: is read
only`), and under `-a` (allexport) the variable is exported too; then the
trace (see below); then the run. With no fields the assignments apply to
the shell (status 0, or the last command substitution's); a name in the
shell-builtin table runs in the shell -- a special builtin's prefix
assignments stay (`x=1 :` sets x), a regular one's are remembered and put
back (`x=1 true` leaves x unset) -- and anything else is a child: a
read-only variable there is still fatal, after the assignments have
expanded (`readonly x=1; x=$(pwd >/s) c` writes s), and the assignments go
into the child's environment (a clone of the process's with every variable
dropped, the exported shell variables and the prefix assignments set;
secrets and LLM identifiers kept), the command is looked up (a name with a
`/` is `Stat`ed; a plain name is searched in `PATH` -- unset finds nothing,
an empty entry is the working directory), then started through
`OS()->StartProcess` with the shell's working directory and its slots 0/1/2
as the child's standard streams. `NotFound` reports `<name>: not found`
(127), a directory or a refused start `<name>: Permission denied` (126);
`$?` is the child's `ExitCode()`. Waiting polls in 50 ms slices so a stop
of the shell reaches the child (`TriggerStop`, 5 s grace: a child still
running then is logged, `hsh: child (pid <pid>, <path>) did not stop
within <ms> ms`) before the shell unwinds with `ShellStopped`.

Under `-x` (xtrace), the trace of a command is dash's: written to stderr
after the redirections apply but to the stderr from before them (so
`echo a 2>f` traces to the shell's stderr, not into `f`, as dash), after the
prefix assignments are made (so `PS4=X` restyles its own trace line) and
nowhere on a redirection error. It is PS4's value as it is, never expanded
(a documented exception), glued to the line with no separator -- only the
seed `"+ "` carries a space -- then the assignments as `name=value` and the
fields, `x=1 echo "a b"` tracing as `+ x=1 echo a b`. The trace is written
even for a command that then fails to start (`not found`).

The shell builtins of this task: `cd` (`HshBuiltinCd.cpp`; regular) is
dash's cdcmd: no operand means HOME (unset or empty is `.`, dash's own
quirk), `-` is OLDPWD with the new directory printed, `""` is `.`; a
destination not empty, not absolute and not starting with `.`/`..` is
looked up in CDPATH (empty entry: the working directory, and the print is
only for a non-empty entry that found it); the result is `OLDPWD` and `PWD` set and exported, as
dash's setpwd -- a read-only one is reported (`cd: PWD: is read only`), left
as it was, and cd's status is 2, the cd itself done, as dash. `export`, `readonly`, `unset`,
`shift` (`HshBuiltinVariables.cpp`; special but `cd`-style errors aside) are
dash's exportcmd/readonlycmd/unsetcmd/shiftcmd byte for byte: the sorted
`-p` listings (`export name`, `export name='value'`, `ShellSingleQuote`),
`name=value` operands, bare names only flagging, unset's `-f`/`[-v]` (a function or a
variable goes), `Illegal option -x`,
`bad variable name`, `is read only`, `Illegal number: <n>` and `can't
shift that many`. `set` (`HshBuiltinSet.cpp`; special) is setcmd: no
arguments lists every set variable sorted and single-quoted (flagged-but-
unset ones stay out), option clusters with `-o` taking the next argument
(anywhere in the cluster, as dash), `--` keeping or (alone) clearing the
positional parameters, a lone `-` clearing `-x`/`-v` and keeping them,
operands becoming the new `$1...`; `set -o` and `set +o` print the option
table (every dash option; the untreated ones are always "off" there and
reported not-treated when set or cleared, through the shell's own stderr
so the reports obey redirections). `test` and `[` are one builtin,
`HshBuiltinTest.cpp` -- see "test" below.

What runs besides simple commands -- compounds, functions, `break`/
`continue`/`return`, `.`/`eval`, `read`, and `-e` (errexit) -- is in "Control
flow" below.

## Control flow

Every compound command runs in the shell itself. `ExecuteCommand` dispatches
on the kind: a brace group `{ ...; }` is `ExecuteList` on its items, a
subshell `( ... )` is `RunSubshell` (an in-process subshell, not a child
process: dash forks; here the state comes back through `SubshellScope` --
see "Pipelines, subshells, jobs"), and `if`/`while`/`until`/`for`/`case` have
their executors (`ExecuteIf`, `ExecuteLoop`, `ExecuteFor`, `ExecuteCase`). A
compound's own redirections wrap the whole compound in one `RedirectionScope`
-- `{ a; b; } >f` opens `f` once -- and a failed one reports and gives status
2 without the body running. `if` runs each condition in a tested context and
the taken branch's list (or the else part, or status 0 with neither);
`while`/`until` test their condition list per iteration; `for` assigns its
variable (through `AssignVariable`: a read-only loop variable is fatal) each
word of `in ...` expanded, or the positional parameters without `in`;
`case` expands its subject once and tries each item's patterns in order
(`MatchPattern` over `ExpandPattern`, so quoting in a pattern is literal),
running the first matching body.

`break [n]`/`continue [n]` throw a `LoopControl{isBreak, levels}` that the
loops catch: each loop decrements and rethrows while `levels` stays above 1,
`break` leaving the loop, `continue` starting the next
iteration (a body so ended has status 0, theirs; one thrown by the condition, `while break; do`, acts on that loop alike). `n` past the running loops just ends them all
(`std::min(levels, LoopDepth())`); outside a loop both return 0, as dash. A
function call (`CallFunction`) runs the definition's body command with its
own positional parameters (`$0` unchanged, the call's fields becoming
`$1...`, put back afterwards) and remembers prefix assignments made for the
call, putting them back like a regular builtin's; `return [n]` throws
`FunctionReturn{status}` (default `$?`), which `CallFunction` turns into the
call's status. Both stop at a subshell boundary: `RunSubshell` catches a
`LoopControl` (the subshell ends, status 0 -- the outer loop is untouched)
and a `FunctionReturn` (the subshell ends with its status), since a forked
dash subshell could not reach the outer loop or caller either. The
unwinding types live next to `ShellExit`/`ShellStopped` in `HshShell.h`.

Functions are the shell's `m_functions`, a name -> `CommandPtr` map filled
when a `FunctionDefinition` executes (the node is copied in, its
`sourceText` included: `BackgroundPrelude` writes every function's source
into the child hsh running a background list, so `f &` finds `f` there).
Lookup is POSIX's: special builtin first, then a function, then a regular
builtin, then `PATH` -- so a function overrides a regular builtin (`cd() {
...; }`) but not `exit`. `unset -f name` removes one.

`-e` (errexit) ends a non-interactive shell with `ShellExit{status}` when a
failing command is the last pipeline of an and-or list run to its end, the
pipeline is not negated, and no tested context surrounds it. A tested
context -- every `&&`/`||` operand but the last pipeline, every negated
pipeline, every condition (`if`, `while`, `until`) -- is a
`Shell::TestedContext` on the executor's call stack, counted by
`m_errexitSuppressed`, which is why a function called in a tested context
inherits it (dash's rule) while its own untested commands stay fatal.
`SubshellScope` saves and restores the suppression with the rest.

`.` (`BuiltinDot`, a special builtin) opens its file (a name with a `/` as
given, another searched in `PATH`; not found/cannot open are fatal, named
after the file) and runs its text one complete command at a time in this
shell, through `Parser::ParseNext` with the reported syntax errors named
after the file; `m_dotDepth` lets `return` unwind to it as to a function.
`eval` (special) joins its arguments with one space and parses and runs the
result the same way, line numbers kept from where it runs. Both run in the
shell itself, so what they assign stays. One command at a time (never
`ParseProgram` on the whole text) because each parsed command runs before
the next is parsed: `.` of a file whose line 2 depends on line 1's run must
work, as parse-a-then-run-a in `Shell::Run`.

`read` (regular, `HshBuiltinRead.cpp`) reads a byte at a time -- never a
buffered chunk -- so `read a; cat` leaves the rest of the same pipe for
`cat`. A line is the bytes up to a newline (end of input ends it too, with
status 1); without `-r` a backslash joins with the next line, protects the
next byte from splitting, and a trailing lone one is dropped. `-p prompt`
writes the prompt to stderr only when slot 0 is a terminal. The line is then
split on `IFS` (unset: ` \t\n`; empty: no splitting): leading/trailing IFS
white space goes, non-white IFS characters delimit once each, the last name
takes the rest with its delimiters, and a leftover name is set empty.
`read` with no name reports `read: arg count`, a bad name
`read: <name>: bad variable name` (both status 2; read is regular so these
are not fatal), an unknown option `read: Illegal option -<c>`.

## Pipelines, subshells, jobs

A pipeline `a | b | c` (and a negated `! a | b`) runs its stages at the same
time, connected by pipes; its status is the last stage's. A stage is either a
**child stage** (`IsChildStage`: a simple command with at least one word whose
first word is literal text, `LiteralText`, naming neither a shell builtin nor
a function, and holding no `CommandSubstitution` in a word or an assignment
value -- one must expand in pass order, not in the started child), a child
process started at once so that
every child stage runs concurrently, or an **in-shell stage** (a shell
builtin, a function, an assignment-only command, a compound command),
run one at a time after every child has been started. Then the shell waits
for each child in order. A child stage of a pipeline is started but not
waited: `RunStage` runs the command with its stdin/stdout replaced (slots 0
and 1, in a subshell) and `ExecuteSimpleCommand`'s `m_startInsteadOfWait`
hands the started child back instead of waiting for it.

The pipe between two stages is a real one (`IFileIO::CreatePipe`: 64 KiB,
blocking), except an **unbounded pipe** (`CreateUnboundedPipe`,
`HshUnboundedPipe.h`: writes append and never block) before an in-shell
reader that follows an in-shell stage. That split keeps a pipeline
deadlock-free without a single thread: the in-shell stages run one at a time,
after every child has been started, so a bounded pipe's reader is either a
running child or the first in-shell stage (which runs at once), and a writer
whose reader comes later is an in-shell stage writing into an unbounded pipe.
Right after a stage is started or has run, the shell drops its own references
to the pipe ends it handed over, so a reader sees end of file once its
writers are gone. An in-shell writer whose reader is done meets a broken pipe
there and the subshell ends, with 141 -- only the subshell dies of it, as a
forked dash subshell would (`WriteOut`/`WriteErr` throw `ShellExit` in a
subshell, where at the top level they keep stopping the shell itself,
`StopForBrokenPipe`).

A documented limitation comes with this: two in-shell stages of one pipeline
run one after the other, not at the same time. So an in-shell stage that
never ends, written into an in-shell reader (`while :; do echo y; done |
while read l; do break; done`), never ends, and its output is held in memory
as it grows -- where dash, forking both, ends at once. A child stage (a
program) has no such limit: it runs concurrently, through a bounded pipe.

`SubshellScope` (`HshSubshell.h`) is the in-process subshell: what a subshell
may change and must not leak -- the `ShellState` (variables, options,
positional parameters, `$?`, `$!`), the working directory, every slot of the
descriptor table and the job list -- saved at construction and put back at
destruction (the directory first, then the slots), so `echo b | read w`
leaves `w` unset and `x=$(cd /; pwd)` the directory alone. It also counts the
shell's subshell depth. `Shell::RunSubshell` wraps a body in one: `exit`
(`ShellExit`) and fatal errors (`ShellError`, reported as at the top level,
status 2) end only the subshell; `ShellStopped` passes through, to end the
whole shell. The scope also saves and restores the
function table and the loop/function/dot depths and the errexit suppression,
and its destructor drops the processes of every job the subshell added from
the shell's live-children list with the job itself -- else each `( true & )`
would leak one.

A background list `cmd &` is started without waiting. One that is a single
pipeline of child stages only starts its stages as a pipeline does, without
the waiting (the first stage's stdin is a `NullInputDescriptor`: an
asynchronous list's stdin is empty, as dash's /dev/null with no job control,
before its own redirections). One that needs the shell itself (a builtin,
`&&`/`||`, a compound command or a function) runs
as a **child hsh**: `hsh -c` of `BackgroundScript(item)` --
`BackgroundPrelude()` (the sourceText of every function the shell knows, so
the child finds them) then the item's `sourceText` -- started from the
running shell's own path (`IProcess::Path()`, no PATH lookup), with `$0` and
the positional parameters passed on, the options that are on among e u f x C
a as one invocation argument, the exported
variables, the shell's working directory and its stdout/stderr, and an empty
stdin. Documented exception: an **unexported** variable does not reach that
child, where a forked dash subshell would see it (the child is a process, not
a fork: it sees only its environment). Either way the start records a **job**
-- every process started and the last stage's pid, which `$!` shows -- kept
in `m_jobs`, oldest first; past 1024 jobs the finished ones are dropped,
oldest first. `wait` (`HshBuiltinWait.cpp`, a regular builtin) waits for
every job's processes (status 0 always) or, given pids, for each one's job --
status the job's last exit code; with dash's messages and statuses
(`wait: Illegal number: <x>` and `wait: No such job: <x>`, both 2; a pid that
is no job's, no process of a job included: 127, no message), and it unwinds
with `ShellStopped` when the shell is asked to stop.

`$(...)` and `` `...` `` are `Shell::RunCommandSubstitution`: the text is
parsed (`ParseProgram`, first line the substitution's), run as a subshell
with its stdout on an **unbounded pipe** -- the reader runs only after the
writer, so a bounded pipe would deadlock past 64 KiB -- and the pipe is read
to end of file (a background child of the subshell that still holds the write
end is waited out, as dash waits for end of file). The expansion drops the
trailing newlines and makes `$?` the substitution's status.

## Redirections

A simple command's redirections change the shell's own descriptor table (the
process's `IFileIO` slots) for the length of the command. `RedirectionScope`
(`HshRedirection.h`): `Apply` applies them in dash's order, after the command
words have expanded, and the table is put back when the scope ends (after the
child has been waited for) unless `exec` with no command called
`Shell::KeepRedirections()`, which makes the scope `Keep()` the changes. Every
slot is saved (`GetDescriptor`, possibly null) before its first change in an
`Apply` -- before an `AddDescriptor`, a `Dup2`, a `CloseDescriptor`, and before a
`CreatePipe` (whose two new slots may be the ones about to be changed) -- and
`Restore()` puts the saved content back, latest change undone first through
`PlaceDescriptor`, never writing or throwing. On the first failure `Apply`
undoes what that call did and returns the message in dash's words (`cannot
open /x: No such file`, `cannot create /x: File exists` / `Directory
nonexistent` / `Is a directory` / `Permission denied` from
`OpenFailureReason`, `<m>: Bad file descriptor` for a dup of an empty slot,
`<n>: Too many open files` when the table is full, `Pipe call failed`); the
caller reports it with status 2 -- unless the command is a special builtin or
the dup word expanded to something that is not exactly one digit, both fatal
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

## The test builtin

`test` and `[` (`HshBuiltinTest.cpp`, one entry, two names; a regular
builtin) are dash's `bltin/test.c` ported with the same window: an index pair
over the arguments, the POSIX two-, three- and four-operand reductions at the
top (so `test a -a` is 1 and `test a -o` is 0, as dash), then the descent
`!` -> `-a` -> `-o` over primary expressions. A bare expression token can
still be an operator when an operand follows it and a binary operator follows
that (`test -a a` is `-a: unexpected operator`); a trailing `(` before the
end is an operand. Errors are reported and the status is 2 (`missing ]`,
`unexpected operator`, `argument expected`, `closing paren expected`,
`Illegal number: <x>`; `[` only needs its last argument to *begin* with `]`
-- `]foo` closes fine, as dash). Numbers are `Atomax10` -- blanks and a sign
around decimal digits, overflow rejected exactly as dash's atomax10 (so
`" 12 " -eq 12` is true). Documented exceptions, all pinned in
`HshBuiltinsTest.cpp`: `-r`/`-w`/`-x`/`-O`/`-G` only test that the file is
there (no permissions or users yet), `-h`/`-L` are always false (nothing in
Haisos creates a link) and so are `-b`/`-p`/`-S`/`-u`/`-g`/`-k` (no device
kinds beyond files, directories and character devices, no set-user-id bits),
and `-ef` compares what `ResolvePath` gives for the two paths (there are no
inode numbers to compare).

## The interactive protocol

`HshCommand::Run` makes the shell interactive when `-i` was given -- whatever
the source (with `-c` it changes only `$-` and error handling, as dash) -- or
when the source is standard input and both descriptor 0 and descriptor 2
exist and are terminals (`RUN -i /bin/hsh` arranges that on the console).

`Shell::RunInteractive` keeps a buffer of the lines typed since the last
complete command; after each line, the whole buffer is parsed again from the
start with `ParserOptions::interactive` set (while the input has not ended)
and `firstLine` the number of that buffer's first line; on an error with
`incomplete` the buffer is kept, PS2 is shown and another line is read;
otherwise the commands run (or the error is reported) and the buffer is
emptied. Every complete command of the buffer is collected before any runs:
`ParseNext` can return a Command whose heredoc is unterminated and only
report it on the next call, and a line already run must not run again when
the buffer is re-parsed a line later.

The prompt is PS1's value when the buffer is empty, PS2's when it is not --
written to stderr as the variable is, never expanded, nothing when unset.
`ReadInputLine` reads slot 0 one byte at a time (so nothing after the line is
taken from a pipe), without the newline; the last line may lack it. Line
numbers count every line read, so an error on the third line typed says
`hsh: 3:`. At the end of the input with the buffer empty the shell writes a
newline and exits with the last command's status; with a buffer pending, it
is parsed once without the interactive flag -- what a script would accept
runs, the rest errors -- and the next read ends the shell. `exit` ends it
with the status given, without the newline.

An error never ends an interactive shell: parse errors and runtime
`ShellError`s are reported, `$?` becomes 2 and the next prompt shows
(`RunOneCommand` reports and continues only when interactive; a
non-interactive shell rethrows, and is ended by the error with status 2).
`-e` and `-n` have no effect interactively. Once a heredoc's delimiter line
has arrived, a lexing error in its body is a plain syntax error, never
"more input needed" (`ReadHereDocBodies` rethrows it with `incomplete`
cleared) -- otherwise the shell would wait at PS2 forever.

## Documented deviations from dash

- `~user` is left as it is (dash expands it through the user database: there
  are no users yet); `~` alone is `HOME`, or the text itself when HOME is
  unset.
- `&>` and `<<<` are bash's operators (`echo a &>f` redirects stdout and
  stderr to `f`; in dash it would be `echo a &` then `>f`).
- A background (`&`) list that needs the shell runs in a child `hsh -c`
  process, which sees only the exported variables (a forked dash subshell
  would see them all).
- Two in-shell stages of one pipeline run one after the other, so an endless
  in-shell writer before an in-shell reader never ends (its output held in
  memory); see "Pipelines, subshells, jobs".
- `${x:}` is a `Bad substitution` (`ParameterOp::Bad`, failing at expansion)
  rather than dash's syntax error `Missing '}'`.
- `$'...'` is not ANSI-C quoting (as dash): a plain `$` followed by the quote.
- An error inside a `$(...)` is reported from parsing its text alone, so it
  names `")"` (the substitution's end) where dash names what its own parser
  happened to see: `echo $(if)` says `")" unexpected (expecting "then")`
  where dash says `")" unexpected`.
- The xtrace's PS4 is written as it is, never parameter-expanded as dash
  does; `-v` is accepted but not acted on (echo of the line as read).
- `test`'s file access and identity tests are approximations where there are
  no permissions, users, links or inode numbers: see "The test builtin".
