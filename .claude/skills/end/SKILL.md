---
name: end
description: Finish the current task branch by pushing it to origin and creating a GitHub PR.
---

Finish the current task branch by pushing and opening a pull request.

## Steps

### 1. Push the branch

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

### 2. Create a pull request

Invoke the `/create-pr` skill.

### 3. Confirm completion

Tell the user the branch has been pushed and a PR has been created (or attempted).
