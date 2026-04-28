---
name: create-pr
description: Create a new GitHub PR for the current branch using gh CLI and propose-pr-description skill. Only works if no PR already exists.
---

Create a new GitHub pull request for the current branch.

## Steps

1. Determine the current branch name:
   ```bash
   CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
   ```

2. Check if a GitHub PR already exists for this branch:
   ```bash
   gh pr view --json number 2>/dev/null
   ```
   - If the command succeeds and returns a PR number, a PR already exists. Stop and inform the user: "A PR already exists for branch <branch>. Use `/update-pr-description` to update it."

3. Detect the base branch for the PR. Try in order:
   - From git upstream tracking:
     ```bash
     UPSTREAM=$(git rev-parse --abbrev-ref @{upstream} 2>/dev/null)
     BASE_BRANCH=${UPSTREAM#origin/}
     ```
   - From git config:
     ```bash
     BASE_BRANCH=$(git config branch.$CURRENT_BRANCH.merge 2>/dev/null | sed 's|refs/heads/||')
     ```
   - From common default branches:
     ```bash
     for CANDIDATE in master main; do
         if git show-ref --verify --quiet refs/remotes/origin/$CANDIDATE 2>/dev/null || \
            git show-ref --verify --quiet refs/heads/$CANDIDATE 2>/dev/null; then
             BASE_BRANCH=$CANDIDATE
             break
         fi
     done
     ```
   - If still undetected, stop and ask the user to specify the base branch.

4. Invoke the `/propose-pr-description` skill to generate the PR title and description.

5. Read the generated file at `/tmp/${CURRENT_BRANCH//\//_}.pr.md`.

6. Parse the file contents:
   - The title is the line after `PR Title: `.
   - The description starts after the `PR Description:` line and continues to the end of the file.

7. Create the PR using `gh pr create`:
   ```bash
   gh pr create --base "$BASE_BRANCH" --title "$PR_TITLE" --body "$PR_DESCRIPTION"
   ```
   - Handle multiline description appropriately (e.g. pipe through stdin or use a variable that preserves newlines).

8. Confirm success to the user with the new PR URL.
