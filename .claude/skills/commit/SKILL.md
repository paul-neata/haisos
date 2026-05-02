---
name: commit
description: Analyze current modified changes and new files, then commit them with a short, descriptive one-line message.
---

Stage all pending changes and create a commit with a concise one-line message.

## Steps

1. Check the working tree status:
   ```bash
   git status --short
   ```
   If there are no changes, tell the user "Nothing to commit." and stop.

2. Gather change context to build the message:
   ```bash
   git diff --stat
   ```
   Also inspect `git diff` for a few representative files if needed to understand intent.

3. Stage all pending changes:
   ```bash
   git add -A
   ```

4. Draft a short one-line commit message (under 72 characters) that summarizes the changes.
   Use the file paths, diff-stat, and the nature of edits to infer intent.
   Examples:
   - `Add IFileSystem interface and Filesystem component`
   - `Fix deprecated curl options in HTTPClient`
   - `Update build scripts to use parallel compilation`
   - `Refactor Agent message buffer locking`

5. Show the user the proposed message and the list of staged files, then create the commit:
   ```bash
   git commit -m "<message>"
   ```

6. Confirm success with the commit hash.
