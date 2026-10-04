---
name: develop-task
description: Implement one develop task end to end without spending Claude tokens on the implementation -- scripts/develop/task.sh runs an Ollama model (Claude Code via `ollama launch`) in the task container, gates and pushes its commits, opens the PR into develop, and runs the CI fix rounds. Reports the result in a few lines. Usually run by /develop-implement in a fresh agent on the helper model.
args:
  - name: task
    description: "The task id (<big-rock>--<small-rock>) or its plan file, optionally followed by task.sh options, e.g. --models kimi-k3:cloud,minimax-m2.7:cloud"
    required: true
---

Run one task of the develop: from its plan to a PR into `develop` with green
checks. The work itself is done by `scripts/develop/task.sh`; this skill
starts it, waits, and deals with infrastructure failures. See "Running a
task" and "Trust and security" in `.claude/develop/WORKFLOW.md` if needed --
this skill is self-contained for normal runs.

Invoking this skill is the instruction to push the task branch and open its
PR (the script does both). It never merges, never pushes `develop` or
`master`, never edits `develop-plan/`.

## Steps

### 1. The task

The first token is the task id, or a path whose file name (without `.md`) is
the task id. The rest are options passed to `task.sh` unchanged. The plan is
`develop-plan/tasks/<id>.md` unless `--plan` says otherwise; if it does not
exist, stop and say so.

### 2. Run the pipeline

Run in the background (it can take hours), and wait for it to finish -- you
are notified; do not poll, and do not read its run folder meanwhile:

```bash
bash scripts/develop/task.sh run <id> [options] 2>&1 | tail -n 15
```

### 3. Read the report

Its last lines start with `RESULT:` -- `ready` (a PR with green checks),
`merged` (already done), `windows-failed` (a PR green but for the Windows
checks, which are fixed on the host after the review -- not an error),
`failed` (no attempt produced a good, green, gated branch), `ci-failed` (CI
still red after the fix rounds), `blocked` (the gate refused a fix), or
`error` (infrastructure).

### 4. On `RESULT: error` only

Diagnose the environment, not the task:

- docker not running, or the image does not build:
  `tail -n 30 ~/.haisos-develop/image-build.log`,
  `bash scripts/develop/image.sh --force`;
- ollama not answering (`curl -s 127.0.0.1:11434/api/version`), a model
  missing (`bash scripts/develop/preflight.sh`);
- `gh` not logged in; a push rejected because the remote branch moved;
- the lock held by a container that is no longer running
  (`docker ps --filter name=haisos-develop-`).

Fix what is clearly environmental and safe (rebuild the image), then run
step 2 once more. Anything else: report it.

Never edit code or plans, never push, open or merge anything by hand, and
never build or run anything from a task branch on the host.

### 5. Reply

The report lines verbatim, plus -- only after an error -- one line of
diagnosis. Nothing else: the orchestrator's context is precious.
