# Task hsh--expansion: hsh variables and word expansion

- Rock: hsh
- Depends on: hsh--parser, hsh--arith-glob
- Size: ~1000 changed lines in ~8 files (pattern matching, globbing and arithmetic were split out into hsh--arith-glob)
- Plan checked against: develop @ 8fb8324
- PR title: hsh: shell variables and POSIX word expansion

## Goal

The piece of `hsh` (the Haisos shell, a dash reimplementation) that turns the
parser's `Word`s into strings, in POSIX order, exactly as dash does: tilde
expansion, parameter expansion (every `${...}` form), command substitution
(through a callback the executor implements), arithmetic expansion, field
splitting by `IFS`, pathname expansion, quote removal, `"$@"` as separate
fields. Plus the shell's variable store (exported and read-only flags), its
options and its special parameters, seeded from the process's environment --
the state the executor and the shell builtins will work on.

Still a library with unit tests: nothing runs, `hsh` is not registered.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`,
`HshWord.h`, `HshError.h`, `HshAst.h`, `HshPattern.h`, `HshGlob.h`,
`HshArithmetic.h`; the root `CLAUDE.md` ("Security: `ICurrentProcess` is the
only door out of a process"); `interfaces/IEnvironment.h`; and the dash
manual's "Word Expansions" section (Tilde, Parameter, Command Substitution,
Arithmetic, White Space Splitting, Pathname Expansion, Quote Removal) and
"Special Parameters" (https://man7.org/linux/man-pages/man1/dash.1.html).
Every behaviour below was checked against dash 0.5.12; check more with
`dash -c '...'`.

What earlier tasks provide (namespace `Haisos::Hsh`, in
`src/components/BuiltinCommands/commands/hsh/`):

- hsh--lexer: `ShellError(message, line = 0, incomplete = false)`;
  `Word { parts, source, line }`; `WordPart { kind, text, parts, op,
  backquoted, line }`; `WordPartKind { Literal, Quoted, DoubleQuoted,
  Parameter, CommandSubstitution, Arithmetic }`; `ParameterOp { None, Length,
  UseDefault, UseDefaultIfUnset, AssignDefault, AssignDefaultIfUnset,
  ErrorIfNull, ErrorIfUnset, UseAlternative, UseAlternativeIfSet,
  RemoveSmallestSuffix, RemoveLargestSuffix, RemoveSmallestPrefix,
  RemoveLargestPrefix, Bad }`; `HereDocument { delimiter, quoted, stripTabs,
  rawBody, body, complete, terminated }`; `IsValidShellName`. Inside double
  quotes, in heredoc bodies and in the operand of a `${...}` that is inside
  double quotes, plain text is `Quoted`; unquoted text is `Literal`; text
  inside `$((...))` is `Literal`.
- hsh--parser: `Assignment { name, value }`, `Redirection`, `CaseItem`
  (patterns are `Word`s), `ForCommand::words` -- the words this task expands.
- hsh--arith-glob: `MatchPattern`, `HasPatternCharacters`, `UnescapePattern`,
  `EscapeForPattern`, `RemovePattern(value, pattern, PatternRemoval)`,
  `PatternRemoval`; `IPathnameSource { ReadDirectory, Exists }`,
  `ExpandPathname(pattern, source)`; `IArithmeticVariables { Get, Set }`,
  `EvaluateArithmetic(expression, variables)`.
- Already on develop: `IEnvironment::GetVariableNames()`, `GetVariable(name)`
  (`interfaces/IEnvironment.h`); `CreateEnvironment()` in
  `src/components/Environment/Environment.h` (for the tests).

The executor (hsh--executor) and the builtins (hsh--control-flow) are
planned on top of the API below: name and shape everything exactly as
written.

## Changes

All in namespace `Haisos::Hsh`, in `src/components/BuiltinCommands/commands/hsh/`;
plain portable C++17.

### `HshVariables.h` and `HshVariables.cpp` (new)

```cpp
// The shell's named variables. Names are valid shell names (IsValidShellName);
// callers check before calling.
class ShellVariables {
public:
    // Every variable of |environment| whose name is a valid shell name, set
    // and exported -- what a shell does with the environment it starts with.
    void ImportFrom(const IEnvironment& environment);

