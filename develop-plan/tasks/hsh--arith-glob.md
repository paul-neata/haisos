# Task hsh--arith-glob: hsh pattern matching, pathname expansion and arithmetic

- Rock: hsh
- Depends on: hsh--lexer
- Size: ~850 changed lines in ~9 files
- Plan checked against: develop @ 0d92271
- PR title: hsh: pattern matching, globbing and arithmetic evaluation

(Split out of hsh--expansion, which came out at ~1900 lines. This task has no
dependency on the AST: it can run any time after hsh--lexer, and
hsh--expansion builds on it.)

## Goal

Three self-contained pieces of `hsh` (the Haisos shell, a dash
reimplementation), as libraries with unit tests:

- **Pattern matching**, one matcher shared by pathname expansion, `case` and
  `${x%pattern}` / `${x#pattern}`: `*`, `?`, `[...]` with `!` negation, ranges
  and `[:class:]`, backslash escapes.
- **Pathname expansion** (globbing) of one pattern against the process's
  files, through an abstract directory source the executor implements with the
  process's `IFileIO` -- sorted as dash sorts, dotfiles only with an explicit
  leading dot, nothing found meaning "keep the word".
- **Arithmetic evaluation** of `$((...))` as dash does it: `intmax_t`,
  dash's operators and precedence, assignments to variables, short-circuit,
  dash's error messages byte for byte.

## Context

