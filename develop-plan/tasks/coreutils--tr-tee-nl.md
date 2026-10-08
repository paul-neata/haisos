# Task coreutils--tr-tee-nl: the tr, tee and nl builtins

- Rock: coreutils
- Depends on: coreutils--sort (its `BuiltinText.h` helpers), base--regex-match (the `Regex` component, for `nl -bpBRE` only)
- Size: ~1000 changed lines in ~9 files
- Plan checked against: develop @ 515f39d
- PR title: Add the tr, tee and nl builtins

## Goal

`tr`, `tee` and `nl` are builtin commands, placed with `BUILTIN rootfs tr
/bin/tr` (and `tee`, `nl`), behaving as GNU coreutils 9.4's (Ubuntu 24.04,
the task container's) with `LC_ALL=C`: the same options, output byte for
byte, the same messages (`tr: missing operand`, `nl: invalid body numbering
style: 'x'`) and exit codes. So `seq -w 1 3 | tr '\n' ' '`, `tr -cd
'[:alnum:]\n'`, `make 2>&1 | tee log` and `nl -ba file` print what they do on
Linux. All three stream: `tr` and `tee` pass each chunk on as it arrives,
`nl` each line.

## Context

Read first: the root `CLAUDE.md` (sections "Security", "Builtin Commands",
"Exit codes", "Automatic Development Rules" rule 9),
`src/components/BuiltinCommands/CLAUDE.md` (especially "Output": buffering
and the broken-pipe rule), `src/components/Regex/CLAUDE.md` and
`src/components/Regex/Regex.h`, `develop-plan/tasks/coreutils--sort.md`
(section `BuiltinText.h`), and the code of `commands/cat/Cat.cpp` (a chunked
stdin reader) and `commands/sort/Sort.cpp` and `commands/uniq/Uniq.cpp` (option table, `ArgMatch`, `BuiltinLineReader`, `OpenInputOperand`).

