# Playbook

Phase: implementing
Pause: no

| # | Task | Status | Version | Depends | PR | Tries | Review | Notes |
|---|------|--------|---------|---------|----|-------|--------|-------|
| 1 | [links--follow-anywhere](tasks/links--follow-anywhere.md) | obsolete | | - | | | | implemented directly on develop in 76079bd |
| 2 | [fd--descriptor-objects](tasks/fd--descriptor-objects.md) | done | 0.4.1 | - | #19 | 1 | 1C fixed, 1M 2L open | Windows min-macro fixed by the reviewer |
| 3 | [fd--process-table](tasks/fd--process-table.md) | done | 0.4.2 | 2 | #20 | 1 | 1H fixed, 1M 1L open | reviewer fixed a racy test |
| 4 | [streams--console-and-start](tasks/streams--console-and-start.md) | done | 0.4.3 | 3 | #21 | 1 | 1M 4L open | |
| 5 | [streams--runtime-streams](tasks/streams--runtime-streams.md) | done | 0.4.4 | 4 | #22 | 1 | 1M 4L open | |
| 6 | [streams--exit-codes](tasks/streams--exit-codes.md) | done | 0.4.5 | 5 | #23 | 1 | 1C fixed, 2M 3L open | reviewer fixed an unprotected Lua __tostring (host abort) |
| 7 | [pipes--pipe-service](tasks/pipes--pipe-service.md) | done | 0.4.6 | 6 | #24 | 1 | 2M 2L open | Windows test time fixed on the host |
| 8 | [pipes--broken-pipe](tasks/pipes--broken-pipe.md) | done | 0.4.7 | 7 | #25 | 1 | 1M 2L open | |
| 9 | [builtins--directories](tasks/builtins--directories.md) | done | 0.4.8 | 4 | #26 | 1 | 3L open | |
| 10 | [builtins--unicode](tasks/builtins--unicode.md) | done | 0.4.9 | - | #27 | 1 | 4L open | Windows red from the 30 s HaisosOS limit; green on re-run |
| 11 | [builtins--wc](tasks/builtins--wc.md) | done | 0.4.10 | 9, 10 | #28 | 1 | 1H fixed, 1M 4L open | reviewer added a chunk-split test |
| 12 | [builtins--man](tasks/builtins--man.md) | done | 0.4.11 | 6, 9, 11 | #29 | 1 | 3L open | |
| 13 | [hsh--lexer](tasks/hsh--lexer.md) | done | 0.4.12 | 9 | #30 | 1 | 2H fixed, 3M 2L open | 3M dash differences folded into later hsh plans |
| 14 | [hsh--parser](tasks/hsh--parser.md) | done | 0.4.13 | 13 | #31 | 1 | 2H fixed, 2M 3L open | |
| 15 | [hsh--arith-glob](tasks/hsh--arith-glob.md) | done | 0.4.14 | 13 | #32 | 1 | 1C fixed, 2M 2L open | reviewer fixed a Windows-only hang (MSVC strtoimax on "0x") |
| 16 | [hsh--expansion](tasks/hsh--expansion.md) | done | 0.4.16 | 14, 15 | #34 | 1 | 2H fixed, 1M open | reviewer checked disputed cases against dash 0.5.12 |
| 17 | [hsh--executor](tasks/hsh--executor.md) | done | 0.4.17 | 8, 16 | #35 | 1 | 1M 3L open | |
| 18 | [hsh--redirections](tasks/hsh--redirections.md) | done | 0.4.18 | 11, 17 | #36 | 1 | 1H fixed, 5L open | |
| 19 | [hsh--pipelines](tasks/hsh--pipelines.md) | todo | | 18 | | | | |
| 20 | [hsh--shell-builtins](tasks/hsh--shell-builtins.md) | todo | | 19 | | | | |
| 21 | [hsh--control-flow](tasks/hsh--control-flow.md) | todo | | 20 | | | | |
| 22 | [hsh--interactive](tasks/hsh--interactive.md) | todo | | 12, 21 | | | | |
| 23 | [streams--coroutine-latch](tasks/streams--coroutine-latch.md) | done | 0.4.15 | 6, 8 | #33 | 1 | 1C 2H 1M fixed, 1M 1L open | follow-up to #23/#25 findings; reviewer fixed coroutine.wrap argument handling |

