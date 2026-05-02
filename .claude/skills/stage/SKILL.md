---
name: stage
description: Stage all current changes in the working tree.
---

Stage all pending changes (new, modified, and deleted files).

## Steps

1. Check the working tree status:
   ```bash
   git status --short
   ```
   If there are no changes, tell the user "Nothing to stage." and stop.

2. Stage all changes:
   ```bash
   git add -A
   ```

3. Show the staged files and confirm:
   ```bash
   git status --short
   ```
