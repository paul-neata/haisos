# Playbook

Phase: implementing
Pause: no

| # | Task | Status | Version | Depends | PR | Tries | Review | Notes |
|---|------|--------|---------|---------|----|-------|--------|-------|
| 1 | [base--fs-rename-times](tasks/base--fs-rename-times.md) | done | 0.5.1 | - | #43 | 2 | 0C 0H fixed, 1M 2L open | |
| 2 | [base--regex-syntax](tasks/base--regex-syntax.md) | done | 0.5.2 | - | #44 | 2 | 0C 3H fixed, 3M 3L open | nesting limit 250 (not 1000) for the Windows stack; Windows CI flaky in HaisosOS.unittests |
| 3 | [base--regex-match](tasks/base--regex-match.md) | done | 0.5.3 | 2 | #45 | 1 | 1C 4H fixed, 3M 3L open | per-thread capture slots grow threads x groups: cap before grep/sed take user patterns |
| 53 | [base--rename-fix](tasks/base--rename-fix.md) | done | 0.5.4 | 1 | #46 | 1 | 0C 0H fixed, 0M 1L open | review #43 follow-up; before 8 |
| 4 | [coreutils--sort](tasks/coreutils--sort.md) | done | 0.5.5 | - | #47 | 1 | 0C 3H fixed, 1M 4L open | -d/-i precedence (M) left for coreutils--sort-orders |
| 5 | [coreutils--sort-orders](tasks/coreutils--sort-orders.md) | done | 0.5.6 | 4 | #48 | 1 | 0C 3H fixed, 1M 4L open | |
| 6 | [coreutils--rm-rmdir](tasks/coreutils--rm-rmdir.md) | done | 0.5.7 | - | #49 | 1 | 0C 1H fixed, 1M 3L open | rm -r follows directory symlinks on PHYSICAL (see Questions) |
| 7 | [coreutils--cp](tasks/coreutils--cp.md) | done | 0.5.8 | 1, 6 | #50 | 1 | 0C 2H fixed, 3M 4L open | 3 M fit coreutils--mv-touch |
| 8 | [coreutils--mv-touch](tasks/coreutils--mv-touch.md) | done | 0.5.17 | 1, 6, 7, 53 | #59 | 3 | 0C 5H fixed, 3M 1L open | first run cut by Ollama limit; finished in a fix round; Windows not checked |
| 9 | [coreutils--names-env](tasks/coreutils--names-env.md) | done | 0.5.9 | - | #51 | 1 | 0C 1H fixed, 2M 6L open | Windows green on CI re-run (HaisosOS.unittests flaky) |
| 10 | [coreutils--chmod-paths](tasks/coreutils--chmod-paths.md) | done | 0.5.10 | - | #52 | 1 | 0C 0H fixed, 1M 3L open | Windows green on CI re-run (HaisosOS.unittests flaky) |
| 11 | [coreutils--printf-seq](tasks/coreutils--printf-seq.md) | done | 0.5.11 | 4 | #53 | 1 | 0C 1H fixed, 2M 3L open | |
| 12 | [coreutils--date](tasks/coreutils--date.md) | done | 0.5.18 | 8, 4 | #60 | 1 | 0C 0H fixed, 6M 5L open | Windows red, not checked |
| 13 | [coreutils--stat](tasks/coreutils--stat.md) | done | 0.5.19 | 12, 11, 4 | #61 | 1 | 0C 1H fixed, 3M 2L open | Windows red, not checked |
| 14 | [coreutils--du-cmp](tasks/coreutils--du-cmp.md) | done | 0.5.20 | 12, 4 | #62 | 1 | 0C 1H fixed, 3M 6L open | Windows red, not checked |
| 15 | [coreutils--test-program](tasks/coreutils--test-program.md) | done | 0.5.12 | 4 | #54 | 1 | 0C 2H fixed, 1M 2L open | |
| 16 | [coreutils--uniq-cut](tasks/coreutils--uniq-cut.md) | done | 0.5.13 | 4 | #55 | 1 | 0C 0H fixed, 2M 6L open | |
| 17 | [coreutils--head-tail](tasks/coreutils--head-tail.md) | done | 0.5.21 | 14, 4, 16 | #63 | 1 | 0C 2H fixed, 3M 5L open | Windows red, not checked (Tail.cpp lacks `<cerrno>`?) |
| 18 | [coreutils--tr-tee-nl](tasks/coreutils--tr-tee-nl.md) | done | 0.5.14 | 4, 3 | #56 | 1 | 0C 3H fixed, 2M 2L open | Linux CI hung once in Build (runner); green on re-run |
| 19 | [search--grep-core](tasks/search--grep-core.md) | done | 0.5.15 | 3, 4, 16 | #57 | 1 | 0C 3H fixed, 1M 2L open | MSVC compile errors fixed in review |
| 20 | [search--grep-recursive](tasks/search--grep-recursive.md) | done | 0.5.16 | 19 | #58 | 1 | 0C 3H fixed, 2M 3L open | Windows green |
| 21 | [search--find-tests](tasks/search--find-tests.md) | done | 0.5.22 | 3, 20, 1, 8, 4 | #64 | 1 | 0C 3H 2M fixed, 0M 5L open | Windows red, not checked; 2843 lines, 1.8M Ollama tokens |
| 22 | [search--find-actions](tasks/search--find-actions.md) | done | 0.5.23 | 21, 9, 11, 6, 12, 4 | #65 | 1 | 0C 1H fixed, 1M 4L open | Windows red, not checked |
| 23 | [search--xargs](tasks/search--xargs.md) | done | 0.5.24 | 22, 9, 4, 6 | #66 | 1 | 1C 2H fixed, 4M 2L open | Windows red, not checked |
| 24 | [search--sed-core](tasks/search--sed-core.md) | done | 0.5.25 | 3, 4 | #67 | 1 | 1C 4H 3M 1L fixed, 4M 1L open | Windows red, not checked; plan wrong on `2,~2p` (GNU prints 2-4) |
| 25 | [search--sed-advanced](tasks/search--sed-advanced.md) | done | 0.5.26 | 24, 1 | #68 | 1 | 0C 2H fixed, 4M 3L open | Windows red, not checked |
| 26 | [search--rg-search](tasks/search--rg-search.md) | done | 0.5.27 | 20 | #69 | 1 | 0C 2H fixed, 0M 3L open | Windows red, not checked; grep -q fix (#58) included, grep 1.2.0 |
| 27 | [search--rg-regex](tasks/search--rg-regex.md) | done | 0.5.28 | 26 | #70 | 1 | 0C 1H fixed, 3M 4L open | Windows red, not checked; clean-room checked |
| 28 | [search--rg-ignore](tasks/search--rg-ignore.md) | done | 0.5.29 | 26 | #71 | 2 | 0C 2H fixed, 4M 3L open | Windows red, not checked; clean-room checked |
| 29 | [diff--diff-core](tasks/diff--diff-core.md) | in-review | 0.5.30 | 4, 12 | #72 | 4 | | runs 1-2 died on the 32000 output-token cap, 3 on Ollama 429 (account changed); finished in fix round 4 |
| 30 | [diff--diff-recursive](tasks/diff--diff-recursive.md) | todo | | 29, 20 | | | | |
| 31 | [diff--patch-core](tasks/diff--patch-core.md) | todo | | 1, 4, 29 | | | | |
| 32 | [diff--patch-fuzz-rej](tasks/diff--patch-fuzz-rej.md) | todo | | 31, 7 | | | | |
| 33 | [awk--lexer](tasks/awk--lexer.md) | todo | | 9, 4 | | | | |
| 34 | [awk--expressions](tasks/awk--expressions.md) | todo | | 33 | | | | |
| 35 | [awk--parser](tasks/awk--parser.md) | todo | | 34 | | | | |
| 36 | [awk--values](tasks/awk--values.md) | todo | | 33, 11 | | | | |
| 37 | [awk--interpreter](tasks/awk--interpreter.md) | todo | | 35, 36, 4 | | | | |
| 38 | [awk--records](tasks/awk--records.md) | todo | | 37, 3 | | | | |
| 39 | [awk--functions](tasks/awk--functions.md) | todo | | 38 | | | | |
| 40 | [awk--printf-math](tasks/awk--printf-math.md) | todo | | 39, 11 | | | | |
| 41 | [awk--io](tasks/awk--io.md) | todo | | 40, 9 | | | | |
| 42 | [tools--jq-parse](tasks/tools--jq-parse.md) | todo | | - | | | | |
| 43 | [tools--jq-json](tasks/tools--jq-json.md) | todo | | - | | | | |
| 44 | [tools--jq-eval](tasks/tools--jq-eval.md) | todo | | 42, 43 | | | | |
| 45 | [tools--jq-paths](tasks/tools--jq-paths.md) | todo | | 44 | | | | |
| 46 | [tools--jq-command](tasks/tools--jq-command.md) | todo | | 45, 4, 26 | | | | |
| 47 | [tools--jq-builtins](tasks/tools--jq-builtins.md) | todo | | 46 | | | | |
| 48 | [tools--jq-text](tasks/tools--jq-text.md) | todo | | 47, 3, 12 | | | | |
| 49 | [tools--tar-create-list](tasks/tools--tar-create-list.md) | todo | | 20, 12 | | | | |
| 50 | [tools--tar-extract](tasks/tools--tar-extract.md) | todo | | 49, 1 | | | | |
| 51 | [tools--processes](tasks/tools--processes.md) | todo | | 12 | | | | |
| 52 | [tools--pgrep-kill](tasks/tools--pgrep-kill.md) | todo | | 51, 3, 9 | | | | |
| 54 | [final--windows-fix](tasks/final--windows-fix.md) | todo | | all | | | | last; Windows fixed on the host and checked on CI, best effort |

