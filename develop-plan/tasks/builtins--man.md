# Task builtins--man: The man builtin, printing the compiled-in pages

- Rock: builtins
- Depends on: builtins--directories, builtins--wc (its page is used in the tests), and streams--exit-codes (the haisos test checks the exit code; earlier in the playbook)
- Size: ~450 changed lines in ~10 files
- Plan checked against: develop @ 0d92271
- PR title: Add the man builtin, showing every builtin's manual page

## Goal

A new builtin `man`, after man-db's `man(1)` (2.12): `man <name>...` prints
the manual page of each builtin named -- `IBuiltinCommand::ManPage()`, which
is the builtin's `--help` text unless the builtin overrides it -- as plain
text, no pager, pages back to back. `man wc` prints exactly what `wc --help`
prints. An unknown page prints `No manual entry for <name>` on stderr and
exits 16, as man-db does. `man -f` (whatis) and `man -k` (apropos) work over
the builtins' names and one-line summaries. Every man-db option is accepted;
the ones `man` does not act on are reported as not treated. `haisos --init`
lists it automatically.

## Context

Read first: the root `CLAUDE.md` ("Builtin Commands", "Security:
`ICurrentProcess` is the only door out of a process", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h`/`.cpp`
(`ParseBuiltinArgs`, `BeginBuiltin`, `BuiltinHelpText`), `BuiltinCommandList.h`.

What earlier tasks provide (as if on develop):

- builtins--directories: `commands/<name>/<Name>.cpp` layout;
  `virtual std::string IBuiltinCommand::ManPage() const` (default
  `BuiltinHelpText(*this)`); `BuiltinContext::ErrorText(text)` (stderr as
  given), `Error(message)` (`man: ` + message + newline), `Out(text)`
  (stdout); the fixture header
  `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsFixture.h`
  with `RunCaptured(command, args, input, workingDirectory) -> Captured{out, err, status}`.
- builtins--wc: `wc` registered (its page is used in tests).
- streams--exit-codes: `haisos` exits with the code of its first failing `RUN`.

Behaviour below was checked against man-db 2.12.0 with output not on a
terminal.

## Changes

### How man reaches the pages (the `ICurrentProcess` rule)

`Man.cpp` includes `BuiltinCommandList.h` and, inside `Run`, calls
`CreateStandardBuiltinCommands()`: a pure function returning fresh,
stateless command objects compiled into Haisos -- program data, like a
static table, not something outside the process. `man` is handed no
`IHaisosOS`, no `IBuiltinCommands`, no filesystem, and touches
`context.IO()` only for its standard streams. Consequence, to document: `man`
shows the pages of every builtin compiled into Haisos, whether or not it is
placed on the filesystem, and regardless of which `IBuiltinCommands` the OS
was created with. (Calling the list from within a command creates a second
`man` object; that is harmless -- commands are stateless.) Update the
comment in `BuiltinCommandList.h` ("Only the registry (BuiltinCommands) and
tests need this list") to name `man` too, and why it may.

### `src/components/BuiltinCommands/BuiltinCommand.h` / `.cpp` -- one parser addition

man-db's `-T[DEVICE]`, `-H[BROWSER]`, `-X[RESOLUTION]` take an optional
argument attached to the short option too (`-Tutf8`), which the existing
`BuiltinArgument::Optional` does not do (ls's `-F` must keep taking none).
Add an enumerator:

```cpp
enum class BuiltinArgument { None, Required, Optional, OptionalAttached };
// OptionalAttached: optional, and taken only when attached -- to the long
// option ("--troff-device=utf8") or to the short one ("-Tutf8"), never from
// the next word ("-T utf8" is -T, then the operand utf8).
```

`ParseBuiltinArgs`: in the short-option loop, an `OptionalAttached` option
takes the rest of the cluster as its argument when there is any (then ends
the cluster), else none. Long options treat it as `Optional`.
`OptionSynopsis` shows it as `Optional` (`-T, --troff-device[=DEVICE]`).

### New `src/components/BuiltinCommands/commands/man/Man.cpp`

`class ManCommand : public IBuiltinCommand` (anonymous namespace) and
`std::shared_ptr<IBuiltinCommand> CreateManCommand()`. `Name()` `"man"`,
`Version()` `"1.0.0"`. `ManPage()` not overridden.

`Help()`: summary `an interface to the system reference manuals`; usage
`man [OPTION...] [SECTION] PAGE...`, `man -k [OPTION...] REGEXP...`,
`man -f [OPTION...] PAGE...`; notes:
```
Pages are compiled in: one per builtin, all in section 1, printed as plain text without a pager.
-k and -f search the builtins' names and one-line summaries.
```

Option table -- every man-db 2.12 option; `T` treated, `-` not treated
(`kBuiltinNotTreated`, no description). Aliases of one man-db option are
separate rows (one long name per row):

| short | long | argument | treated | description |
|-------|------|----------|---------|-------------|
| `C` | `config-file` | Required `FILE` | - | |
| `d` | `debug` | | - | |
| `D` | `default` | | - | |
| | `warnings` | Optional `WARNINGS` | - | |
| `f` | `whatis` | | T | `one-line description of each PAGE` |
| `k` | `apropos` | | T | `search names and descriptions` |
| `K` | `global-apropos` | | - | |
| `l` | `local-file` | | - | |
| `w` | `where` | | - | |
| | `path` | | - | |
| | `location` | | - | |
| `W` | `where-cat` | | - | |
| | `location-cat` | | - | |
| `c` | `catman` | | - | |
| `R` | `recode` | Required `ENCODING` | - | |
| `L` | `locale` | Required `LOCALE` | - | |
| `m` | `systems` | Required `SYSTEM` | - | |
| `M` | `manpath` | Required `PATH` | - | |
| `S` | `sections` | Required `LIST` | - | |
| `s` | | Required `LIST` | - | |
| `e` | `extension` | Required `EXTENSION` | - | |
| `i` | `ignore-case` | | T | `case-insensitive names (default)` |
| `I` | `match-case` | | T | `case-sensitive names` |
| | `regex` | | - | |
| | `wildcard` | | - | |
| | `names-only` | | - | |
| `a` | `all` | | - | |
| `u` | `update` | | - | |
| | `no-subpages` | | - | |
| `P` | `pager` | Required `PAGER` | - | |
| `r` | `prompt` | Required `STRING` | - | |
| `7` | `ascii` | | - | |
| `E` | `encoding` | Required `ENCODING` | - | |
| | `no-hyphenation` | | - | |
| | `nh` | | - | |
| | `no-justification` | | - | |
| | `nj` | | - | |
| `p` | `preprocessor` | Required `STRING` | - | |
| `t` | `troff` | | - | |
| `T` | `troff-device` | OptionalAttached `DEVICE` | - | |
| `H` | `html` | OptionalAttached `BROWSER` | - | |
| `X` | `gxditview` | OptionalAttached `RESOLUTION` | - | |
| `Z` | `ditroff` | | - | |
| `?` | | | T, id `kBuiltinOptionHelp` | `show this help` |
| | `usage` | | - | |
| `V` | | | T, id `kBuiltinOptionVersion` | `show the version` |

(`--help` and `--version` come from the shared parser as for every builtin;
the `-?` and `-V` rows make man-db's short forms work through `BeginBuiltin`.)

#### Run, step by step

1. `ParseBuiltinArgs(context.Args(), Options())`. On an error, man-db's
   wording: `context.Error(parsed.error)` then
   `ErrorText("Try 'man --help' or 'man --usage' for more information.\n")`,
   exit 1. (Not `BeginBuiltin`'s Try line, which lacks `--usage`.)
2. Otherwise `BeginBuiltin(context, *this, 1, status)` -- it parses again
   (no error now) and handles `--help`/`-?`, `--version`/`-V` and the
   not-treated reports; `nullopt` -> return `status`.
3. Mode: the last of `-f`/`-k` given, else page mode. Case: the last of
   `-i`/`-I`, default insensitive (ASCII case folding).
4. The pages: `CreateStandardBuiltinCommands()`, looked up by `Name()`.

**Page mode** (operands = `ops`):

- `ops` empty: stderr `What manual page do you want?\nFor example, try 'man man'.\n`, exit 1.
- A **section** is: one of `1 n l 8 3 0 2 3type 3posix 3pm 3perl 3am 5 4 9 6 7`,
  or two or more characters whose first is a digit and second is not a digit
  (`1x`, `3ssl`; `10` is not one) -- man-db's `is_section`.
- `ops` is one section only (`man 1`): stderr
  `No manual entry for 1\n(Alternatively, what manual page do you want from section 1?)\nFor example, try 'man man'.\n`, exit 1.
- Two or more and the first is a section: that is the section, the rest are
  names. Every builtin page is in section `1`, and only the exact section `1`
  finds it (`man 1x ls` finds nothing, as man-db).
- For each name in order: found (and section absent or `1`) -> `Out(ManPage())`;
  else stderr `No manual entry for <name>\n`, or with a section
  `No manual entry for <name> in section <section>\n` (name and section as
  typed, unquoted). Pages follow each other with nothing in between (man-db
  off a terminal). Exit 16 if any name was not found, else 0.

**Whatis (`-f`)**: every operand is a keyword (no section handling).

- No operand: stdout `whatis what?\n`, exit 1.
- For each keyword in order: the builtin whose name equals it (by the case
  rule) -> stdout the **whatis line** for it, with the keyword *as typed* as
  its name (man-db prints `LS (1)` for `man -f LS`); none ->
  stderr `<keyword>: nothing appropriate.\n`.
- Exit 16 if no keyword matched, else 0.

**Apropos (`-k`)**: every operand is a POSIX extended regular expression
(`std::regex::extended | std::regex::icase`, always case-insensitive).

- No operand: stdout `apropos what?\n`, exit 1.
- A keyword that does not compile: stderr
  ``apropos: fatal: regex `<keyword>': Invalid regular expression\n``
  (backquote, then quote), exit 2, nothing else printed.
- A builtin matches when some keyword is found (`std::regex_search`) in its
  name or in its `Help().summary`. Stdout: the whatis line of each matching
  builtin, once, in name order. Each keyword that matched nothing: stderr
  `<keyword>: nothing appropriate.\n`. Exit 16 if nothing matched, else 0.

**Whatis line**: `<name> (1)` left-aligned in 20 columns (no padding if
longer), then ` - `, the builtin's `Help().summary`, newline (man-db's
`%-20s - %s`):

```
ls (1)               - list directory contents
wc (1)               - print newline, word, and byte counts for each file
```

Worked examples (man-db 2.12, not on a terminal; `$` ends a line):

```
man wc            -> out = `wc --help` output, exit 0
man ls wc         -> out = ls help immediately followed by wc help, exit 0
man nosuch        -> err "No manual entry for nosuch$", exit 16
man ls nosuch     -> out ls help, err "No manual entry for nosuch$", exit 16
man n1 n2         -> err "No manual entry for n1$" "No manual entry for n2$", exit 16
man 1 ls          -> out ls help, exit 0
man 8 ls          -> err "No manual entry for ls in section 8$", exit 16
man 1 nosuch      -> err "No manual entry for nosuch in section 1$", exit 16
man 10 ls         -> err "No manual entry for 10$", out ls help, exit 16
man               -> err "What manual page do you want?$" "For example, try 'man man'.$", exit 1
man 1             -> err "No manual entry for 1$" "(Alternatively, what manual page do you want from section 1?)$" "For example, try 'man man'.$", exit 1
man LS            -> out ls help, exit 0
man -I LS         -> err "No manual entry for LS$", exit 16
man -f ls wc      -> the two whatis lines above, exit 0
man -f LS         -> "LS (1)               - list directory contents$", exit 0
man -f ls nosuch  -> out ls line, err "nosuch: nothing appropriate.$", exit 0
man -f            -> out "whatis what?$", exit 1
man -k 'list director'  -> out ls line, exit 0
man -k '^(ls|cat)$'     -> out cat line then ls line, exit 0
man -k nosuchxyz  -> err "nosuchxyz: nothing appropriate.$", exit 16
man -k '['        -> err "apropos: fatal: regex `[': Invalid regular expression$", exit 2
man -k            -> out "apropos what?$", exit 1
man -y            -> err "man: invalid option -- 'y'$" "Try 'man --help' or 'man --usage' for more information.$", exit 1
man --frob        -> err "man: unrecognized option '--frob'$" + the same Try line, exit 1
man -P less ls    -> err "Parameter -P is not treated by HaisosOS man v. 1.0.0$", out ls help, exit 0
man -Tutf8 ls     -> err "Parameter -T is not treated by HaisosOS man v. 1.0.0$", out ls help
man -V            -> out "man (HaisosOS builtin) 1.0.0$"
```

### `src/components/BuiltinCommands/BuiltinCommandList.h`, `CMakeLists.txt`

Declare `CreateManCommand()`, add it to `CreateStandardBuiltinCommands()` in
name order (cat, echo, ls, man, mkdir, pwd, wc); add `commands/man/Man.cpp`
to the library sources. Registering is what puts `man` in `GetCommands()` and
in the `haisos --init` template (rule 9; never hand-write the `BUILTIN`
line).

### Root `CLAUDE.md` rules that bite

- Every option of man-db's `man` in the table, untreated ones reported; the
  `--help` shape generated (`Based on Linux man:
  https://man7.org/linux/man-pages/man1/man.1.html`); version 1.0.0.
- Messages and exit codes are man-db's (16 for a missing page, 1 for usage,
  2 for a bad regex) -- man-db, not coreutils, is the "real command" here.
- `ICurrentProcess` rule: as explained above; nothing else is reached.

## Tests

### `tests/unit/components/BuiltinCommands.unittests/ManTest.cpp` (new)

Add to `add_executable(BuiltinCommands.unittests ...)`. `#include
"BuiltinCommandsFixture.h"`; `TEST_F(BuiltinCommandsTest, Man...)`, all with
`RunCaptured`, comparing `out`, `err` and `status` exactly:

- `ManPrintsEachBuiltinsHelp`: for every name in `builtins->GetCommands()`,
  `man <name>` out == `<name> --help` out, err empty, status 0.
- `ManPrintsSeveralPagesBackToBack`: `man ls wc`.
- `ManReportsAMissingPage`: `nosuch`; `ls nosuch`; `n1 n2`.
- `ManTakesASectionFirst`: `1 ls`, `8 ls`, `1 nosuch`, `1x ls`, `3posix ls`
  (`No manual entry for ls in section 3posix`), `10 ls`.
- `ManWithoutAPage`: no operand; `1` alone.
- `ManIgnoresCaseUnlessAskedNotTo`: `LS`, `-I LS`, `-I -i LS` (found).
- `ManWhatis`: `-f ls wc`, `-f LS`, `-f ls nosuch`, `-f nosuch`, `-f`.
- `ManApropos`: `-k 'list director'`, `-k '^(ls|cat)$'`, `-k nosuchxyz`,
  `-k '['`, `-k`, and `-k LIST` (case-insensitive).
- `ManUsageErrors`: `-y`, `--frob`.
- `ManShortHelpAndVersion`: `-?` out == `--help` out; `-V` out.
- `ManNotTreatedOptionsStillShowThePage`: `-P less ls`, `-Tutf8 ls`,
  `-X100 ls` (`Parameter -X ...`), `--warnings=all ls`.

And in `BuiltinCommandsTest.cpp`:

- `ListsEveryBuiltinSortedWithAVersion`: the names now include `man`.
- `TEST(BuiltinArgsTest, OptionalAttachedTakesOnlyAnAttachedArgument)`: an
  option table `{'T', "troff-device", 1, BuiltinArgument::OptionalAttached, "DEVICE"}`,
  `{'F', "classify", 2, BuiltinArgument::Optional, "WHEN"}`, `{'l', "", 3}`;
  `ParseBuiltinArgs({"-Tutf8", "-T", "x", "--troff-device=a", "--troff-device", "-Fl"}, table)`
  gives no error, operands `{"x"}`, and options in order: 1 with argument
  `utf8`; 1 without; 1 with `a`; 1 without; 2 without; 3.

The generic tests (help shape, version, untreated options accepted and
reported, man page = help) cover `man` with no change; check that
`EveryUntreatedOptionIsAcceptedAndReported` passes for the `OptionalAttached`
rows (it passes no argument to them).

### `tests/haisos/builtins.haisostest/builtins.haisostest.js`

Add `BUILTIN rootfs man /bin/man` (and keep wc's) to the generated
haisosfile. Add a runner variant on `spawnSync` that returns
`{stdout, stderr, status}` without throwing on a non-zero exit. Checks:
`RUN /bin/man wc` stdout equals `RUN /bin/wc --help` stdout (both non-empty);
`RUN /bin/man nosuch`: stderr contains `No manual entry for nosuch`, stdout
has no such line, `status` is 16.

### Commands

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinArgsTest.*'
./output/linux/CliParser.unittests --gtest_filter='*TheInitTemplates*'
bash ./scripts/test_linux.sh L U
bash ./scripts/test_linux.sh L H builtins
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: intro lists `man`; table row
  `man` | 1.0.0 | `-f -k -i -I`, a section first (`man 1 ls`), several pages
  | pages are compiled in (each builtin's `ManPage()`, its `--help` unless
  overridden), all section 1, plain text, no pager; `-k` matches names and
  summaries only; everything else of man-db's reported as not treated.
  A short paragraph: how `man` reaches the pages
  (`CreateStandardBuiltinCommands()`, program data) and why that keeps the
  `ICurrentProcess` rule. Mention `BuiltinArgument::OptionalAttached` where
  `ParseBuiltinArgs` is described.
- Root `CLAUDE.md`: "Builtin Commands" table row `man` -- "Prints a builtin's
  manual page (`man ls`, `man 1 ls`, `-f`, `-k`): its `--help` text, or a full
  page for `hsh`"; the intro sentence and Directory Structure line list `man`.

## Acceptance

- [ ] `commands/man/Man.cpp` exists, registered, in `CMakeLists.txt`; `haisos --init` lists `# BUILTIN rootfs man /bin/man`.
- [ ] `man <name>` prints exactly `ManPage()`; `man wc` == `wc --help`.
- [ ] Every worked example above holds byte for byte.
- [ ] `Man.cpp` uses no `IHaisosOS`, `IBuiltinCommands` or filesystem; the pages come from `CreateStandardBuiltinCommands()`.
- [ ] Every man-db option is in the table; `BuiltinArgument::OptionalAttached` parses `-Tutf8` and leaves `ls -Fl` as before.
- [ ] Both CLAUDE.md tables updated.
- [ ] Linux build passes; all unit tests and the haisos tests pass.

## Out of scope

- `hsh`'s full page (`ManPage()` override, hsh--interactive).
- A pager, formatting (bold, underline), `MANWIDTH`, terminal-dependent output.
- Pages for anything but builtins (agents, Lua scripts, files on disk);
  `-l`, `-w`, `-K`, `-a`, `--regex`/`--wildcard` modes, `-s`/`-S` section lists.
