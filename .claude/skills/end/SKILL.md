---
name: end
description: Finish the current task branch by building on Linux and Windows, pushing, and creating a GitHub PR.
---

Finish the current task branch by building on both platforms, pushing, and opening a pull request.

## Steps

### 1. Build on Linux

Invoke the `/build` skill for Linux:
```bash
/build L
```
If the build fails, report the failure and stop. Do not proceed to PR creation.

### 2. Build on Windows (if on WSL)

Detect WSL:
```bash
IS_WSL=0
if [[ "$(uname -s)" == Linux* ]] && grep -qi microsoft /proc/version 2>/dev/null; then
    IS_WSL=1
fi
```

If `IS_WSL == 1`, invoke the `/build` skill for Windows:
```bash
/build W
```
If the Windows build fails, report the failure and stop. Do not proceed to PR creation.

If not on WSL, print "Windows build skipped (not on WSL)."

### 3. Push the branch

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

### 4. Create a pull request

Invoke the `/create-pr` skill.

### 5. Confirm completion

Tell the user the branch has been pushed and a PR has been created (or attempted).
