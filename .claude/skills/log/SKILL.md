---
name: log
description: Show commits on the current branch that are not on the base branch.
---

Print the commit log from the base branch to HEAD.

## Steps

1. Detect the base branch:
   ```bash
   BASE_BRANCH=$(./scripts/base_branch.sh)
   ```

2. Print the commits on the current branch that are not on the base:
   ```bash
   git log --format="%h | %ad | %cd | %s" "$BASE_BRANCH"..
   ```

3. If there are no commits yet, print "(no commits yet)".
