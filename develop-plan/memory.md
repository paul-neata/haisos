# Memory

- The 2d overlap check matches the bare `CLAUDE.md` in nearly every plan; when only the root CLAUDE.md overlaps, look at its diff -- table rows and the /claude-docker model line are no reason to re-check a plan.
- 2026-10-08: the host (WSL, 7.6 GiB RAM, 2 GiB swap) ran critically low on memory during base--regex-syntax's first container run and the run was killed; check `free -h` before a task.
- 2026-10-08: WSL interop broken on the host (every .exe: "Exec format error"). Plan Direction: ignore task PRs' Windows checks (no waiting, re-runs or fixes); a PR green on Linux is reviewed and merged, Notes "Windows not checked". Windows built and tested once in the final phase; final--windows-fix (#54) runs last.
- HaisosOS.unittests is flaky on Windows CI (#44, #51, #52 -- on #52 a 30 s timeout, a hang); a fix task is asked in Questions.
- Windows' 1 MB stack: recursive parsers need low depth limits (Regex nesting 250).
- Interim agent notifications ("may be interim") carry a token count: record with it at once and correct the record at the next publish -- don't wait.
- Before coreutils--mv-touch starts: its plan re-check folds in #50's three open mediums (move cp's duplicated helpers into BuiltinCopy.h and reuse them in mv; backup mode worked out once at the end as GNU, '$VERSION_CONTROL' in the error, -S included; -n wins over a later --update=WORD) -- and waits for the answer to the rm -r symlink Question.
- When awk--functions or search--find-actions is re-checked: note that ParsePrintfSpec (BuiltinPrintf.h) does not skip the `q` length modifier (#53 review).
- glm-5.3 habit: GCC-only C++ (positional aggregate inits that narrow, a local named `stdin`) -- MSVC-only compile errors show up on Windows CI; the reviewer can fix them portably and CI verifies.
- When search--rg-search (or another grep-touching task) is re-checked: fold in #58's medium -- Grep.cpp:913, `if (stopped) break;` after the recursive walk so -q stops searching later operands.
- On this host plain `grep` is ugrep; GNU grep 3.11 is /usr/bin/grep (reference for reviews).
