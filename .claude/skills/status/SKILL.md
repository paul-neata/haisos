---
name: status
description: Show the current git status of the working tree.
---

Display the current git status in short format.

## Steps

1. Show the working tree status:
   ```bash
   git status --short
   ```

2. Also show the current branch:
   ```bash
   git rev-parse --abbrev-ref HEAD
   ```
