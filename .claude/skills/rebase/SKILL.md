---
name: rebase
description: Rebase the current branch onto its base branch. Resolve conflicts if needed, then build and test to verify correctness.
---

Rebase the current branch onto its detected base branch. If conflicts occur, resolve them automatically where possible, then build and test to ensure correctness.

## Steps

### 1. Detect the base branch

Determine the base branch using `scripts/base_branch.sh`:

```bash
BASE_BRANCH=$(./scripts/base_branch.sh)
```

- If the script fails, stop and ask the user to specify the base branch.

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

2. If `HAISOS_VERSION` is among the conflicted files, resolve it to the **next minor version after the base's**, rather than picking either side: take the version on `origin/$BASE_BRANCH`, increment `MINOR` and reset `PATCH` to 0 (e.g. base `0.3.0` becomes `0.4.0`). The branch's version was bumped from an older master, so neither side is right: the branch must land one minor version above what is on the base now.
   ```bash
   BASE_VERSION=$(git show "origin/$BASE_BRANCH:HAISOS_VERSION" | tr -d '[:space:]')
   if [[ ! "$BASE_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
       echo "HAISOS_VERSION on origin/$BASE_BRANCH is not MAJOR.MINOR.PATCH: '$BASE_VERSION'"
       exit 1
   fi
   NEW_VERSION="${BASH_REMATCH[1]}.$((BASH_REMATCH[2] + 1)).0"
   echo "$NEW_VERSION" > HAISOS_VERSION
   git add HAISOS_VERSION
   echo "HAISOS_VERSION conflict resolved: base $BASE_VERSION -> $NEW_VERSION"
   ```
   If the base's version does not match `MAJOR.MINOR.PATCH`, stop and tell the user rather than guessing. The result depends only on the base, so a later commit of the same rebase that conflicts on `HAISOS_VERSION` again resolves to the same version. If `HAISOS_VERSION` was the only conflicted file, go straight to step 4.4.

3. For each other conflicted file:
   - Read the file contents.
   - Resolve the conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`). Preserve the intended code by choosing the correct side or merging both as appropriate.
   - Write the resolved file.
   - Stage it:
     ```bash
     git add "<file>"
     ```

4. Continue the rebase:
   ```bash
   git rebase --continue
   ```

5. If new conflicts appear, repeat from step 4.1.

6. If `git rebase --continue` fails because no changes were made (e.g. all conflicts were resolved to match the incoming version), you may need to skip the empty commit:
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
