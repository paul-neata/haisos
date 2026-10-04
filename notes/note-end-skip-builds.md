# /end should skip builds when nothing significant changed

`/end` (`.claude/skills/end/SKILL.md`) always runs `/build` for the host
platform and `/build W` (the Windows cross-build from WSL) before it commits,
pushes and creates or updates the PR. When the branch changes only files that
cannot affect the binaries, those builds cost minutes and prove nothing.

Example: `task/add_new_develop_skills` (PR #17) changed only `.claude/`
(skills, `WORKFLOW.md`), `scripts/develop/`, `scripts/base_branch.sh`,
`.github/`, `CLAUDE.md` and a Markdown conversation export. `/end` still ran
both full builds.

Idea: before building, have `/end` look at
`git diff --name-only origin/<base>...HEAD` plus the uncommitted changes, and
skip both builds (and any tests) when every changed file is insignificant,
saying so in its report. Candidates for "insignificant":

- `notes/`, `develop-plan/`, `.claude/`, and `*.md` files anywhere (including
  the `CLAUDE.md` files);
- `scripts/develop/` and helper scripts the build does not use.

Anything under `src/`, `interfaces/`, `tests/`, `extern/`, `CMakeLists.txt`,
`*.cmake`, the `scripts/build_*`/`scripts/test_*` scripts, `HAISOS_VERSION`
(if it is compiled in) or `.github/` build/test scripts still triggers the
builds. When in doubt, build. CI still builds every push, so skipping the
local build loses little.
