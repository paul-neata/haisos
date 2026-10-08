# Memory

- The 2d overlap check matches the bare `CLAUDE.md` in nearly every plan; when only the root CLAUDE.md overlaps, look at its diff -- table rows and the /claude-docker model line are no reason to re-check a plan.
- 2026-10-08: the host (WSL, 7.6 GiB RAM, 2 GiB swap) ran critically low on memory during base--regex-syntax's first container run and the run was killed; check `free -h` before a task.
- 2026-10-08: WSL interop broken on the host (every .exe: "Exec format error") -- windows.sh cannot build. User decision (2026-10-08): continue without the host Windows build. A Windows-only red: re-run the failed CI job once (`gh run rerun <run> --failed`, `wait_ci.sh <pr>`); still red -> merge anyway, Notes "Windows red", and fix it on the host in the final phase once interop is back (or report the final Windows run as skipped).
- HaisosOS.unittests is flaky on Windows CI (#44: failed once, passed on re-run, untouched by the PR).
- Windows' 1 MB stack: recursive parsers need low depth limits (Regex nesting 250).
- Interim agent notifications ("may be interim") carry a token count: record with it at once and correct the record at the next publish -- don't wait.
