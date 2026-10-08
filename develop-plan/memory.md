# Memory

- The 2d overlap check matches the bare `CLAUDE.md` in nearly every plan; when only the root CLAUDE.md overlaps, look at its diff -- table rows and the /claude-docker model line are no reason to re-check a plan.
- 2026-10-08: the host (WSL, 7.6 GiB RAM, 2 GiB swap) ran critically low on memory during base--regex-syntax's first container run; Claude Code killed the background task.sh ~10 min in (no commits left). Stopped for the user.
