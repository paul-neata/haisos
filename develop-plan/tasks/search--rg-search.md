# Task search--rg-search: rg -- searching, output modes, binary files

- Rock: search
- Depends on: search--grep-recursive
- Size: ~900 changed lines in ~8 files
- Plan checked against: develop @ ccb9dbe
- PR title: Add the rg builtin: ripgrep 14 search and output

## Goal

`rg` is a builtin command, placed with `BUILTIN rootfs rg /bin/rg`,
behaving as ripgrep 14.1.1: a recursive search of `.` by default (paths
printed without `./`), its two output styles -- to a terminal file
headings, line numbers and a blank line between files; off a terminal
(a pipe, a file) `path:line` style without line numbers unless `-n` --, its
output modes (`-c --count-matches -l --files-without-match -o -q --files`),
context, `-m`, `-M`, `-0`, colours, binary files, its error messages and
exit statuses (0 a match, 1 none, 2 an error). So `rg -n -w Create src |
cut -d: -f1 | uniq -c` prints what it prints on Linux.

What rg skips while walking -- hidden files, `.gitignore`/`.ignore`/
`.rgignore`, `-g`, `-t`, `--max-depth` -- is `search--rg-ignore`: in this
task the walk visits everything. Rust-regex syntax and ripgrep's exact
regex error messages are `search--rg-regex`: here patterns go to `Regex`'s
Perl syntax as written.

## Context

Read first: `develop-plan/tasks/search--grep-core.md` and
`search--grep-recursive.md` and their code in
`src/components/BuiltinCommands/commands/grep/`, `src/components/BuiltinCommands/CLAUDE.md`,
the root `CLAUDE.md` ("Security", "Builtin Commands", rule 9).

What exists, by exact name:
- `commands/grep/GrepMatcher.h`: `GrepSyntax`, `GrepWholeWord`,
  `GrepMatcherOptions`, `GrepMatcher::Create(patterns, options, error)`,
  `Find(line, start, begin, end)`, `Matches(line)`.
- `commands/grep/GrepContext.h`: `GrepContext(before, after, enabled,
  separatorAcrossFiles, printLine, printSeparator)`, `BeginFile()`,
  `Line(text, lineNumber, byteOffset, selected, extendsAfter)`,
  `AfterPending()`.
- `BuiltinText.h`: `OpenInputOperand`, `InputOpenFailure`,
  `BuiltinLineReader`. `BuiltinFnmatch.h`: `FnMatch` (not needed yet).
- `BuiltinCommand.h`: `ParseBuiltinArgs`, `BuiltinOption` (with `hidden`),
  `BuiltinHelp`, `BuiltinHelpText`, `BuiltinReferenceUrl`.
- `IFileIO::ReadDirectory`, `IFileIO::Stat`; the environment through
  `context.Process().GetEnvironment()`.

Reference: ripgrep 14.1.1. The container has no `rg` package, but Claude
Code ships ripgrep inside its binary and becomes it when started as `rg`:
`(exec -a rg "$(npm root -g)/@anthropic-ai/claude-code/bin/claude.exe" ARGS)`
(check `--version` says `ripgrep 14.1.1`); compare with `--sort path` (rg
searches in parallel, so its own order varies) and `LC_ALL=C`. Every
expected output below was produced that way.

## Changes

### Rules that bite (root CLAUDE.md)

ICurrentProcess is the only door out (files and directories through
`context.IO()` only); the real command's output byte for byte; every flag of
rg 14 in `Options()` (untreated ones `kBuiltinNotTreated`, reported);
`--help` from `BuiltinHelpText`, `--version`, default `ManPage()`;
registered in `CreateStandardBuiltinCommands()` (so the `haisos --init`
template gets `# BUILTIN rootfs rg /bin/rg`); sources in `CMakeLists.txt`;
`rg` added to the exact list of `ListsEveryBuiltinSortedWithAVersion`;
portable C++17; reads stop promptly on `TriggerStop()`.

### `BuiltinCommand.h` / `.cpp` -- a reference other than man7

ripgrep has no page on man7.org. Add to `BuiltinHelp` a last member
`std::string referenceUrl;` ("empty: `BuiltinReferenceUrl` of the real
command"); `BuiltinHelpText` uses it when set. (If another task already
added an equivalent, use that instead.) rg sets
`https://github.com/BurntSushi/ripgrep/blob/14.1.1/GUIDE.md`; its `--help`
line reads `Based on Linux rg: <that url>`.

