---
name: begin
description: Start a new task branch from master. Stashes any local changes, updates master, creates the new branch, bumps the minor version in HAISOS_VERSION, shows commits since base, and prunes merged local branches.
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

### 4. Bump the minor version

The version lives in `HAISOS_VERSION` at the repo root, as `MAJOR.MINOR.PATCH`. Every new task branch starts a new minor version: increment `MINOR` and reset `PATCH` to 0 (e.g. `0.1.2` becomes `0.2.0`).

```bash
OLD_VERSION=$(tr -d '[:space:]' < HAISOS_VERSION)
if [[ ! "$OLD_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "HAISOS_VERSION is not MAJOR.MINOR.PATCH: '$OLD_VERSION'"
    exit 1
fi
NEW_VERSION="${BASH_REMATCH[1]}.$((BASH_REMATCH[2] + 1)).0"
echo "$NEW_VERSION" > HAISOS_VERSION
echo "Version: $OLD_VERSION -> $NEW_VERSION"
```

If `HAISOS_VERSION` does not match `MAJOR.MINOR.PATCH`, stop and tell the user rather than guessing.

Leave the change uncommitted: it is committed along with the branch's work (e.g. by `/commit` or `/end`).

Tell the user: "Version bumped from <old> to <new>."

### 5. Print commit log since base

Detect the base branch:

```bash
BASE_BRANCH=$(./scripts/base_branch.sh)
```

Print the commits on the new branch that are not on the base:

```bash
git log --format="%h | %ad | %cd | %s" "$BASE_BRANCH"..
```

If there are no commits yet, print "(no commits yet)".

### 6. Confirm the new branch

Print the current branch name to confirm:

```bash
git rev-parse --abbrev-ref HEAD
```

Tell the user: "New branch '{{branch_name}}' created and checked out."

### 7. Prune old branches

Invoke the `/prune-old-branches` skill. It deletes local branches whose every commit is already on `origin/master` by exact hash, and lists every other branch with the command to delete it by hand.