Read first: `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` and
`HshError.h` (hsh--lexer), the root `CLAUDE.md` ("Security: `ICurrentProcess`
is the only door out of a process"), and the dash manual's "Shell Patterns",
"Pathname Expansion" and "Arithmetic Expansion" sections
(https://man7.org/linux/man-pages/man1/dash.1.html). Every behaviour below was
checked against dash 0.5.12; check more with `dash -c '...'`.

What earlier tasks provide (namespace `Haisos::Hsh`, in
`src/components/BuiltinCommands/commands/hsh/`):

- hsh--lexer: `ShellError(message, line = 0, incomplete = false)` in
  `HshError.h`; `IsValidShellName` in `HshWord.h`; the `Hsh.unittests`
  executable in `tests/unit/components/Hsh.unittests/` (filter `Hsh`).
- Already on develop: `DirectoryEntry { std::string name; char type; }` and
  `DirectoryEntryType::Dir` in `interfaces/IFileSystemService.h`; `IFileIO::ReadDirectory`
  (lists `.` and `..` first) and `IFileIO::Stat` in `interfaces/IFileIO.h`.
  This task does not call them itself (see `IPathnameSource`).

## Changes

All in namespace `Haisos::Hsh`, in `src/components/BuiltinCommands/commands/hsh/`.
Plain portable C++17 -- no `<glob.h>`, `<fnmatch.h>`, `<regex.h>`: the code
is built for Windows/MSVC and WASM too. Characters are bytes (dash has no
multibyte support: `?` matches one byte).

### `HshPattern.h` and `HshPattern.cpp` (new)

```cpp
// Whether |text| as a whole matches the shell pattern |pattern|.
bool MatchPattern(std::string_view pattern, std::string_view text);

// Whether |pattern| has anything MatchPattern treats specially: an unescaped
// '*' or '?', or an unescaped '[' that opens a complete bracket expression.
bool HasPatternCharacters(std::string_view pattern);

// The text a pattern without special characters matches: its escapes removed ("a\*" -> "a*").
std::string UnescapePattern(std::string_view pattern);

// |text| with '\\', '*', '?' and '[' escaped, so that it matches only itself.
std::string EscapeForPattern(std::string_view text);

enum class PatternRemoval { SmallestPrefix, LargestPrefix, SmallestSuffix, LargestSuffix };

// ${name#pattern}, ${name##pattern}, ${name%pattern}, ${name%%pattern}:
// |value| without the shortest/longest prefix/suffix |pattern| matches, or
// |value| unchanged when none does. An empty pattern removes nothing.
std::string RemovePattern(std::string_view value, std::string_view pattern, PatternRemoval which);
```

Pattern syntax:

- `*` matches any string (including empty), `?` any one character.
- `\c` matches `c` literally (any c); a trailing lone `\` matches `\`.
- `[` opens a bracket expression: `!` right after `[` negates it (`^` does
  NOT: in dash `[^a]` is the set of `^` and `a`); a `]` right after `[` or
  `[!` is a member; `a-z` is a byte range (unsigned byte values), a `-` first
  or last is a member; `[:alpha:]` and the other classes `alnum alpha blank
  cntrl digit graph lower print punct space upper xdigit` (ASCII, as the C
  locale; an unknown class name matches nothing); `\c` inside is the member
  `c`. A `[` with no closing `]` is an ordinary character (`[a`, `[`, and
  `[!]`, which has no closing bracket since its `]` is a member).
- Everything else matches itself.

Matching: the classic two-index loop with backtracking to the last `*` (each
non-`*` element consumes exactly one character, so remembering only the last
star position is enough) -- no recursion, no regex. `RemovePattern` tries the
prefixes/suffixes of `value` from shortest to longest (smallest) or longest to
shortest (largest) with `MatchPattern`.

### `HshGlob.h` and `HshGlob.cpp` (new)

```cpp
// Where pathname expansion reads directories from. The executor implements it
// with the running process's IFileIO (ReadDirectory, Stat) -- so globbing goes
// through ICurrentProcess like every other file access, never around it.
class IPathnameSource {
public:
    virtual ~IPathnameSource() = default;
    // The entries of the directory at |path| ("." and ".." included, as
    // IFileIO::ReadDirectory lists them); a relative path is relative to the
    // working directory; empty when it is not a directory.
    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;
    // Whether anything is at |path| (IFileIO::Stat succeeds).
    virtual bool Exists(const std::string& path) = 0;
};

// Every pathname |pattern| matches, sorted byte by byte (std::string's
// operator<, as dash sorts with strcmp: "B" before "a"); empty when nothing
// matches -- the caller then keeps the word as it was. |pattern| is in
// MatchPattern's syntax; an unescaped '/' separates components.
std::vector<std::string> ExpandPathname(const std::string& pattern, IPathnameSource& source);
```

Algorithm:

1. Split the pattern into components at each unescaped `/` (a bracket
   expression never spans a `/`). A leading `/` makes the path absolute: the
   result starts with `/` and the first directory read is `/`. Empty
   components (from `//` or a trailing `/`) are kept.
2. Walk the components with a list of prefixes (initially one: `""`, or `/`
   for an absolute pattern):
   - A component without pattern characters: append its unescaped text (and
     the `/` that follows, if any) to every prefix, without reading anything.
     If it is the last component, keep only the prefixes for which
     `Exists(prefix)` -- only reached when an earlier component had pattern
     characters (`*/f`).
   - A component with pattern characters: for each prefix, read the directory
     it names (`"."` for the empty prefix, the prefix without its trailing `/`
     otherwise, `/` for `/`) and keep each entry whose name matches the
     component, skipping names that start with `.` unless the component
     starts with a literal `.` (`\.` counts). `.` and `..` come from the
     listing and match `.*` (`echo .*` gives `. .. .hid`, as dash). If more
     components follow, keep only entries of type `DirectoryEntryType::Dir`
     and continue with `prefix + name + "/"`; a trailing empty component
     (`d*/`) means the same and the result keeps the `/` (`d1/ d2/`).
3. Sort the results; return them (empty if none).

`ExpandPathname` is called only for words with pattern characters (the caller
checks `HasPatternCharacters`).

### `HshArithmetic.h` and `HshArithmetic.cpp` (new)

```cpp
// The variables $((...)) reads and assigns.
class IArithmeticVariables {
public:
    virtual ~IArithmeticVariables() = default;
    // The variable's value; nullopt when it is unset.
    virtual std::optional<std::string> Get(const std::string& name) = 0;
    // Sets it; false (changing nothing) when it is read-only.
    virtual bool Set(const std::string& name, const std::string& value) = 0;
};

// Evaluates |expression| -- the text of a $((...)) after its own parameter
// expansions and command substitutions -- as dash does. Throws ShellError
// (line 0) on an error.
intmax_t EvaluateArithmetic(const std::string& expression, IArithmeticVariables& variables);
```

Language (dash's, nothing more):

- Tokens: blanks (space, tab, newline) are skipped; a **number** starts with a
  digit and is exactly what `std::strtoimax(p, &end, 0)` consumes (decimal,
  `0` octal, `0x`/`0X` hex; saturating at `INTMAX_MAX` on overflow) -- so
  `08` is `0` then `8` and `0x` is `0` then the name `x`, both
  `expecting EOF`; a **name** is `[A-Za-z_][A-Za-z0-9_]*`; operators, longest
  first: `<<=` `>>=` `<<` `>>` `<=` `>=` `==` `!=` `&&` `||` `*=` `/=` `%=`
  `+=` `-=` `&=` `^=` `|=` and the single characters `+ - * / % < > & ^ | ! ~ ? : = ( )`.
  There is no `++`, `--`, `**` or `,` (`--1` is `-(-1)`; `2 ** 3` and
  `x++` fail with `expecting primary`; `1,2` with `expecting EOF`).
- Precedence, lowest first: assignment (`=` and the compound ones; right
  associative; only directly after a name -- `3=4` is `expecting EOF`),
  `?:` (right associative), `||`, `&&`, `|`, `^`, `&`, `== !=`,
  `< <= > >=`, `<< >>`, `+ -`, `* / %`, unary `+ - ~ !` (prefix, repeatable),
  primary: number, name, `( expr )`.
- Values are `intmax_t` with wrapping two's-complement arithmetic (compute in
  `uint64_t`/`uintmax_t` to avoid undefined behaviour): `INTMAX_MAX + 1` is
  `INTMAX_MIN`. Shift counts are taken modulo 64 (`1 << 64` is 1, as on
  x86-64 dash). `/` and `%` truncate toward zero (`-7 / 2` is -3, `-7 % 2` is
  -1); `INTMAX_MIN / -1` is `INTMAX_MIN` and `INTMAX_MIN % -1` is 0 (dash
  crashes there; hsh does not). Comparisons and `!`, `&&`, `||` give 0 or 1.
- A name's value: unset or empty is 0 (also with `set -u`); otherwise its
  value must be, after optional leading blanks, what `strtoimax(..., 0)`
  reads (sign allowed) followed only by blanks (`" -0x1f "` is -31), else
  `Illegal number: <value>` (`abc`, `1+2`, `12 a`, `09`). Values are not
  evaluated as expressions.
- Assignment stores the decimal result with `Set`; a false return is
  `<name>: is read only`. The expression's value is the assigned value
  (`$((x = y = 3))` sets both).
- `&&`, `||` and `?:` do not evaluate the branch not taken: nothing in it is
  looked up, assigned or divided (`0 && 1/0` is 0, `0 && (x=5)` leaves x).
  Syntax errors in it are still errors.

Errors, thrown as `ShellError(message)`, where `<expr>` is the whole
`expression` argument exactly as given (spaces included):

| Message | When |
|---|---|
| `arithmetic expression: expecting primary: "<expr>"` | a number, name or `(` was needed (`1 +`, empty, ` `, `@`, `2 ** 3`) |
| `arithmetic expression: expecting EOF: "<expr>"` | text left after a complete expression (`a b`, `08`, `1.5`, `x[1]`, `3=4`) |
| `arithmetic expression: expecting ')': "<expr>"` | `(1` |
| `arithmetic expression: expecting ':': "<expr>"` | `1 ? 2` |
| `arithmetic expression: division by zero: "<expr>"` | `/` or `%` (or `/=`, `%=`) by 0 when evaluated |
| `Illegal number: <value>` | a variable's value is not a number |
| `<name>: is read only` | assigning a read-only variable |

### `src/components/BuiltinCommands/CMakeLists.txt`

Add `commands/hsh/HshPattern.cpp`, `commands/hsh/HshGlob.cpp`,
`commands/hsh/HshArithmetic.cpp` to the `BuiltinCommands` sources.

## Tests

Add three files to `add_executable(Hsh.unittests ...)` in
`tests/unit/components/Hsh.unittests/CMakeLists.txt`. Table-driven where
possible.

`HshPatternTest.cpp`:

- `HshPatternTest.Matches`: rows {pattern, text, expected}: `*`, `a*`, `*b`,
  `a*b*c`, `?`, `a?c`, `[abc]`, `[!a]`, `[^a]` matching `^` and `a` but not
  `b`, `[a-c]`, `[]a]`, `[!]a]`, `[a-]`, `[[:upper:]]`, `[[:space:]]`,
  `[[:digit:][:alpha:]]`, `[[:nope:]]`, `\*` vs `*` and `x`, `[a` and `[`
  as literals, `[!]` matching only the text `[!]`, empty pattern vs empty text,
  bytes of a UTF-8 `é` with `??`.
- `HshPatternTest.HasPatternCharacters`: `a`, `a*`, `a\*`, `[a`, `[a]`, `?`.
- `HshPatternTest.UnescapeAndEscape`: round trips.
- `HshPatternTest.RemovePattern`: from dash: `hello` with `l*`: smallest
  suffix `hel`, largest suffix `he`; `*l` prefix: `lo` / `o`; `a.b.c` with
  `.*` / `*.`; `aaa` with `a*` four ways (`aa`, ``, `aa`, ``); `/a/b` with
  `*/` largest prefix `b`; no match leaves the value; empty pattern.

`HshGlobTest.cpp` -- a fake `IPathnameSource` over a map of directory path to
entries (with `.` and `..`), recording the paths read:

- `HshGlobTest.MatchesInTheWorkingDirectory`: files `B C a ab 'a b' .hid`
  and dirs `d1 d2`: `*` gives `B C a a b ab d1 d2` (byte order), `a?` gives
  `ab`, `[!a]*` gives `B C d1 d2`, `.*` gives `. .. .hid`, `nomatch*` gives
  nothing.
- `HshGlobTest.Components`: `*/f` (only `d2/f` exists), `*/*/f`, `d*/`
  (`d1/ d2/`), `d1/*/`, `./*` (`./B` ...), `d1/../*` (literal components are
  not read: check the recorded paths), absolute `/b*`.
- `HshGlobTest.EscapedCharactersAreLiteral`: `a\*` with a file named `a*`
  is not a pattern (HasPatternCharacters false); `\[a]` likewise.

`HshArithmeticTest.cpp` -- a fake `IArithmeticVariables` over a map plus a
set of read-only names:

- `HshArithmeticTest.Values`: rows {expression, expected} from dash: `1+2`,
  `3/2` 1, `-7/2` -3, `-7%2` -1, `7 % -3` 1, `1<<3` 8, `5&3` 1, `5|3` 7,
  `5^3` 6, `~0` -1, `!3` 0, `2>1` 1, `1&&0` 0, `0||2` 1, `1 < 2 < 3` 1,
  `(1+2)*3` 9, `010` 8, `0X10` 16, `0x1f` 31, `--1` 1, `1 +-+ 2` -1,
  `!0 + ~1` -1, `1 ? 2 : 3 ? 4 : 5` 2, `0 ? 1 : 0 ? 4 : 5` 5,
  `99999999999999999999` INTMAX_MAX, `-9223372036854775808` -INTMAX_MAX,
  `9223372036854775807 + 1` INTMAX_MIN, `1 << 64` 1, `1<<63` INTMAX_MIN,
  `-1 >> 1` -1, `INTMAX_MIN / -1` (written out) INTMAX_MIN.
- `HshArithmeticTest.Variables`: unset is 0, empty is 0, `" 12 "` 12,
  `" -0x1f "` -31, `+5`, `010` 8; `x = 3` sets x; `x = y = 3`; every compound
  assignment in sequence from x=5 (`+=2` 7, `-=1` 6, `*=2` 12, `/=3` 4,
  `%=3` 1, `<<=2` 4, `>>=1` 2, `&=7` 2, `|=8` 10, `^=1` 11); `y=y+1` with y
  unset gives 1; short-circuit `0 && (x=5)` leaves x unset, `1 || 1/0` is 1,
  `0 ? 1/0 : 2` is 2.
- `HshArithmeticTest.Errors`: each message of the table byte for byte:
  `1/0` and `5%0` division by zero with `"1/0"`; `1 +`, `` (empty), `  `,
  `@`, ` 2 ** 3 `, `a++` expecting primary; `a b c`, `08`, `1.5`, `x[1]`,
  `3=4`, `1,2`, ` 0x ` expecting EOF; `(1` expecting ')'; ` 1 ? 2 `
  expecting ':'; variables `abc`, `1+2`, `12 a`, `09` -> `Illegal number: ...`;
  assigning a read-only `r` -> `r: is read only`.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U Hsh
bash ./scripts/test_linux.sh L U
```

## Docs

`src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: a short section
each on the pattern matcher (one function for globbing, `case` and
`${x%p}`; `[^...]` is not negation, as dash), pathname expansion
(`IPathnameSource` implemented over the process's `IFileIO`; sorted by byte;
dotfiles; `.`/`..`), and arithmetic (dash's operators only; wrapping;
shift modulo 64; `INTMAX_MIN / -1`).

## Acceptance

- [ ] `MatchPattern` is the only matcher (no `fnmatch`, no regex); it does not recurse.
- [ ] Globbing reads directories only through `IPathnameSource`; nothing here
      touches `IFileIO`, `IFileSystem` or the host filesystem.
- [ ] Results are sorted by byte; `.*` lists `.` and `..`; nothing found is an empty result.
- [ ] Arithmetic has no undefined behaviour on overflow, shifts or `INTMAX_MIN / -1`.
- [ ] Every error message matches its table row byte for byte.
- [ ] Builds on Linux; `Hsh.unittests` passes; all other unit tests still pass.

## Out of scope

- Building patterns from words (quoted characters escaped), field splitting,
  `$((...))` inside words, `set -f` (hsh--expansion); the `IPathnameSource`
  implementation over `IFileIO` (hsh--executor); `case` itself
  (hsh--control-flow).
- bash's extended globs, `**`, `GLOBIGNORE`, `nocaseglob`, brace expansion,
  locale collation, multibyte characters.
