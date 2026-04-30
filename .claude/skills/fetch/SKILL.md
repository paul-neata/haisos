---
name: fetch
description: Run git fetch and force-update the local master branch to match origin/master.
---

Fetch the latest refs from origin and reset the local `master` branch to point at `origin/master`.

## Steps

1. Run `git fetch` to update all remote refs.
2. Force-update the local `master` branch to match `origin/master`:
   ```bash
   git branch -f master origin/master
   ```
3. Report the new `master` commit hash:
   ```bash
   git rev-parse master
   ```