    std::optional<std::string> Get(const std::string& name) const;  // nullopt when unset
    bool IsSet(const std::string& name) const;
    // Sets the value, creating the variable; false, changing nothing, when it is read-only.
    bool Set(const std::string& name, const std::string& value);
    // Removes it with its flags; false when it is read-only; true when it was not there.
    bool Unset(const std::string& name);

    // export NAME: marks it exported, set or not (an unset one is exported once set).
    void Export(const std::string& name);
    bool IsExported(const std::string& name) const;
    // readonly NAME: from then on Set and Unset fail.
    void MakeReadonly(const std::string& name);
    bool IsReadonly(const std::string& name) const;

    // Every name that is set or carries a flag, sorted by byte (for `set`,
    // `export -p`, `readonly -p`).
    std::vector<std::string> Names() const;
    // Name and value of every set, exported variable, sorted by name: what a
    // command the shell starts gets as its environment.
    std::vector<std::pair<std::string, std::string>> ExportedVariables() const;
};

// dash's options (set -e, ...).
struct ShellOptions {
    bool errexit = false;      // -e
    bool noglob = false;       // -f
    bool interactive = false;  // -i
    bool noexec = false;       // -n
    bool stdinInput = false;   // -s
    bool xtrace = false;       // -x
    bool verbose = false;      // -v
    bool noclobber = false;    // -C
    bool allexport = false;    // -a
    bool nounset = false;      // -u
};
// $-: the letters of the options that are on, in dash's order "uaCvxsnife"
// (dash -euaCfx -c 'echo $-' prints uaCxfe).
std::string OptionLetters(const ShellOptions& options);

// Everything a running shell knows that expansions read. The executor owns
// one; a subshell works on a copy.
struct ShellState {
    ShellVariables variables;
    ShellOptions options;
    std::string arg0 = "hsh";                   // $0
    std::vector<std::string> positional;        // $1, $2, ...
    int lastExitStatus = 0;                     // $?
    uint64_t shellPid = 0;                      // $$
    std::optional<uint64_t> lastBackgroundPid;  // $!: unset until a command runs in the background
};
```

Implementation: a `std::map<std::string, Entry>` with `Entry {
std::optional<std::string> value; bool exported; bool readonly; }`.
`IsValidShellName` from `HshWord.h`.

### `HshExpansion.h` and `HshExpansion.cpp` (new)

```cpp
struct CommandSubstitutionResult {
    std::string output;   // everything the command wrote to its stdout
    int exitStatus = 0;
};

// What expansion needs from the shell running it. The executor implements it:
// ReadDirectory/Exists with the process's IFileIO (ICurrentProcess::IO() --
// never a filesystem of its own), RunCommandSubstitution by parsing the text
// (ParseProgram) and running it in a subshell with its stdout on a pipe.
class IExpansionHost : public IPathnameSource {
public:
    // Runs |source| -- the text of a $(...) as written, or of a `...` after
    // backquote processing -- whose first line is |line|. Throws ShellError
    // when the shell must stop (a syntax error is reported by the subshell
    // itself and only gives a status).
    virtual CommandSubstitutionResult RunCommandSubstitution(const std::string& source, int line) = 0;
};

class Expander {
public:
    Expander(ShellState& state, IExpansionHost& host);

    // Command words and for-loop words: every expansion, field splitting,
    // pathname expansion (unless options.noglob), quote removal. Zero or more
    // fields per word.
    std::vector<std::string> ExpandWords(const std::vector<Word>& words);
    std::vector<std::string> ExpandWord(const Word& word);

    // An assignment's value (x=..., and the operand of ${x:=...}): tilde at the
    // start and after every unquoted ':', parameters, command substitutions,
    // arithmetic, quote removal. No splitting, no globbing.
    std::string ExpandAssignmentValue(const Word& value);

    // A redirection target, a here-string, the case subject: tilde at the
    // start, parameters, command substitutions, arithmetic, quote removal --
    // one string, no splitting, no globbing (dash does neither for a
    // non-interactive shell's redirections: `echo hi > *` writes a file "*").
    std::string ExpandToString(const Word& word);

