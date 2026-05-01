---
name: begin
description: Start a new task branch from master. Stashes any local changes, updates master, creates the new branch, and shows commits since base.
args:
  - name: branch_name
    description: The new branch name to create (e.g. task/job_to_be_done)
    required: true
---

Start a new task branch from an up-to-date master.

## Steps

### 1. Stash local changes

Check if there are any uncommitted changes:

```bash
git status --porcelain
```

If there are changes, stash them and announce it to the user:

```bash
git stash push -m "auto-stash before beginning $(date +%Y-%m-%d_%H:%M:%S)"
```

Tell the user: "Local changes detected and stashed."

### 2. Update master

```bash
git checkout master
git pull
```

### 3. Create the new branch

Use the `branch_name` argument provided by the user:

```bash
git checkout -b "{{branch_name}}"
```

### 4. Print commit log since base

Detect the base branch:

```bash
BASE_BRANCH=$(./scripts/base_branch.sh)
```

Print the commits on the new branch that are not on the base:

```bash
git log --format="%h | %ad | %cd | %s" "$BASE_BRANCH"..
```

If there are no commits yet, print "(no commits yet)".

### 5. Confirm the new branch

Print the current branch name to confirm:

```bash
git rev-parse --abbrev-ref HEAD
```

Tell the user: "New branch '{{branch_name}}' created and checked out."
