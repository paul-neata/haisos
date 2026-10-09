# Memory

- **CLEAN ROOM, above all (user, 2026-10-09):** no code copied from any program, whatever its licence; all written from scratch, behaviour matched only from docs/specs/papers/real output. Every plan re-check prompt and every review prompt must say so: strip any "port of X" / mirrored function structure from plans; copied-looking code is critical in reviews.

- The 2d overlap check matches the bare `CLAUDE.md` in nearly every plan; when only the root CLAUDE.md overlaps, look at its diff -- table rows and the /claude-docker model line are no reason to re-check a plan.
- Memory (user, 2026-10-09: Claude Code runs out of memory on this PC; WSL 7.6 GiB, 2 GiB swap): at every task boundary check `free -h` and clean leftovers (task.sh/wait_ci.sh processes, haisos-develop-* containers). Peak use is the container build (8 cc1plus x ~300 MB) plus two Claude sessions (~470 MB each).
- 2026-10-08: WSL interop broken on the host (every .exe: "Exec format error"). Plan Direction: ignore task PRs' Windows checks (no waiting, re-runs or fixes); a PR green on Linux is reviewed and merged, Notes "Windows not checked". Windows built and tested once in the final phase; final--windows-fix (#54) runs last.
- HaisosOS.unittests is flaky on Windows CI (#44, #51, #52 -- on #52 a 30 s timeout, a hang); a fix task is asked in Questions.
- Windows' 1 MB stack: recursive parsers need low depth limits (Regex nesting 250).
- Interim agent notifications ("may be interim") carry a token count: record with it at once and correct the record at the next publish -- don't wait.
- glm-5.3 habit: GCC-only C++ (positional aggregate inits that narrow, a local named `stdin`) -- MSVC-only compile errors show up on Windows CI; the reviewer can fix them portably and CI verifies.
- On this host plain `grep` is ugrep; GNU grep 3.11 is /usr/bin/grep (reference for reviews).
- A run cut short (here by Ollama's 429) still ends `ready` when the leftovers build and pass: before the review, compare the PR's file list with the plan (tests, CMake, BuiltinCommandList.h, docs).
- When a task touching date/BuiltinDate (coreutils--stat, du, ls -l time styles) is re-checked: #60's mediums are candidates to fold in -- date --help lists no +FORMAT conversions, set-operand docs say ParseDateString but code uses ParseTouchStamp, FormatDateTime flag edge cases (%^P, %_N/%-3N, %_:z, last of - _ 0 wins); GNU date 9.4 on the host is the reference.
- Every process of a builtin shares one command object: glm-5.3 kept per-run state in its members (#66, critical) -- reviews check for it.
- glm-5.3 can try to write a huge file in one reply and die on "exceeded the 32000 output token maximum" (diff--diff-core, twice: exit 1, ~nothing committed, yet `ready`). Check claude.jsonl's result; the fix-round feedback must demand <=250 lines per Write/Edit and a commit per step -- and every plan re-check now writes that rule into the plan's Context (it worked: fix round 4 finished diff-core). A permanent fix (CLAUDE_CODE_MAX_OUTPUT_TOKENS or the prompt) is a scripts/ change, on master.
- Clean-room re-checks on Sonnet missed GNU-internal names in diff--diff-recursive's plan (switch_string, diff_dirs -> #73 critical). For plans of GNU tools (patch, awk, tar, pgrep/kill) re-check on Opus, and grep the plan for snake_case/C function names before starting.
- When diff--patch-core is re-checked: fold in #73's medium (Diff.cpp ~655: `diff -e` on two distinct identical files without a final newline -- GNU warns, exit 2; only the same resolved path is silent) and low (DiffFindLongOption: exact match first).
- When awk--functions is re-checked: ParsePrintfSpec (BuiltinPrintf.h) does not skip the `q` length modifier (#53 review).