    // A case pattern: as ExpandToString, but every character that was quoted
    // comes out escaped (EscapeForPattern) so MatchPattern takes it literally,
    // while unquoted * ? [ keep their meaning (`"a"*`, `$x` with x='*', `\*`).
    std::string ExpandPattern(const Word& word);

    // A heredoc body: rawBody as it is when the delimiter was quoted; else the
    // body's parameters, command substitutions and arithmetic expanded (no
    // tilde, no splitting, no globbing).
    std::string ExpandHereDocument(const HereDocument& hereDoc);
};
```

Every method throws `ShellError` (line 0: the executor knows the command's
line) on an expansion error. Command substitutions set
`state.lastExitStatus` to their status, in order (so `x=$(false)` leaves `$?`
at 1); nothing else here changes `lastExitStatus`.

#### The walk

One recursive walk over a word's parts, shared by every method, builds a list
of **fields under construction**. A field is a sequence of elements; each is a
character with two flags -- `quoted` (it came from a quoted context: no
splitting, literal for globbing) and `splittable` (it came from an unquoted
expansion: subject to IFS splitting) -- or a **quote mark**, an element with no
character recording that a quoted (possibly empty) string stood here. A
**hard break** ends the current field and starts a new one (between the
values of `$@`). The walk carries two flags: `inDoubleQuotes`, and
`inOperand` (inside the operand of an unquoted `${...}`).

Per part:

- **Literal**: its characters, not quoted; splittable only when `inOperand`
  (dash splits `${x:-a b}` into two fields). Tilde: see below.
- **Quoted**: a quote mark, then its characters, quoted.
- **DoubleQuoted**: a quote mark -- unless its parts are only `Parameter`
  parts named `@` with op None (so `"$@"` alone with no positional
  parameters gives no field at all) -- then its parts with `inDoubleQuotes`.
- **Parameter**: see "Parameters". A value is emitted as characters quoted
  when `inDoubleQuotes`, else splittable; a quoted value is preceded by a
  quote mark (so `"$x"` with x empty still gives one empty field).
- **CommandSubstitution**: `host.RunCommandSubstitution(text, line)`; every
  trailing `'\n'` of the output removed; `state.lastExitStatus` set; emitted
  as a value.
- **Arithmetic**: its parts walked into one string (as ExpandToString, but no
  tilde), `EvaluateArithmetic` with an `IArithmeticVariables` over
  `state.variables` (Get; Set, plus Export when `options.allexport`), the
  decimal result emitted as a value.

**Tilde** (ExpandWords, ExpandToString, ExpandPattern, ExpandAssignmentValue;
not ExpandHereDocument, not inside arithmetic): when the word's first part is
a Literal starting with `~`, the tilde-prefix is the `~` and the characters
after it up to the first `/` in that Literal (in ExpandAssignmentValue, up to
the first `/` or `:`), or the whole Literal when it has neither and is the
last part. A prefix that is `~` alone becomes the value of `HOME` (empty if
HOME is empty), emitted quoted (`HOME="/a b"` gives one field); `HOME` unset,
`~user`, or a Literal that ends before a `/` with more parts after it
(`~"/a"`, `~$x`) leave the text as it is. In ExpandAssignmentValue the same is
done after every unquoted `:` of a Literal part (`x=~:~/b:a~` gives
`/h:/h/b:a~`). The operand of `${x:-~}` is a word of its own: a tilde at its
start is expanded (unquoted `${`), not inside double quotes.

#### Parameters

The value of a name:

- `@`, `*`: the positional parameters, as a list. Always set; null when
  joined they are empty.
- `#`: their count; `?`: `lastExitStatus`; `$`: `shellPid`; `-`:
  `OptionLetters(options)`; `0`: `arg0`; `!`: `lastBackgroundPid`, unset
  when empty; digits: positional parameter n (`positional[n-1]`), unset
  beyond the count; a name: `state.variables.Get`.

Then by op (*unset* / *null* = set and empty):

| Op | Result |
|---|---|
| None | the value; unset with `options.nounset` (and not `@`/`*`): error `<name>: parameter not set` |
| Length | decimal length in bytes (`@`/`*`: of the parameters joined with spaces); same nounset error |
| UseDefault / UseDefaultIfUnset | when unset or null (`:-`) / unset (`-`): the operand, walked in place (with `inOperand` when not in double quotes); else the value |
| AssignDefault / AssignDefaultIfUnset | when unset or null / unset: the operand expanded as by ExpandAssignmentValue, assigned (name not a variable name, e.g. `${1=a}`: error `<name>: bad variable name`; read-only: `<name>: is read only`; Export too when allexport), and the new value used; else the value |
| ErrorIfNull / ErrorIfUnset | when unset or null / unset: error `<name>: <operand expanded as by ExpandToString>`, or with no operand `<name>: parameter not set or null` (`:?`) / `<name>: parameter not set` (`?`); else the value |
| UseAlternative / UseAlternativeIfSet | when set and not null (`:+`) / set (`+`): the operand, walked in place; else nothing |
| Remove* | the value with `RemovePattern(value, pattern, which)`, the pattern being the operand expanded as by ExpandPattern; `@`/`*`: applied to each parameter; unset with nounset: the nounset error |
| Bad | error `Bad substitution` |

How `@` and `*` are emitted (op None, or after a Remove op):

- in double quotes, `@`: each parameter quoted, preceded by a quote mark, with
  a hard break between consecutive ones (`"x$@y"` with `a b` gives `xa`, `by`;
  with `"" ""` two empty fields);
- in double quotes, `*`: the parameters joined by the first character of
  `IFS` (a space when IFS is unset, nothing when it is empty), as one quoted
  value (one field even with no parameters);
- unquoted, either: each parameter splittable, with a hard break between them
  (so they stay separate fields even when IFS is empty, and an empty one
  disappears).

For any other op on `@`/`*`, the value is the parameters joined with spaces.

#### Finishing

- **Fields** (ExpandWord): split each field under construction:
  1. Let IFS be the variable's value, `" \t\n"` when unset. When it is empty,
     no splitting happens (only hard breaks separate fields). IFS white space
     is whichever of space, tab, newline is in IFS.
  2. Walk the elements. Skip leading splittable IFS-white-space characters.
     A splittable IFS character ends the current field: if it is white space,
     emit the field when it has content, skip the following splittable IFS
     white space, and if a splittable non-white IFS character comes next,
     consume it (and the white space after it) as part of the same
     delimiter; if it is a non-white IFS character, emit the field even if
     empty and skip the splittable IFS white space after it. Any other
     element (a character not splittable or not in IFS, or a quote mark)
     is content of the current field. At the end, emit the field when it has
     content.
     (So IFS=`: ` splits ` :a : :b: ` into ``, `a`, ``, `b`; IFS=`:` splits
     `:a::b:` into ``, `a`, ``, `b`; `  a  b  ` with the default IFS into `a`,
     `b`; an unquoted empty `$x` gives no field.)
  3. Pathname expansion, unless `options.noglob`: build a pattern from the
     field -- a quoted character as `\` + character (except `/`, kept as is),
     an unquoted `\` as `\\`, any other unquoted character as is. When
     `HasPatternCharacters(pattern)` and `ExpandPathname(pattern, host)` is
     not empty, its results replace the field; otherwise the field stays.
  4. Quote removal: a field's text is its characters (quote marks dropped).
- **String** (ExpandToString, ExpandAssignmentValue, ExpandHereDocument, an
  operand used as a string): hard breaks become the first character of IFS
  (space when unset, nothing when empty -- dash: `x=$@` with IFS=x gives
  `axb`), quote marks are dropped, no splitting, no globbing.
- **Pattern** (ExpandPattern, the operand of a Remove op): as String, but each
  quoted character passed through `EscapeForPattern`.

#### Errors (byte for byte; `ShellError(message)`)

| Message | When |
|---|---|
| `Bad substitution` | a `Bad` parameter part is expanded |
| `<name>: parameter not set` | nounset and an unset parameter; `${name?}` |
| `<name>: parameter not set or null` | `${name:?}` |
| `<name>: <text>` | `${name?text}`, `${name:?text}` |
| `<name>: bad variable name` | `${1=a}`, `${@:=a}`: assigning what is not a variable |
| `<name>: is read only` | assigning a read-only variable (also from `$((...))`) |
| the arithmetic messages of hsh--arith-glob | from `EvaluateArithmetic` |

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `commands/hsh/HshVariables.cpp` and `commands/hsh/HshExpansion.cpp` to
the `BuiltinCommands` sources. No new link dependency (`IEnvironment` is an
interface header only).

## Tests

Add `HshVariablesTest.cpp` and `HshExpansionTest.cpp` to
`add_executable(Hsh.unittests ...)` in
`tests/unit/components/Hsh.unittests/CMakeLists.txt`, and `Environment` to its
`target_link_libraries` (for `CreateEnvironment()` in the variables test).

The expansion tests build words by lexing: a helper takes a source like
`echo $x "a b"`, runs `ParseProgram`, and takes the words of the first simple
command (or of its assignment, redirection, case or for), so tests are
written as shell text. A fake `IExpansionHost` holds a map of
directory -> entries for globbing and a map of command text ->
{output, status} for substitutions, and records the texts and lines it was
asked to run. Most tests are tables of {state setup, source, expected fields}.

`HshVariablesTest.cpp`:

- `HshVariablesTest.SetGetUnset`: set, overwrite, unset, `IsSet`; Get of
  unknown is nullopt.
- `HshVariablesTest.ReadonlyAndExport`: Set/Unset of a read-only fail and
  change nothing; `Export` of an unset name, then Set: in
  `ExportedVariables`; `Names` sorted by byte, flagged-but-unset names included.
- `HshVariablesTest.ImportFromEnvironment`: an environment from
  `CreateEnvironment()` with `HOME`, `PATH` and an invalid name `A-B`:
  the first two set and exported, the third skipped.
- `HshVariablesTest.OptionLetters`: none is empty; e,u,a,C,f,x on gives `uaCxfe`;
  i alone gives `i`.

`HshExpansionTest.cpp` (expected values from dash):

- `HshExpansionTest.QuotingAndQuoteRemoval`: `'a b'`, `"a b"`, `a\ b`,
  `"\$x"`, `""` (one empty field), `''""` (one), `a""` (`a`).
- `HshExpansionTest.Tilde`: HOME=/h: `~` `~/x` `a~` `"~"` `\~` `~"/a"` `~:`
  `~root` (left as is: `~user` is not supported -- a documented deviation, dash gives `/root`); HOME empty gives an empty field; HOME unset leaves
  `~/a`; HOME="/a b" gives one field; assignment `x=~:~/b:a~` gives
  `/h:/h/b:a~`; `${x:-~}` gives `/h`, `"${x:-~}"` gives `~`.
- `HshExpansionTest.SpecialParameters`: `$#`, `$?`, `$$`, `$!` (empty when
  unset), `$-`, `$0`, `$1`, `$10` (`$1` then `0`), `${10}`.
