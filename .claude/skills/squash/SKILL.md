---
name: squash
description: Squash all commits on the current branch into a single commit using the first commit's message. Detects the base branch automatically.
---

Squash all commits on the current branch (from the detected base branch up to HEAD) into a single commit, reusing the message from the branch's first commit.

## Steps

### 1. Detect the base branch

Use the following methods in order:

- **From PR**:
  ```bash
  BASE_BRANCH=$(gh pr view --json baseRefName -q .baseRefName 2>/dev/null)
  ```
- **From git upstream tracking**:
  ```bash
  UPSTREAM=$(git rev-parse --abbrev-ref @{upstream} 2>/dev/null)
  BASE_BRANCH=${UPSTREAM#origin/}
  ```
- **From git config**:
  ```bash
  CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
  BASE_BRANCH=$(git config branch.$CURRENT_BRANCH.merge 2>/dev/null | sed 's|refs/heads/||')
  ```
- **From commit ancestry** (checks which default branch HEAD actually descends from):
  ```bash
  for CANDIDATE in master main; do
      if git merge-base --is-ancestor "origin/$CANDIDATE" HEAD 2>/dev/null || \
         [ -n "$(git log --oneline "origin/$CANDIDATE..HEAD" 2>/dev/null | head -1)" ]; then
          BASE_BRANCH=$CANDIDATE
          break
      fi
  done
  ```
- **Fallback to common default branch refs**:
  ```bash
  for CANDIDATE in master main; do
      if git show-ref --verify --quiet refs/remotes/origin/$CANDIDATE 2>/dev/null || \
         git show-ref --verify --quiet refs/heads/$CANDIDATE 2>/dev/null; then
          BASE_BRANCH=$CANDIDATE
          break
      fi
  done
  ```
- If no base branch is found, stop and ask the user to specify it.

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
