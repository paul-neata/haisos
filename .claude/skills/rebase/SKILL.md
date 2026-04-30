---
name: rebase
description: Rebase the current branch onto its base branch. Resolve conflicts if needed, then build and test to verify correctness.
---

Rebase the current branch onto its detected base branch. If conflicts occur, resolve them automatically where possible, then build and test to ensure correctness.

## Steps

### 1. Detect the base branch

Determine the base branch using the following methods in order:

- **From PR** (most reliable):
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

### 2. Fetch the latest base branch

```bash
git fetch origin "$BASE_BRANCH"
```

### 3. Start the rebase

```bash
git rebase "origin/$BASE_BRANCH"
```

- If the rebase completes with no output indicating conflicts and exits 0, report success and stop.
- If conflicts are reported, proceed to step 4.

### 4. Resolve rebase conflicts

While the rebase is in progress (verify with `git rev-parse --git-path rebase-merge` or check for `.git/rebase-merge` / `.git/rebase-apply`):

1. List conflicted files:
   ```bash
   git diff --name-only --diff-filter=U
   ```

2. For each conflicted file:
   - Read the file contents.
   - Resolve the conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`). Preserve the intended code by choosing the correct side or merging both as appropriate.
   - Write the resolved file.
   - Stage it:
     ```bash
     git add "<file>"
     ```

3. Continue the rebase:
   ```bash
   git rebase --continue
   ```

4. If new conflicts appear, repeat from step 4.1.

5. If `git rebase --continue` fails because no changes were made (e.g. all conflicts were resolved to match the incoming version), you may need to skip the empty commit:
   ```bash
   git rebase --skip
   ```

### 5. Verify with build and test

If any conflicts were resolved during the rebase:

1. Invoke the `/build` skill for the current platform.
2. After `/build` completes, invoke the `/test` skill for the current platform and all test types.

3. If the build or tests fail:
   - Diagnose the failure.
   - Edit the relevant source files to fix the issues.
   - Re-invoke `/build` and `/test` until they pass.

4. Report the final outcome to the user.