- `HshExpansionTest.ParameterOps`: every op set/null/unset, from dash:
  x=1: `${x+set} ${y+set} ${y-unset}` gives `set unset`; `${x:-a b}` unquoted
  2 fields, quoted 1; `${x-${y-z}}` gives `z`; `${x:=a b}` assigns `a b`;
  x=hello: `${x%l*} ${x%%l*} ${x#*l} ${x##*l} ${#x}` gives `hel he lo o 5`;
  x=abc: `${x#"a"} ${x#\a} ${x#[ab]} ${x%?}` gives `bc bc bc ab`;
  x='*': `${x#"*"}` gives nothing, `${x#*}` gives `*`; `${x#}` keeps x;
  x=é: `${#x}` is 2; `set -- a b c`: `${#@}` and `${#*}` are 5;
  `set -- a b`: `${@#a}` gives `b`; `${@-x}` gives nothing; `${*:-y}` gives `y`.
- `HshExpansionTest.ParameterErrors`: each message of the table byte for
  byte: `${x?}`, `${x:?}` (x empty), `${x?a b  c}`, `${1=a}`, read-only
  `${r:=v}`, `${x;}` (Bad substitution), nounset with `$x`, `${#x}`,
  `${x#a}`, `$1` -- but not `${x-ok}`, `$#`, `$@`.