## Directions
- (plan, 2026-10-08) For the rest of this develop, ignore the Windows checks of task PRs: do not wait on them, re-run them or fix them (no `windows.sh`, no Windows fix agent); a PR green on Linux is reviewed and merged, with "Windows not checked" in its Notes. Windows is built and tested only once, in the final phase, on the whole develop; then run final--windows-fix (#54) as the last task. -> done: from search--grep-recursive on, Windows checks of task PRs are ignored; Windows only in the final phase, final--windows-fix last.
- (user, 2026-10-09) **Clean room, above every other rule**: no code is copied from any other program, whatever its licence; all code is written from scratch, matching only documented or observed behaviour (root CLAUDE.md "Clean-room rule", goal.md Clarifications). Every plan re-check removes anything that ports or mirrors another program's source structure; every review treats code that looks copied as critical. -> done: rule added to the root CLAUDE.md, goal.md, memory.md; applied to every plan re-check and review from search--rg-ignore on.

## Questions
- (implement, 2026-10-09) Clean room, already merged: coreutils--test-program (#54) was planned as "a port of coreutils 9.4 src/test.c" with test.c's functions (posixtest, two_arguments, three_arguments, beyond, ...), and several helpers are described in comments as gnulib's (filevercmp, nstrftime, xstrtoumax, file_prefixlen). Add a task that audits the merged builtins for copied or ported code and rewrites from scratch whatever is (at least /bin/test's evaluator)? Also: the task prompt (scripts/develop/task_prompt.md) and the review skill should carry the rule too -- they change only on master.
- (implement, 2026-10-08) base--fs-rename-times review (#43): the Windows rename emulation removes an empty target directory before MoveFileExW and loses it if the move fails (e.g. `/d` -> `/d/sub`). The reviewer suggests a follow-up task before coreutils--mv-touch (row 8) relies on it: refuse a new path under the old one before removing the target, and recreate it if MoveFileExW fails. Add such a task (depends on 1, before 8)? -> answered: yes -- added base--rename-fix (#53, after row 3, depends on 1; row 8 now depends on 53). It also takes the review's two low findings (ToFileTime overflow, the misplaced HaisosOSTest comment).
- (implement, 2026-10-08) coreutils--rm-rmdir review (#49), medium: `rm -r` stats with Stat, which follows links, so on a PHYSICAL filesystem it descends into a symlinked directory and empties its target, even outside the tree (GNU removes only the link). DELETE already does this, but rm makes it reachable from any process, and cp/mv/find -delete would reuse RemoveOperand. The reviewer suggests giving IFileIO a no-follow status (or a link entry type) before those tasks, and until then documenting it in the rm row of the BuiltinCommands CLAUDE.md. Add such a task (before coreutils--mv-touch, row 8)? I continue with coreutils--cp meanwhile. -> answered: no task. Haisos does not support links, so `rm -r` following a symlinked directory (and deleting outside the tree) is accepted, consistent with DELETE and the agreed behaviour; cp/mv/find -delete may reuse RemoveOperand as is. coreutils--mv-touch (row 8) and what waits on it go ahead.
- (implement, 2026-10-08) HaisosOS.unittests is flaky on Windows CI: it failed on #44, #51 and #52 (on #52 killed after its 30 s limit, so a hang), none of which touch it, and passed on each re-run. Every third PR now needs a CI re-run. Add a task to find and fix the Windows hang (likely a stop/destruction timing race in HaisosOS)? With WSL interop broken on the host, it could only be verified on CI. -> answered: for this develop, no Windows checks per task (see Directions); Windows is built and tested once, in the final phase, and the last task, final--windows-fix (#54), tries to fix the Windows build and tests, the HaisosOS.unittests hang included. If it does not get there, the rest is left for a later PR.
- (implement, 2026-10-08) coreutils--mv-touch (PR #59): the Ollama account hit its monthly usage limit (HTTP 429, "add usage credits: https://ollama.com/settings"). The first run stopped half-way (its one commit is the leftovers task.sh committed: the BuiltinCopy refactor, BuiltinDate, Mv.cpp, Touch.cpp) yet built and passed, so it came back `ready`; the review (blocked, 5H 2M 2L open, commented on #59) found CMake, registration, tests and docs missing and a BuiltinDate zone bug, and its fix round got 429 at once. Runs in ~/.haisos-develop/runs/20261007T055748Z/coreutils--mv-touch/. No task can run until Ollama usage is restored (credits, a new month, or another provider/model in Task models). Then: a fix round on #59 with the reviewer's feedback (`task.sh fix`), or re-run the task from scratch? -> answered (user, 2026-10-08, in the implement session): Ollama credits reset, continue; the implement session runs the fix round on #59 with the review's feedback.

## Adjustments
- 2026-10-08 05:30 (implement) base--regex-match refreshed: checked against d2f11f2; added #44 review outcomes (nesting limit 250 for the Windows stack, add the missing Regex.h Semantics doc comments)
- 2026-10-08 05:48 (implement) user: continue without the host Windows build (WSL interop broken); Windows-only reds get one CI re-run, then merge with "Windows red" noted, fixed on the host in the final phase
- 2026-10-08 07:14 (implement) coreutils--sort re-checked against f1e1dc9: nothing to change
- 2026-10-08 07:54 (implement) coreutils--sort-orders refreshed against d947afa; folded in #47 review medium (-d wins over -i, error text '-dn'), with test SortDictionaryWinsOverNonprinting
- 2026-10-08 08:31 (implement) coreutils--rm-rmdir refreshed against 74a3f56: names the shared BuiltinText helpers (ArgMatch for --interactive/--preserve-root values), list ordering after pwd and before sort
- 2026-10-08 09:02 (implement) coreutils--cp refreshed against 57ade71: reuse RemoveOperand/RemoveFile (BuiltinRemove) and ArgMatch/GnuQuote (BuiltinText), full paths for FilesystemUtils.h and the list files
- 2026-10-08 09:43 (implement) coreutils--names-env (row 9) runs before coreutils--mv-touch (row 8): row 8 waits for the answer to the rm -r symlink Question
- 2026-10-08 09:44 (implement) coreutils--names-env refreshed against b7b1ee6: BuiltinCommands CMake, current builtin list and order, reuse of WriteFully/GnuQuote
- 2026-10-08 10:29 (implement) coreutils--chmod-paths (row 10) also runs before coreutils--mv-touch (row 8), still waiting on the rm -r symlink Question
- 2026-10-08 10:29 (implement) coreutils--chmod-paths refreshed against c7d53a1: BuiltinCommands CMake, list slots, current 17-name expectation, reuse of GnuQuote/WriteFully
- 2026-10-08 11:08 (implement) coreutils--printf-seq refreshed against 3ea5b35 (BuiltinCommands CMake, list slots, GnuQuote already present, BeginBuiltin overload for seq); runs before coreutils--mv-touch, still waiting on the symlink Question
- 2026-10-08 11:52 (implement) coreutils--test-program refreshed against 9d69b73 (BuiltinCommands CMake, list order, true/false as the Run model); runs while mv-touch (and 12-14, which depend on it) wait on the symlink Question
- 2026-10-08 13:09 (implement) coreutils--uniq-cut refreshed against 90728a3 (BuiltinCommands CMake, list slots, current 25-name expectation)
- 2026-10-08 13:43 (implement) coreutils--tr-tee-nl refreshed against 515f39d (Regex notes pointer, BuiltinText helpers, CMake, list order); runs before coreutils--head-tail (row 17), which waits on du-cmp via mv-touch
- 2026-10-08 16:14 (implement) search--grep-core refreshed against 668e09a (registration slots, CMake, test list; Out of scope: Pike VM capture-slot memory from #45)
- 2026-10-08 17:18 (implement) search--grep-recursive refreshed against aa9ea93 (real GrepOneInput/GrepFileResult, CMake, test fixture); folded in #57 medium: -P -w gets the boundary check grep already uses for -G/-E/-F -w (Regex has no lookarounds, not added here), test GrepPerlWordWrap
- 2026-10-08 18:35 (implement) search--grep-recursive: its first container run (17:19 UTC) was interrupted with the previous implement session; restarted, version 0.5.16 kept.
- 2026-10-08 18:35 (implement) Direction acted on: task PRs' Windows checks are ignored for the rest of the develop.
- 2026-10-08 (implement) coreutils--mv-touch refreshed against 2706b1a (real BuiltinCopy/BuiltinPrompt/BuiltinRemove names, CMake and list slots); folded in #50's three mediums (cp helpers moved into BuiltinCopy and reused by mv; backup mode resolved once at the end with `$VERSION_CONTROL`, -S included; -n beats a later --update=WORD) and the answered rm -r symlink decision
- 2026-10-08 (implement) coreutils--mv-touch back in review: Ollama credits reset by the user; fix round on #59 with the review's round-1 feedback
- 2026-10-08 (implement) coreutils--date refreshed against 7b806fb (BuiltinDate names from #59, CMake/test slots, BuiltinDateTest exists); folded in #59 medium: previousWasTime only when no zone attached (`2024-01-02T03:04Z +1 hour`)
- 2026-10-09 (implement) coreutils--stat refreshed against 96ca945 (Stat(path, FileStatus&) call, CopyStatMissingReason, SetTimes-based precision test, list/CMake slots; warns that ParsePrintfSpec skips h/L while stat's %h/%Ld are directives)
- 2026-10-09 (implement) coreutils--du-cmp refreshed against f561090 (FnMatch from BuiltinFnmatch.h for --exclude, InputOpenFailure/FileStatus fields, both ls HumanSize uses move to FormatHumanSize, list/CMake/test slots)
- 2026-10-09 (implement) coreutils--head-tail refreshed against 6221bce (ArgMatch/BuiltinLineReader/WriteFully signatures, hidden digit options as uniq's, list/CMake/test slots, init-template test filter `haisos`)
- 2026-10-09 (implement) search--find-tests refreshed against a24afec (reuse FnMatch/ParseDateString/Regex; -size keeps its own grammar; Regex already linked; list/CMake/test slots; IFileIO::kStdIn)
- 2026-10-09 (implement) search--find-actions refreshed against 04e3300 (aligned with #64's find code: LookupPrimary/FindActionPrimaries, -prune, slots, shared FindTestTree.h); folded in #64 lows (firstNonOption removed; -type/-xtype D refused as GNU 4.9) and #53's ParsePrintfSpec `q` (-printf scans its own directives)
- 2026-10-09 (implement) search--find-actions and search--xargs: the planned helper EmptyInputDescriptor renamed OpenEmptyInput -- a class of that name already exists in the same namespace (Console/ConsoleDescriptors.h)
- 2026-10-09 (implement) search--xargs refreshed against a092a28 (OpenEmptyInput in BuiltinRunProgram.h, slots); folded in #65 medium: a null OpenEmptyInput fails the run instead of handing over the caller's stdin, in xargs and find -ok
- 2026-10-09 (implement) search--xargs: its first container run (06:29 UTC) was interrupted with the previous implement session; restarted, version 0.5.24 kept.
- 2026-10-09 (implement) user: Windows stays ignored on task PRs and CI for the rest of the develop; fixed at the end (final--windows-fix) or in a separate PR
- 2026-10-09 (implement) search--sed-core refreshed against f6f5bec (Regex already linked; list/CMake/test slots after rmdir/rm, before seq)
- 2026-10-09 (implement) search--sed-advanced refreshed against c68238e (sed-core's real names, CMake/test slots, sed 1.1.0); folded in #67's follow-ups: s///w and w FILE to end of line, SedIsStoppedPromptly on a held-open stdin pipe, a Regex multiline . / [^...] test, the /[/p error row
- 2026-10-09 (implement) search--rg-search refreshed against 82119e0 (GrepContext::SetPrinters, GrepFile.h/GrepSettings.h, rg keeps its own read loop, list/CMake/test slots); folded in #58 medium as a separate item: grep -q stops after a recursive match (GrepQuietStopsAfterRecursiveMatch)
- 2026-10-09 (implement) search--rg-regex refreshed against b089568 (rg-search's real names, CMake/test slots, Regex Perl now takes \cX/\e/\1); folded in #69's three lows (--no-messages, -f open failures via a shared OpenFailureText, unreadable directory reported, exit 2)
- 2026-10-09 (implement) search--rg-ignore refreshed against d6c7924 (rg 1.2.0, walk lambda in RgSearch(), CMake/test slots); clean-room: ignore-crate mentions reworded as observed behaviour, rule added to Context; folded in #70's two regex mediums (0-minimum quantifier on an assertion, quantified multi-byte code point)
- 2026-10-09 (implement) diff--diff-core re-planned clean-room against e62c40e (Opus): GNU's internal steps removed; Myers' published linear-space algorithm plus output rules verified on the host's GNU diff 3.10 (byte-identical with diff -d in ~2000 cases, with GNU's default in 946/950 real-source cases); documented exception: hunks may be placed differently (same change count) around repeated lines with mixed edits, and on huge very different files GNU's default gives up minimality; -d accepted, no effect; three wrong format statements corrected (-E -Z, -E after \b/\r, octal-quoted names, -t after \r)