### `commands/rg/Rg.cpp` (new) -- the command

`CreateRgCommand()` (declared in `BuiltinCommandList.h`); name `rg`,
version `1.0.0`. `Help()`: summary `recursively search the current
directory for lines matching a pattern`; usage `rg [OPTIONS] PATTERN
[PATH ...]`, `rg [OPTIONS] -e PATTERN ... [PATH ...]`, `rg [OPTIONS]
--files [PATH ...]`; notes: paths are searched one at a time, in operand
order and each directory's entries in byte order (rg's `--sort path`
order); long flags may be abbreviated (rg wants them whole); patterns use
Haisos's Perl-subset regex; with no PATH, standard input is searched only
when it is not a terminal and is not empty at once (see below).

Option table: every flag of `rg --help` (14.1.1), one row each, `-h`
having id `kBuiltinOptionHelp` and `-V` `kBuiltinOptionVersion`, and the
negations rg accepts as hidden rows. Treated **in this task**:
`-e --regexp=PATTERN`, `-f --file=PATTERNFILE`, `-s --case-sensitive`,
`-i --ignore-case`, `-S --smart-case`, `-F --fixed-strings`,
`--no-fixed-strings`, `-v --invert-match`, `--no-invert-match`,
`-x --line-regexp`, `-w --word-regexp`, `-m --max-count=NUM`,
`-a --text`, `--no-text`, `--binary`, `--no-binary`,
`-A --after-context=NUM`, `-B --before-context=NUM`, `-C --context=NUM`,
`-b --byte-offset`, `--no-byte-offset`, `--column`, `--no-column`,
`--color=WHEN`, `--context-separator=SEPARATOR`, `--no-context-separator`,
`--heading`, `--no-heading`, `-n --line-number`, `-N --no-line-number`,
`-M --max-columns=NUM`, `-0 --null`, `-o --only-matching`, `-p --pretty`,
`-q --quiet`, `-H --with-filename`, `-I --no-filename`, `-c --count`,
`--count-matches`, `-l --files-with-matches`, `--files-without-match`,
`--include-zero`, `--no-include-zero`, `--line-buffered`,
`--no-line-buffered`, `--block-buffered`, `--no-block-buffered`,
`--no-messages`, `--messages`, `--files`, `--sort=SORTBY` (values `path`
and `none` treated -- the order is already path order --, others reported
with `NotTreated("--sort=<value>")`), `--sort-files`, `--no-sort-files`.
Listed **for `search--rg-ignore`** (not treated yet): `-L --follow`,
`--no-follow`, `-g --glob=GLOB`, `--iglob=GLOB`, `--glob-case-insensitive`,
`--no-glob-case-insensitive`, `-. --hidden`, `--no-hidden`,
`-d --max-depth=NUM`, `--no-ignore`, `--ignore`, `--no-ignore-dot`,
`--ignore-dot`, `--no-ignore-exclude`, `--ignore-exclude`,
`--no-ignore-parent`, `--ignore-parent`, `--no-ignore-vcs`,
`--ignore-vcs`, `--no-require-git`, `--require-git`,
`-t --type=TYPE`, `-T --type-not=TYPE`, `--type-list`, `-u --unrestricted`.
**Not treated** (Haisos lacks what they need, or a later develop may add
them): `--pre=COMMAND`, `--no-pre`, `--pre-glob=GLOB`, `-z --search-zip`,
`--no-search-zip`, `--crlf`, `--no-crlf`, `--dfa-size-limit=NUM`,
`-E --encoding=ENCODING`, `--no-encoding`, `--engine=ENGINE`, `--mmap`,
`--no-mmap`, `-U --multiline`, `--no-multiline`, `--multiline-dotall`,
`--no-multiline-dotall`, `--no-unicode`, `--unicode`, `--null-data`,
`-P --pcre2`, `--no-pcre2`, `--regex-size-limit=NUM`, `--stop-on-nonmatch`,
`-j --threads=NUM`, `--auto-hybrid-regex`, `--no-auto-hybrid-regex`,
`--no-pcre2-unicode`, `--pcre2-unicode`, `--ignore-file=PATH`,
`--ignore-file-case-insensitive`, `--no-ignore-file-case-insensitive`,
`--max-filesize=NUM`, `--no-ignore-files`, `--ignore-files`,
`--no-ignore-global`, `--ignore-global`, `--one-file-system`,
`--no-one-file-system`, `--type-add=TYPESPEC`, `--type-clear=TYPE`,
`--colors=COLOR_SPEC`, `--field-context-separator=SEPARATOR`,
`--field-match-separator=SEPARATOR`, `--hostname-bin=COMMAND`,
`--hyperlink-format=FORMAT`, `--max-columns-preview`,
`--no-max-columns-preview`, `--path-separator=SEPARATOR`, `--passthru`,
`-r --replace=REPLACEMENT`, `--sortr=SORTBY`, `--trim`, `--no-trim`,
`--vimgrep`, `--json`, `--no-json`, `--debug`, `--trace`,
`--no-ignore-messages`, `--ignore-messages`, `--stats`, `--no-stats`,
`--generate=KIND`, `--no-config`, `--pcre2-version`.
(`-.` is a short option named `.`; `ParseBuiltinArgs` handles it.)

