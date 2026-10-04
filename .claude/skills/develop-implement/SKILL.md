---
name: develop-implement
description: Run the develop, in the implement session -- open the develop PR (WIP, a draft) and keep it as a live view and memory; loop over the playbook: take in plan changes through /develop-update, act on directions, re-check the next plan, hand the task to /develop-task and its PR to /develop-code-review in fresh agents, have Windows fixed on the host when only Windows is red, record the outcome and bump the patch version; then the whole-develop review, the final Linux and Windows test runs, and the develop PR made ready. Resumable at any point, and re-entered when tasks are added later.
args:
  - name: options
    description: "Optional: --once (one task, then stop), --no-final (stop before the final phase), --final (go straight to the final phase)"
    required: false
---

The orchestrator of the develop workflow. Read `.claude/develop/WORKFLOW.md`
once per conversation (skip it if already read). Invoking this skill is the
instruction to commit and push plan files on `develop`, to open and update
the develop PR, to run tasks (which push task branches and open PRs) and to
merge task PRs into `develop` -- never into `master`.

**Keep this conversation lean** -- it lives for the whole develop: scripts
print a few lines, agents reply in a few lines, you read the playbook but not
the code, the plans or the logs. All state is in `develop-plan/` and on
GitHub, and the develop PR carries your memory and log, so this skill can be
stopped and started again at any point -- after a `/clear`, a compaction or a
crash -- without losing anything.

**Publishing** is always `/develop-update`'s engine, run directly:
`bash scripts/develop/update.sh -m "<message>" --log "<entry>"`. It commits
the plan, takes in the other session's changes, pushes, and re-renders the
develop PR. On exit 3 (a rebase conflict) follow step 4 of
`.claude/skills/develop-update/SKILL.md`. Every checkpoint below -- the
beginning, each task's start and end, the end -- goes through it, with a
one-line log entry.

Settings come from `develop-plan/goal.md` (`## Settings`): `Review model`
(default `opus`), `Helper model` (default `sonnet`); the task models are read
by the scripts.

## 1. The beginning

1. **Your memory.** Read `develop-plan/memory.md` and the last 15 lines of
   `develop-plan/log.md`: what you did and noted before this conversation.
2. **Checks:**
   ```bash
   git rev-parse --abbrev-ref HEAD      # develop, in this (implement) clone
   git status --porcelain               # clean
   bash scripts/develop/preflight.sh
   ```
   Any `FAIL`: stop and report it.
3. **The baseline**, once per develop (no folder
   `~/.haisos-develop/runs/<develop id>/baseline--develop` yet -- the develop
   id is `goal.md`'s `Created` time without `-`, `:`): `develop` must be green
   in the container, which also warms its build. In the background, waiting
   for it:
   ```bash
   bash scripts/develop/container.sh test baseline--develop --base origin/develop \
       --branch-rev origin/develop --branch-name develop-baseline --tests U 2>&1 | tail -n 2
   ```
   Red: stop and report; no task can pass on a red `develop`.
4. **The develop PR**: set the playbook's `Phase: implementing` (unless the
   tasks left are all `done`/`obsolete` and `--final` was given: `final`),
   then publish and open the PR:
   ```bash
   bash scripts/develop/update.sh -m "Implement: start" --log "implement session started"
   bash scripts/develop/develop_pr.sh ensure
   ```
   It is titled `WIP [M.m] <title>` and is a draft until the develop is done.

## 2. The loop -- one task per round

### 2a. Take in the plan

```bash
bash scripts/develop/update.sh
bash scripts/develop/state.sh
```

Then read `develop-plan/playbook.md`. From the `INCOMING` part and the
playbook:

- `Pause: yes` -> publish (`--log "paused by the plan session"`) and stop.
- **Directions** without `-> done` (the plan session's instructions to you --
  pause after a task, reorder, a model for a task, skip something, ...): act
  on each, mark it `-> done: <what you did>`, and record it in the
  Adjustments.
