# Task search--find-tests: find -- the expression language, options and tests

- Rock: search
- Depends on: base--fs-rename-times (`SetTimes`, used by the tests), base--regex-match (`Regex`), coreutils--sort (`BuiltinText.h`), coreutils--mv-touch (`BuiltinDate.h`: `ParseDateString`, `LocalTimeOf`, `SecondsFromLocalTime`), search--grep-recursive (`FnMatch`, contract 6)
- Size: ~1000 changed lines in ~8 files (at the upper limit: if it grows past ~1100 while planning the work, split off the second group of tests -- `-size`, the time tests, `-perm`, owners, `-links`, `-inum`, `-samefile` -- into a task of their own)
- Plan checked against: develop @ a24afec
- PR title: Add the find builtin: expressions, options and tests

## Goal

`find` is a builtin command (placed with `BUILTIN rootfs find /bin/find`)
that behaves as GNU findutils 4.9.0's `find` (Ubuntu 24.04, the task
container's `/usr/bin/find`) for everything but the actions that run
programs or write elsewhere (those come in search--find-actions):

- `find [-H] [-L] [-P] [-D debugopts] [-Olevel] [starting-point...] [expression]`;
  no starting point is `.`; no expression is `-print`; an expression without
  an action is `( expr ) -print`.
- Operators `( )`, `!`/`-not`, `-a`/`-and` (and implicit between two
  primaries), `-o`/`-or`, `,`, with GNU's precedence and error messages.
- Global options `-depth -d -maxdepth -mindepth -mount -xdev -noleaf
  -ignore_readdir_race -noignore_readdir_race -files0-from -help --help
  -version --version`, positional options `-daystart -follow -regextype
  -warn -nowarn`, every test GNU has (what Haisos cannot know answered as
  documented below), and the actions `-print -print0 -prune -quit`.
- `find nope` prints `find: 'nope': No such file or directory` and exits 1
  after the other starting points are done.

`find . -name '*.h' -newer CMakeLists.txt -print0` (acceptance scenario 1)
works, piped into `xargs -0` once that exists.

## Context

Read first: root `CLAUDE.md` ("Security", "Builtin Commands", rule 9),
`src/components/BuiltinCommands/CLAUDE.md`, `BuiltinCommand.h/.cpp`,
`commands/ls/Ls.cpp` (how a builtin walks directories with `IFileIO`),
`src/components/Filesystem/FilesystemUtils.h` (`CurrentFileDateTime`,
`EntryTypeOf`), `interfaces/IFileIO.h`, `interfaces/IFileSystemService.h`
(`FileStatus`, `FileDateTime`, `DirectoryEntryType`).

What earlier tasks provide, as if on develop (read their plans in
`develop-plan/tasks/` for the exact text):
- `src/components/Regex/Regex.h` (base--regex-syntax/-match): `Regex::Compile(pattern,
  RegexOptions{syntax, ignoreCase, multiline}, error)` -> `shared_ptr<const Regex>`
  or null with GNU's message; `Search(text, start, match, flags)`;
  `RegexMatch::groups[0]`. CMake target `Regex` (already linked to `BuiltinCommands`:
  nothing to add).
- `BuiltinFnmatch.h` (search--grep-recursive, contract 6; on develop):
  `bool FnMatch(std::string_view pattern, std::string_view text, int flags = 0)`,
  flags `kFnmPathname`, `kFnmNoEscape`, `kFnmPeriod`, `kFnmLeadingDir`,
  `kFnmCaseFold`; glibc fnmatch semantics. Reuse it, never a second matcher.
