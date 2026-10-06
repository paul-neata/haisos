# Task hsh--interactive: interactive hsh, its manual page, and the six scenarios end to end

- Rock: hsh
- Depends on: hsh--control-flow, builtins--man
- Size: ~800 changed lines in ~12 files (about 250 of them the manual page's text)
- Plan checked against: develop @ 8fb8324
- PR title: hsh: interactive mode, full manual page, end-to-end scenarios

## Goal

- `RUN -i /bin/hsh` gives an interactive shell on the console, as dash does
  on a terminal: the prompt `$ ` (PS1) on stderr before each command, `> `
  (PS2) while a command is incomplete (an open quote, `if`, heredoc, trailing
  `&&`...), errors reported with dash's line numbers (`hsh: 3: nosuch: not found`)
  without ending the shell, `exit` ending it, and the end of input ending it
  with the last status (after writing a newline to stderr, as dash).
- `hsh` is interactive when `-i` is given, or when it reads commands from
  standard input (no `-c`, no script) and both its stdin and its stderr are
  terminals (dash's rule).
- `man hsh` shows a full manual page -- a short guide to the language with a
  one-line example per topic -- instead of the `--help` text.
- `haisos --init` suggests `# ENV PATH=/bin` next to the builtin lines.
- All six acceptance scenarios of `develop-plan/goal.md` pass as haisos tests.
- hsh is `1.0.0`; the docs describe it whole.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` (all of
it, including the interactive protocol hsh--lexer documented),
`HshShell.h`, `HshInvocation.h`, `HshParser.h`, `HshError.h`; the root
`CLAUDE.md` ("The `haisosfile` DSL", "Builtin Commands", rule 9);
`develop-plan/goal.md` (scenarios 1-6, the console clarifications);
`src/haisos/HaisosFileParser.cpp` (`GetHaisosFileTemplate`);
`tests/haisos/builtins.haisostest/builtins.haisostest.js`; the dash manual
(Invocation: when a shell is interactive; `PS1`, `PS2`). Behaviour checked
against `dash -i` 0.5.12 with piped input.

What earlier tasks provide (as if on develop; the code wins on names):

- hsh--lexer / hsh--parser: `LexerOptions::interactive`,
  `ParserOptions { firstLine, interactive, endOfInputName }`; with
  `interactive`, an input that ends where more is needed gives an `Error`
  result with `incomplete` true (an unterminated heredoc and a trailing
  backslash-newline too); `ParseResult { status, commands, errorMessage,
  errorLine, incomplete }`; the documented protocol: keep a buffer of the
  lines typed since the last complete command and re-parse it whole after
  each line, with `firstLine` the number of its first line.
- hsh--executor .. hsh--control-flow: `Shell` (`Run`, `ExecuteList`,
  `WriteErr`, `Report`, `State()`, `IO()`), the exceptions `ShellExit`,
  `ShellStopped`, `ShellError`; `Invocation { source, options, ... }`,
  `ShellOptions::interactive` (set by `-i`); `HshCommand` in `Hsh.cpp`;
  `HshShellFixture.h`.
- builtins--man: `man <name>` prints `IBuiltinCommand::ManPage()`; tests
  `BuiltinCommandsTest.ManPrintsEachBuiltinsHelp` (in `ManTest.cpp`) and
  `EveryBuiltinsManPageIsItsHelp` (in `BuiltinCommandsTest.cpp`,
  builtins--directories) assume every page is its help.
- streams--console-and-start: `RUN -i <program>` sets
  `StartProcessOptions::interactive`, giving the program the console's input
  as stdin (a terminal: `IsTerminal()` true, reads return a typed line plus
  `\n`, then 0 forever at end of input); console output and error are
  terminals too, unbuffered (a prompt without newline shows at once).
- streams--exit-codes: `haisos` exits with the code of the first failing `RUN`.
- `tests/mocks/MockFileDescriptor.h` (`MockFileDescriptor(bool isTerminal)`,
  `Feed`, `EndInput`, `Written()`).

## Changes

All in namespace `Haisos::Hsh`, `src/components/BuiltinCommands/commands/hsh/`,
plain portable C++17, unless said otherwise.

### Rules that bite (restated)

- The shell reads its input only from its own slot 0 and writes prompts to
  its slot 2 (`IO()`); nothing else.
- Rule 9: `hsh` is already registered; the new template line is
  `# ENV PATH=/bin`, written in `GetHaisosFileTemplate` (an `ENV` line is not
  a builtin line -- the builtin lines stay generated from `GetCommands()`).
- `--help` keeps its one generated shape; only `ManPage()` is overridden
  (the builtin rule: "override `ManPage()` only if it has more to say").
- MSVC refuses a single string literal over ~16 KB (error C2026): the page is
  several raw string literals, one per section, concatenated.
- hsh's version becomes `1.0.0`.

### Deciding interactive (`Hsh.cpp` / `HshShell.cpp`)

After `ParseInvocation`: when the source is `StandardInput` (no `-c`, no
script) and `IO().GetDescriptor(0)` and `GetDescriptor(2)` both exist and are
terminals, set `options.interactive`. `-i` sets it whatever the source (with
`-c`, `-i` changes only `$-` and the error handling below, as in dash).
`$-` then contains `i`.

### The interactive loop (`HshShell.cpp`)

`Run` with `options.interactive` and the `StandardInput` source reads and runs
commands line by line instead of reading stdin whole:

```cpp
// Reads one line from slot 0, one byte at a time (so nothing after the line
// is taken from a pipe), without its '\n'. nullopt at the end of input (a 0
// read, an empty slot, or an error other than kIOInterrupted, which throws
// ShellStopped). The last line may end without '\n'.
std::optional<std::string> ReadInputLine();
```

1. `lineNumber = 0`, `buffer` empty, `bufferFirstLine = 1`.
2. Prompt: `WriteErr(PS1's value)` when the buffer is empty, else
   `WriteErr(PS2's value)`; an unset variable writes nothing. Prompts are
   written as they are, not expanded (a documented exception: dash expands
   parameters in them).
3. `ReadInputLine()`:
   - a line: `++lineNumber`; append it and `\n` to the buffer.
   - end of input with an empty buffer: `WriteErr("\n")`, return
     `lastExitStatus` (`false` then end of input exits 1, as dash).
   - end of input with a pending buffer: parse it once more with
     `interactive` false (so what a script would accept runs, and what it
     would not gives its error), run or report as below, empty the buffer,
     and go on to step 2 (the next read sees the end again and ends the
     shell): `echo "a` then end of input reports
     `hsh: 2: Syntax error: Unterminated quoted string`, shows `$ `, writes
     `\n` and exits 2.
4. `Parser parser(buffer, {bufferFirstLine, true, "end of file"})`; loop
   `ParseNext()`:
   - `Command`: run it as `Run` runs a command (below);
   - `EndOfInput`: done;
   - `Error` with `incomplete`: keep the buffer (step 2 shows PS2); leave
     the loop without emptying it;
   - `Error`: `WriteErr(FormatShellError(arg0, errorLine, errorMessage))`,
     `lastExitStatus` = 2; done.
   When done (not incomplete): empty the buffer, `bufferFirstLine = lineNumber + 1`.
5. Running a command interactively: as the non-interactive `Run` does, except
   that a `ShellError` is reported (`hsh: <line>: <message>`), sets `$?` to 2
   and the shell goes on; `ShellExit` ends the shell with its status (no
   newline written); `ShellStopped` ends it as before. `-e` and `-n` have no
   effect in an interactive shell (dash).
6. Loop to step 2.

Factor the "run one complete command and handle its exceptions" part out of
the non-interactive `Run` so both paths share it (one private function, the
interactive flag deciding whether a `ShellError` ends the shell).

A non-interactive shell reading stdin keeps reading it whole (dash's block
reads have the same effect on commands that read stdin). A stop requested
while the shell waits on the console's input takes effect only when a line
or the end of input arrives (the console's read is uninterruptible, goal.md).

### `HshManPage.h` / `HshManPage.cpp` (new) and `Hsh.cpp`

```cpp
// hsh's manual page, as `man hsh` prints it: plain text, ending in '\n'.
std::string HshManPage();
```

`HshCommand::ManPage()` returns `HshManPage()`. The page, plain text (no
formatting codes), section headings at column 0 in capitals, body indented 7
spaces, no line over 79 characters, a blank line between sections. The
sections, in order, with exactly these headings (the haisos test looks for
them):

```
NAME
SYNOPSIS
DESCRIPTION
OPTIONS
QUOTING AND ESCAPING
PARAMETERS AND EXPANSIONS
PIPELINES
REDIRECTIONS
HERE-DOCUMENTS (HEREDOCS)
LISTS
GROUPS AND SUBSHELLS
IF
WHILE AND UNTIL
FOR
CASE
FUNCTIONS
BUILTIN COMMANDS
EXIT STATUS
DIFFERENCES FROM DASH
SEE ALSO
```

What each holds (the prose is the implementer's; keep each topic section to
a short paragraph or a compact list, and end it with one line
`       Example: <a command line that runs as shown in hsh>`):

- NAME: `hsh - the Haisos shell, a command interpreter after dash`.
- SYNOPSIS: the three usage forms of `--help`.
- DESCRIPTION: what hsh is (dash's language, run as a Haisos builtin); where
  commands come from (its builtins, then `PATH` -- builtins, `.md` agents,
  `.lua` scripts -- each started as a Haisos process with the shell's
  exported variables, directory and standard streams); interactive versus
  script, `-c` and stdin; `RUN -i /bin/hsh` and `ENV PATH=/bin`.
- OPTIONS: every treated letter with its `-o` name and a few words, `-o`/`+o`,
  `+X`; the untreated ones listed as accepted and reported.
- QUOTING AND ESCAPING: `'...'`, `"..."` (what stays special inside), `\`,
  line continuation. Example: `echo 'single $HOME' "double $HOME" \$HOME`.
- PARAMETERS AND EXPANSIONS: variables, `$1`..., `$#`, `$@`, `$*`, `$?`, `$$`,
  `$!`, `$-`, `$0`; every `${...}` form; `$(...)` and backquotes; `$((...))`;
  field splitting by `IFS`; globbing `*`, `?`, `[...]`; tilde; the order of
  expansions. Example: `x=file.txt; echo ${x%.txt} $((1 + 2)) $(echo sub) *.md`.
- PIPELINES: `|`, `!`, the last command's status, stages running together.
  Example: `ls /bin | wc -l`.
- REDIRECTIONS: every form, one per line: `< > >| >> <> n>&m n<&m n>&- n<&- &> <<<`
  and an IO number; noclobber. Example: `ls /nope > /out.txt 2>&1`.
- HERE-DOCUMENTS (HEREDOCS): `<<WORD`, `<<-WORD`, a quoted delimiter
  suppressing expansion. Example: `cat <<EOF` (with its body and `EOF` on the
  next lines shown indented).
- LISTS: `;`, newline, `&&`, `||`, `&` (programs run in the background directly; a builtin, function or compound command in a child hsh seeing only exported variables), `$!`, `wait`. Example:
  `ls /nope || echo "failed: $?"`.
- GROUPS AND SUBSHELLS: `{ ...; }`, `( ... )` and what does not leak.
  Example: `(cd /bin; ls) | wc -l`.
- IF: `if`/`elif`/`else`/`fi`, `test` and `[`. Example:
  `if [ -d /bin ]; then echo yes; else echo no; fi`.
- WHILE AND UNTIL: loops, `break [n]`, `continue [n]`, `read`. Example:
  `while read line; do echo "<$line>"; done < /notes.txt`.
- FOR: with `in`, without (over `"$@"`). Example: `for f in a b c; do echo $f; done`.
- CASE: patterns, `|`, `*)`, `;;`. Example: `case "$1" in hi*) echo greeted;; *) echo plain;; esac`.
- FUNCTIONS: definition, arguments, `return`, redirections on the body,
  lookup order. Example: `greet() { echo "hello $1"; }; greet world`.
- BUILTIN COMMANDS: every hsh builtin with its usage on one line (`.`, `:`,
  `[`, `break`, `cd`, `continue`, `eval`, `exec`, `exit`, `export`, `false`,
  `read`, `readonly`, `return`, `set`, `shift`, `test`, `true`, `unset`,
  `wait`), special builtins marked.
- EXIT STATUS: 0, 1, 2 (syntax and usage errors), 126, 127, 128+n, 141 (a
  broken pipe), 143 (stopped); the status of a pipeline, a list, a compound
  command, a script.
- DIFFERENCES FROM DASH: every documented exception, one line each --
  `&>` and `<<<` (bash's); `${x:}` is `Bad substitution`; `~user` not
  expanded; `$(...)` syntax errors worded from the inner text; `$0` is `hsh`;
  `--help`/`--version` first only; prompts and `PS4` not expanded; `-v`
  accepted, not acted on; a background builtin, function or compound
  command runs in a child hsh, which sees only exported variables; `test -r/-w/-x/-O/-G` true for anything that
  exists, `-h/-L` never; no job control, line editing, history, `trap`,
  `local`, `getopts`; commands run as Haisos processes, not by `#!`.
- SEE ALSO: `dash(1)` (https://man7.org/linux/man-pages/man1/dash.1.html),
  `man`, and every other builtin by name.

### `Hsh.cpp`

Version `1.0.0`; the help notes unchanged except what the earlier tasks
added.

### `src/haisos/HaisosFileParser.cpp` -- `GetHaisosFileTemplate`

Right after the generated `# BUILTIN rootfs ...` lines (before the `#` line
that follows them), add exactly:

```
#
# hsh, the shell, finds commands by name in PATH; with the builtins in /bin:
# ENV PATH=/bin
```

and next to the `# RUN -i /chat.md` example, the line `# RUN -i /bin/hsh`
preceded by a comment line `# An interactive shell on the console:`.

## Tests

### Unit: `tests/unit/components/Hsh.unittests/HshInteractiveTest.cpp` (new)

`TEST_F(HshShellTest, ...)` through `RunCaptured("hsh", {"-i"}, input)` (a
file as stdin, so `-i` makes it interactive), out and err compared byte for
byte with dash's:

- `PromptsAndExit`: input `echo hi\nnosuch\nexit 4\n` -> out `hi\n`, err
  `$ $ hsh: 2: nosuch: not found\n$ `, status 4.
- `IncompleteCommandsShowPs2`: `if true\nthen echo t\nfi\n` -> out `t\n`,
  err `$ > > $ \n`, status 0; `cat <<E\nx\nE\n` -> out `x\n`, err
  `$ > > $ \n`; `echo a &&\necho b\n` -> out `a\nb\n`, err `$ > $ \n`.
- `ErrorsDoNotEndTheShell`: `fi\necho after\n` -> out `after\n`, err
  `$ hsh: 1: Syntax error: "fi" unexpected\n$ $ \n`, status 0;
  `echo ${x?}\necho after\n` -> err `$ hsh: 1: x: parameter not set\n$ $ \n`,
  out `after\n`.
- `LineNumbersCountEveryLine`: `if true\nthen nosuch\nfi\nnosuch2\n` -> err
  contains `hsh: 2: nosuch: not found\n` and `hsh: 4: nosuch2: not found\n`.
- `EndOfInput`: `false\n` -> err `$ $ \n`, status 1; `echo "a\n` -> err
  `$ > hsh: 2: Syntax error: Unterminated quoted string\n$ \n`, status 2;
  `` (nothing) -> err `$ \n`, status 0.
- `PromptsFollowPs1AndPs2`: `PS1='% '; PS2='+ '\nif true\nthen :; fi\n` ->
  err `$ % + % \n`.
- `ScenarioFour`: input `echo hi | wc -c\ncd /bin; ls | wc -l\nnosuch\nexit 4\n`
  -> out `3\n<N>\n` (`<N>` = the number of builtins in `/bin`), err
  `$ $ $ hsh: 3: nosuch: not found\n$ `, status 4.
- `ATerminalStdinMakesItInteractive`: start `/bin/hsh` with `os->StartProcess`,
  `stdIn` and `stdErr` = `MockFileDescriptor(true)` (one each), `stdOut` a
  file; feed `echo $-\n`, then `EndInput()`; wait: the stdout line holds the
  letter `i` (and `s`), the stderr mock's `Written()` starts with `$ `. With
  `stdErr` not a terminal (`MockFileDescriptor(false)`): the line has no `i`,
  and nothing is written to stderr.
- `CommandStringIsNotInteractive`: `RunCaptured("hsh", {"-c", "echo $-"})`
  with a terminal-free fixture -> `\n`.

### Unit: the manual page

- `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp`,
  `EveryBuiltinsManPageIsItsHelp`: skip `hsh`.
- `tests/unit/components/BuiltinCommands.unittests/ManTest.cpp`,
  `ManPrintsEachBuiltinsHelp`: skip `hsh`; new `ManShowsTheShellsFullPage`:
  `man hsh` out == `ManPage()` of the hsh command; it has more lines than
  `hsh --help`; every heading of the list above is a line of it, in order;
  no line is longer than 79 characters; every `Example:` line is indented 7.

### Unit: the template

`tests/unit/haisos/haisos.unittests/HaisosFileParserTest.cpp`, new
`TemplateSuggestsPathForTheShell`: the template from
`GetHaisosFileTemplate({"cat", "hsh"})` has the line `# ENV PATH=/bin` after
the last `# BUILTIN rootfs` line; uncommenting `CREATE_DIR /bin`, the
`BUILTIN` lines and that line gives a haisosfile that parses, with an
`envEntries` entry named `PATH` with value `/bin`. The existing template
tests must pass unchanged.

### Haisos: `tests/haisos/hsh.haisostest/hsh.haisostest.js` (new)

Shaped like `builtins.haisostest.js`, on `spawnSync` (stdio piped, `input`
for stdin, a 60 s timeout), returning `{stdout, stderr, status}`. Every
haisosfile starts with the preamble of goal.md's "Acceptance scenarios"
(`FS rootfs MEM` ... `CREATE /abc.txt multiline END` ... `END`). One haisos
run per check:

1. `RUN /bin/hsh -c 'cat /abc.txt | wc -l; ls /bin | wc -l > /n.txt; cat /n.txt'`
   -> stdout exactly `3\n6\n`, status 0.
2. `RUN /bin/hsh -c 'ls /nope 2>/err.txt || echo "failed: $?"; cat /err.txt; cat < /abc.txt >> /out.txt 2>&1 && wc -c /out.txt'`
   -> stdout exactly `failed: 2\nls: cannot access '/nope': No such file or directory\n14 /out.txt\n`,
   status 0; `RUN /bin/ls /nope` -> stdout empty, stderr contains the `ls:`
   line, status 2; `RUN /bin/hsh -c 'exit 3'` -> status 3.
3. `CREATE /count.sh multiline END` with goal.md's script, then
   `RUN /bin/hsh /count.sh hello` -> stdout exactly `ok 3 2\ngreeted\n`, status 0.
4. `RUN -i /bin/hsh`, input `echo hi | wc -c\ncd /bin; ls | wc -l\nnosuch\nexit 4\n`
   -> stdout exactly `3\n6\n`; stderr contains `hsh: 3: nosuch: not found`
   and `$ ` exactly four times, the first at its start; status 4.
5. `CREATE /p.lua multiline END` with `print("to stdout")` / `error("boom")`,
   `RUN /bin/hsh -c '/p.lua 2>/e.txt | wc -l; cat /e.txt'` -> stdout exactly
   `1\nlua: /p.lua:2: boom\n`, status 0; `RUN /p.lua` -> status 1.
6. `RUN /bin/man wc` stdout equals `RUN /bin/wc --help` stdout (both non-empty);
   `RUN /bin/hsh -c 'man hsh | wc -l'` prints a number greater than twice the
   line count of `RUN /bin/hsh --help`; `RUN /bin/man hsh` stdout has the
   lines `QUOTING AND ESCAPING`, `PARAMETERS AND EXPANSIONS`, `PIPELINES`,
   `REDIRECTIONS`, `HERE-DOCUMENTS (HEREDOCS)`, `LISTS`, `IF`,
   `WHILE AND UNTIL`, `FOR`, `CASE`, `FUNCTIONS`; `RUN /bin/man nosuch` ->
   stderr contains `No manual entry for nosuch`, status 16.

Print `hsh haisos test passed` at the end; on failure the scenario number and
what differed, and exit 1.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/haisos.unittests --gtest_filter='*Template*'
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H hsh
bash ./scripts/test_linux.sh L H
```

## Docs

- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a section
  "Interactive" (when, the loop, prompts, line numbers, end of input, errors
  not ending the shell); the man page (`HshManPage.cpp`, its sections, the
  literal-size rule); new files.
- `src/components/BuiltinCommands/CLAUDE.md`: `hsh` row -- 1.0.0, the whole
  description (`-c`, scripts, stdin, interactive; the language; the
  builtins) and every documented exception (as in the man page's
  DIFFERENCES FROM DASH, short); `ManPage()` overridden by `hsh`.
- Root `CLAUDE.md`:
  - "Builtin Commands" table: the `hsh` row's final text -- "The Haisos
    shell, after dash: `-c`, scripts, stdin or interactive (`RUN -i
    /bin/hsh`); quoting, expansions, pipelines, redirections, heredocs,
    lists, control flow, functions; commands found in `PATH`"; the `man` row
    says `hsh` has a full page.
  - "The `haisosfile` DSL": in the example block, `ENV PATH=/bin` (with a
    comment: where hsh finds commands by name) and
    `RUN -i /bin/hsh               # an interactive shell on the console`;
    in the `RUN -i` paragraph, one sentence: `RUN -i /bin/hsh` runs the shell
    interactively, with the prompt on stderr.
  - The `haisos --init` description, if it lists what the template holds:
    mention the `ENV PATH=/bin` suggestion.

## Acceptance

- [ ] Interactive exactly when `-i`, or stdin input with stdin and stderr terminals.
- [ ] Prompts, PS2, line numbers, error handling and the end of input match the unit tests byte for byte.
- [ ] Input is read one byte at a time; the buffer is re-parsed from its first line each time.
- [ ] `man hsh` prints the full page with the headings in order; `hsh --help` is unchanged in shape; no single string literal over 16 KB.
- [ ] `# ENV PATH=/bin` is in the template; the builtin lines are still generated; template tests pass.
- [ ] `tests/haisos/hsh.haisostest/hsh.haisostest.js` passes all six scenarios.
- [ ] Root and component docs updated; all unit and haisos tests pass.

## Out of scope

- Line editing, history, completion, job control, `ignoreeof`, prompt
  expansion (goal.md: a later develop).
- Incremental reading of a non-interactive stdin.
- Windows-specific console behaviour beyond what the console descriptors
  already give.