## Directions
- (plan, 2026-10-06) Run streams--coroutine-latch (#23) next, before hsh--expansion. -> done: started it next, as 0.4.15

## Questions
- (implement, 2026-10-06) streams--exit-codes (#23) left a medium: `exit()` inside a Lua coroutine re-arms the stop hook only on the coroutine's thread, so after `coroutine.resume` catches it the script runs up to ~1000 more instructions, tool calls included (LuaProcess.cpp:614; fix: also re-arm on `self->m_luaState` in LuaExitTrampoline, test `coroutine.resume(coroutine.create(function() exit(2) end)) print('after')` prints nothing, exits 2). Plus no test for the LLM-round-cap failure (Agent.cpp:507). Add a small follow-up task now, or leave both for the final-review fixes? Until answered: left for the final review. Update after pipes--broken-pipe (#25): the same gap now also hits `print` in a coroutine (LuaProcess.cpp:619, LuaPrintTrampoline) -- one fix covers both: re-arm on `self->m_luaState` in both trampolines, plus a coroutine test for each. -> answered: a small follow-up task now, streams--coroutine-latch (#23), run next. It also arms the hook on resumers of nested coroutines (wrapped coroutine.resume/wrap) and adds the round-cap test.

- (implement, 2026-10-06) Windows: HaisosOS.unittests.exe takes ~29.5 s on Windows against CI's 30 s per-executable timeout -- the existing HaisosOSTest.cpp alone is ~29.4 s (127.0.0.1:9999 takes ~2 s to refuse on Windows; HaisosOSTest still uses it). Every later task adding HaisosOS tests risks a red Windows check. Options: raise the timeout (scripts/ or .github/, so the plan session or master), switch HaisosOSTest to 0.0.0.0:9999 as #24 did for its own tests (a small follow-up task), or split the executable. Until answered: tasks go on; a Windows timeout is fixed on the host as for #24. Update after builtins--unicode (#27): it hit again on a PR that does not touch HaisosOS (HaisosOS.unittests ran ~33 s and was killed); a re-run passed -- so the limit now makes Windows CI flaky for every task. -> answered: raise the timeout only (scripts/internal/test.js getTimeout, unit tests 30 s). The plan session does this on develop outside plan mode, not a task. HaisosOSTest keeps 127.0.0.1:9999. Until it lands, keep fixing Windows timeouts on the host or re-running.

## Adjustments
- 2026-10-06 05:57 (implement) fd--process-table refreshed: checked-against line fixed to 7b72167; Tests note to drop the stale 'IFileIO keeps int fds' comment (PR #19 finding)
- 2026-10-06 06:28 (implement) streams--console-and-start refreshed: checked against 9492a45, no content changes needed
- 2026-10-06 07:01 (implement) streams--runtime-streams refreshed: checked against f2cc5d5; Context notes what #19-#21 provide, Agent.cpp line numbers fixed; preliminary MockFileDescriptor WriteCalls fix (PR #21 finding); Tests: wait for the process before reading a mock's writes
- 2026-10-06 07:36 (implement) streams--exit-codes refreshed: checked against 04a0a36; agent_*.js test step now fails on `result.status !== 0 || result.signal` (PR #22 finding)
- 2026-10-06 08:27 (implement) pipes--pipe-service refreshed: checked against 9c15ee7; AReaderSeesEndOfFileWhenTheWritersProgramEnds waits for the writer before ExitCode()
- 2026-10-06 10:35 (implement) pipes--broken-pipe refreshed: checked against faf4c9e; new pipe/builtin tests wait for the process before reading its exit code or output
- 2026-10-06 10:59 (implement) builtins--directories refreshed: checked against 51f49c3; RunCaptured reads ExitCode() (ExitStatus() is gone since #23)
- 2026-10-06 11:23 (implement) builtins--unicode refreshed: checked against ff1dabd, no content changes needed
- 2026-10-06 13:29 (implement) builtins--wc refreshed: checked against 7374f6d, no content changes needed
- 2026-10-06 14:20 (implement) builtins--man refreshed: checked against 2e744cf; haisos test reuses the existing non-throwing spawnSync pattern from streams--exit-codes
- 2026-10-06 14:39 (implement) hsh--lexer refreshed: checked against 1209878, no content changes needed
- 2026-10-06 15:22 (implement) hsh--parser refreshed: checked against a8381c6; preliminary fixes from #30's review: Lexer move ops / unused ctor removed, heredoc \<newline> joining before an unquoted delimiter (dash), with tests
- 2026-10-06 16:07 (implement) hsh--arith-glob refreshed: checked against 4f49203, no content changes needed
- 2026-10-06 16:59 (implement) streams--coroutine-latch run before hsh--expansion, per the plan direction; its plan is current (checked against 7e857e9). The 5 dash follow-ups from #30-#32 still go into hsh--expansion's re-check
- 2026-10-06 17:31 (implement) hsh--expansion refreshed: checked against 608b6cc; preliminary dash fixes folded in -- pattern-operand `'` quoting (#30), the missing 'Source text'/'AST dump' docs (#31), arithmetic assignment lookup order (#32); size ~1090. Two more (#31 heredoc odd-backslash join, #32 glob bracket never spans '/') moved to hsh--redirections' re-check to keep this task under ~1100 lines
- 2026-10-06 19:10 (implement) hsh--executor refreshed: checked against 7fd962a, interfaces unchanged; preliminary fix from #34 added to the plan ("$@$@" with no positional parameters gives one empty field). The re-check agent had instead edited and built src/tests on the host; those edits were reverted, nothing committed
- 2026-10-06 19:45 (implement) hsh--redirections refreshed: checked against 64378f3, interfaces unchanged; preliminary dash fixes moved here from hsh--expansion: heredoc join only on an odd number of trailing backslashes (#31), glob brackets never span '/' (#32); fd-number parse checks the whole string; size ~770