- `BuiltinText.h` (coreutils--sort): `std::string GnuQuote(std::string_view)`
  (GNU's quote() in the C locale: `'a\'b'`), `BuiltinLineReader` (lines split on
  a delimiter, `'\0'` for `-files0-from`), `OpenInputOperand`.
- `BuiltinDate.h` (coreutils--mv-touch / date): `bool ParseDateString(std::string_view
  text, FileDateTime now, FileDateTime& out)` (the local-zone inline overload
  of the one taking `bool utc`), `std::tm LocalTimeOf(int64_t)`,
  `std::optional<int64_t> SecondsFromLocalTime(const std::tm&)`. (`FormatDateTime`
  is not needed until `-printf`.) `BuiltinSize.h`'s `ParseSizeWithSuffix` is not
  used: `-size`'s suffixes (`c w b k M G`, rounding up, a `+`/`-` sign) are
  find's own and are parsed in `FindTests.cpp`.
- `IFileSystem::SetTimes(path, const optional<FileDateTime>& atime, const optional<FileDateTime>& mtime)`
  (base--fs-rename-times; `IFileIO` has it too) -- the tests set file times
  with it on the fixture's `root`.

Facts Haisos gives `find` to work with, and what follows (documented
exceptions, consistent with `ls -l` and the `stat` plan): every file's mode
is `0777` plus its type, owner and group `haisos`, uid and gid `0`, inode
`0`, no symbolic links (so `-H -L -P -follow` are all the same and `-type l`
never matches), no device numbers or filesystem types, no birth time.

Rules that bite:
- `ICurrentProcess` is the only door out: every file access through
  `context.IO()` (`Stat`, `ReadDirectory`, `ResolvePath`, `OpenFile`, `GetDescriptor`); no
  `IFileSystem` or `IHaisosOS` held.
- GNU's output and messages byte for byte (C locale: ASCII quotes); every
  argument the real command accepts is accepted; `--help` from
  `BuiltinHelpText`; `--version` from `BuiltinVersionText`; the default
  `ManPage()`.
- Registered in `CreateStandardBuiltinCommands()` (which puts it in the
  `haisos --init` template); sources in the component's `CMakeLists.txt`.
- Portable C++17: no POSIX headers, no `<regex>`.
- Stops promptly on `TriggerStop()` (`context.StopRequested()` checked per
  file visited); a broken stdout pipe ends it with 141 as other builtins
  (`context.Out` does that).

## Changes

All new code in `src/components/BuiltinCommands/commands/find/`, namespace
`Haisos` (internal types in `Haisos::Find`). The parser and the primaries
are built so that search--find-actions adds actions by adding table
rows in a new file, without touching the parser.

### `commands/find/FindExpression.h` (new) -- internal types, exactly these names

```cpp
namespace Haisos::Find {

// One file being looked at.
struct FindFile {
    std::string path;           // as find prints it: ".", "./a", "a//x", "/"
    std::string name;           // its base name, as -name sees it (below)
    std::string startingPoint;  // the starting point it was found under, as given
    int depth = 0;              // 0 for a starting point
    FileStatus status;          // IFileIO::Stat of it
};

struct FindSettings {                          // the global options
    std::vector<std::string> startingPoints;   // "." when none is given
    bool depthFirst = false;                   // -depth, -d (and -delete, later)
    bool depthGiven = false;                   // -depth/-d written explicitly
    int maxDepth = -1;                         // -1: none
    int minDepth = 0;
    bool ignoreReaddirRace = false;
    std::optional<std::string> files0From;     // -files0-from FILE
};

// What every primary is handed while the tree is walked.
struct FindRun {
    BuiltinContext& context;
    FindSettings& settings;
    int exitStatus = 0;       // becomes 1 on any reported error
    bool quit = false;        // -quit: stop everything after this file
    bool prune = false;       // -prune: do not descend into the current file
};

// One primary in the expression (a test, an action, or an option, which is
// always true).
class FindPrimary {
public:
    virtual ~FindPrimary() = default;
    virtual bool Evaluate(FindRun& run, const FindFile& file) = 0;
    // Once, after the walk ends (normally or by -quit): -exec ... + runs its
    // last batch here, -fprint flushes its file. Default: nothing.
    virtual void Finish(FindRun& run) {}
};

struct FindNode {
    enum class Kind { And, Or, Not, Comma, Primary };
    Kind kind = Kind::Primary;
    std::unique_ptr<FindNode> left;    // And/Or/Comma: both; Not: left
    std::unique_ptr<FindNode> right;
    std::unique_ptr<FindPrimary> primary;
};

// And/Or short-circuit; Comma evaluates both and gives the right one; Not negates.
bool EvaluateFindNode(FindNode& node, FindRun& run, const FindFile& file);

class FindParser;

enum class FindPrimaryKind { GlobalOption, PositionalOption, Test, Action };

struct FindPrimaryEntry {
    const char* name;               // "-name", spelled as on the command line
    FindPrimaryKind kind;
    bool suppressesDefaultPrint;    // true for every action but -prune and -quit
    // Reads the primary's arguments through |parser| and returns it. An
    // option that only changes settings returns a primary that is always
    // true. Null after parser.Fail(...).
    std::function<std::unique_ptr<FindPrimary>(FindParser& parser, const std::string& name)> parse;
};

// The rows of this task (FindTests.cpp); search--find-actions adds
// FindActionPrimaries() (FindActions.cpp) and the parser looks in both.
const std::vector<FindPrimaryEntry>& FindTestPrimaries();

// State of the parse that primaries' parse functions read and change.
struct FindParseState {
    bool warnings = false;          // -warn/-nowarn; default: stdin is a terminal
    FileDateTime startTime;         // CurrentFileDateTime() when find started
    FileDateTime timeOrigin;        // startTime, or after -daystart the start of tomorrow
    RegexSyntax regexSyntax = RegexSyntax::Basic;   // from -regextype
    bool regexEmacs = true;         // the default type, emacs (translated, below)
    std::string firstNonOption;     // the first test/action seen, for the option-order warning
};

class FindParser {
public:
    FindParser(BuiltinContext& context, const std::vector<std::string>& args);
    // Parses everything. Returns true with |expression| (null: no expression)
    // and |anyAction| set; false when find must end now with *exitStatus
    // (1 after an error, 0 after -help/-version).
    bool Parse(FindSettings& settings, std::unique_ptr<FindNode>& expression,
               bool& anyAction, int& exitStatus);

    // For the parse functions:
    bool NextArgument(const std::string& primaryName, std::string& out);  // else Fail("missing argument to `-x'")
    void Fail(const std::string& message);   // context.Error(message); the parse fails, exit 1
    void Warn(const std::string& message);   // context.Error("warning: " + message) when state.warnings
    BuiltinContext& Context();
    FindSettings& Settings();
    FindParseState& State();
};

}
```

`FindParser` and `FindRun` are plain classes (no interface): held on the
stack of `Run`. Primaries are held by `unique_ptr` inside the tree -- fine,
the `Create()` rule is for classes implementing `interfaces/`.

### `commands/find/FindParser.cpp` (new) -- the command line

1. **Leading options**, while the argument is one of: `-H`, `-L`, `-P`
   (accepted; no links, nothing to do), `-D` (takes the next argument;
   `context.NotTreated("-D")`), `-O...` (`-O`, `-O3`: `NotTreated("-O")`),
   `--` (ends them, dropped). Anything else ends this step.
2. **Starting points**: arguments up to the first that "looks like an
   expression" -- GNU's rule: starts with `-` and is longer than 1; or is
   exactly `!` or `(`; or is exactly `)` or `,` and is not the first
   argument of this step (a leading `,` or `)` is a path). `-` alone is a path.
   None: `.`.
3. **The expression**, recursive descent over the rest:
   ```
   expr    := or (',' or)*
   or      := and (('-o' | '-or') and)*
   and     := unary (('-a' | '-and')? unary)*      -- implicit -a
   unary   := ('!' | '-not') unary | '(' expr ')' | primary
   ```
   A primary name is looked up in `FindTestPrimaries()` (later also
   `FindActionPrimaries()`); `--help` and `--version` are the same as
   `-help` and `-version`. Messages, each then exit 1 (all verified with
   `LC_ALL=C /usr/bin/find` 4.9.0):
   - unknown: ``find: unknown predicate `-foo'`` (also `-Dfoo` after a path)
   - an argument that is not a primary or operator where one is expected:
     ``find: paths must precede expression: `x'``, followed -- only when
     `x` names something that exists (`Stat`) -- by
     ``find: possible unquoted pattern after predicate `-name'?`` naming the
     last primary parsed (e.g. `find -name x .`; `find . -empty extra` gives
     only the first line)
   - ``find: missing argument to `-name'``
   - `find: invalid expression; you have used a binary operator '-o' with nothing before it.` (also `-a`, `,` ...)
   - `find: expected an expression after '-o'` (also `'!'` at the end, `-a`, `,`)
   - `find: invalid expression; I was expecting to find a ')' somewhere but did not see one.`
   - `find: you have too many ')'`
   - `find: invalid expression; empty parentheses are not allowed.`
4. **Global option order**: a GlobalOption after a Test or Action (not after
   another option) warns, when warnings are on, `find: warning: you have
   specified the global option -maxdepth after the argument -name, but
   global options are not positional, i.e., -maxdepth affects tests
   specified before it as well as those specified after it.  Please specify
   global options before other arguments.` -- the argument named is the
   **first** test/action seen (`State().firstNonOption`); note the two
   spaces before "Please". `-d` also warns `find: warning: the -d option is
   deprecated; please use -depth instead, because the latter is a
   POSIX-compliant feature.` (when warnings are on).
5. Warnings are on when descriptor 0 is a terminal
   (`IO().GetDescriptor(IFileIO::kStdIn)` non-null and `IsTerminal()`), then switched by
   `-warn`/`-nowarn` as they are parsed.
6. `anyAction` is true when some parsed primary's entry has
   `suppressesDefaultPrint`. `Run` then wraps: no expression -> `-print`;
   no action -> `And(expression, -print)`.
7. `-help`/`--help` prints `BuiltinHelpText(command)` to stdout,
   `-version`/`--version` `BuiltinVersionText`, at once, exit 0 (the parse
   stops there, as GNU's does).

### `commands/find/FindTests.cpp` (new) -- `FindTestPrimaries()`

Number arguments `N`, `+N`, `-N` (GNU's `get_comp_type`): digits only for
integers; a bad one is ``find: invalid argument `x' to `-mtime'``. Rows, in
GNU's table order (each a small `FindPrimary` subclass or a lambda wrapper):

