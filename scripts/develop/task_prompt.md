# Haisos develop task {{TASK_ID}} ({{MODE}})

You are implementing one task of the Haisos project (a C++ platform; read the
root `CLAUDE.md`, already loaded, and the `CLAUDE.md` of every component or
tool you touch). You work alone and unattended: nobody will answer questions.
When something is unclear, choose what the plan intends, and record the
choice in your summary.

## Where you are

- `/work` is a git repository on branch `{{BRANCH}}`. Stay on this branch.
- There is no remote and no GitHub access: do not push, do not open PRs, do
  not use `gh`. Your commits are collected when you finish.
- You have about {{TIMEOUT_MIN}} minutes. Commit as soon as the build is
  green, and again at the end, so nothing is lost if time runs out.

## What to do

{{MODE_INSTRUCTIONS}}

## Build and test

- Build (release, Linux only): `bash ./scripts/build_linux_on_linux.sh`
- Unit tests: `bash ./scripts/test_linux.sh L U`; one area:
  `bash ./scripts/test_linux.sh L U <filter>`. The runner prints
  `No tests found.` and exits 0 when a filter matches nothing -- check that
  the tests you added actually ran.
- Fix failures until the build and the unit tests pass. Never disable,
  delete or weaken a test to make it pass; if something cannot be fixed,
  leave it failing and explain why in the summary.

## Committing

- `git add` the files you changed, then `git commit -m "<message>"`, with the
  commit message the plan gives (under 72 characters). Several commits are
  fine.
- No `Co-Authored-By`, `Signed-off-by` or other trailers in commit messages.
- Do not rewrite history: no `git rebase`, `git commit --amend` or
  `git reset` on commits that were already there when you started.

## Never touch

These are refused automatically, and the whole task with them:

- `develop-plan/`, `notes/`, `HAISOS_VERSION`
- `.claude/`, `.github/`, `scripts/`, `extern/`, `.vscode/`, `.devcontainer/`,
  `.gitattributes`, `.gitmodules`, `.envrc`, `.mcp.json`
- symbolic links, git submodules, files over 1 MB

Also: no network access beyond what the build needs, nothing outside `/work`
except `/tmp`, no hard-coded secrets, keys or tokens, and no text addressed to
reviewers or AI agents in code, comments or docs.

## When you are done

Write `/out/summary.md` (at most 60 lines): what you changed (files), how
the plan's acceptance checklist is met item by item, deviations from the plan
and why, the build and test results (passed/failed counts), and anything
left unfinished.