- `HshExpansionTest.PositionalParameters`: `set -- a "b c" d`: `"$@"` 3 fields,
  `$@` 4, `"$*"` 1 (`a b c d`), with IFS=: `"$*"` is `a:b c:d`, with IFS
  empty `"$*"` is `ab cd` and `$*` still 3 fields (`a`, `b c`, `d`);
  `"x$@y"` with `a b` gives `xa`, `by`; with none `xy`; `"$@"` with none gives
  no field; `"$@"""` with none gives one empty field; `set -- "" ""`:
  `"$@"` gives two empty fields, `$@` none; `set -- "" a`: `$@` gives `a`;
  `x="$@"` (assignment) with IFS=x gives `axb`.
- `HshExpansionTest.FieldSplitting`: default IFS on `  a  b  `; IFS=: on
  `:a::b:`; IFS=`: ` on ` :a : :b: `; IFS empty; `"$x"` never split; an
  unquoted empty `$x` with `""` and `$x` around giving one empty field
  (`$x "" $x`); IFS=: and `${x:-a:b}` giving 2 fields; `x=" "`, `$x` giving
  none.
- `HshExpansionTest.CommandSubstitution`: `$(echo a)` output `"a\n\n"` gives
  `a`; unquoted output with spaces splits, quoted does not;
  `lastExitStatus` set from the result; the host got the text and line
  (`$(...)` text as written, backquote text after processing); nested in
  `${x:-$(echo d)}`.
