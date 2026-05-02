---
name: end
description: Finish the current task branch by building on current and other platforms, committing, pushing, and updating/creating a GitHub PR.
---

Finish the current task branch by building, committing, pushing, and updating or creating a pull request.

## Steps

### 1. Build on current platform

Invoke the `/build` skill for the current platform:
```bash
/build
```
If the build fails, report the failure and stop. Do not proceed.

### 2. Build on the other platform (if applicable)

Detect the platform:
```bash
IS_WSL=0
if [[ "$(uname -s)" == Linux* ]] && grep -qi microsoft /proc/version 2>/dev/null; then
    IS_WSL=1
fi
```

If on Linux or WSL, also invoke the `/build` skill for Windows:
```bash
/build W
```
If the Windows build fails, report the failure and stop. Do not proceed.

If not on Linux/WSL, print "Cross-platform build skipped (not on Linux/WSL)."

### 3. Commit any pending changes

Invoke the `/commit` skill.

### 4. Push the branch

Determine the current branch:
```bash
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
```

Push the branch. Use `-u origin HEAD` on the first push to set upstream tracking:
```bash
git push -u origin "$CURRENT_BRANCH"
```

If the push fails because the branch already has an upstream set, fall back to:
```bash
git push
```

### 5. Update or create a pull request

Check if a GitHub PR already exists for this branch:
```bash
gh pr view --json number 2>/dev/null
```

- If a PR exists, invoke the `/update-pr-description` skill.
- If no PR exists, invoke the `/create-pr` skill.

### 6. Confirm completion

Tell the user the branch has been built, committed, pushed, and the PR has been updated or created.
