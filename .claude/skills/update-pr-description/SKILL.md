---
name: update-pr-description
description: Update the title and description of an existing GitHub PR for the current branch using gh CLI and propose-pr-description skill.
---

Update the title and description of the GitHub PR attached to the current branch.

## Steps

1. Determine the current branch name:
   ```bash
   CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
   ```

2. Check if a GitHub PR exists for this branch:
   ```bash
   gh pr view --json number 2>/dev/null
   ```
   - If the command exits with a non-zero status or outputs nothing, there is no PR for this branch. Stop and inform the user: "No GitHub PR found for branch <branch>. Use `/create-pr` to create one."

3. If a PR exists, invoke the `/propose-pr-description` skill to generate the PR title and description.

4. After `/propose-pr-description` completes, read the generated file at `/tmp/${CURRENT_BRANCH//\//_}.pr.md`.

5. Parse the file contents:
   - The title is the line after `PR Title: `.
   - The description starts after the `PR Description:` line and continues to the end of the file.

6. Update the PR using `gh pr edit`:
   ```bash
   gh pr edit --title "$PR_TITLE" --body "$PR_DESCRIPTION"
   ```
   - If the description contains newlines, pass it correctly (e.g. via a here-document or by ensuring the shell command handles multiline strings).

7. Confirm success to the user with the PR URL (obtainable via `gh pr view --json url -q .url`).