What exists (by exact name):
- `BuiltinCommand.h`: `IBuiltinCommand`, `BuiltinOption`, `BuiltinArgument`
  (`Optional`: long `--x=value` only), `kBuiltinNotTreated`, `BeginBuiltin` (two overloads: on the context's own arguments, or on a caller-given list -- tr/tee/nl use the first),
  `BuiltinContext` (`Out` -- buffered when stdout is not a terminal --,
  `Flush`, `Error`, `ErrorText`, `TryHelp`, `StopRequested`, `IO()`,
  `Process()`), `ShellEscapeQuoted(name, always)`, `BuiltinHelpText`.
- `ICurrentProcess::StopForBrokenPipe()` (`interfaces/IProcess.h`): ends
  the program quietly with exit code 141, as SIGPIPE.
- `IFileDescriptor::Write` returns `kIOBrokenPipe` only for a pipe whose
  reader is gone -- so "an error writing to a pipe" is exactly that result;
  `kIOError` is any other failure.
- From coreutils--sort (merged), `BuiltinText.h` (also used by uniq and cut; `GnuQuote` is what tr's/cut's quoting uses): `GnuQuote`, `ArgChoice`,
  `ArgMatch(context, longOption, value, choices)` (prints GNU's argmatch
  diagnostic; caller returns 1), `OpenInputOperand(context, name,
  InputOpenFailure&)`, `BuiltinLineReader(context, input, delimiter)` /
  `LineReadResult Next(line, delimited)`, `WriteFully(descriptor, bytes)`.
- From base--regex-match, `src/components/Regex/Regex.h` (target `Regex`,
  already linked into BuiltinCommands, see its "Notes for callers" -- just `#include "Regex.h"`):
  `Regex::Compile(std::string_view pattern, const RegexOptions& options,
  std::string& error)` -> `std::shared_ptr<const Regex>` (null on a bad
  pattern, `error` holding glibc's text, e.g. `Unmatched ( or \(`),
  `RegexOptions { syntax = RegexSyntax::Basic, ignoreCase, multiline }`,
  `bool Search(std::string_view text, size_t start, RegexMatch& match, int flags = 0) const`.
- `FilesystemUtils.h`: `kFileOpenWriteCreateTruncate`,
  `kFileOpenWriteCreateAppend`, `kFileCreateMode`.
- Tests: `BuiltinCommandsFixture.h` (`BuiltinCommandsTest`, `WriteFile`,
  `RunCaptured(command, args, input, workingDirectory)` -> `out`, `err`,
  `status`; stdout and stderr are files, not terminals).

Rules that bite (root `CLAUDE.md`, restated):
- `ICurrentProcess` is the only door out of a process: every file through
  `context.IO()`; nothing holds an `IFileSystem` or `IHaisosOS`.
- Builtins: GNU's output byte for byte (C locale: ASCII quotes); every option
  of the real command in `Options()`, untreated ones `kBuiltinNotTreated`;
  `--help` only from `BuiltinHelpText`; `--version`; default `ManPage()`;
  registered in `CreateStandardBuiltinCommands()` (which writes the `haisos
  --init` template line -- never by hand); sources in `CMakeLists.txt`;
  tests added to the single `add_executable(BuiltinCommands.unittests ...)` line of the test `CMakeLists.txt`.
- Portable C++17; no POSIX headers, no `<regex>` (nl uses `Regex`).
- Reads stop promptly on `TriggerStop()`; broken pipes end with 141.

## Changes

### `commands/tr/Tr.cpp` (new) -- and `commands/tr/TrSets.h/.cpp` if it helps

`CreateTrCommand()`; name `tr`, version `1.0.0`, `usageErrorStatus` 1.
Options (all treated): `-c -C --complement` (`-c`: long `complement`; `-C`
short only, the same in the C locale), `-d --delete`, `-s
--squeeze-repeats`, `-t --truncate-set1`.
`Help()`: summary `translate or delete characters`; usage `tr [OPTION]...
STRING1 [STRING2]`; notes: the SET syntax handled (`\NNN \\ \a \b \f \n \r
\t \v`, `CHAR1-CHAR2`, `[CHAR*]`, `[CHAR*REPEAT]`, `[:class:]`, `[=CHAR=]`),
bytes only (the C locale).

**Operands** (messages exact; each + Try line, exit 1):
- none: `tr: missing operand`;
- translating (no `-d`, or `-d` with `-s`... see below) needs 2: with 1,
  `tr: missing operand after 'X'` then `Two strings must be given when
  translating.` -- or, with `-d -s`, `Two strings must be given when both
  deleting and squeezing repeats.` (X GnuQuote: `'\\'` for `\`);
- too many: `tr: extra operand 'X'` (the first extra), and with `-d` alone a
  second line `Only one string may be given when deleting without
  squeezing repeats.`
Counts: `-d` alone: 1; `-d -s`: 2; `-s` alone: 1 or 2 (2 = translate then
squeeze); otherwise 2. Operands beyond the allowed count -> extra operand.
`-t` is ignored unless translating.

**Parsing a SET** into a list of elements, then expanded to bytes:
- Escapes: `\\ \a \b \f \n \r \t \v`; `\` + 1-3 octal digits (a 3-digit
  value above 0377 takes only 2 digits and warns, on stderr, `tr: warning:
  the ambiguous octal escape \777 is being\n\tinterpreted as the 2-byte
  sequence \077, 7` -- then goes on); `\` + any other byte is that byte; a
  `\` at the very end is a `\` and warns `tr: warning: an unescaped
  backslash at end of string is not portable`.
- `X-Y` range (X, Y possibly escaped): Y < X -> `tr: range-endpoints of
  'z-a' are in reverse collating sequence order`, exit 1. A `-` first or
  last is literal.
- `[:name:]` class: `alnum alpha blank cntrl digit graph lower print punct
  space upper xdigit` (ASCII ctype, ascending byte order); an unknown name
  -> `tr: invalid character class 'foo'`, 1. An unterminated `[:alpha` is
  literal bytes.
- `[=c=]`: the byte c; more than one byte -> `tr: ab: equivalence class
  operand must be a single character` (unquoted), 1. `[.a.]` is literal
  bytes (verified).
- `[c*n]` / `[c*]`: n decimal, or octal with a leading 0, 0 meaning `[c*]`;
  a bad n -> `tr: invalid repeat count 'x' in [c*n] construct`, 1. In
  STRING1 `[c*n]` is c repeated n times, but `[c*]` is an error `tr: the [c*]
  repeat construct may not appear in string1`. In STRING2 one `[c*]` fills
  STRING2 up to STRING1's length; a second -> `tr: only one [c*] repeat
  construct may appear in string2`.

**Checks when translating** (exit 1, message only):
- STRING2 may hold only the classes `upper` and `lower`: else `tr: when
  translating, the only character classes that may appear in\nstring2 are
  'upper' and 'lower'`; and no `[=c=]`: `tr: [=c=] expressions may not
  appear in string2 when translating`.
- each `[:upper:]`/`[:lower:]` of STRING2 must start at the same expanded
  position as a `[:lower:]`/`[:upper:]` (or the same class) of STRING1, else
  `tr: misaligned [:upper:] and/or [:lower:] construct` (examples, all
  verified: `a-z` vs `[:upper:]` misaligned; `[:lower:]` vs `A-C` fine;
  `abc` vs `x[:upper:]` misaligned; `[:upper:]` vs `[:lower:][:lower:]`
  misaligned; `[:lower:][:upper:]` vs `[:upper:][:lower:]` fine;
  `[:upper:]` vs `[:upper:]` fine, identity). An aligned pair maps through
  `toupper`/`tolower` (identity for the same class).
- STRING1 longer than STRING2 (no `-t`) and STRING2 ending in a class -> `tr:
  when translating with string1 longer than string2,\nthe latter string
  must not end with a character class`.
- empty STRING2 with a non-empty STRING1 (no `-t`) -> `tr: when not
  truncating set1, string2 must be non-empty`.
- with `-c` and a class in STRING1, STRING2 must expand to one byte
  (`[x*]` or a single byte): else `tr: when translating with complemented
  character classes,\nstring2 must map all characters in the domain to one`.

**Doing it**: `-c` replaces SET1 by every byte not in it, ascending.
Translation: STRING2 shorter than SET1 is padded with its last byte (with
`-t`, SET1 is cut to STRING2's length instead); map[SET1[i]] = STRING2[i] in
order, a later duplicate winning (`tr aa xy` maps a to y). `-d`: drop bytes
in SET1. `-s`: after translating/deleting, a run of the same byte that is in
the squeeze set (SET2 when two strings are given, else SET1) becomes one --
the run state carries across chunks. Read stdin 64 KiB at a time, write each
processed chunk with `context.Out` (buffered off a terminal, as GNU's
stdio). Read error -> `tr: read error: Input/output error`, 1; stopped ->
return quietly.

### `commands/tee/Tee.cpp` (new)

`CreateTeeCommand()`; name `tee`, version `1.0.0`, `usageErrorStatus` 1.
Options: `-a --append` (treated), `-i --ignore-interrupts`
(`kBuiltinNotTreated`: Haisos has no separate interrupt signal to ignore),
`-p` (treated: `--output-error=warn-nopipe`), `--output-error[=MODE]`
(`Optional`, treated; MODE via `ArgMatch` over `warn warn-nopipe exit
exit-nopipe`; no value means `warn-nopipe`).
`Help()`: summary `read from standard input and write to standard output
and files`; usage `tee [OPTION]... [FILE]...`; notes: the MODEs in a line.

Behaviour (GNU tee 9.4):
- Open every FILE first, in order, with `kFileOpenWriteCreateTruncate` (or
  `...Append` with `-a`) and `kFileCreateMode`. A `-` is a file named `-`
  (GNU 9.4, verified). A failure is reported and that file dropped, the rest
  go on, exit status 1 at the end: `tee: NAME: Is a directory` (a `Stat`
  says directory), `tee: NAME: No such file or directory` (its directory is
  missing), else `tee: NAME: Permission denied` (NAME ShellEscapeQuoted).
- Outputs: stdout (descriptor 1, written **directly** with `WriteFully`,
  not through `context.Out`, so every chunk passes through at once and the
  write result is seen) first, then the files in order.
- Loop: read stdin (up to 64 KiB); 0 -> done; stopped -> quiet return;
  error -> `tee: read error: Input/output error` (verify), status 1, done.
  Write the chunk to each live output. On a failed write of output O (its
  name through `ShellEscapeQuoted`; stdout's is `standard output`, so it
  prints as `'standard output'` -- verified: `tee: 'standard output':
  Broken pipe`):
  - no `--output-error`/`-p` (GNU's default): `kIOBrokenPipe` ->
    `context.Process().StopForBrokenPipe()` and return (exit 141); any other
    error -> `tee: O: Input/output error`, drop O, status 1;
  - `warn`: any error, `kIOBrokenPipe` included (`tee: 'standard output': Broken pipe`) ->
    message, drop O, status 1;
  - `warn-nopipe`: `kIOBrokenPipe` -> drop O silently (status unchanged);
    other errors as `warn`;
  - `exit`: any error -> message, return 1 at once;
  - `exit-nopipe`: `kIOBrokenPipe` -> drop O silently; other errors ->
    message, return 1.
  When no output is left, stop reading and return the status.
- Two FILE operands naming the same file are opened twice (each at its own
  position), as GNU does.

### `commands/nl/Nl.cpp` (new)

`CreateNlCommand()`; name `nl`, version `1.0.0`, `usageErrorStatus` 1.
Options (all treated), with their GNU names: `-b --body-numbering=STYLE`,
`-d --section-delimiter=CC`, `-f --footer-numbering=STYLE`, `-h
--header-numbering=STYLE`, `-i --line-increment=NUMBER`, `-l
--join-blank-lines=NUMBER`, `-n --number-format=FORMAT`, `-p
--no-renumber`, `-s --number-separator=STRING`, `-v
--starting-line-number=NUMBER`, `-w --number-width=NUMBER`.
`Help()`: summary `number lines of files`; usage `nl [OPTION]... [FILE]...`;
notes: `Default options are: -bt -d'\:' -fn -hn -i1 -l1 -n'rn' -s<TAB> -v1
-w6`, the STYLEs (`a t n pBRE`) and FORMATs (`ln rn rz`) in a line each.

Option values (messages exact):
- STYLE: `a`, `t`, `n`, or `p` + a BRE compiled now with
  `Regex::Compile(bre, RegexOptions{}, error)` (Basic syntax); anything else
  -> `nl: invalid body numbering style: 'x'` (`header`/`footer` for `-h`/`-f`;
  GnuQuote) + Try, 1; a bad BRE -> `nl: <error>` (e.g. `nl: Unmatched ( or
  \(` for `-b 'p\('`), 1, no Try.
- FORMAT: `ln` (`%-*jd`), `rn` (`%*jd`), `rz` (`%0*jd`, so -3 in width 6 is
  `-00003`); else `nl: invalid line number format: 'xx'` + Try, 1.
- `-v`, `-i`: any intmax (negative allowed; `-i 0` allowed); not a number ->
  `nl: invalid starting line number: 'x'` / `nl: invalid line number
  increment: 'x'`, 1, no Try (out of intmax range: the same text + `: Value
  too large for defined data type`, verified for
  `-v 99999999999999999999`).
- `-l`: 1..intmax; `0` -> `nl: invalid line number of blank lines: '0':
  Numerical result out of range`; not a number -> without the suffix; 1.
- `-w`: 1..INT_MAX; `0` -> `nl: invalid line number field width: '0':
  Numerical result out of range`; above INT_MAX -> `...: '2147483648': Value
  too large for defined data type`; not a number -> `nl: invalid line number
  field width: 'x'`; 1.
- `-d CC`: one byte -> that byte then `:`; empty -> sections disabled; more
  than two bytes allowed. Header delimiter = CC three times, body = twice,
  footer = once; a line equal to one of them (whole line) is a delimiter.
- `-s STRING`: any bytes, default TAB.

Processing (FILE operands concatenated into one stream, `-` or none =
stdin; each line via `BuiltinLineReader` with `\n`):
- A delimiter line switches the section (header style `-h`, body `-b`,
  footer `-f`), resets the number to `-v` unless `-p`, and prints an empty
  line (`\n`). Sections start in the body.
- Other lines, by the current section's style: `a` -> numbered, except that
  with `-l N` (N > 1) an empty line is numbered only when it is the N-th
  consecutive empty line since the last numbered one (the count resets to 0
  when it numbers, and on any non-empty line); `t` -> numbered when
  non-empty; `n` -> not numbered; `p` -> numbered when `Search(line, 0,
  match)` finds a match.
- Numbered: the number in FORMAT and width `-w`, then `-s`, then the line,
  then `\n` (also for a last line without one); then the number +=
  increment -- if that overflows intmax, remember it, and at the **next**
  numbered line print `nl: line number overflow` and return 1 (so `printf
  'a\nb\n' | nl -v 9223372036854775807` prints the first line, then fails).
- Not numbered: `-w` spaces plus as many spaces as `-s` has bytes, then
  the line (`       ` -- 7 spaces -- by default).
- File errors: `nl: NAME: No such file or directory`, `nl: NAME: Is a
  directory`, `nl: NAME: Permission denied`, read `nl: NAME: Input/output
  error` (ShellEscapeQuoted); reported, the next file goes on, exit 1 at
  the end.

### Registration and build

`BuiltinCommandList.h`: declare `CreateNlCommand()`, `CreateTeeCommand()`,
`CreateTrCommand()` and add them to `CreateStandardBuiltinCommands()`.
`src/components/BuiltinCommands/CMakeLists.txt` (the `add_library(BuiltinCommands STATIC ...)` list, alphabetical by directory): add `commands/nl/Nl.cpp`, `commands/tee/Tee.cpp`, `commands/tr/Tr.cpp` (and `TrSets.cpp` if split). The declarations and `CreateStandardBuiltinCommands()` entries go in alphabetical position: `CreateNlCommand` after `CreateMkdirCommand`, `CreateTeeCommand` after `CreateSortCommand` (before `CreateTestCommand`), `CreateTrCommand` after `CreateTestCommand` (before `CreateTrueCommand`).

## Tests

New `TrTest.cpp`, `TeeTest.cpp`, `NlTest.cpp` in
`tests/unit/components/BuiltinCommands.unittests/` (added to the
`add_executable(BuiltinCommands.unittests ...)` line in its `CMakeLists.txt`), `TEST_F(BuiltinCommandsTest, ...)`. Expected outputs are
GNU 9.4's with `LC_ALL=C` (all verified unless marked); for anything else,
run it in the container and paste the result.

tr (input via `RunCaptured`'s `input`):
- `TrTranslatesAndPads`: `abc x` on `abcabc` -> `xxxxxx`; `-t abc x` -> `xbcxbc`; `abc xy` on `abcc` -> `xyyy`; `aa xy` on `a` -> `y`; `'\n' ' '` on `01\n02\n03\n` -> `01 02 03 `.
- `TrClasses`: `'[:lower:]' '[:upper:]'` on `Hello` -> `HELLO`; reverse -> `hello`; `'[:lower:][:upper:]' '[:upper:][:lower:]'` on `aB` -> `Ab`; `'[:lower:]' 'A-C'` on `abc` -> `ABC`.
- `TrDeleteSqueezeComplement`: `-d '[:digit:]'` on `a1b2` -> `ab`; `-cd '[:alnum:]\n'` on `a-b c!\n` -> `abc\n`; `-s ' '` on `a   b    c` -> `a b c`; `-s 'a-z' 'A-Z'` on `aabbcc` -> `ABC`; `-ds a b` on `aabbcc` -> `bcc`; `-cs '[:alpha:]' '\n'` on `hi, there  you\n` -> `hi\nthere\nyou\n`; `-c a x` on `abc` -> `axx` (and on `abc\n` -> `axxx`: the newline is in the complement); `-c b xy` on `abc` -> `yby`.
- `TrRepeatsAndEscapes`: `a '[b*]'` on `aaa` -> `bbb`; `abc '[x*2]y'` -> `xxy`; `abc 'x[y*]z'` -> `xyz`; `a-e '[x*3]y'` on `abcde` -> `xxxyy`; `'[=a=]' x` on `aba` -> `xbx`; `'\101\n' 'z '` on `ABA\n` -> `zBz `; `'\141' '\142'` on `ab` -> `bb`; `'a\-z' 123` on `a-z` -> `123`; `'[.a.]' x` on `[.a]` -> `xxxx`.
- `TrWarnings`: `'a\' x` on `a\` -> out `xx`, err `tr: warning: an unescaped backslash at end of string is not portable\n`, 0; `'\777' x` -> err `tr: warning: the ambiguous octal escape \777 is being\n\tinterpreted as the 2-byte sequence \077, 7\n`, 0.
- `TrOperandErrors`: no args -> `tr: missing operand\nTry 'tr --help' for more information.\n`, 1; `a` -> `tr: missing operand after 'a'\nTwo strings must be given when translating.\nTry ...`; `-ds a` -> `...after 'a'\nTwo strings must be given when both deleting and squeezing repeats.\nTry ...`; `-d a b` -> `tr: extra operand 'b'\nOnly one string may be given when deleting without squeezing repeats.\nTry ...`; `a b c` -> `tr: extra operand 'c'\nTry ...`; `'\'` -> `tr: missing operand after '\\'...`.
- `TrSetErrors` (each exit 1, message only): `z-a x`, `'[:foo:]' x`, `a-z '[:upper:]'`, `a '[:digit:]'`, `a '[=b=]'`, `-c '[:upper:]' '[:lower:]'`, `-c '[:upper:]' ab`, `a '[b*x]'`, `'[a*]' x`, `ab '[b*][c*]'`, `'[=ab=]' x`, `abc ''` -- with the exact texts of "Parsing a SET" and "Checks when translating".

tee:
- `TeeWritesStdoutAndFiles`: input `hi\n`, `tee /w/a /w/b` -> out `hi\n`, both files `hi\n`, 0.
- `TeeAppend`: `/w/a` = `x\n`; `-a /w/a` with `y\n` -> file `x\ny\n`.
- `TeeContinuesPastAFailedFile`: `tee /w/a /nodir/x /docs /w/b` -> out the input, err `tee: /nodir/x: No such file or directory\ntee: /docs: Is a directory\n`, both good files written, exit 1.
- `TeeDashIsAFile`: `tee -` in `/w` -> `/w/-` holds the input.
- `TeeOutputErrorModes`: `--output-error=foo` -> `tee: invalid argument 'foo' for '--output-error'\nValid arguments are:\n  - 'warn'\n  - 'warn-nopipe'\n  - 'exit'\n  - 'exit-nopipe'\nTry 'tee --help' for more information.\n`, 1; `-p` and `--output-error` accepted.
- `TeeBrokenPipe`: on the model of `EchoIntoAPipeWithNoReaderExits141Quietly` in `BuiltinCommandsTest.cpp` (`os->GetPipeService()->CreatePipe()`, read end released before the start, the write end as `StartProcessOptions::stdOut`, stdin a file of a few bytes): `tee /w/a` -> exit `kExitCodeBrokenPipe` (141), nothing on stderr; `tee -p /w/a` -> exit 0, no stderr, `/w/a` holds the whole input; `tee --output-error=warn /w/a` -> exit 1, stderr `tee: 'standard output': Broken pipe\n` (stderr to a file, as `RunCaptured` does), `/w/a` complete.
- `TeeIgnoreInterruptsIsReported`: `-i` -> err `Parameter -i is not treated by HaisosOS tee v. 1.0.0\n`, output still copied.

nl (with `N` = `a\n\nb\n\\:\\:\\:\nh1\n\\:\\:\nb1\n\n\\:\nf1\n\\:\\:\nb2\n`):
- `NlDefaults`: `N` -> `     1\ta\n       \n     2\tb\n\n       h1\n\n     1\tb1\n       \n\n       f1\n\n     1\tb2\n`.
- `NlAllStyles`: `-ba -ht -ft` -> `     1\ta\n     2\t\n     3\tb\n\n     1\th1\n\n     1\tb1\n     2\t\n\n     1\tf1\n\n     1\tb2\n`; `-p -ba` -> numbers 1..6 never reset (`     4\tb1\n     5\t\n` ... `     6\tb2\n`).
- `NlFormatsAndWidths`: `-n ln -w 3 -s '|'` -> `1  |a\n    \n2  |b\n...`; `-n rz -v 5 -i 2` -> `000005\ta\n       \n000007\tb\n...`; `-v -3 -n rz` on `a\n` -> `-00003\ta\n`; `-v -3 -n ln -w 4` -> `-3  \ta\n`; `-w 1 -v 123` -> `123\ta\n`; `-s ''` on `x\n` -> `     1x\n`; `printf 'a'` -> `     1\ta\n`.
- `NlJoinBlankLines`: `-ba -l 2` on `x\n\n\n\n\ny\n` -> `     1\tx\n       \n     2\t\n       \n     3\t\n     4\ty\n`.
- `NlRegexStyle`: `-b pb` on `N` -> `       a\n       \n     1\tb\n\n       h1\n\n     1\tb1\n       \n\n       f1\n\n     1\tb2\n`; `-b 'pa\|b'` on `a\nb\n` -> both numbered; `-b 'p\('` -> `nl: Unmatched ( or \(\n`, 1.
- `NlSectionDelimiters`: `-d XY` on `a\nXY\nb\n` -> `     1\ta\n\n       b\n`; `-d X` on `a\nX:\nb\n` -> the same; `-d ''` on `\:\:\na\n` -> `     1\t\:\:\n     2\ta\n` (sections disabled).
- `NlOverflow`: `-v 9223372036854775807` on `a\nb\n` -> out `9223372036854775807\ta\n`, err `nl: line number overflow\n`, 1.
- `NlOptionErrors`: `-b x` -> `nl: invalid body numbering style: 'x'\nTry 'nl --help' for more information.\n`, 1; `-f x` -> `footer`; `-n xx` -> `nl: invalid line number format: 'xx'\nTry ...`; `-w 0`, `-w x`, `-w 2147483648`, `-v x`, `-i x`, `-l 0` -> the messages above, 1.
- `NlFiles`: `nl - /w/f` with stdin `x\n` -> numbering continues across them; `nl /docs nofile` -> `nl: /docs: Is a directory\nnl: nofile: No such file or directory\n`, 1.

Generic tests: `ListsEveryBuiltinSortedWithAVersion` -- insert `"nl"`, `"tee"`,
`"tr"` in sorted position into the list as it is on develop (`... "mkdir", "nl", "printf" ...`, `... "sort", "tee", "test", "tr", "true", "uniq" ...`).
`EveryUntreatedOptionIsAcceptedAndReported` runs `tee -i /docs` (must
report; the directory failure that follows is fine).

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Tr*:BuiltinCommandsTest.Tee*:BuiltinCommandsTest.Nl*'
bash ./scripts/test_linux.sh L U
```
(The script's filter must match the executable's name; the direct run narrows.)

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: `nl`, `tee`, `tr` in the
  opening list; rows in "The commands" (all 1.0.0; tr: every option and SET
  form, bytes only; tee: `-a -p --output-error`, exception: `-i` not
  treated -- no separate interrupt in Haisos; stdout written unbuffered,
  chunk by chunk; nl: every option, `-bpBRE` through the `Regex` component).
- Root `CLAUDE.md`: the three in the Builtin Commands sentence and table
  (`tr`: `Translates, deletes or squeezes bytes (-c -d -s -t, ranges, classes, [c*n])`;
  `tee`: `Copies standard input to standard output and files (-a -p --output-error)`;
  `nl`: `Numbers lines (-b -h -f styles incl. pBRE, -d -i -l -n -p -s -v -w)`);
  the `BuiltinCommands/` directory-tree line.

## Acceptance

- [ ] tr, tee, nl registered; `--help` standard shape; `--version`; `man`.
- [ ] Every GNU 9.4 option accepted; `tee -i` reported as not treated.
- [ ] tr: every SET form, every check and message, translation/padding/
      truncation, `-c -d -s` combinations byte-exact.
- [ ] tee: outputs opened up front, failures reported and skipped, the four
      MODEs and the default (141 on a broken stdout pipe), stdout unbuffered.
- [ ] nl: styles, sections, formats, `-l`, overflow, every message.
- [ ] nl's `p` style uses `Regex::Compile` with `RegexSyntax::Basic`.
- [ ] All file access via `context.IO()`; reads stop on `StopRequested`.
- [ ] Builds on Linux; all unit tests pass. Both CLAUDE.md files updated.

## Out of scope

- Multibyte characters (tr in the C locale works on bytes, as GNU tr does).
- Signals: `tee -i` (not treated).
- sort, uniq, cut (their own tasks).