- **Plan changes** (new or re-planned tasks, a changed goal or rocks, a new
  version): read what changed and change direction where needed -- the next
  task, the order, a task made pointless (`obsolete`, with an Adjustments
  line). A task already `in-progress` or later is not re-planned; the plan
  session adds a follow-up instead.
- `Phase: done` or `final` with tasks `todo` again (added after the end):
  `Phase: implementing` -- the PR goes back to WIP and a draft.

### 2b. Reconcile with GitHub

GitHub is the truth for branches, PRs and merges; fix the playbook where it
disagrees:

- a PR merged, status not `done` -> `done` (PR number; Review `?` when no
  record exists);
- `in-progress` or `in-review` with an open PR -> resume that task at 2e
  (`task.sh` resumes from the PR) or 2f;
- `in-progress` with no branch, no PR and no running container -> `todo`.

### 2c. Pick the next task

The first row, in playbook order, whose status is `todo`, `in-progress` or
`in-review` and whose `Depends` are all `done` (or `obsolete`).

- None, and every task is `done` or `obsolete` -> section 3 (unless
  `--no-final`).
- None, but some are `blocked` -> publish (`--log "waiting: <blocked tasks>"`)
  and stop: report the blocked tasks and the open questions;
  `/develop-plan answer` in the plan session unblocks them.

### 2d. Check its plan -- cheaply first

```bash
PLAN=develop-plan/tasks/<id>.md
CHECKED=$(sed -n 's/^- Plan checked against: develop @ *//p' "$PLAN")
git diff --name-only "$CHECKED" origin/develop -- . ':!develop-plan' ':!notes' \
    | grep -Fxf <(grep -oE '[A-Za-z0-9_./-]+\.(h|hpp|cpp|c|txt|md|js|lua|json|cmake)' "$PLAN" | sort -u)
```

No overlap (nothing the plan names has changed since it was written): go on.
Otherwise -- or when the plan has no `Plan checked against` line -- spawn a
fresh agent (`general-purpose`, model: the helper model):

````
Re-check the plan develop-plan/tasks/<id>.md of a Haisos develop against the
current code: it was written at develop <CHECKED>; since then these files it
names changed: <list>. Read the plan and those changes (git diff <CHECKED>
origin/develop -- <files>). Update only what is now wrong in the plan (names,
signatures, paths, what earlier tasks already did) and set its line
"- Plan checked against: develop @ <git rev-parse --short origin/develop>".
Do not change its goal or scope; if the goal no longer makes sense, say so
instead. Do not commit. Reply in at most 5 lines: what changed.
````

and add an Adjustments line `(implement) <id> refreshed: <what>`. The
orchestrator may also, with an Adjustments line each: re-order `todo` tasks
(a dependency discovered), mark a task `obsolete` when an earlier one already
did its work. The goal, the rocks, and dropping user-visible behaviour are the
user's: ask (a Question) instead.

### 2e. Run the task

Its **version**: `HAISOS_VERSION` on `develop` with the patch number plus one
(`0.4.2` -> `0.4.3`), unless its Version cell already has one (a resumed
task). Set the row to `in-progress` with that Version, and publish:

```bash
bash scripts/develop/update.sh -m "Playbook: start <id>" --log "start <id> (<version>)"
```

Spawn a fresh agent in the background and wait for its notification:

- description `develop-task <id>`, `general-purpose`, model: the helper model;
- prompt: `Run the develop-task skill for task <id>: read
  .claude/skills/develop-task/SKILL.md and follow it exactly. Arguments: <id>.
  Reply with its report only.`

The task PR is titled `[<version>] <PR title>` and links the develop PR.
Note the agent's token use from the notification. By the report's `RESULT`:

- `ready` or `windows-failed` -> status `in-review`, PR number, Tries (the
  container runs); publish (`--log "<id>: PR #<n>, <result>"`); go to 2f.
