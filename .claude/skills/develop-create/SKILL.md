---
name: develop-create
description: Start a develop -- one big, user-visible feature at a time. Cuts `develop` from a fresh origin/master, bumps the minor version once for the whole develop, writes the develop-plan/ skeleton (goal with settings and metadata, rocks, playbook), commits and pushes it. Refuses while another develop exists. Next step: /develop-plan begin.
model: sonnet
args:
  - name: title
    description: A short title for the develop, e.g. "Pipes and redirections" (optional; /develop-plan can set it)
    required: false
---

Start a develop. The workflow is described in `.claude/develop/WORKFLOW.md`;
this skill is its first step. Invoking it is the instruction to commit and
push the new `develop` branch.

## Steps

### 1. Check that nothing is in the way

```bash
bash scripts/develop/git_ssh.sh
git status --porcelain
git fetch origin --prune
git rev-parse -q --verify origin/develop
git rev-parse -q --verify refs/heads/develop
gh auth status
```

- `git_ssh.sh` says `FAIL`: git cannot reach origin over SSH (see "Git over
  SSH" in `WORKFLOW.md`); stop and show its line.
- A dirty working tree: stop, list the files, and ask the user to commit or
  stash them.
- `origin/develop` exists: a develop is in progress. Stop, and show
  `bash scripts/develop/state.sh`; it ends with `/develop-close`.
- A local `develop` without `origin/develop`: a leftover. Stop and ask the
  user whether to delete it (`git branch -D develop`); never delete it
  unasked.
- `gh` not logged in: stop and say so.

### 2. Create the branch

```bash
git switch --no-track -c develop origin/master
git config branch.develop.haisos-base master
```

### 3. Bump the minor version, once for the whole develop

```bash
OLD_VERSION=$(tr -d '[:space:]' < HAISOS_VERSION)
if [[ ! "$OLD_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "HAISOS_VERSION is not MAJOR.MINOR.PATCH: '$OLD_VERSION'"; exit 1
fi
NEW_VERSION="${BASH_REMATCH[1]}.$((BASH_REMATCH[2] + 1)).0"
echo "$NEW_VERSION" > HAISOS_VERSION
```

Stop and tell the user if the version is not `MAJOR.MINOR.PATCH`. Task
branches never touch `HAISOS_VERSION` (the gate refuses it).

### 4. Write the `develop-plan/` skeleton

`<title>` is the argument, or `untitled` if none was given. `<created>` is
`date -u +%Y-%m-%dT%H:%M:%SZ`, `<base>` is `git rev-parse --short origin/master`.

`develop-plan/goal.md`:

```
# Develop: <title>

## Metadata
- Created: <created>
- Base: master @ <base>

## Settings
- Task models: kimi-k3:cloud
- Attempts per model: 2
- CI fix rounds: 3
- Task timeout minutes: 120
- Review model: opus
- Helper model: sonnet

## Goal
(to be written with /develop-plan)

## Clarifications

## Acceptance scenarios

## Out of scope
```

`develop-plan/rocks.md`:

```
# Big rocks

(to be written with /develop-plan)
```

`develop-plan/playbook.md`:

```
# Playbook

Phase: planning
Pause: no

| # | Task | Status | Version | Depends | PR | Tries | Review | Notes |
|---|------|--------|---------|---------|----|-------|--------|-------|

## Directions

## Questions

## Adjustments
```

`develop-plan/memory.md` (the implement session's notes to itself, shown on
the develop PR):

```
# Memory

```

`develop-plan/log.md` (the implement session's log, shown on the develop PR):

```
# Log

```

The version lives in `HAISOS_VERSION` alone: plan mode (`/develop-plan begin`) may set
it to anything, each task adds one to its patch (see "Versions" in
`WORKFLOW.md`).

### 5. Commit and push

```bash
git add HAISOS_VERSION develop-plan
git commit -m "Start develop $NEW_VERSION: <title>"
git push -u origin develop
```

No `Co-Authored-By` or other trailers in the message.

### 6. Report

- The branch, the version (`<old> -> <new>`), the commit.
- Next, in the plan session: `/develop-plan begin`, then describe the goal;
  publish the plan with `/develop-update` whenever it is worth sharing.
- For the implement session, a second clone on `develop`, e.g.:
  `git clone <origin url> ~/src/haisos-implement && cd ~/src/haisos-implement && git switch develop`,
  then copy `.claude/settings.local.json` into it (it is not versioned), and
  start `/develop-implement` there once the playbook has tasks -- it opens
  the develop PR (`WIP [M.m] <title>`). The two sessions keep each other up
  to date with `/develop-update`.