- `HshExpansionTest.Arithmetic`: `$((1+2))`, `$(($x * 3))` with x=2,
  `$((x=3))` assigns x, `$((y))` with y unset is 0, `"$((1+2))"`,
  `$((1 + $(echo 2)))` with the host returning `2`; a read-only assignment
  error; `$((1/0))` error text.
- `HshExpansionTest.PathnameExpansion`: with the fake host's listing
  (`B C a ab 'a b' .hid d1 d2`): `*`, `a*`, `"a"*` (`a a b ab`), `a\*`, `"*"`
  (literal), `x="a*"; $x` globs and `"$x"` does not, `x="[a]"; $x` gives `a`,
  `nomatch*` kept, `noglob` on keeps `*`.
- `HshExpansionTest.Patterns`: `ExpandPattern` of `"a*"` is `a\*`, of `a*` is
  `a*`, of `$x` with x='*' is `*`, of `"$x"` is `\*`, of `\\` is `\\`.
- `HshExpansionTest.StringContexts`: `ExpandToString` of `$x` with x="a  b"
  keeps the spaces, of `*` gives `*`; `ExpandHereDocument` of a quoted
  heredoc returns rawBody as is (with `$x`), of an unquoted one expands `$x`,
  `$(...)`, `$((...))` and keeps `~` and `"`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a section on
`ShellState` (who owns it, subshells copy it), `ShellVariables`, the
`Expander` methods and which context uses which (command words, assignments,
redirections, here-strings, case subject and patterns, heredocs), the order of
expansions, the element model (quoted / splittable / quote mark / hard
break), and `IExpansionHost` (the executor implements it with the process's
`IFileIO` and a subshell; globbing never bypasses `ICurrentProcess`).

## Acceptance

- [ ] Every type and function above exists with exactly these names, fields
      and signatures, in namespace `Haisos::Hsh`.
- [ ] Expansion reads files only through `IExpansionHost` (no `IFileIO`,
      `IFileSystem` or host filesystem access here) and runs commands only
      through `RunCommandSubstitution`.
- [ ] Pattern matching, globbing and arithmetic are hsh--arith-glob's
      functions, not reimplemented.
- [ ] `"$@"`, `"$*"`, `$@`, `$*` and field splitting give dash's fields for
      every case in the tests.
- [ ] Every error message matches its table row byte for byte.
- [ ] Builds on Linux; `Hsh.unittests` passes; all other unit tests still pass.

## Out of scope

- Implementing `IExpansionHost` (parsing and running substitutions, reading
  directories through `IFileIO`), the shell's startup variables (`IFS`,
  `PS1`, `PS2`, `PS4`, `OPTIND`, `PPID`, `PWD`), turning `ExportedVariables`
  into a child's `IEnvironment`, `set -x` tracing, `-a` on plain assignments
  (hsh--executor); `export`, `readonly`, `unset`, `set`, `shift`, `local`
  scopes (hsh--control-flow; `local` is out of the develop).
- `~user`, `~+`, `~-`; bash's `${x:offset}`, `${x/a/b}`, `${!x}`, `${x^^}`,
  `$'...'`; locale-aware lengths (bytes, as dash).
