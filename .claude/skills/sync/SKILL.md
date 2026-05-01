---
name: sync
description: Sync the current branch by auto-committing any local changes, fetching latest, rebasing onto the base branch, and showing commits since base.
---

Sync the current branch with the latest upstream changes.

## Steps

### 1. Auto-commit local changes

Check if there are any uncommitted changes:

```bash
git status --porcelain
```

If there are changes, generate a quick commit message from the changed files:

```bash
CHANGED_FILES=$(git diff --name-only HEAD)
COMMIT_MSG="Update $(echo "$CHANGED_FILES" | head -1 | xargs basename)"
if [ "$(echo "$CHANGED_FILES" | wc -l)" -gt 1 ]; then
    COMMIT_MSG="Update multiple files"
fi
git add -A
git commit -m "$COMMIT_MSG"
```

### 2. Fetch latest refs

```bash
git fetch
```

### 3. Rebase onto base branch

Invoke the `/rebase` skill.

### 4. Print commit log since base

Detect the base branch:

```bash
BASE_BRANCH=$(./scripts/base_branch.sh)
```

Print the commits on the current branch that are not on the base:

```bash
git log --format="%h | %ad | %cd | %s" "$BASE_BRANCH"..
```

If there are no commits, print "(no commits yet)".
