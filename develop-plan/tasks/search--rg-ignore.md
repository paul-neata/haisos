# Task search--rg-ignore: rg -- what it skips: hidden, ignore files, globs, types

- Rock: search
- Depends on: search--rg-search
- Size: ~900 changed lines in ~8 files
- Plan checked against: develop @ d6c7924
- PR title: rg: hidden files, .gitignore/.ignore/.rgignore, -g, -t, -u

## Goal

`rg` skips, while walking, what ripgrep 14 skips: hidden files and
directories, whatever `.gitignore` files (inside a git repository),
`.git/info/exclude`, `.ignore` and `.rgignore` files say, with gitignore's
pattern rules (`!` negation, anchoring with `/`, trailing `/` for
directories, `**`); and it gains the filters `-g/--glob` (with `!`),
`--iglob`, `--glob-case-insensitive`, `-t/-T` with a built-in subset of
ripgrep's type list (`--type-list`), `--hidden`, `--no-ignore` and its
finer forms, `--no-require-git`, `-u/-uu/-uuu`, `--max-depth`, and `-L`
accepted. So `rg TODO` in a project prints what it prints on Linux --
build output and `node_modules` left out exactly as ripgrep leaves them out.

## Context

**Clean-room rule (the user's; root `CLAUDE.md`):** never read, copy, port or
paraphrase another program's source -- ripgrep's, its `ignore` crate's,
`globset`'s, git's, whatever the licence. Everything here is written from
scratch from the behaviour this plan states and from the real program's
observed output; where something is unclear, run `rg` and look at what it
prints, not at how it is written.

Read first: `develop-plan/tasks/search--rg-search.md` and its code in
`src/components/BuiltinCommands/commands/rg/`: `RgSearch.cpp` -- the walk is
the `walk` lambda inside `RgSearch()` (it reads `context.IO().ReadDirectory(dir)`,
drops `.`/`..`, sorts the `DirectoryEntry` list by name, recurses on
`DirectoryEntryType::Dir`, skips `CharDevice`, and prints `--files` paths
with `printer.Path(child)` or calls the `searchFile(child, false)` lambda;
operands are handled in the loop after it; an implicit `.` has an empty
prefix so names print without `./`; the "No files were searched" message
already exists, driven by `filesSearched`); `RgSearch.h` (`RgSettings`,
`RgResult`, `RgSearch(context, settings, matcher, paths)`); `Rg.cpp` (the
`RgOptionId` enum and the option table -- a flag is treated by giving it an
id there instead of `kBuiltinNotTreated`, and a `case` in `Run()`'s option
loop; `ParseNumberFlag` reads `NUM` flags with rg's errors);
`RgRegex.cpp` (the Rust-syntax translator, not touched except as under
"Folded in"). Also `src/components/BuiltinCommands/BuiltinFnmatch.h`
(`FnMatch(pattern, text, flags)`, `kFnmPathname`, `kFnmPeriod`,
`kFnmCaseFold`, `kFnmLeadingDir`, `kFnmNoEscape` -- glibc fnmatch), the root
`CLAUDE.md` ("Security", "Builtin Commands").

What exists, by exact name: `rg` 1.1.0 (both `search--rg-search` and
`search--rg-regex` are done; this task makes it 1.2.0), whose option table
already lists every flag of this task as `kBuiltinNotTreated` (`-L --follow
--no-follow -g --glob --glob-case-insensitive --no-glob-case-insensitive
-. --hidden --no-hidden --iglob -d --max-depth --maxdepth --no-ignore
--ignore --no-ignore-dot --ignore-dot --no-ignore-exclude --ignore-exclude
--no-ignore-parent --ignore-parent --no-ignore-vcs --ignore-vcs
--no-require-git --require-git -t --type -T --type-not --type-list -u
--unrestricted`; `--ignore-file*`, `--no-ignore-files`/`--ignore-files`,
`--no-ignore-global`/`--ignore-global`, `--max-filesize`, `--one-file-system`,
`--type-add`, `--type-clear` stay not treated); `--binary`/`--no-binary`
are already treated (`RgSettings::binary`: a walked file is then searched as
an operand is); `RgSettings` has no filter fields yet;
`FnMatch`; `IFileIO::ReadDirectory`, `IFileIO::Stat`, `IFileIO::ResolvePath`
(the absolute path inside the OS), `BuiltinLineReader` and
`OpenInputOperand` (`BuiltinText.h`) for reading ignore files. The unit-test
slots: `tests/unit/components/BuiltinCommands.unittests/RgTest.cpp` (fixture
`BuiltinCommandsTest`, helpers `RunCaptured("rg", {...}, std::nullopt, cwd)`,
`WriteTo`, the `root` in-memory filesystem) and `RgRegexTest.cpp`; the
existing Rg tests hold no hidden or ignored files, so they must pass
unchanged.

Reference: ripgrep 14.1.1 run as `rg` (observed output only) (see `search--rg-search.md`), with
`--sort path` and `LC_ALL=C`; every expected output below was produced so.

## Changes

### Rules that bite (root CLAUDE.md)

Ignore files are read and directories walked only through `context.IO()`
(ICurrentProcess is the only door out); the option table stays complete
(these flags now treated); rg's output and messages byte for byte; portable
C++17; `StopRequested()` checked per directory entry.

### `commands/rg/RgIgnore.h` / `RgIgnore.cpp` (new)

```cpp
enum class RgMatch { None, Ignore, Whitelist };

// One gitignore-syntax file (or the -g globs): its patterns in order.
class RgGitignore {
public:
    // |text| the file's content; |caseFold| for --iglob/--glob-case-insensitive.
    static RgGitignore Parse(std::string_view text, bool caseFold);
    // |relativePath|: '/'-separated, relative to the file's directory (no
    // leading "./"). The last pattern that matches decides: Ignore, or
    // Whitelist for a '!' pattern; None when no pattern matches.
    RgMatch Match(std::string_view relativePath, bool isDirectory) const;
    bool HasWhitelist() const;  // any '!'-less pattern (for -g semantics)
    bool Empty() const;
};
```

Pattern lines (gitignore(5), as ripgrep is observed to read them): a trailing
`\r` dropped; blank lines and lines starting with `#` skipped (`\#` is a
literal `#`); trailing spaces dropped unless escaped (`\ `); a leading `!`
negates (`\!` is a literal `!`); a trailing `/` makes the pattern match
directories only (removed). If what remains holds a `/` (a leading one is
removed), the pattern is **anchored**: matched against the whole relative
path; otherwise against the **last component** only, at any depth. `**`
as a whole component: leading `**/` = any number of leading directories
(zero included), trailing `/**` = everything below, `/**/` in the middle =
zero or more directories; any other `**` is `*`. Matching splits pattern
and path on `/` and matches component lists, each component with
`FnMatch(component, name, caseFold ? kFnmCaseFold : 0)` -- `*` never
crosses a `/` because components never hold one; a `**` component tries
every number of path components (an explicit loop, not recursion per
byte).

For `-g`: the same parser with the globs as lines (`-g` and `--iglob` may
mix: keep each pattern's own caseFold).

### The walk (`RgSearch.cpp`)

Settings (new fields of `RgSettings`, plus the globs and types, which `Rg.cpp` parses and hands over): `hidden` (`--hidden`/`-.`; `--no-hidden`), `noIgnoreDot`
(`.ignore`, `.rgignore`), `noIgnoreVcs` (`.gitignore`,
`.git/info/exclude`), `noIgnoreExclude` (`.git/info/exclude` only),
`noIgnoreParent`, `requireGit` (default true; `--no-require-git`),
`binary` (the existing `RgSettings::binary`: `--binary`, `-uuu`), `maxDepth` (optional), the
globs, the types. `--no-ignore` sets the three `noIgnore*`; `--ignore`,
`--ignore-dot`, `--ignore-vcs`, `--ignore-exclude`, `--ignore-parent`,
`--require-git` undo their counterparts; last one wins. `-u` counted: 1 =
`--no-ignore`, 2 = also `--hidden`, 3 = also `--binary`. `-L`/`--follow`
and `--no-follow`: accepted, no effect (documented: Haisos's filesystems
follow a physical filesystem's links themselves; rg by default does not).
`-d/--max-depth NUM` (number errors as rg-search's).

**Operands are never filtered**: a file or directory named on the command
line is searched / walked even when hidden, ignored, excluded by a glob or
not of the type (verified: `rg -l TODO -g '!*.c' src/a.c` -> `src/a.c`;
`rg --files .hid` lists `.hid/h.c`). Depth: an operand is depth 0, its
entries 1, and so on; with `--max-depth N` nothing deeper than N is
visited (`-d 0` with a directory operand lists nothing).

**Ignore files in effect** for a directory D being walked, by kind, each
from D up through its ancestors:
- `.rgignore` and `.ignore` in D and every ancestor of D (the ancestors
  above the operand only without `--no-ignore-parent`) -- unless
  `noIgnoreDot`.
- `.gitignore` -- unless `noIgnoreVcs` -- only when D is inside a git
  repository (an ancestor-or-self directory holding an entry named `.git`,
  a directory or a file), and then only the `.gitignore` files from D up to
  and including the repository's root (the nearest such directory); with
  `--no-require-git`, those of every ancestor, repository or not.
- `.git/info/exclude` of that repository root -- unless `noIgnoreVcs` or
  `noIgnoreExclude`.
Ancestors are found from `IO().ResolvePath(operand)` upward to `/`; read
each file once (cache by absolute directory), parse with `RgGitignore`,
and match an entry by its path relative to the file's own directory
(`.git/info/exclude`: relative to the repository root).

**Deciding an entry met while walking** (ripgrep's observed order):
1. Globs (`-g`, `--iglob`), matched against the path relative to the
   working directory (the printed path without a leading `./`): Ignore ->
   skip; Whitelist -> search/descend, skipping steps 2-4; no match: a
   *file* is skipped when any non-`!` glob exists; otherwise go on.
2. Ignore files, by kind in this precedence: `.rgignore`, `.ignore`,
   `.gitignore`, `.git/info/exclude`; within a kind the deepest directory
   whose file has a matching pattern decides; the first kind with a
   decision wins. Ignore -> skip; Whitelist -> remember "whitelisted".
3. Types (files only): with `-t`, a file matching none of the selected
   types' globs -> skip; matching -> whitelisted; with `-T`, a file
   matching a negated type -> skip (`-T` wins over `-t`).
4. Hidden (a name starting with `.`), not `--hidden`, not whitelisted ->
   skip.
A skipped directory is not descended. Then the binary rule of rg-search
applies to files (`settings.binary` treats walked files as operands, as
`SearchFile`'s `binaryAsOperand` already does). The decision is made in the
`walk` lambda, per entry, before the `Dir` / `--files` / `searchFile`
branches; `--files` lists exactly what would be searched. A glob-only
filter (`-g '*.zzz'`) that leaves nothing searched takes the existing
`noFilesSearched` path.

### Types (`commands/rg/RgTypes.cpp`, new)

A table `name -> globs` matched against a file's base name with
`FnMatch(glob, name, 0)` -- a built-in subset of rg 14's list (rg has 200+;
documented): exactly these rows, in this order, which is also what
`--type-list` prints (`name: glob, glob, ...\n` each, as rg prints them):
```
c: *.[chH], *.[chH].in, *.cats
cmake: *.cmake, CMakeLists.txt
config: *.cfg, *.conf, *.config, *.ini
cpp: *.[ChH], *.[ChH].in, *.[ch]pp, *.[ch]pp.in, *.[ch]xx, *.[ch]xx.in, *.cc, *.cc.in, *.hh, *.hh.in, *.inl
cs: *.cs
css: *.css, *.scss
csv: *.csv
docker: *Dockerfile*
go: *.go
h: *.h, *.hh, *.hpp
html: *.ejs, *.htm, *.html
java: *.java, *.jsp, *.jspx, *.properties
js: *.cjs, *.js, *.jsx, *.mjs, *.vue
json: *.json, *.sarif, composer.lock
jsonl: *.jsonl
kotlin: *.kt, *.kts
lua: *.lua
make: *.mak, *.mk, [Gg][Nn][Uu]makefile, [Gg][Nn][Uu]makefile.am, [Gg][Nn][Uu]makefile.in, [Mm]akefile, [Mm]akefile.am, [Mm]akefile.in
markdown: *.markdown, *.md, *.mdown, *.mdwn, *.mdx, *.mkd, *.mkdn
md: *.markdown, *.md, *.mdown, *.mdwn, *.mdx, *.mkd, *.mkdn
php: *.php, *.php3, *.php4, *.php5, *.php7, *.php8, *.pht, *.phtml
py: *.py, *.pyi
python: *.py, *.pyi
readme: *README, README*
ruby: *.gemspec, *.rb, *.rbw, .irbrc, Gemfile, Rakefile, config.ru
rust: *.rs
sh: *.bash, *.bashrc, *.csh, *.cshrc, *.ksh, *.kshrc, *.sh, *.tcsh, *.zsh, .bash_login, .bash_logout, .bash_profile, .bashrc, .cshrc, .kshrc, .login, .logout, .profile, .tcshrc, .zlogin, .zlogout, .zprofile, .zshenv, .zshrc, bash_login, bash_logout, bash_profile, bashrc, profile, zlogin, zlogout, zprofile, zshenv, zshrc
sql: *.psql, *.sql
swift: *.swift
toml: *.toml, Cargo.lock
ts: *.cts, *.mts, *.ts, *.tsx
txt: *.txt
typescript: *.cts, *.mts, *.ts, *.tsx
xml: *.dtd, *.rng, *.sch, *.xhtml, *.xjb, *.xml, *.xml.dist, *.xsd, *.xsl, *.xslt
yaml: *.yaml, *.yml
zig: *.zig
```
(Each row is what `rg --type-list` of 14.1.1 prints for that name.) `-t`/`-T` with a name not in the table
-> `rg: unrecognized file type: foo`, exit 2. `--type-list` prints the
table and exits 0 (no pattern needed). `--type-add`/`--type-clear` stay
not treated.

### `Rg.cpp`

Flip the flags above to treated ids (new `RgOptionId` enumerators, `case`s in
`Run()`; a `-t`/`-T` name is checked there, before the search, so the error
exits 2 at once); bump the version to 1.2.0; `Help()` notes: the
type list is a subset (`--type-list` shows it); no global gitignore
(`core.excludesFile`) is read; `-L` has no effect.

## Tests

`tests/unit/components/BuiltinCommands.unittests/RgIgnoreTest.cpp` (new; add
it to `add_executable(BuiltinCommands.unittests ...)` in
`tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt`, after
`RgRegexTest.cpp`; the new sources `commands/rg/RgIgnore.cpp` and
`commands/rg/RgTypes.cpp` go in `src/components/BuiltinCommands/CMakeLists.txt`
after `commands/rg/RgRegex.cpp`). Plain `TEST(RgGitignoreTest, ...)` on `RgGitignore`:
`*.log` ignores `a/drop.log`; then `!keep.log` whitelists `a/keep.log`;
`build/` ignores the directory `build` and `x/build` but not the file
`x/build.txt`; `/top.txt` ignores `top.txt` only at the top (`a/top.txt`:
None); `a/**/f.txt` ignores `a/b/c/f.txt` and `a/f.txt`; `**/foo` matches
`foo` and `x/y/foo`; `abc/**` matches `abc/x/y`, not `abc`; `\#x` matches
`#x`; `# c` is a comment; trailing spaces dropped; case-fold Parse matches
`*.txt` against `B.TXT`.

`TEST_F(BuiltinCommandsTest, RgIgnore...)`, run from `/repo`, a tree written
by the test (as the verified run): `.git/` (empty directory),
`.gitignore` = `*.log\n!keep.log\nbuild/\n/top.txt\na/**/f.txt\n`, files
(content `hi\n`) `a/b/c/f.txt a/keep.log a/drop.log a/B.TXT build/o.txt
x/build/o.txt x/build.txt top.txt a/b/deep.md .hidden/h.txt`. Expected
(`rg --files`, verified, one per line):
- `RgIgnoreDefault`: `a/B.TXT a/b/deep.md a/keep.log x/build.txt`.
- `RgIgnoreOutsideRepository`: the same tree without `.git` -> also
  `a/b/c/f.txt a/drop.log build/o.txt top.txt x/build/o.txt` (no
  `.gitignore` outside a repository); with `--no-require-git` -> the
  default list again.
- `RgIgnoreFromSubdirectory`: working directory `/repo/a`: `b/deep.md`,
  `B.TXT`, `keep.log` -> in byte order `B.TXT\nb/deep.md\nkeep.log\n`
  (`B` sorts before `b`) -- the parent's `.gitignore` applies.
- `RgIgnoreDotFileWins`: add `a/.ignore` = `!drop.log` -> `a/drop.log`
  listed too; a `.gitignore` above the repository root (`/.gitignore` =
  `deep.md`) changes nothing.
- `RgIgnoreUnrestricted`: `-u` -> every non-hidden file (9 lines:
  `a/B.TXT a/b/c/f.txt a/b/deep.md a/drop.log a/keep.log build/o.txt
  top.txt x/build/o.txt x/build.txt`); `-uu` -> also `.gitignore` and
  `.hidden/h.txt` (first, in byte order); `--hidden` -> `.gitignore
  .hidden/h.txt` + the default list.
- `RgIgnoreGlobs`: `-g '*.txt'` -> `a/b/c/f.txt top.txt x/build.txt`
  (globs override `.gitignore`, but `build/` stays unwalked);
  `--iglob '*.txt'` -> `a/B.TXT` first, then the same; `-g '!*.md'` ->
  `a/B.TXT a/keep.log x/build.txt`; `-g '*.txt' -g '!top.txt'` ->
  `a/b/c/f.txt x/build.txt` (the last matching glob wins).
- `RgIgnoreHiddenWhitelisted`: `/repo/.ignore` = `!.hidden` -> `.hidden/h.txt`
  first, then the default list.
- `RgIgnoreTypes`: `-t txt -T md` -> `x/build.txt`; `-t foo` -> err
  `rg: unrecognized file type: foo\n`, 2; `-tmd` -> `a/b/deep.md`.
- `RgIgnoreMaxDepth`: `-d 1` -> nothing, exit 1; `-d 2` -> `a/B.TXT
  a/keep.log x/build.txt`.
- `RgIgnoreOperandsNotFiltered`: `rg --files top.txt build` -> `top.txt\nbuild/o.txt\n`.
- `RgIgnoreNothingSearched`: `rg -g '*.zzz' x` -> the "No files were
  searched" message, 2.
- `RgTypeList`: `--type-list` prints exactly the table, 0.
- `RgIgnoreFollowAccepted`: `-L --files` -> the default list, no
  "not treated" report.

Commands:

```
bash ./scripts/build_linux_on_linux.sh
bash ./scripts/test_linux.sh L U BuiltinCommands
./output/linux/BuiltinCommands.unittests --gtest_filter='*RgIgnore*:*RgGitignore*:*RgType*'
bash ./scripts/test_linux.sh L U
```

## Folded in: #70's open mediums (`RgRegex.cpp`, `RgRegexTest.cpp`)

Two small fixes to the Rust-syntax translator, a few dozen lines each:
- A quantifier with a 0 minimum on a zero-width assertion makes it
  optional, as in Rust's regex: `^*abc`, `\b?x`, `^{0,2}`, `$*`, `\A*x`
  accept input where the assertion does not hold (`^*abc` matches `xabc`;
  `\b?x` matches `ax`). Today `EmitQuantifier` (line ~164, simple `* ? +`)
  keeps the assertion, and `ParseCountedQuantifier` (~485) drops it only for
  an explicit `{0}`-style minimum reached through `boundaryRepeat`. A
  non-zero minimum (`+`, `{1,}`, `{2}`) still leaves the assertion itself.
  Tighten `RgRegexTest.cpp` ~207-209 (`^*`, `$*`, `\A*x` matching only the
  empty/`x` text) with cases that fail on the old reading: `^*abc` on
  `xabc` true, `\b?x` on `ax` true, `^{0,2}abc` on `xabc` true, and
  `^+abc` on `xabc` still false.
- A quantifier after a multi-byte UTF-8 literal, or after `\u{..}` /
  `\x{..}` naming a code point above 0x7f, repeats the whole code point,
  not its last byte: the literal path (~221, bytes >= 0x80 emitted raw one
  at a time) and `EmitCodePoint` (~637) must emit such a code point's bytes
  as one atom (grouped `(?:...)`). Tests: `Ã©+` read as `é+` matches
  `Ã©Ã©` fully (a match of 4 bytes); `\u{e9}{2}` matches
  `Ã©Ã©` and not `Ã©©`; `\u{e9}?x` on `x` true.

## Docs

`src/components/BuiltinCommands/CLAUDE.md`: the `rg` row (version 1.2.0; replace
its "no ignore rules yet (`search--rg-ignore`) ... reported as not treated"
sentence, and add the two regex fixes to its quantifier notes;
hidden/ignore files/globs/types treated; documented exceptions: the type
list is a subset, no global gitignore, `-L` without effect,
`--ignore-file`/`--type-add` not treated); a short "rg's filters" paragraph
naming `RgIgnore.h` and the decision order. Root `CLAUDE.md`: the rg row
mentions `.gitignore`.

## Acceptance

- [ ] Every expected list above matches, in byte order.
- [ ] Decision order exactly: globs, ignore files by kind, types, hidden;
      operands never filtered; skipped directories not descended.
- [ ] `.gitignore` only inside a repository (unless `--no-require-git`),
      up to its root; `.ignore`/`.rgignore` anywhere.
- [ ] gitignore pattern rules (`!`, anchoring, trailing `/`, `**`) as tested;
      no recursion per byte.
- [ ] Files read only through `context.IO()`.
- [ ] The two folded-in regex fixes have the tests above.
- [ ] Full unit suite passes; CLAUDE.md files updated.

## Out of scope

- `--ignore-file`, `--type-add`, `--type-clear`, global gitignore,
  `--one-file-system`, symbolic links.
- The search and output (`search--rg-search`) and the regex syntax
  (`search--rg-regex`), apart from the two mediums folded in above; #70's
  other open items.
