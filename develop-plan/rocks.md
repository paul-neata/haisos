# Big rocks

## base -- Foundations: filesystem operations and the regex engine
What the commands stand on. `IFileSystem` and `IFileIO` gain `Rename` (what
`mv`, `sed -i` and `patch` need) and `SetTimes` (`touch`, `cp -p`, `tar -x`),
implemented by every filesystem (physical on Linux and Windows, the Windows
full filesystem, in-memory, read-only, sub, mounted/composed, device) and by
`ProcessFileIO`. And a new `Regex` component (`src/components/Regex/`),
portable C++17 without `<regex>`: GNU BRE and ERE with GNU's extensions,
leftmost-longest, and a Perl subset, leftmost-first -- shared by grep, sed,
find, awk, rg and jq.
Touches: `interfaces/`, Filesystem, HaisosOS (`ProcessFileIO`), test mocks, new Regex component.
Tasks: base--fs-rename-times, base--regex-syntax, base--regex-match, base--rename-fix

## coreutils -- File commands, small utilities, text filters
The everyday commands: `cp mv rm rmdir touch chmod`; `basename dirname
realpath which env true false sleep printf seq date stat du cmp test [`;
`head tail sort uniq cut tr tee nl`. GNU coreutils' output, options,
messages and exit codes. Shared pieces written once here: the `printf`
format engine (reused by `seq -f`, `find -printf`, awk), the date parser
(`touch -d`, `date -d`), running a program found in `PATH` (`env`, later
`xargs`, `find -exec`, awk's `system`), and `test`/`[` sharing hsh's evaluator.
Touches: BuiltinCommands (and hsh, for the shared test evaluator).
Tasks: coreutils--sort, coreutils--sort-orders, coreutils--rm-rmdir, coreutils--cp, coreutils--mv-touch, coreutils--names-env, coreutils--chmod-paths, coreutils--printf-seq, coreutils--date, coreutils--stat, coreutils--du-cmp, coreutils--test-program, coreutils--uniq-cut, coreutils--head-tail, coreutils--tr-tee-nl

## search -- grep, find, xargs, sed, rg
How agents find and change text: `grep`/`egrep`/`fgrep` (recursive,
context, `--include`, `-o`, `-P`), `find` (the expression language,
`-exec`, `-print0`, `-delete`, `-printf`), `xargs`, `sed` (every command,
`-i`, `-n`, `-E`), and `rg` with ripgrep's own output format and
`.gitignore` handling.
Touches: BuiltinCommands, Regex.
Tasks: search--grep-core, search--grep-recursive, search--find-tests, search--find-actions, search--xargs, search--sed-core, search--sed-advanced, search--rg-search, search--rg-regex, search--rg-ignore

## diff -- diff and patch
Comparing and applying edits: `diff` (Myers; normal, `-u`, `-c`, `-q`,
`-r`, `-N`) and `patch` (unified, context and normal diffs; `-p`, `-R`,
`--dry-run`, offsets, fuzz, `.rej` files), GNU diffutils' and GNU patch's
output.
Touches: BuiltinCommands.
Tasks: diff--diff-core, diff--diff-recursive, diff--patch-core, diff--patch-fuzz-rej

## awk -- POSIX awk
A POSIX awk in `commands/awk/`, with its own `CLAUDE.md` as hsh has:
lexer, parser, interpreter (fields, patterns, ranges, arrays, control
flow), built-in and user functions with `printf`, and I/O (`getline`,
redirections, pipes, `system`), matching gawk `--posix`.
Touches: BuiltinCommands, Regex.
Tasks: awk--lexer, awk--expressions, awk--parser, awk--values, awk--interpreter, awk--records, awk--functions, awk--printf-math, awk--io

## tools -- jq, tar, processes
A `jq` subset on nlohmann::json (jq 1.7's output byte for byte), `tar` for
ustar archives (create, list, extract; no compression), and `ps`/`pgrep`/
`pkill`/`kill` over Haisos's own processes (`IHaisosOS::GetRunningProcesses()`
through `ICurrentProcess::OS()`).
Touches: BuiltinCommands, Regex (jq's `test`/`match`).
Tasks: tools--jq-parse, tools--jq-json, tools--jq-eval, tools--jq-paths, tools--jq-command, tools--jq-builtins, tools--jq-text, tools--tar-create-list, tools--tar-extract, tools--processes, tools--pgrep-kill
