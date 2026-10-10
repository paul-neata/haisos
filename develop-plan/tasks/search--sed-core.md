# Task search--sed-core: sed -- scripts, addresses, s, and the basic commands

- Rock: search
- Depends on: base--regex-match (`Regex`, with `multiline`), coreutils--sort (`BuiltinText.h`)
- Size: ~950 changed lines in ~8 files
- Plan checked against: develop @ f6f5bec
- PR title: Add the sed builtin: scripts, addresses, s and basic commands

## Goal

`sed` is a builtin command (placed with `BUILTIN rootfs sed /bin/sed`)
behaving as GNU sed 4.9 (Ubuntu 24.04, the task container's) for: the
options `-n -e -f -E -r -s` (`--posix` accepted), scripts from `-e`, `-f`
and the first operand, `;` and newlines, `{ }` blocks, every address form,
and the commands `s` (all flags but `w FILE` and `e`), `d p n q Q = #
:label` (`b t T` and the rest come in search--sed-advanced). It streams
its input line by line, and reports script errors exactly as GNU does
(`sed: -e expression #1, char 3: unterminated `s' command`). So `sed -E
's/(Create)\(\)/\1Instance()/g' a.cpp` (acceptance scenario 2, without
`-i`) prints what GNU prints.

## Context

Read first: root `CLAUDE.md` ("Security", "Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h/.cpp`
(`BeginBuiltin`, `BuiltinContext::Out`/`Error`/`ErrorText`/`Flush`),
`commands/cat/Cat.cpp` (reading files and stdin, stopping).

What earlier tasks provide, as if on develop (their plans in
`develop-plan/tasks/` are the authority):
- `src/components/Regex/Regex.h`: `Regex::Compile(pattern, RegexOptions{RegexSyntax::Basic/Extended, ignoreCase, multiline}, error)`;
  `Search(text, start, match, flags)` with `kRegexNotBol`/`kRegexNotEol`;
  `GroupCount()`; `RegexMatch::groups` ((-1,-1) for a group that took no
  part). `.` and `[^...]` match `\n`; `^`/`$` anchor at the text's ends, also
  at inner newlines with `multiline` -- GNU sed's pattern-space semantics.
  The engine does **not** translate `\n`, `\t` ...: sed does (below).
  Compile errors carry glibc's text (`Unmatched ( or \(`). `Regex` is already linked to `BuiltinCommands`
  (CMake `target_link_libraries`).
- `BuiltinText.h` (coreutils--sort): `OpenInputOperand(context, name,
  failure)` with `InputOpenFailure`, `BuiltinLineReader(context, input,
  delimiter)` and `Next(line, delimited)` with `LineReadResult`.

Rules that bite: files only through `context.IO()`; GNU's messages, output
and exit statuses byte for byte; every GNU sed option in `Options()`
(untreated ones `kBuiltinNotTreated`); `--help` from `BuiltinHelpText`;
registered in `CreateStandardBuiltinCommands()` (the `--init` template);
portable C++17 without `<regex>` or POSIX headers; stops promptly on a stop
(the line reader returns `Stopped`), a broken stdout pipe ends it with 141
(through `context.Out`).

## Changes

All in `src/components/BuiltinCommands/commands/sed/`, namespace
`Haisos::Sed` for the internals. Designed so search--sed-advanced adds
commands in the same `switch`es.

### `commands/sed/SedScript.h` (new) -- the compiled script

```cpp
namespace Haisos::Sed {

enum class AddressKind { None, Line, Last, Regex, Step /* first~step */, Zero /* 0, of 0,/re/ */,
                         RelativeLines /* +N */, Multiple /* ~N */ };
struct Address {
    AddressKind kind = AddressKind::None;
    uint64_t line = 0, step = 0;              // Line: line; Step: line~step; +N / ~N: line = N
    std::shared_ptr<const Regex> regex;       // Regex; null for "//" (the last regex used)
};

struct Replacement;   // the parsed right-hand side of s (pieces: literal text,
                      // group n (0 for &), case conversion \L \U \l \u \E)

struct Command {
    Address a1, a2;
    bool negate = false;         // addr!
    char name = 0;               // 's', 'd', 'p', '{', '}', ':', 'b', ...
    // s: the regex (null: the last regex), the replacement, flags
    std::shared_ptr<const Regex> regex;
    std::shared_ptr<Replacement> replacement;
    bool global = false, print = false;
    uint64_t occurrence = 1;     // s///N
    int printCount = 0;          // GNU allows only one p
    std::string text;            // label (: b t T), file name (r R w W), a/i/c text
    int intArg = 0;              // q/Q exit code, l line length
    size_t jump = 0;             // '{': index after the matching '}'; b/t/T: target index
    bool rangeActive = false;    // runtime state of a range address
    uint64_t rangeEnd = 0;       // runtime: the last line of a +N / numeric range
};

struct Script {
    std::vector<Command> commands;
    bool quietFromScript = false;   // "#n" as the first line of the script
};

}
```

### `commands/sed/SedParser.h/.cpp` (new)

```cpp
namespace Haisos::Sed {
struct ScriptPiece { std::string text; bool fromFile; std::string fileName; };  // one -e, -f, or the operand
// Parses every piece in order into |script|. On an error writes GNU's
// message with context.Error and returns false (exit 1).
bool ParseScript(BuiltinContext& context, const std::vector<ScriptPiece>& pieces,
                 bool extendedRegex, Script& script);
}
```

How GNU assembles a script: each `-e` text and each `-f` file's content are
read as one stream, each piece followed by a newline (so `-e 'a\' -e 'foo'`
is one `a` command whose text is `foo` -- in search--sed-advanced). Error
locations: within an `-e` (or the script operand) `-e expression #N, char
M` -- N counts `-e` pieces from 1 (the operand script is #1), M is the
number of characters of that piece read so far, including the one that
revealed the error; within `-f F`, `file F line L` (L 1-based). Unmatched
`{` is reported after the whole script with char 0. `-f` unreadable:
`sed: couldn't open file nosuch.sed: No such file or directory`, exit **4**
(`-f -` reads stdin).

Grammar, GNU's (whitespace and `;` between commands skipped; `#` comment to
end of line; `#n` as the first two characters of the script, followed by a
newline or the end, is `-n` -- but only in the first piece, and only when
it is `-e`/operand or `-f`: `sed -e '#n' -e p` prints once):
- address: `N` (decimal), `$`, `/RE/` or `\cREc` (any delimiter c; `\c`
  inside is a literal c), each regex optionally followed by `I` and/or `M`;
  `first~step`; then optionally `,` and a second: `N`, `$`, `/RE/`, `+N`,
  `~N`. `0,/RE/` only with a regex second address. Spaces allowed before
  `!` and the command; `!` may be followed by spaces.
- `{` opens a block (its index stacked), `}` closes it (takes no address).
- commands of this task: `s`, `d`, `p`, `n`, `q [N]`, `Q [N]`, `=`, `:LABEL`
  (label to newline or `;`, whitespace before it skipped), `#`.
  `q`/`Q` take at most one address (`1,2q` -> ``sed: -e expression #1,
  char 4: command only uses one address``); `:` takes none (`1,2:a` ->
  `sed: -e expression #1, char 4: : doesn't want any addresses`); `=` takes
  two.
- After a command: spaces, then `;`, newline, `}` or `#`; anything else
  ``extra characters after command``.

`s` parsing: delimiter any byte but newline and backslash; the regex up to
an unescaped delimiter (`\delim` is the delimiter itself, literally;
inside a bracket expression the delimiter does not end it -- GNU: `s/[/]/x/`
is fine); then the replacement likewise; then flags: `g`, `p`, a number
(`N` > 0; 0 -> ``number option to `s' command may not be zero``), `i`/`I`,
`m`/`M`, `e` (accepted, `NotTreated("s///e")`, no effect), `w FILE` (comes
with search--sed-advanced: until then ``unknown option to `s'``).

Regex text conversion before `Regex::Compile` (GNU sed's own escapes),
outside and inside brackets: `\n` newline, `\t` tab, `\f` `\v` `\a` `\r`,
`\dNNN` decimal, `\oNNN` octal, `\xHH` hex, `\cX` control-X. In brackets
GNU accepts `[\n]` and `[\t]` as newline/tab (sed's preprocessing), other
backslashes in brackets stay literal. Empty regex (`//`) = the last regex
used at run time (`sed: -e expression #1, char 0: no previous regular
expression`, exit 1, if there was none -- checked when it runs). `-E`/`-r`
-> `RegexSyntax::Extended`; `I` -> `ignoreCase`; `M` -> `multiline`. A
compile error is reported at the position after the s command's flags (or
the address): ``sed: -e expression #1, char 8: Unmatched ( or \(``.

Replacement: `&` whole match, `\1`-`\9` groups (a group number above the
regex's `GroupCount()` -> ``invalid reference \1 on `s' command's RHS``,
at the char after the flags), `\n` newline, `\t` etc. as above, `\&` and
`\\` literal, backslash-newline a newline, `\L \U` (case of the rest until
`\E`), `\l \u` (the next character only), `\E`; any other `\c` is c.

Errors to reproduce (verified with `LC_ALL=C sed` 4.9; the char numbers matter):

| Script | Message after `sed: -e expression #1, char ` |
|--------|------|
| `s/a/b` | ``5: unterminated `s' command`` |
| `s/a` | ``3: unterminated `s' command`` |
| `s/[/x/` | ``6: unterminated `s' command`` |
| `k` | ``1: unknown command: `k'`` |
| `p;k` | ``3: unknown command: `k'`` |
| `{p` | ``0: unmatched `{'`` |
| `p}` | ``2: unexpected `}'`` |
| `s/a/b/q` | ``7: unknown option to `s'`` |
| `s/a/b/gg` | ``8: multiple `g' options to `s' command`` |
| `s/a/b/pp` | ``8: multiple `p' options to `s' command`` |
| `s/a/b/0` | ``7: number option to `s' command may not be zero`` |
| `s/\(a/b/` | `8: Unmatched ( or \(` |
| `s/a/\1/` | ``7: invalid reference \1 on `s' command's RHS`` |
| `:` | `1: ":" lacks a label` |
| `0p` | `2: invalid usage of line address 0` |
| `0,5p` | `4: invalid usage of line address 0` |
| `1,2,3p` | ``4: unknown command: `,'`` |
| `1!!p` | ``3: multiple `!'s`` |
| `p x` | `3: extra characters after command` |
| `/x` | `2: unterminated address regex` |
| `\%x` | `3: unterminated address regex` |

`sed -e p -e k` -> ``sed: -e expression #2, char 1: unknown command: `k'``;
`-f bad.sed` (second line `k`) -> ``sed: file bad.sed line 2: unknown
command: `k'``. A `b`/`t`/`T` label not defined anywhere: ``sed: can't
find label for jump to `foo'``, exit **4** (with search--sed-advanced; the
label table is built here). Commands search--sed-advanced adds are
``unknown command`` until then.

### `commands/sed/SedExecutor.h/.cpp` (new)

```cpp
namespace Haisos::Sed {
struct ExecSettings { bool quiet; bool separate; char delimiter = '\n'; };
// Runs |script| over |inputs| ("-" is stdin), writing through context.Out.
// Returns the exit status: 0, 2 if an input could not be read (the others
// still are), q/Q's code, 4 on an I/O error, 1 on a runtime script error.
int RunScript(BuiltinContext& context, Script& script, const std::vector<std::string>& inputs,
              const ExecSettings& settings);
}
```

**Input** (a class `SedInput` in this file): opens the operands in turn
with `OpenInputOperand`; a missing one -> `sed: can't read nofile: No such
file or directory` (the name raw, unquoted), status 2, next; a directory
-> `sed: read error on dd: Is a directory`, and sed stops at once with
exit 4 (later files are not read: verified). Lines come from `BuiltinLineReader`
with the delimiter. It always reads **one line ahead**, so it knows whether
the current line is the last (`$`): across files, unless `-s` (then each
file's last line is `$`, and line numbers restart at 1 with each file --
GNU: with `f` = 2 lines and `g` = 1 line, `sed -s -n '$=' f g` prints `2`
then `1`, `sed -n '$=' f g` prints `3`). It remembers whether the current line ended with
its delimiter.

**Output and the missing newline** (GNU's `output_missing_newline`): a line
read without a trailing newline is written without one; but before
anything else is written to the same output afterwards, the missing newline
is written first. So `printf line | sed p` prints `line\nline` (no final
newline) and `sed -n p nonl in.txt` prints `line\none\n...`.

**The cycle**: read a line into the pattern space; run the commands from
index 0; at the end, unless `-n` (or `#n`), print the pattern space and its
newline; repeat. Commands:
- address matching per command: `Line` n == current line; `Last` last line;
  `Regex` search in the pattern space (the empty regex = last used; every
  regex used -- address or `s` -- becomes "last used"); `Step` first~step:
  line >= first and (line - first) % step == 0 (step 0: line == first);
  ranges: when inactive and a1 matches -> active (and if a2 is a line number
  <= the current line, the range is just this line); while active, a2 is
  checked from the next line on (`/re/` searched, `N` reached or passed, `+N`
  counts, `~N` the next multiple of N) and ends the range inclusive; `0,/re/`
  starts active before line 1 so the regex can end it on line 1. Then
  `negate`.
- `{` with a non-matching address jumps past its `}`.
- `s`: leftmost match, then replace occurrence N (with `g`: N and every
  later one); after an empty match, the next search starts one byte later
  with that byte copied, and an empty match right where the previous match
  ended is skipped (`echo abc | sed 's/x*/-/g'` -> `-a-b-c-`; `echo baaac |
  sed 's/a*/x/g'` -> `xbxcx`; `echo foooo | sed 's/o/0/2g'` -> `fo000`).
  Searches after the first use `kRegexNotBol` only through `start > 0` (the
  engine looks at `text[start-1]`). Sets the "substituted" flag (for `t`,
  next task); `p` prints the result.
- `d`: end the cycle without printing. `p`: print. `n`: print (unless
  quiet), read the next line; none -> GNU (not POSIXLY_CORRECT) ends without
  running the rest and without printing again. `q [N]`: print (unless
  quiet), exit N. `Q [N]`: exit N, no print. `=`: the line number and `\n`
  to the output. `:` nothing. `}` nothing.

Verified behaviours to keep: `sed -n '2,4{p;2q}'` prints `2`; `sed -n
'$!{n;p}'` on 1,2,3 prints `2`; `sed 'n;d'` prints `1`, `3`; `sed -n
'0,/one/p'` prints `one`; `sed -n '1,/one/p'` prints all three lines;
`sed -n '2,1p'` prints `two`; `sed -n '2,~2p'` prints `two`; `sed -n
'/two/,+1p'` prints `two`, `three`; `sed 1~2p -n` (options after the
script) works.

### `commands/sed/Sed.cpp` (new) -- the command

`CreateSedCommand()`; `sed`, `1.0.0`; summary `stream editor for filtering
and transforming text`; usage `sed [OPTION]... {script-only-if-no-other-script}
[input-file]...`.

Options (GNU sed 4.9's, every one): treated here -- `-n, --quiet` (and a
second row `--silent`, same id), `-e, --expression=script`, `-f,
--file=script-file`, `-E, --regexp-extended`, `-r` (same id as -E), `-s,
--separate`; table rows that search--sed-advanced turns into treated ones,
**kBuiltinNotTreated for now**: `-i, --in-place[=SUFFIX]`
(OptionalAttached), `-l, --line-length=N` (Required), `-z, --null-data`
(and `--zero-terminated`), `-u, --unbuffered`, `--follow-symlinks`,
`--sandbox`; not treated for good: `--debug`, `--posix` (GNU extensions
stay on: documented).

`Run`: `BeginBuiltin(context, *this, 1, status)`; collect pieces from `-e`
and `-f` in order; none -> the first operand is the script. No script at
all: GNU prints its usage to stderr with status 1 -- write
`BuiltinHelpText(*this)` with `ErrorText` and return 1. Then
`ParseScript`, `RunScript` with the remaining operands (none: `-`).

### Registration and build

`BuiltinCommandList.h`: declare `CreateSedCommand()` after `CreateRmdirCommand()` and register it there in `CreateStandardBuiltinCommands()` (before `CreateSeqCommand()`, alphabetical);
`src/components/BuiltinCommands/CMakeLists.txt`: `commands/sed/Sed.cpp`, `SedParser.cpp`, `SedExecutor.cpp` (before `commands/seq/`); the test file goes into `tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt`'s `add_executable` list (after `RmTest.cpp`).

## Tests

`tests/unit/components/BuiltinCommands.unittests/SedTest.cpp` (new, in the
CMakeLists): `TEST_F(BuiltinCommandsTest, Sed...)` on `RunCaptured`, input
mostly through stdin (`RunCaptured`'s third argument), files written with
the fixture's `WriteFile`. Every expected output verified with `LC_ALL=C
sed` in the container.

- `SedPrintsAndDeletes`: `p`, `-n p`, `d`, `2d`, `$d`, `-n '$='` on `one\ntwo\nthree\n`; `=`.
- `SedSubstitute`: `s/o/0/`, `s/o/0/g`, `s/o/0/2`, `s/o/0/2g` on `foooo`; `s/x*/-/g` on `abc`; `s/a*/x/g` on `baaac`; `s/b*/X/2` on `abc` -> `aXc`; `s|/|\||`.
- `SedReplacementEscapes`: `&`, `\1`, `\n`, `\t`, backslash-newline; `s/\(o\)\(n\)/\U\2\E\1x\u&/` on `one` -> `NoxOne`; `\l`, `\L...\E`.
- `SedExtendedAndFlags`: `-E 's/(b|c)/[\1]/g'` -> `a[b][c]`; `s/x/Y/I` on `aXb` -> `aYb`; `s/^/>/Mg` on `x` -> `>x` (multi-line pattern spaces need `N`: tested in the next task); `-r` as `-E`.
- `SedEscapesInRegex`: `s/[\t]/T/` on `a\tb`; `s/\t/T/`; `s/[.]/X/`; `s/\w\+/[&]/` on `hello`.
- `SedAddresses`: `2,3p`, `/two/,$d`, `1~2p`, `2,~2p`, `/two/,+1p`, `0,/one/p` vs `1,/one/p`, `2,1p`, `$!d`, `/x/I p`, `\%t%p`, `/x/ !p`.
- `SedBlocksAndSemicolons`: `1 { p } `, `{p};p`, `p ; p`, `-n '2,4{p;2q}'`, comments `  # c\np`, `#n\np` (quiet), `-e '#n' -e p` (not quiet).
- `SedQuit`: `2q`, `2Q`, `q 5` -> exit 5 after printing `one`; `$q;d`; `1,2q` and `1,2:a` errors (exit 1); `1,2=` allowed.
- `SedNext`: `n;d`; `$!{n;p}`; `n;s/1/X/` on one line.
- `SedMissingNewline`: input `line` (no newline): `p` -> `line\nline`; files `nonl` + `in.txt` with `-n p` -> `line\none\ntwo\nthree\n`.
- `SedSeveralFiles`: `-n '$p' a b` -> last of b only; `-s -n '$p' a b` -> both; with a = 2 lines, b = 1: `-s -n '$=' a b` -> `2\n1\n`, `-n '$=' a b` -> `3\n`; `p nofile in.txt` -> the can't-read line, the rest, exit 2; `p /docs in.txt` -> `sed: read error on /docs: Is a directory`, exit 4, nothing of in.txt; `-` among files reads stdin.
- `SedScriptErrors`: every row of the error table, exit 1; `-e p -e k`; `-f` with a bad second line; `-f nosuch.sed` (exit 4); no script (help text on stderr, 1).
- `SedLastRegex`: `-n '/x/{s//[&]/p}'` -> `[x]`; `s//y/` with no regex -> `sed: -e expression #1, char 0: no previous regular expression`, 1.
- `SedIsStoppedPromptly`: stdin a pipe kept open (start with `os->StartProcess`, `StartProcessOptions::stdIn` the read end of a pipe made with the fixture's filesystem service, or `interactive` input), `TriggerStop()`, 143 within 1 s.

Update `ListsEveryBuiltinSortedWithAVersion` with `"sed"`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Sed*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list; a `sed`
  1.0.0 row (treated so far; not yet: the commands of search--sed-advanced
  and `-i -l -z -u --follow-symlinks --sandbox`; `--posix` and `--debug` not
  treated, `s///e` not treated); a paragraph on `commands/sed/` (parser,
  executor, how a command is added).
- Root `CLAUDE.md`: Builtin Commands table row and command lists.

## Acceptance

- [ ] Every message, char position and exit status in this plan matches `LC_ALL=C sed` 4.9 in the container.
- [ ] Input is streamed (one line of lookahead), never read whole; a stop ends it within a second.
- [ ] Files only through `context.IO()`; no `<regex>`.
- [ ] `sed` registered; `--init` template and generic builtin tests green (untreated options reported).
- [ ] `SedScript.h` structures leave room for the next task's commands without a redesign.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- `N D P h H g G x b t T a i c r R w W y l z F v e` and `s///w`, `-i -l -z
  -u --follow-symlinks --sandbox` (search--sed-advanced).
- `--posix` strictness, `--debug` output.
