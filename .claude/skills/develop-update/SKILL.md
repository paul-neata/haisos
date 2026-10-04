---
name: develop-update
description: Sync this clone's develop with origin/develop and publish the plan -- fetch, commit what changed in develop-plan/ (and notes/, HAISOS_VERSION), rebase onto whatever the other session or merged task PRs pushed (resolving conflicts), push, refresh the develop PR, and report what came in. How the plan and implement sessions talk to each other; with nothing to commit it just brings develop up to date.
args:
  - name: message
    description: Optional commit message for the local plan changes; derived from the changes when omitted
    required: false
---

One step of the conversation between the two develop sessions (see "Two
sessions, one develop" in `.claude/develop/WORKFLOW.md`): publish what this
session changed in the plan, take in what the other one did. Invoking this
skill is the instruction to commit plan files on `develop` and push them.

The work is done by `scripts/develop/update.sh`; this skill picks the commit
message, resolves rebase conflicts, and reads out what came in.

## Steps

### 1. Be on `develop`

```bash
git rev-parse --abbrev-ref HEAD
git status --porcelain
```

Not on `develop`: if the working tree is clean and `origin/develop` exists,
`git switch develop`; otherwise stop and say why. A rebase already in progress
(`HEAD` detached, `git status` says "rebasing"): go to step 4.

### 2. The commit message

Only `develop-plan/`, `notes/` and `HAISOS_VERSION` are committed; anything
else in the tree stays as it is (the script says so). If they have changes,
the message is the argument, or a short one derived from them -- what changed
in the plan, e.g. `Plan: add task pipes--three-stage; answer 2 questions`.

### 3. Run it

```bash
bash scripts/develop/update.sh -m "<message>"     # or without -m when nothing changed
```

Exit status 0: go to step 5. 4: plan changes but no message -- run again
with `-m`. 3: a conflict, step 4. Anything else: report the error.

### 4. Resolve the rebase conflicts

The local commits are being replayed onto `origin/develop`. In each
conflicted file (`git status --short`: `UU`, `AA`, ...), the side between
`<<<<<<<` and `=======` is `origin/develop` (the other session, or a merged
task), the other side is this session's commit. Keep **both intents**,
following who owns what:

- **Playbook rows**: the Status, Version, PR, Tries and Review cells are the
  implement session's; new rows, the order, and the plans of tasks not yet
  started are the plan session's. A row changed on both sides takes each
  cell from its owner. Rows added on either side are all kept.
- **Questions, Directions, Adjustments, the log**: keep every line from
  both sides, in time order; a `-> answered` / `-> done` mark wins.
- **`Pause:` / `Phase:`**: the most recent intent -- a pause set by the plan
  session stays set.
- **A task plan, `goal.md`, `rocks.md`**: merge by content; if both sides
  rewrote the same part differently, keep the plan session's and note the
  other in the playbook's Adjustments.
- **`HAISOS_VERSION`**: a version set by the plan session wins over a task's
  patch bump; otherwise the higher version.

Then:

```bash
git add <files>
GIT_EDITOR=true git rebase --continue
```

Repeat for each commit that conflicts; then run `bash scripts/develop/update.sh`
again (without `-m`) to push. Never `git rebase --skip` or `--abort` (they
drop or undo work), never force-push `develop`.

### 5. Report

In a few lines:

- what was committed and pushed (or "nothing to publish");
- **what came in** (the script's `INCOMING` part): the other session's plan
  changes, tasks started or merged, questions and directions -- the part the
  caller acts on;
- the develop PR line.

The plan session reads it to follow the implementation; the implement
session reads it to take in plan changes before its next task.
