# Playbook

Phase: implementing
Pause: no

| # | Task | Status | Version | Depends | PR | Tries | Review | Notes |
|---|------|--------|---------|---------|----|-------|--------|-------|
| 1 | [links--follow-anywhere](tasks/links--follow-anywhere.md) | obsolete | | - | | | | implemented directly on develop in 76079bd |
| 2 | [fd--descriptor-objects](tasks/fd--descriptor-objects.md) | done | 0.4.1 | - | #19 | 1 | 1C fixed, 1M 2L open | Windows min-macro fixed by the reviewer |
| 3 | [fd--process-table](tasks/fd--process-table.md) | in-review | 0.4.2 | 2 | #20 | 1 | | |
| 4 | [streams--console-and-start](tasks/streams--console-and-start.md) | todo | | 3 | | | | |
| 5 | [streams--runtime-streams](tasks/streams--runtime-streams.md) | todo | | 4 | | | | |
| 6 | [streams--exit-codes](tasks/streams--exit-codes.md) | todo | | 5 | | | | |
| 7 | [pipes--pipe-service](tasks/pipes--pipe-service.md) | todo | | 6 | | | | |
| 8 | [pipes--broken-pipe](tasks/pipes--broken-pipe.md) | todo | | 7 | | | | |
| 9 | [builtins--directories](tasks/builtins--directories.md) | todo | | 4 | | | | |
| 10 | [builtins--unicode](tasks/builtins--unicode.md) | todo | | - | | | | |
| 11 | [builtins--wc](tasks/builtins--wc.md) | todo | | 9, 10 | | | | |
| 12 | [builtins--man](tasks/builtins--man.md) | todo | | 6, 9, 11 | | | | |
| 13 | [hsh--lexer](tasks/hsh--lexer.md) | todo | | 9 | | | | |
| 14 | [hsh--parser](tasks/hsh--parser.md) | todo | | 13 | | | | |
| 15 | [hsh--arith-glob](tasks/hsh--arith-glob.md) | todo | | 13 | | | | |
| 16 | [hsh--expansion](tasks/hsh--expansion.md) | todo | | 14, 15 | | | | |
| 17 | [hsh--executor](tasks/hsh--executor.md) | todo | | 8, 16 | | | | |
| 18 | [hsh--redirections](tasks/hsh--redirections.md) | todo | | 11, 17 | | | | |
| 19 | [hsh--pipelines](tasks/hsh--pipelines.md) | todo | | 18 | | | | |
| 20 | [hsh--shell-builtins](tasks/hsh--shell-builtins.md) | todo | | 19 | | | | |
| 21 | [hsh--control-flow](tasks/hsh--control-flow.md) | todo | | 20 | | | | |
| 22 | [hsh--interactive](tasks/hsh--interactive.md) | todo | | 12, 21 | | | | |

## Directions

## Questions

## Adjustments
- 2026-10-06 05:57 (implement) fd--process-table refreshed: checked-against line fixed to 7b72167; Tests note to drop the stale 'IFileIO keeps int fds' comment (PR #19 finding)
