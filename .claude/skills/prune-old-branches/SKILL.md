---
name: prune-old-branches
description: Delete local branches whose every commit is already on origin/master by exact hash; list the rest with the command that deletes them by hand.
---

Remove local branches that are provably merged into the base branch exactly as they are, and report every other branch without touching it.

A branch counts as merged **only** when its tip commit is an ancestor of `origin/master` -- so each of its commits is on master with the same hash. Same content under different hashes (a squash merge, a rebase merge, a cherry-pick) is **not** proof: those branches are kept and reported. Remote branches are never touched.

## Steps

### 1. Run the prune script

```bash
./scripts/prune_old_branches.sh master
```

The script fetches `origin/master`, then for each local branch other than `master`/`main`, the current branch, and any branch checked out in a worktree:

- if `git merge-base --is-ancestor <branch> origin/master` holds, deletes it (`git branch -D`, safe here because the check has proved nothing would be lost);
- otherwise, keeps it and prints how many of its commits are not on `origin/master`, followed by the command that deletes it.

### 2. Report

Relay the script's output to the user:

- **Pruned**: the branches deleted, with the commit each pointed at (so one can be recreated with `git branch <name> <hash>` if needed).
- **Kept**: each branch the skill would not delete, each followed by the exact command to delete it by hand, e.g.:
  ```
  git branch -D task/some_branch
  ```
  Do **not** run those commands yourself; deleting a kept branch is the user's decision.
- **Skipped**: the current branch and any branch checked out in another worktree, if present.

If the script fails (e.g. `origin/master` cannot be fetched), report the error and stop. Nothing will have been deleted.