- `merged` -> status `done`; go to 2h.
- `failed`, `ci-failed`, `blocked` -> status `blocked`, Notes: the `LAST
  FAILURE` in a few words; add a Question: `(implement, <date>) <id>: <LAST
  FAILURE>; runs in <RUN FOLDER>. Retry with another model, re-plan, split,
  or drop?`; publish (`--log "<id> blocked: <reason>"`); next round (another
  task may still be runnable). If the report shows the task was too big
  (timed out, a huge diff), say so in the question. Never implement the task
  yourself.
- `error` -> publish (`--log "stopped: <error>"`) and stop: the environment
  needs attention.

### 2f. Review

Spawn a fresh agent in the background and wait for it:

- description `develop-code-review #<n>`, `general-purpose`, model: the review
  model;
- prompt: `Run the develop-code-review skill for PR #<n>: read
  .claude/skills/develop-code-review/SKILL.md and follow it exactly. Reply
  with its step 8 report only.`

By its `VERDICT`:

- `merged` -> 2h.
- `approved` (only Windows is red) -> 2g.
- `blocked` -> status `blocked`, a Question with the reason; publish; next
  round.
- `malicious` -> **stop everything**: status `blocked`, `Pause: yes`, a
  Question with the evidence, publish (`--log "STOPPED: suspected malicious
  change in <id>"`); empty the container's workspace
  (`rm -rf ~/.haisos-develop/work`); tell the user plainly what was found and
  where. Do not resume without the user.

### 2g. Windows, on the host

The review has cleared the code; the container cannot build Windows, so a
fresh agent does it on the host (`general-purpose`, model: the helper model,
in the background):

````
Fix the Windows build and unit tests of task <id> (PR #<n>, branch
task/<id>) of a Haisos develop. Its code has been reviewed and cleared up to
<REVIEWED HEAD>; only the Windows CI checks fail, and the task container
cannot build Windows, so you work on the host. Invoking this is the
instruction to commit on task/<id> and push it through scripts/develop/task.sh.

1. git fetch -q origin && git branch -f task/<id> origin/task/<id>
   Its head must be <REVIEWED HEAD>; if not, stop and reply "head moved".
2. bash scripts/develop/windows.sh prepare <id>   (prints the worktree W)
   The failed CI log: <WINDOWS LOG from the task report, or
   gh run view <run> --log-failed>.
3. bash scripts/develop/windows.sh build -- in the background when it may
   take over 10 minutes; wait for it. Fix the errors in W's files (the root
   CLAUDE.md and the touched components' CLAUDE.md apply; keep Linux
   behaviour unchanged; prefer the portable fix). Repeat until BUILD: OK.
4. bash scripts/develop/windows.sh test [<filter>] -- fix until green. Never
   disable or weaken a test.
5. git -C <W> add -A && git -C <W> commit -m "Windows: <what>"  (no trailers)
6. bash scripts/develop/task.sh verify <id>  -- Linux, in the container:
   must end RESULT: ready.
7. bash scripts/develop/task.sh push <id> 2>&1 | tail -n 12  -- in the
   background; must end RESULT: ready.
8. Free the branch for the next steps: git -C <W> switch -q --detach
Reply in at most 8 lines: RESULT (ready | failed), your commits (hash,
subject), git diff --shortstat <REVIEWED HEAD> origin/task/<id>, and what
remains if it failed.
````

- `ready` with at most ~40 changed lines -> merge it yourself, then clean up
  (the Windows worktree stays, detached -- its builds are incremental):
  ```bash
  SHA=$(gh pr view <n> --json headRefOid -q .headRefOid)
  gh pr merge <n> --squash --match-head-commit "$SHA" --subject "<PR title> (#<n>)" \
      --body "Develop task <id>. Review: <counts from the review>. Windows fixed on the host."
  git branch -D task/<id>; git fetch -q origin --prune
  ```
- `ready` with more -> a review of the new commits only: as 2f, with the
  prompt's PR argument `<n> --since <REVIEWED HEAD>`.
- `failed` -> `blocked` with a Question, as in 2e.

### 2h. Record

- Playbook: status `done`, PR, Tries, Review (`<c>C <h>H fixed, <m>M <l>L
  open`), Notes if anything is worth a line.