| Primary | Kind | Behaviour |
|---------|------|-----------|
| `-depth`, `-d` | global | `depthFirst = depthGiven = true` |
| `-maxdepth N`, `-mindepth N` | global | non-negative decimal, else `find: Expected a positive decimal integer argument to -maxdepth, but got 'x'` (GnuQuote) |
| `-mount`, `-xdev` | global | accepted; `NotTreated("-xdev")` (no device ids: the walk crosses mounts) |
| `-noleaf` | global | accepted, nothing to do (an optimisation switch) |
| `-ignore_readdir_race`, `-noignore_readdir_race` | global | sets/clears `ignoreReaddirRace` |
| `-files0-from FILE` | global | starting points read from FILE (`-` stdin), NUL-separated; with starting points on the command line: `find: extra operand '.'` then `find: file operands cannot be combined with -files0-from`, exit 1; unopenable: `find: cannot open 'nofile' for reading: No such file or directory`, exit 1; an empty name (two NULs): `find: 'bad0':2: invalid zero-length file name` (the file name in GnuQuote, the item number), status 1, the others still searched |
| `-help`, `-version` | global | as step 7 |
| `-daystart` | positional | first time only: `timeOrigin` = (startTime + 86400 s) minus its local time of day (`LocalTimeOf`), i.e. the start of tomorrow, local |
| `-follow` | positional | accepted, nothing to do (no links) |
| `-regextype T` | positional | below |
| `-warn`, `-nowarn` | positional | switch warnings |
| `-true`, `-false` | test | |
| `-name P`, `-iname P` | test | `FnMatch(P, file.name, 0 / kFnmCaseFold)` -- no `kFnmPeriod`: `*` matches a leading dot, as GNU. A P holding `/` (other than exactly `/`) warns (warnings on): `find: warning: '-name' matches against basenames only, but the given pattern contains a directory separator ('/'), thus the expression will evaluate to false all the time.  Did you mean '-wholename'?` (`'-iname'` for -iname) |
| `-path P`, `-wholename P`, `-ipath P`, `-iwholename P` | test | `FnMatch(P, file.path, 0 / kFnmCaseFold)` (`*` matches `/`); P ending in `/` (and not `/`) warns `find: warning: -path ./a/ will not match anything because it ends with /.` |
| `-regex R`, `-iregex R` | test | the whole path must match: `Search(path, 0)` finds `groups[0] == (0, path.size())` (leftmost-longest makes this exact). Compiled at parse; failure: `find: failed to compile regular expression 'R': <Regex's message>` (GnuQuote) |
| `-type L`, `-xtype L` | test | L a comma list of `b c d p f l s D`; `f` File, `d` Dir, `c` CharDevice; `b p l s D` never match; `-xtype` = `-type` (no links). Errors: `find: Unknown argument to -type: q`; `find: Must separate multiple arguments to -type using: ','` (`ff`); `find: Duplicate file type 'f' in the argument list to -type.`; `find: Arguments to -type should contain at least one letter` (empty) |
| `-empty` | test | a File of size 0, or a Dir whose `ReadDirectory` holds nothing but `.` and `..` |
| `-size [+-]N[cwbkMG]` | test | unit bytes c 1, w 2, b 512 (default), k 1024, M 1048576, G 1073741824; the file's size in units rounded **up**; `+` greater, `-` less, else equal (so `-size -1k` is only empty files). Errors: ``find: invalid -size type `q'`` (bad unit, or nothing after the sign: `` `+' ``); ``find: Invalid argument `k' to -size`` (no digits, `1.5k`) |
| `-amin -cmin -mmin N` | test | age = timeOrigin - the file's atime/ctime/mtime (seconds, with nanoseconds, may be negative); N may be fractional (`1.5`): `+N` age > N*60; `-N` age < N*60; `N`: (N-1)*60 < age <= N*60 |
| `-atime -ctime -mtime N` | test | same age; `+N` age >= (N+1)*86400; `-N` age < N*86400; `N`: N*86400 <= age < (N+1)*86400 (fractional N allowed). So `-mtime 0` is the last 24 h, a future time matches only `-N` |
| `-used N` | test | days from the file's ctime to its atime, with `-atime`'s comparison; bad N: `find: Invalid argument x to -used` (no quotes) |
| `-newer F`, `-anewer F`, `-cnewer F` | test | the file's mtime / atime / ctime strictly later than F's mtime; F `Stat`-ed at parse: missing -> `find: 'F': No such file or directory` (GnuQuote), exit 1 |
| `-newerXY R` | test | X (the file's) and Y (the reference's) from `a c m`, Y also `t` (R is a date: `ParseDateString(R, startTime, out)`, else ``find: I cannot figure out how to interpret 'R' as a date or time`` with GnuQuote); X or Y `B`: `find: This system does not provide a way to find the birth time of a file.` then ``find: invalid predicate `-newerBt'``, exit 1; another letter: ``find: invalid predicate `-newerqm'`` |
| `-perm [-/]MODE` | test | the file's permission bits are 0777. MODE octal (at most 07777) or symbolic (chmod's clauses `[ugoa]*[-+=][rwxXst]*`, comma-separated, applied to 0 with umask 0; `X` as `x`); bad: `find: invalid mode 'M'` (also `+222`). Exact: bits == MODE; `-MODE`: every MODE bit is in 0777; `/MODE`: any MODE bit in 0777, or MODE 0. `/0` (or `/000`) always prints `find: warning: you have specified a mode pattern /0 (which is equivalent to /000). The meaning of -perm /000 has now been changed to be consistent with -perm -000; that is, while it used to match no files, it now matches all files.` (verify the exact text for `/000`) |
| `-user U`, `-group G` | test | `haisos` or `0`: true for every file; another number: false; another name: `find: 'U' is not the name of a known user` / `find: 'G' is not the name of an existing group`, exit 1 |
| `-uid N`, `-gid N` | test | compare 0 with N (`+`/`-` allowed) |
| `-nouser`, `-nogroup` | test | false (every file has an owner) |
| `-links N` | test | `status.linkCount` |
| `-inum N` | test | compare inode 0 with N |
| `-samefile F` | test | `IO().ResolvePath(file.path) == ResolvePath(F)` (no hard links); F missing: as `-newer` |
| `-lname P`, `-ilname P` | test | false (no links) |
| `-readable`, `-writable`, `-executable` | test | true (no permissions) |
| `-fstype T` | test | false; `NotTreated("-fstype")` (no filesystem types) |
| `-context C` | test | `find: invalid predicate -context: SELinux is not enabled.`, exit 1 (what GNU prints without SELinux) |
| `-print` | action | `path + "\n"` via `context.Out` |
| `-print0` | action | `path + '\0'` |
| `-prune` | action, no suppression | true; sets `run.prune` (ignored when `depthFirst`) |
| `-quit` | action, no suppression | true; sets `run.quit` |

