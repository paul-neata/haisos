---
name: develop-close
description: Close the develop -- the one step into master, run by the user after looking at the develop PR. Checks the develop is done and nothing is left open, merges a moved master into develop, keeps the summary, deletes develop-plan/ (commit, push), waits for the checks, squash-merges the develop PR into master as "[M.m] <title>" with its summary, deletes develop, and leaves both clones on master, ready for the next /develop-create.
args:
  - name: options
    description: "Optional: --force to close even with tasks not done (they are listed in the merge message as dropped)"
    required: false
---

Merge the develop into `master`. Invoking this skill is the user's instruction
to commit and push on `develop`, to merge the develop PR into `master`, and to
delete `develop` -- the only skill that touches `master`. See
`.claude/develop/WORKFLOW.md`.

## Steps

### 1. Check that it can close

```bash
git rev-parse --abbrev-ref HEAD          # develop, in this clone
git status --porcelain                   # clean
bash scripts/develop/update.sh
bash scripts/develop/state.sh
gh pr list --base develop --state open --json number,headRefName,title
gh pr list --base master --head develop --state open --json number,title,isDraft,url
docker ps --filter name=haisos-develop- --format '{{.Names}}'
```

Stop and report when:

- a task PR into `develop` is still open -- once `develop` is deleted, GitHub
  retargets open PRs based on it to `master`: merge or close them first;
- there is no open develop PR into `master`, or it is still a draft / `WIP`
  (the playbook's `Phase` is not `done`) -- `/develop-implement` makes it
  ready when the develop is done;
- a task container is running, or the implement session is still looping
  (ask the user to stop it);
- the playbook has tasks not `done` or `obsolete` -- unless `--force`; then
  list them in the merge message as dropped.

### 2. Bring `master` in, if it moved

```bash
git merge-base --is-ancestor origin/master origin/develop || git merge --no-edit origin/master
```

On a conflict in `HAISOS_VERSION`: `master`'s version plus one minor, patch
0. Any other conflict: stop and ask the user. Push (`git push origin
develop`) when a merge was made.

### 3. Keep the summary, then delete `develop-plan/`

The squash commit's message is rendered now, while the plan files exist:

```bash
bash scripts/develop/develop_pr.sh summary > "$HOME/.haisos-develop/close-message.md"
VERSION=$(tr -d '[:space:]' < HAISOS_VERSION)
git rm -r -q develop-plan
bash scripts/develop/update.sh -m "Close develop $VERSION: remove develop-plan" --no-pr
```

The develop PR's description is not re-rendered any more: it keeps the whole
dashboard -- tasks, memory, log -- as it was at the end. The plan files stay
in the PR's history; the open findings are in
`notes/note-<M.m>-review-findings.md`.

### 4. Wait for the checks of this last commit

In the background, waiting for the notification:

```bash
bash scripts/develop/wait_ci.sh <develop PR number> --timeout-min 60
```

Not green: stop and report (nothing is merged).

### 5. Merge into `master`

Squash, as every PR into `master` -- one commit per develop: its subject is
the PR's title (`[M.m] <title>`), its message the summary of step 3:

```bash
SHA=$(gh pr view <n> --json headRefOid -q .headRefOid)
TITLE=$(gh pr view <n> --json title -q .title)
gh pr merge <n> --squash --match-head-commit "$SHA" \
    --subject "$TITLE (#<n>)" --body-file "$HOME/.haisos-develop/close-message.md"
gh pr view <n> --json state,mergeCommit -q '"\(.state) \(.mergeCommit.oid)"'
```

With `--force`, add a line to the message file first: `Dropped tasks: <ids>`.

### 6. Delete `develop` and clean up

The repository deletes a merged PR's branch; check, and delete it only if it
is still there:

```bash
git ls-remote --heads origin develop
# still listed: git push origin --delete develop
git switch master
git pull -q --ff-only origin master
git branch -D develop
git config --remove-section branch.develop 2>/dev/null || true
for b in $(git for-each-ref --format='%(refname:short)' 'refs/heads/task/*'); do
    gh pr list --head "$b" --state merged --json number -q '.[0].number' | grep -q . && git branch -D "$b"
done
git worktree prune
rm -rf ~/.haisos-develop/review
```

The Windows worktree (`bash scripts/develop/windows.sh path`) and the
container workspace stay: the next develop builds incrementally from them.
The run folders stay under `~/.haisos-develop/runs/<develop id>/`.

### 7. Report

The merge commit on `master`, the develop PR, and that the other session's
clone must leave `develop` too (`git switch master && git pull && git branch
-D develop`). Next develop: `/develop-create`.