### Parsing and errors (`Rg.cpp`)

Parse with `ParseBuiltinArgs` directly. rg's messages are `rg: <text>\n`
on stderr, exit 2, **no Try line**; translate the parser's errors:
- `unrecognized option '--foo'`, an ambiguous prefix, and `invalid option
  -- 'k'` -> `rg: unrecognized flag --foo` / `rg: unrecognized flag -k`
  (rg's "similar flags" hint is not printed -- documented);
- `option requires an argument -- 'e'` -> `rg: missing value for flag -e:
  missing argument for option '-e'`; the long form likewise with `--regexp`;
- `option '--heading' doesn't allow an argument` (given `--heading=yes`) ->
  `rg: invalid CLI arguments: unexpected argument for option '--heading': "yes"`.
Then `--help`/`--version`, then `ReportNotTreated`.
Number flags (`-A -B -C -m -M`, later `-d`): decimal digits only, else
`rg: error parsing flag <spelling>: value is not a valid number: invalid
digit found in string` (`<spelling>` as `ParsedBuiltinOption::spelling`:
`-A`, `--max-count`); `-1` is invalid that way too. `--color` values
`never`, `auto`, `always`, `ansi` (= always), else `rg: error parsing flag
--color: choice 'bad' is unrecognized`.
Last one wins among `-s -i -S`, `--heading/--no-heading`, `-n/-N`,
`-H/-I`, `-c`/`--count-matches`/`-l`/`--files-without-match`.

Patterns: `-e` (each one pattern), `-f FILE` (each line one pattern, an
empty line an empty pattern; `-` is stdin; missing -> `rg: FILE: No such
file or directory (os error 2)`, exit 2; an empty file adds none); with
neither, the first operand is the pattern -- none -> `rg: ripgrep requires
at least one pattern to execute a search`, exit 2 (not with `--files`,
where every operand is a path). A pattern holding a newline byte -> exactly:
```
rg: the literal "\n" is not allowed in a regex

Consider enabling multiline mode with the --multiline flag (or -U for short).
When multiline mode is enabled, new line characters can be matched.
```
exit 2.

Matcher: one `GrepMatcher` with syntax `Perl` and the single pattern
`(?:p1)|(?:p2)|...` (rg's own joining; leftmost-first: `rg -o -e a -e ab`
on `ab` prints `a`); `-F`: each pattern escaped first (every byte that is
not `[A-Za-z0-9_]` written `\xhh`); `-w` -> `NonWordNeighbours` (rg's
half-boundaries: the bytes around the match are not word bytes); `-x` ->
`wholeLine`; case: `-i` ignoreCase, `-S` ignoreCase only when no pattern
holds an ASCII uppercase letter that is not right after an unescaped `\`
(`\S` and `\W` do not count). On a compile error print
`rg: regex parse error:\n    (?:<the joined pattern>)\nerror: <Regex's
message>\n`, exit 2 (`search--rg-regex` replaces this with rg's exact
frames).

### `commands/rg/RgSearch.h` / `RgSearch.cpp` (new) -- walking, reading, printing

**What is searched.** Operands after the pattern are paths, in order. With
none: if standard input (slot 0) is not a terminal, read from it once; if
that first read returns data, search standard input as `<stdin>` (with
that data first); if it ends at once, search `.` instead (rg looks at
whether stdin is a file or a pipe -- a descriptor has no type in Haisos,
so an input that is empty at once, /dev/null's case, means "no stdin":
documented). Otherwise search `.` -- the **implicit** path, whose entries
print without `./`. A path that is a directory is walked: entries of
`ReadDirectory` minus `.`/`..`, in byte order, depth first, files and
directories together; a child is printed as the operand + `/` + name, no
`/` added when the operand already ends with `/` (`./src/` -> `./src/a.c`,
`src//sub` -> `src//sub/b.h`). `CharDevice`s met while walking are
skipped; a device operand is searched.

**Names shown** (`-H`/`-I` override): yes unless there is exactly one
operand and it is not a directory, or standard input is searched.
**Heading** (`--heading`/`--no-heading` override): on when stdout is a
terminal (`context.OutIsTerminal()`) and names are shown. **Line numbers**
(`-n`/`-N` override): on when stdout is a terminal. **Colour**: `always`;
`auto` (the default) = a terminal, `TERM` set and not `dumb`, and
`NO_COLOR` unset or empty. `-p` = `--color=always --heading -n`.

**Reading** (per file): the grep-core chunk loop's shape -- `Read` up to
65536 bytes, a carry, line numbers and byte offsets -- written here.
Binary files (no `-a`): a file named as an operand (or any file with
`--binary`): from the first chunk holding a NUL, each NUL also ends a
line, nothing more is printed, and the first match ends the file: then
print `<path>: binary file matches (found "\0" byte around offset N)\n`
(no `<path>: ` when names are not shown; N the offset of the first NUL) to
**stdout**; `-c`/`-l`/`-q` count it as a file with that match. A file
found while walking: a chunk holding a NUL ends the file silently (lines
of earlier chunks stay printed). Verified: an operand `x\0y TODO\n` ->
`binary file matches (found "\0" byte around offset 1)`; the same file met
while walking `src` -> nothing; `rg -c TODO bin` -> `1`.

**Errors**: a missing path -> `rg: P: No such file or directory (os error
2)`; when it is the only operand: `rg: P: IO error for operation on P: No
such file or directory (os error 2)` (verified for `nosuch` and
`nosuch/a`). Hidden by `--no-messages`; either way the exit becomes 2
(unless `-q` found a match: 0). Nothing searched at all with the implicit
path -> `rg: No files were searched, which means ripgrep probably applied a
filter you didn't expect.\nRunning with --debug will show why files are
being skipped.\n`, exit 2.

**Printing** (separators uncoloured; `S` is `:` for a match, `-` for
context):
- line style: `[path S][line S][column S][offset S]text\n`; with `-0` the
  path is followed by `\0` instead of `S`. Column (`--column`) is the
  1-based byte column of the first match; offset (`-b`) the line's byte
  offset (with `-o`, the match's).
- heading style: before a file's first output line, a blank line if
  anything was printed before, then `path\n`; lines without the path.
- `-o`: one line per non-empty match.
- `-c`/`--count-matches` (also `-c -o`): `[path:]N` per file with N > 0
  (all with `--include-zero`); names shown as above (`rg -c TODO a.c` ->
  `3`). `-l`: `path\n`; `--files-without-match`: paths with no match;
  `-0` ends them with `\0` instead of `\n`. `-q`: nothing; stop at the
  first match.
- `-M N` (N > 0): a line longer than N bytes prints `[Omitted long
  matching line]` (context: `[Omitted long context line]`) in place of its
  text, prefixes kept.
- Context: a `GrepContext` with `separatorAcrossFiles` = not heading;
  separator `--` (`--context-separator`, none with
  `--no-context-separator`). `-m N`: after N matching lines, go on only
  while trailing context is owed, passing later matches as **selected
  with `extendsAfter` false** (rg prints them as matches:
  `rg -n -m1 -A4 TODO a.c` -> `2:two TODO\n3-three\n4-four\n5:five TODO\n6-six\n`).
- `--files`: each file that would be searched, one per line (`\0` with
  `-0`), no pattern; exit 0 if any.
- Colours (byte-exact): path `\e[0m\e[35m` P `\e[0m`; line number
  `\e[0m\e[32m` N `\e[0m`; column and offset `\e[0m` N `\e[0m`; match
  `\e[0m\e[1m\e[31m` M `\e[0m`. So `rg --color=always -n TODO a.c t1`
  (no heading) gives `\e[0m\e[35ma.c\e[0m:\e[0m\e[32m2\e[0m:two \e[0m\e[1m\e[31mTODO\e[0m\n`;
  a context line `\e[0m\e[35ma.c\e[0m-\e[0m\e[32m3\e[0m-three\n`; `-c` ->
  `\e[0m\e[35ma.c\e[0m:3\n`; `--files` and `-l` colour the path.
- `--line-buffered`: flush after each line.

**Exit**: 0 when a line matched (with `--files` / `-l` /
`--files-without-match`: when a path was printed), 1 otherwise, 2 after
any error (unless `-q` matched).

## Tests

`tests/unit/components/BuiltinCommands.unittests/RgTest.cpp` (new, in the
CMakeLists), `TEST_F(BuiltinCommandsTest, Rg...)`. Tree under `/r`:
`src/a.c` (grep-core's a.c), `src/sub/b.h` = `TODO sub\n`, `src/bin.dat` =
`x\0y TODO\n`, `t1` = `TODO\n`, `k` = `none\n`; run from `/r`.
`RunCaptured` is "off a terminal"; `Run` (the console) is a terminal, with
stdout and stderr merged into lines (and no `TERM` in the OS environment,
so no colour unless a test sets it).
- `RgSearchesDotByDefault`: `rg TODO` (RunCaptured, no stdin input) ->
  `src/a.c:two TODO\nsrc/a.c:five TODO\nsrc/a.c:ten TODO\nsrc/sub/b.h:TODO sub\nt1:TODO\n`, 0.
- `RgExplicitDotKeepsPrefix`: `rg TODO .` -> the same lines with `./`.
- `RgSingleFile`: `rg TODO t1` -> `TODO\n`; `-n` -> `1:TODO\n`; `-H` -> `t1:TODO\n`.
- `RgTerminalStyle`: `Run("rg", {"TODO", "src"})` -> lines `src/a.c`, `2:two TODO`, `5:five TODO`, `10:ten TODO`, ``, `src/sub/b.h`, `1:TODO sub`.
- `RgContext`: `-n -A1 -B1 'five|ten' src/a.c` -> `4-four\n5:five TODO\n6-six\n--\n9-nine\n10:ten TODO\n`; `-A1 TODO src/sub/b.h t1 src/a.c` -> `src/sub/b.h:TODO sub\n--\nt1:TODO\n--\nsrc/a.c:two TODO\nsrc/a.c-three\n--\nsrc/a.c:five TODO\nsrc/a.c-six\n--\nsrc/a.c:ten TODO\n`; `--heading -n -A1 TODO src/a.c t1` -> `src/a.c\n2:two TODO\n3-three\n--\n5:five TODO\n6-six\n--\n10:ten TODO\n\nt1\n1:TODO\n`; the `-m1 -A4` case above.
- `RgCountsAndLists`: `-c TODO src` -> `src/a.c:3\nsrc/sub/b.h:1\n` (bin.dat skipped while walking); `-c TODO t1 k` -> `t1:1\n`; `--include-zero -c TODO t1 k` -> `t1:1\nk:0\n`; `--count-matches T src/a.c` -> `3\n`; `-l TODO` -> `src/a.c\nsrc/sub/b.h\nt1\n`; `--files-without-match TODO t1 k` -> `k\n`, 0; `--files-without-match TODO t1` -> empty, 1; `-l TODO k` -> empty, 1.
- `RgOnlyMatchingAndColumns`: `-o -n 'T.D' src/a.c` -> `2:TOD\n5:TOD\n10:TOD\n`; `-b TODO t1 src/a.c` first line `t1:0:TODO`; `--column -n TODO t1` -> `1:1:TODO\n`.
- `RgNull`: `-0 -l TODO src` -> `src/a.c\0src/sub/b.h\0`; `-0 -n TODO src/sub/b.h src/a.c` starts `src/sub/b.h\0` `1:TODO sub\n`.
- `RgCaseFlags`: `-S todo t1` -> `TODO\n`; `-S Todo t1` -> 1; `-i -s todo t1` -> 1; `-s -i todo t1` -> `TODO\n`; `-F 'T.D' t1` -> 1; `-w -o 'f\w*' src/a.c` -> `four\nfive\n`; `-x six src/a.c` -> `six\n`.
- `RgPatterns`: `-o -e a -e ab` on stdin `ab\n` -> `a\n`; `-c -f pf k` with `pf` = `TODO\n\n` -> `1\n`; `-f empty k` -> empty, 1; pattern with a newline -> the multiline message, 2.
- `RgStdin`: input `a TODO\n`: `rg TODO` -> `a TODO\n`; `-H -n` -> `<stdin>:1:a TODO\n`.
- `RgMaxColumns`: `-M 5 -n TODO src/a.c src/sub/b.h` -> `src/a.c:2:[Omitted long matching line]\nsrc/a.c:5:[Omitted long matching line]\nsrc/a.c:10:[Omitted long matching line]\nsrc/sub/b.h:1:[Omitted long matching line]\n`; `-M 3 -n -A1 two src/a.c` -> `2:[Omitted long matching line]\n3-[Omitted long context line]\n`.
- `RgBinary`: the three verified cases under "Reading"; `-a TODO src/bin.dat` prints the line raw.
- `RgErrors`: `rg x nosuch` -> err `rg: nosuch: IO error for operation on nosuch: No such file or directory (os error 2)\n`, 2; `rg x nosuch t1` -> `rg: nosuch: No such file or directory (os error 2)\n`, 2; `-q TODO nosuch t1` -> 0; no arguments (stdin empty) -> `rg: ripgrep requires at least one pattern to execute a search\n`, 2; `--foo x` -> `rg: unrecognized flag --foo\n`; `-k x` -> `rg: unrecognized flag -k\n`; `-e` -> `rg: missing value for flag -e: missing argument for option '-e'\n`; `--heading=yes x` -> the "invalid CLI arguments" line; `-A x y` -> `rg: error parsing flag -A: value is not a valid number: invalid digit found in string\n`; `--color=bad x` -> `rg: error parsing flag --color: choice 'bad' is unrecognized\n`; all exit 2.
- `RgFilesLists`: `--files src` -> `src/a.c\nsrc/bin.dat\nsrc/sub/b.h\n`; `--files src t1` -> those then `t1`.
- `RgColor`: `--color=always` cases of the Colours bullet; `Run` with `TERM=xterm` set on the OS environment colours by default; `TERM=dumb` does not.
- `RgHelpAndVersion`: `--version` -> `rg (HaisosOS builtin) 1.0.0\n`; `-h` and `--help` print the standard shape with the GitHub reference line.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='*Rg*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: an `rg` row (1.0.0; treated
  flags; documented exceptions: path order instead of rg's parallel order,
  long-flag abbreviations accepted, no "similar flags" hint, standard input
  only when it is not empty at once, Perl-subset regex (until
  `search--rg-regex`), no ignore rules yet (until `search--rg-ignore`));
  a line on `BuiltinHelp::referenceUrl`.
- Root `CLAUDE.md`: an `rg` row in the Builtin Commands table.

## Acceptance

- [ ] `rg` registered; `--help` in the standard shape with the ripgrep
      reference; `--version`; every rg 14 flag in the table.
- [ ] Terminal vs non-terminal defaults exactly as rg (heading, line
      numbers, colour with `TERM`).
- [ ] Output modes, context, `-m`, `-M`, `-0`, colours byte-exact as listed.
- [ ] Binary files: operands report, walked files skipped.
- [ ] Error messages and exit statuses as listed; no Try lines.
- [ ] Reuses `GrepMatcher` and `GrepContext`; files only through `context.IO()`.
- [ ] Full unit suite passes; both CLAUDE.md tables updated.

## Out of scope

- Hidden files, ignore files, `-g`, `-t`, `--max-depth`, `-u`, `-L`:
  `search--rg-ignore`.
- Rust-regex syntax, rg's regex error frames with carets: `search--rg-regex`.
- `--replace`, `--json`, `--vimgrep`, multiline, PCRE2, encodings.