- `HAISOS_VERSION`: the task's version -- unless the plan session has set
  another version meanwhile (then leave it: the next task counts on from
  there).
- `develop-plan/reviews/<id>.md` in the review record format of
  `WORKFLOW.md`: the verdict, the merge hash, the findings table from the
  review's reply, and the tokens line -- Claude: the task, review and Windows
  agents' counts from their notifications, summed; Ollama: the task report's
  `OLLAMA TOKENS`.
- `develop-plan/memory.md`: only when this task taught you something worth
  keeping for the rest of the develop (a model's habit, a plan weakness that
  recurs, a user decision). Keep it under ~40 lines; rewrite rather than
  grow.
- Publish:
  ```bash
  bash scripts/develop/update.sh -m "Playbook: <id> done (#<n>), <version>" \
      --log "<id> merged #<n> as <version>: <review counts>, <tries> tries"
  ```
- Tell the user one line: `<id>: merged #<n> as <version>, <review counts>`.

`--once`: stop here. Otherwise the next round.

## 3. The final phase

Set `Phase: final` and publish (`--log "final phase"`).

### 3a. Whole-develop review

`develop-plan/final-review.md` records the develop head it reviewed
(`- Reviewed: develop @ <sha>`):

- no `final-review.md` yet -> spawn the review agent (model: the review
  model) with `Run the develop-code-review skill with --develop: read
  .claude/skills/develop-code-review/SKILL.md and follow it exactly. Reply
  with its report only.`;
- task merges since its reviewed head (tasks added after an earlier end) ->
  the same with `--develop --since <sha>`: only what came after;
- otherwise -> 3b.

It writes `final-review.md` and, for critical or high findings,
`tasks/final--review-fixes*.md`. Add a playbook row for each fixes task
(`todo`, no dependencies), set `Phase: implementing`, publish (`-m "Plan:
final review" --log "final review: <counts>"`), and go back to the loop: the
fixes are tasks like any other, implemented by Ollama and reviewed.

### 3b. Final test runs -- both in the background

Linux, all test types, in the container, from a clean build; the integration
and haisos tests talk to the host's ollama:

```bash
HAISOS_ENDPOINT=http://127.0.0.1:11434/api/chat HAISOS_MODEL=<the project's model, e.g. kimi-k2.6:cloud> \
bash scripts/develop/container.sh test final--linux --base origin/develop --branch-rev origin/develop \
    --branch-name develop-final --tests '*' --clean-build 2>&1 | tail -n 2
```

Windows, on the host -- the whole develop has been reviewed:

```bash
bash scripts/develop/windows.sh prepare develop && bash scripts/develop/windows.sh build \
    && bash scripts/develop/windows.sh test
```

Write the results under `## Final tests` in `final-review.md` (by platform
and test type). A unit-test failure is a bug that CI missed: add a Question,
publish, and stop. An integration or haisos test failure caused by the LLM's
answer rather than by the change is reported as such, not fixed.

### 3c. Exploratory testing -- reserved

Not yet. Later: haisosfiles invented from `goal.md`'s acceptance scenarios
and the directives, tools and builtins the develop added, fuzzed haisosfiles,
run in the container; the results go into `final-review.md`.

### 3d. What stays open

Write `notes/note-<M.m>-review-findings.md` (with the `/note` conventions: a
heading, then one line per open medium or low finding from `reviews/*.md` and
`final-review.md` -- severity, file, finding, task and PR), so they outlive
`develop-plan/`.

### 3e. Done

Set `Phase: done`, and publish:

```bash
bash scripts/develop/update.sh -m "Implement: done" --log "develop done: <n> tasks, final tests <summary>"
```

The develop PR is re-rendered as `[M.m] <title>` -- no WIP -- and marked
ready for review. Report: the PR link, the test results, the open questions
if any, and that `/develop-close` merges it once the user has looked at it.
If the plan session adds tasks later, running this skill again picks them up
(2a), and the PR goes back to WIP until they are done.
