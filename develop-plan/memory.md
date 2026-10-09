# Memory

- The 2d overlap check matches the bare `CLAUDE.md` in nearly every plan; when only the root CLAUDE.md overlaps, look at its diff -- table rows and the /claude-docker model line are no reason to re-check a plan.
- 2026-10-08: the host (WSL, 7.6 GiB RAM, 2 GiB swap) ran critically low on memory during base--regex-syntax's first container run and the run was killed; check `free -h` before a task.
- 2026-10-08: WSL interop broken on the host (every .exe: "Exec format error"). Plan Direction: ignore task PRs' Windows checks (no waiting, re-runs or fixes); a PR green on Linux is reviewed and merged, Notes "Windows not checked". Windows built and tested once in the final phase; final--windows-fix (#54) runs last.
- HaisosOS.unittests is flaky on Windows CI (#44, #51, #52 -- on #52 a 30 s timeout, a hang); a fix task is asked in Questions.
- Windows' 1 MB stack: recursive parsers need low depth limits (Regex nesting 250).
- Interim agent notifications ("may be interim") carry a token count: record with it at once and correct the record at the next publish -- don't wait.
- glm-5.3 habit: GCC-only C++ (positional aggregate inits that narrow, a local named `stdin`) -- MSVC-only compile errors show up on Windows CI; the reviewer can fix them portably and CI verifies.
- When search--rg-search (or another grep-touching task) is re-checked: fold in #58's medium -- Grep.cpp:913, `if (stopped) break;` after the recursive walk so -q stops searching later operands.
- On this host plain `grep` is ugrep; GNU grep 3.11 is /usr/bin/grep (reference for reviews).
- A run cut short (here by Ollama's 429) still ends `ready` when the leftovers build and pass: before the review, compare the PR's file list with the plan (tests, CMake, BuiltinCommandList.h, docs).
- When a task touching date/BuiltinDate (coreutils--stat, du, ls -l time styles) is re-checked: #60's mediums are candidates to fold in -- date --help lists no +FORMAT conversions, set-operand docs say ParseDateString but code uses ParseTouchStamp, FormatDateTime flag edge cases (%^P, %_N/%-3N, %_:z, last of - _ 0 wins); GNU date 9.4 on the host is the reference.
- Every process of a builtin shares one command object: glm-5.3 kept per-run state in its members (#66, critical) -- reviews check for it.
- When search--sed-advanced is re-checked: fold in #67's follow-ups -- `s///w FILE` (and `w`) filename runs to end of line as GNU's; SedIsStoppedPromptly on a held-open stdin pipe; a Regex unit test for multiline (M) `.`/`[^...]` not matching `\n`; the `/[/p` error row.
- When awk--functions is re-checked: ParsePrintfSpec (BuiltinPrintf.h) does not skip the `q` length modifier (#53 review).
