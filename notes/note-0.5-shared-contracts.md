# Develop 0.5: the shared contracts between task plans

The task plans of develop 0.5 ("Various unix commands used by coding agents")
were written in parallel, so the pieces several commands share were named up
front. The task that introduces each piece defines it exactly in its plan
(`develop-plan/tasks/<id>.md`, the authority); every other plan uses it by
these names, as if already on develop.

## The shared pieces

| Piece | Introduced by | Used by |
|-------|---------------|---------|
| `IFileSystem`/`IFileIO` `Rename(oldPath, newPath)` and `SetTimes(path, optional<FileDateTime> access, optional<FileDateTime> modification)`; results `kFileSystemError` (-1) and `kFileSystemCrossDevice` (-2, a rename across filesystems -- never errno) in `interfaces/IFileSystemService.h` | base--fs-rename-times | cp, mv (copy-then-remove on `kFileSystemCrossDevice`), touch, sed -i, patch, tar |
| `Regex` component (`src/components/Regex/Regex.h`, target `Regex`, already linked into BuiltinCommands): `RegexSyntax {Basic, Extended, Perl}`, `RegexOptions {syntax, ignoreCase, multiline}`, `Regex::Compile(pattern, options, error)` -> `shared_ptr<const Regex>`, `Search(text, start, RegexMatch&, flags)` with `kRegexNotBol`/`kRegexNotEol`, `GroupCount()`, `GroupNames()`, `Options()`; `RegexMatch::groups` as (begin, end) pairs, (-1, -1) unmatched. Basic/Extended: glibc's rules, leftmost-longest, `.` matches '\n'. Perl: PCRE2's `.` and `$`, leftmost-first. Callers translate their own escapes first (the component's CLAUDE.md, "Notes for callers") | base--regex-syntax, base--regex-match | nl, grep, find, sed, rg, awk, jq, pgrep |
| `BuiltinText.h`: `GnuQuote`, `ArgChoice`/`ArgMatch`, `OpenInputOperand`/`InputOpenFailure`, `BuiltinLineReader`/`LineReadResult`, `WriteFully` | coreutils--sort | nearly every text-reading builtin |
| `BuiltinCompare.h`: `CompareNumeric`; later `CompareGeneralNumeric`, `CompareHumanNumeric`, `CompareMonth`, `CompareVersion` | coreutils--sort, coreutils--sort-orders | sort, (ls -v later) |
| `BuiltinOption::hidden` (parsed, not shown in --help: `head -5`, `grep -5`) | coreutils--uniq-cut | head, tail, grep, diff |
| `BuiltinPrompt` (`Ask`, GNU's yes/no from stdin); `BuiltinRemove.h` (`RemoveOptions`, `RemoveOperand`) | coreutils--rm-rmdir | cp, mv, find -ok, xargs -p |
| `BuiltinCopy.h` (`CopyOptions`, `CopyPath`, `BackupPathFor`, `ParseBackupControl`, `BackupMode`) | coreutils--cp | mv, patch -b, sed -i suffix |
| `BuiltinDate.h`: `ParseDateString`, `ParseTouchStamp` (+ `bool utc` overloads), `LocalTimeOf`, `SecondsFromLocalTime`, `SecondsFromUtc`; `FormatDateTime(format, FileDateTime, utc)` | coreutils--mv-touch; FormatDateTime by coreutils--date | touch, date, stat, du, find, diff, tar, ps |
| `BuiltinRunProgram.h`: `FindProgramInPath(context, name, const IEnvironment* = nullptr)`, `RunProgramOptions` (stdIn/stdOut/stdErr, workingDirectory, environment), `RunProgramAndWait(context, path, args, options, bool* started)` (flushes stdout first, starts through `ICurrentProcess::OS()`), `kBuiltinDefaultSearchPath`, `SearchPathEntries`; `stopAtFirstOperand` for `ParseBuiltinArgs`/`BeginBuiltin`; `EmptyInputDescriptor` (search--find-actions); `StartProgram` and `WaitForProgram`, with `RunProgramAndWait` rebuilt on them (awk--io) | coreutils--names-env | env, find -exec, xargs, awk |
| `BuiltinPrintf.h`: `PrintfSpec`, `ParsePrintfSpec`, `FormatPrintfSigned/Unsigned/Float/String`, `AppendPrintfEscape` | coreutils--printf-seq | printf, seq, stat, find -printf, awk |
| `BuiltinSize.h`: `ParseSizeWithSuffix`, `FormatHumanSize` (ls's `HumanSize` moves here) | coreutils--du-cmp | du, head, tail, ls, find -size |
| `BuiltinTestExpression.h`: `TestDialect {Dash, Gnu}`, `EvaluateTestExpression` (hsh's evaluator moved) | coreutils--test-program | hsh, test, [ |
| `BuiltinFnmatch.h`: `FnMatch(pattern, text, flags)`, `kFnmPathname = 1`, `kFnmNoEscape = 2`, `kFnmPeriod = 4`, `kFnmLeadingDir = 8`, `kFnmCaseFold = 16` (glibc fnmatch) | search--grep-recursive | grep, find, rg, diff -x, tar --exclude |
| `commands/grep/GrepMatcher.h`, `GrepContext.h` | search--grep-core, search--grep-recursive | rg |
| `BuiltinHelp::referenceUrl` (a reference not on man7.org) | search--rg-search | rg, jq |
| `IProcess::Args()`, `IProcess::StartTime()`; `BuiltinProcessList.h` (`SnapshotProcesses`) | tools--processes | ps, pgrep, pkill, kill |

## Rules every builtin task restates

- `ICurrentProcess` is the only door out of a process: files through `IO()`,
  processes and pipes through `OS()`/`IO().CreatePipe`; nothing holds an
  `IFileSystem` or `IHaisosOS`.
- Classes implementing `interfaces/` have private constructors and a static
  `Create()` returning `shared_ptr`.
- GNU's output byte for byte (C locale), every option of the real command in
  `Options()` (untreated ones reported), `--help` from `BuiltinHelpText`,
  `--version`, `man` page; registered in `CreateStandardBuiltinCommands()`
  (which also puts it in `haisos --init`); its name added to the exact list
  of `ListsEveryBuiltinSortedWithAVersion`; rows in both CLAUDE.md tables.
- Portable C++17 (Linux, Windows/MSVC, WASM): no POSIX headers, no `<regex>`.
- Stops promptly on `TriggerStop()` (143); a broken pipe exits 141.
- Test filter for `test_linux.sh` is `BuiltinCommands` (it matches the test
  executable's name), plus a direct `--gtest_filter` run.
