# Playbook

Phase: implementing
Pause: no

| # | Task | Status | Version | Depends | PR | Tries | Review | Notes |
|---|------|--------|---------|---------|----|-------|--------|-------|
| 1 | [base--fs-rename-times](tasks/base--fs-rename-times.md) | done | 0.5.1 | - | #43 | 2 | 0C 0H fixed, 1M 2L open | |
| 2 | [base--regex-syntax](tasks/base--regex-syntax.md) | done | 0.5.2 | - | #44 | 2 | 0C 3H fixed, 3M 3L open | nesting limit 250 (not 1000) for the Windows stack; Windows CI flaky in HaisosOS.unittests |
| 3 | [base--regex-match](tasks/base--regex-match.md) | in-progress | 0.5.3 | 2 | | | | |
| 53 | [base--rename-fix](tasks/base--rename-fix.md) | todo | | 1 | | | | review #43 follow-up; before 8 |
| 4 | [coreutils--sort](tasks/coreutils--sort.md) | todo | | - | | | | |
| 5 | [coreutils--sort-orders](tasks/coreutils--sort-orders.md) | todo | | 4 | | | | |
| 6 | [coreutils--rm-rmdir](tasks/coreutils--rm-rmdir.md) | todo | | - | | | | |
| 7 | [coreutils--cp](tasks/coreutils--cp.md) | todo | | 1, 6 | | | | |
| 8 | [coreutils--mv-touch](tasks/coreutils--mv-touch.md) | todo | | 1, 6, 7, 53 | | | | |
| 9 | [coreutils--names-env](tasks/coreutils--names-env.md) | todo | | - | | | | |
| 10 | [coreutils--chmod-paths](tasks/coreutils--chmod-paths.md) | todo | | - | | | | |
| 11 | [coreutils--printf-seq](tasks/coreutils--printf-seq.md) | todo | | 4 | | | | |
| 12 | [coreutils--date](tasks/coreutils--date.md) | todo | | 8, 4 | | | | |
| 13 | [coreutils--stat](tasks/coreutils--stat.md) | todo | | 12, 11, 4 | | | | |
| 14 | [coreutils--du-cmp](tasks/coreutils--du-cmp.md) | todo | | 12, 4 | | | | |
| 15 | [coreutils--test-program](tasks/coreutils--test-program.md) | todo | | 4 | | | | |
| 16 | [coreutils--uniq-cut](tasks/coreutils--uniq-cut.md) | todo | | 4 | | | | |
| 17 | [coreutils--head-tail](tasks/coreutils--head-tail.md) | todo | | 14, 4, 16 | | | | |
| 18 | [coreutils--tr-tee-nl](tasks/coreutils--tr-tee-nl.md) | todo | | 4, 3 | | | | |
| 19 | [search--grep-core](tasks/search--grep-core.md) | todo | | 3, 4, 16 | | | | |
| 20 | [search--grep-recursive](tasks/search--grep-recursive.md) | todo | | 19 | | | | |
| 21 | [search--find-tests](tasks/search--find-tests.md) | todo | | 3, 20, 1, 8, 4 | | | | |
| 22 | [search--find-actions](tasks/search--find-actions.md) | todo | | 21, 9, 11, 6, 12, 4 | | | | |
| 23 | [search--xargs](tasks/search--xargs.md) | todo | | 22, 9, 4, 6 | | | | |
| 24 | [search--sed-core](tasks/search--sed-core.md) | todo | | 3, 4 | | | | |
| 25 | [search--sed-advanced](tasks/search--sed-advanced.md) | todo | | 24, 1 | | | | |
| 26 | [search--rg-search](tasks/search--rg-search.md) | todo | | 20 | | | | |
| 27 | [search--rg-regex](tasks/search--rg-regex.md) | todo | | 26 | | | | |
| 28 | [search--rg-ignore](tasks/search--rg-ignore.md) | todo | | 26 | | | | |
| 29 | [diff--diff-core](tasks/diff--diff-core.md) | todo | | 4, 12 | | | | |
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

## Directions

## Questions
- (implement, 2026-10-08) base--fs-rename-times review (#43): the Windows rename emulation removes an empty target directory before MoveFileExW and loses it if the move fails (e.g. `/d` -> `/d/sub`). The reviewer suggests a follow-up task before coreutils--mv-touch (row 8) relies on it: refuse a new path under the old one before removing the target, and recreate it if MoveFileExW fails. Add such a task (depends on 1, before 8)? -> answered: yes -- added base--rename-fix (#53, after row 3, depends on 1; row 8 now depends on 53). It also takes the review's two low findings (ToFileTime overflow, the misplaced HaisosOSTest comment).

## Adjustments
- 2026-10-08 05:30 (implement) base--regex-match refreshed: checked against d2f11f2; added #44 review outcomes (nesting limit 250 for the Windows stack, add the missing Regex.h Semantics doc comments)
- 2026-10-08 05:48 (implement) user: continue without the host Windows build (WSL interop broken); Windows-only reds get one CI re-run, then merge with "Windows red" noted, fixed on the host in the final phase
