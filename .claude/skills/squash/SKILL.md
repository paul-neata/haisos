---
name: squash
description: Squash all commits on the current branch into a single commit using the first commit's message. Detects the base branch automatically.
---

Squash all commits on the current branch (from the detected base branch up to HEAD) into a single commit, reusing the message from the branch's first commit.

## Steps

### 1. Detect the base branch

Use `scripts/base_branch.sh`:

```bash
BASE_BRANCH=$(./scripts/base_branch.sh)
```

- If the script fails, stop and ask the user to specify the base branch.

### 2. Find the merge-base commit

```bash
BASE_COMMIT=$(git merge-base HEAD "origin/$BASE_BRANCH" 2>/dev/null || git merge-base HEAD "$BASE_BRANCH" 2>/dev/null)
```
- If `BASE_COMMIT` cannot be determined, stop and ask the user.

### 3. Get the first commit message on the branch

```bash
FIRST_MESSAGE=$(git log --format="%s" "${BASE_COMMIT}..HEAD" | tail -1)
```
- If `FIRST_MESSAGE` is empty, stop and inform the user.

### 4. Squash the commits

```bash
git reset --soft "$BASE_COMMIT"
git commit -m "$FIRST_MESSAGE"
```

### 5. Confirm

Report the squashed commit hash (`git rev-parse HEAD`) and the commit message to the user.