`-regextype T` names (GNU's list, in the error's order): `findutils-default`,
`ed`, `emacs`, `gnu-awk`, `grep`, `posix-awk`, `awk`, `posix-basic`,
`posix-egrep`, `egrep`, `posix-extended`, `posix-minimal-basic`, `sed`.
Unknown: `find: Unknown regular expression type 'foo'; valid types are
'findutils-default', 'ed', 'emacs', 'gnu-awk', 'grep', 'posix-awk', 'awk',
'posix-basic', 'posix-egrep', 'egrep', 'posix-extended',
'posix-minimal-basic', 'sed'.` (one line), exit 1. Treated: `emacs` and
`findutils-default` (the default), `posix-basic`, `grep` (Basic),
`posix-extended`, `posix-egrep`, `egrep` (Extended). The rest map to Basic
(`ed sed posix-minimal-basic`) or Extended (the awks), with
`context.NotTreated("-regextype " + T)`.

Emacs syntax is translated to Basic before compiling, outside brackets:
`+` -> `\+`, `?` -> `\?`, `\+` -> `+`, `\?` -> `?`, `\{` -> `{`, `\}` -> `}`
(emacs has no intervals; `{`/`}` stay literal); bracket expressions are
copied as they are. Verified: `-regex '.+x.*'` matches `./a/x.txt`,
`'.\+x.*'` does not, `'./a\|./a/b'` matches both, `'.*x{1}.*'` and
`'.*x\{1\}.*'` match nothing. One emacs difference to keep: an unterminated
bracket reports `find: failed to compile regular expression '[': Invalid
regular expression` (not glibc's POSIX "Unmatched [ ..." text).

### `commands/find/Find.cpp` (new) -- the command

`CreateFindCommand()`; name `find`, version `1.0.0`; summary `search for
files in a directory hierarchy`; usage `find [-H] [-L] [-P] [-Olevel] [-D
debugopts] [starting-point...] [expression]`.

`Options()` (only what `--help` and the generic tests read; `find` does not
use `ParseBuiltinArgs`): `-H` ("never follow symbolic links: there are
none"), `-L` ("follow symbolic links: there are none"), `-P` ("never
follow symbolic links (the default)"), `-D` Required `DEBUGOPTS`
kBuiltinNotTreated, `-O` OptionalAttached `LEVEL` kBuiltinNotTreated. (The
generic test `EveryUntreatedOptionIsAcceptedAndReported` runs `find -D x
/docs` and `find -O /docs`: both must print the report and list /docs.)

`Help().notes` (a few lines): the primaries treated (tests, operators,
options, actions), then the documented exceptions -- mode 0777, owner
`haisos` uid 0, inode 0, no links (`-H -L -P -follow` the same, `-type l`
never), `-xdev`/`-mount`/`-fstype` not treated (no devices or filesystem
types), `-context` fails as without SELinux, entries listed in the order
the filesystem gives them -- and a line `Not treated primaries: -xdev,
-mount, -fstype` (the last line stays the generated `Not treated
arguments: -D, -O`). search--find-actions updates the notes.

`Run`:
1. `FindParser(...).Parse(...)`; wrap the expression for `-print` (step 6).
2. For each starting point (or `-files0-from` name): `Stat` through
   `IO()`; failing: `find: 'p': No such file or directory` (GnuQuote),
   `exitStatus = 1`, next. Otherwise visit it at depth 0.
3. **Visit(file)**:
   - the name: for a starting point, the path with trailing `/` removed
     (all slashes: `/`), after its last `/`; below it, the entry name;
   - if `!depthFirst` and `depth >= minDepth`: evaluate (reset `run.prune`
     first);
   - if a directory, `maxDepth < 0 || depth < maxDepth`, and not
     (`run.prune` and `!depthFirst`): `ReadDirectory`, skipping `.` and
     `..`, in the order given; the child path is `path + name` when `path`
     ends in `/`, else `path + "/" + name` (GNU: `find a/` gives `a/x`,
     `find a//` gives `a//x`); `Stat` it -- failing: unless
     `ignoreReaddirRace`, `find: 'path': No such file or directory`,
     status 1 -- and visit it;
   - if `depthFirst` and `depth >= minDepth`: evaluate after the children;
   - after each evaluation, `run.quit` ends the whole walk; between files,
     `context.StopRequested()` ends it (return 1, the process reports 143).
   - A path longer than 4096 bytes is not descended into (`find: 'p': File
     name too long`, status 1) -- a guard against a link cycle on a
     physical disk, where Stat follows links.
4. Call `Finish(run)` on every primary in the tree (in parse order), also
   after `-quit`.
5. Return `run.exitStatus`.

### Registration and build

- `BuiltinCommandList.h`: declare `CreateFindCommand()`, add it to
  `CreateStandardBuiltinCommands()` (alphabetical: after `CreateFgrepCommand`,
  before `CreateGrepCommand`).
- `src/components/BuiltinCommands/CMakeLists.txt`: `commands/find/Find.cpp`,
  `commands/find/FindParser.cpp`, `commands/find/FindTests.cpp` in the
  `add_library(BuiltinCommands ...)` list, after `commands/false/False.cpp`
  (`Regex` is already in its `target_link_libraries`).

## Tests

New `tests/unit/components/BuiltinCommands.unittests/FindTest.cpp` (include
`BuiltinCommandsFixture.h`), added to the `add_executable(BuiltinCommands.unittests
...)` list in that directory's `CMakeLists.txt` (after `EnvTest.cpp`);
`TEST_F(BuiltinCommandsTest, Find...)` on `RunCaptured` (stdout, stderr,
status byte for byte; its stdin is not a terminal, so warnings start off).

A helper in the file builds `/proj` on the fixture's in-memory `root`:
`/proj/a/` (dir), `/proj/a/x.txt` = `hi\n`, `/proj/a/b/` (dir),
`/proj/a/b/empty` (empty), `/proj/a/y.md` = 1100 bytes, `/proj/z.h` =
`h\n`. Runs use working directory `/proj`. The in-memory filesystem lists
a directory's entries in byte order of their names, so expected outputs
are in that order; check each against `LC_ALL=C /usr/bin/find` in the
container on the same tree (GNU prints the disk's order: compare
`| sort`ed, or create the files in name order on an ext4 tmpdir). Times are
set with `root->SetTimes(...)` relative to `CurrentFileDateTime()`.

- `FindPrintsTheTreeByDefault`: `find` and `find .` -> `.\n./a\n./a/b\n./a/b/empty\n./a/x.txt\n./a/y.md\n./z.h\n`, 0.
- `FindStartingPointsKeepTheirSpelling`: `find a/ -maxdepth 1` -> `a/\na/b\na/x.txt\na/y.md\n`; `find a// -maxdepth 1` -> `a//`, `a//b`, ...; `find /proj/z.h` -> `/proj/z.h\n`.
- `FindMissingStartingPoint`: `find a nope z.h` -> out `a ... z.h` lines, err `find: 'nope': No such file or directory\n`, 1; `find "a'b"` -> `find: 'a\'b': No such file or directory\n`.
- `FindDepthOptions`: `-maxdepth 1`, `-mindepth 2 -maxdepth 2`, `-depth` (children before `./a/b`), `-d`.
- `FindNameAndPath`: `-name '*.md'`, `-iname 'X*'`, `-name '\a'` (-> `./a`), `-name '.*' -maxdepth 1` (-> `.`), `-path './a/*'`, `-ipath './A/X*'`, `-wholename`.
- `FindOperators`: `-name x.txt -o -name z.h`; `! -type d`; `-not -name '*.*' -type f`; `\( -name a -o -name b \) -type d`; `-name a , -name b` (-> `./a/b` only: the comma's value is its right side); `-false -o -true`.
- `FindImplicitPrint`: `-name a -prune -o -print` -> `.`, `./z.h`; `-name a -print -name zz` (explicit print suppresses the implicit one); `-true -o -quit` prints everything; `-name a -quit` prints nothing.
- `FindPrint0`: `-name '*.h' -print0` -> `./z.h\0`.
- `FindPruneAndQuit`: `-path ./a -prune` lists `./a` but nothing below; `-print -quit` -> `.\n`.
- `FindTypeAndEmpty`: `-type f`, `-type d,f`, `-type l` (nothing), `-empty` (-> `./a/b/empty`), `-xtype f`; errors `-type q`, `-type ff`, `-type f,f`, `-type ''` with the exact texts above, 1.
- `FindSize`: `-size +0 -type f`, `-size -1k` (empty only), `-size 3c` (`./a/x.txt`), `-size 3` (y.md: 1100 bytes is 3 blocks), `-size +1k`; errors `-size 3q`, `-size k`, `-size +`.
- `FindTimes`: y.md mtime now-3 days, x.txt now-30 s, z.h now-90 s, `empty` now+100 s: `-mmin 1` -> x.txt; `-mmin -2` -> x.txt, z.h, empty (and the directories, made now: compare with `-type f`); `-mtime +1 -type f` -> y.md; `-mtime 0 -type f` -> x.txt, z.h; `-mtime -1 -type f` -> x.txt, z.h, empty; `-daystart -mtime 0 -type f` holds x.txt and empty (keep the test away from midnight: skip it when local time is within 2 minutes of midnight).
- `FindNewer`: `-newer a/x.txt`, `-anewer`, `-cnewer`, `-newermt '2000-01-01'` (all), `-newermt garbage` (the message, 1), `-newerBt x` (both lines, 1), `-newerqm a` (1), `-newer nope` (1).
- `FindPermAndOwners`: `-perm 777 -maxdepth 0` -> `.`; `-perm 644` -> nothing; `-perm -u=rwx,g+r -maxdepth 0` -> `.`; `-perm /222`; `-perm /0 -maxdepth 0` -> the warning and `.`; `-perm g+q` and `-perm +222` -> `find: invalid mode ...`, 1; `-user haisos -maxdepth 0`, `-uid 0`, `-gid +1` (nothing), `-nouser` (nothing), `-user nosuch` (1).
- `FindLinksInumSamefile`: `-links +2 -type d` (directories with a subdirectory -- link count 3 -- `.`, `./a`; `./a/b` has 2), `-inum 0 -maxdepth 0`, `-samefile a/x.txt` -> `./a/x.txt`, `-samefile nope` (1), `-lname x` (nothing), `-readable -maxdepth 0`.
- `FindRegex`: `-regex '.*x\.txt'`, `-regex 'x.*'` (nothing: whole path), `-regex '.+x.*'`, `-regex '.\+x.*'` (nothing), `-regex './a\|./a/b'`, `-regextype posix-extended -regex '.*(x|z)\..*'`, `-iregex '.*X\.TXT'`, `-regex '['` and `-regex '\('` errors (texts above), `-regextype foo` (the full message), `-regextype sed -regex '.*'` (reported as not treated, matches).
- `FindExpressionErrors`: each message of FindParser step 3 and the global-option warning (with `-warn`: `-warn -name x -maxdepth 1`; and `-warn -type f -name x -maxdepth 1` names `-type`); `-d` with `-warn`; `-name x/y` and `-path ./a/` warnings with `-warn`; `-maxdepth -1`, `-maxdepth x`; `-mtime x`, `-mtime 1x`, `-uid x`, `-links x`, `-inum x`, `-used x`; `-context x`; `-foo`; `find -name x .` (both lines), `find . -empty extra` (one line).
- `FindLeadingOptions`: `find -H -L -P -O3 -D stat . -maxdepth 0` -> `.` plus the two not-treated reports on stderr; `find -- . -maxdepth 0` -> `.`.
- `FindFiles0From`: a file `/proj/list0` = `a\0z.h\0`: `find -files0-from list0 -maxdepth 0` -> `a\nz.h\n`; with stdin `a\0` and `-files0-from -`; `find . -files0-from list0` (the two lines, 1); `-files0-from nofile`; `a\0\0` (the zero-length line, 1).
- `FindHelpAndVersion`: `find --help`, `find . -help`, `find -version` print the builtin's texts, exit 0.
- `FindIsStoppedPromptly`: a tree of 2000 files; start `find /proj` with `os->StartProcess`, `TriggerStop()`, finishes within 1 s with 143.

Update `BuiltinCommandsTest.cpp`'s `ListsEveryBuiltinSortedWithAVersion`
list with `"find"` between `"fgrep"` and `"grep"`.

Commands:
```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='BuiltinCommandsTest.Find*'
bash ./scripts/test_linux.sh L U
```

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the command list (top paragraph);
  the `BuiltinFnmatch.h` entry (find now uses `FnMatch`, drop "are to reuse it
  too" for find); a table row (Builtin | Version | Treated | Exceptions) for
  `find` 1.0.0 (treated: operators, options, tests, `-print -print0
  -prune -quit`; exceptions: mode 0777, owner haisos uid/gid 0, inode 0, no
  links, `-xdev -mount -fstype -D -O` not treated, `-context` fails, order of
  entries the filesystem's); a short paragraph on `commands/find/` (parser,
  `FindPrimaryEntry` tables, how actions are added).
- Root `CLAUDE.md`: Builtin Commands table row and the command lists.

## Acceptance

- [ ] `find` registered; appears in the `--init` template; generic builtin tests green (help shape, version, untreated `-D`/`-O` reported).
- [ ] Every message in this plan is byte for byte GNU find 4.9.0's (C locale), checked in the container.
- [ ] Every file access goes through `context.IO()`; nothing holds an `IFileSystem` or `IHaisosOS`.
- [ ] `FindExpression.h` has exactly the names above (the next task builds on them).
- [ ] No `<regex>`, no POSIX headers; Windows-safe code.
- [ ] Both CLAUDE.md files updated.

## Out of scope

- `-exec -execdir -ok -okdir -delete -printf -fprint -fprint0 -fprintf -ls
  -fls` (search--find-actions) and xargs (search--xargs). Until then they are
  ``unknown predicate``.
- Sorting the output (GNU does not sort either).
