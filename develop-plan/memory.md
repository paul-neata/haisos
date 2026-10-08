# Memory

- The 2d overlap check matches the bare `CLAUDE.md` in nearly every plan; when only the root CLAUDE.md overlaps, look at its diff -- table rows and the /claude-docker model line are no reason to re-check a plan.
- 2026-10-08: the host (WSL, 7.6 GiB RAM, 2 GiB swap) ran critically low on memory during base--regex-syntax's first container run and the run was killed; check `free -h` before a task.
- 2026-10-08: WSL interop broken on the host (every .exe: "Exec format error", no WSLInterop in binfmt_misc) -- windows.sh cannot build; the user must restore it (e.g. `wsl --shutdown`) before 2g fixes or the final Windows run. Until then, a Windows-only red: first re-run the failed CI job (`gh run rerun <run> --failed`, `wait_ci.sh <pr>`).
- HaisosOS.unittests is flaky on Windows CI (#44: failed once, passed on re-run, untouched by the PR).
- Windows' 1 MB stack: recursive parsers need low depth limits (Regex nesting 250).
