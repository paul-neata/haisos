---
name: develop-status
description: Show the develop in progress, read-only -- the version and goal, the playbook's progress, open questions, the task PRs and branches on GitHub, the develop PR, and the task container if one is running. Changes nothing but the remote-tracking refs.
---

Show where the develop stands, without changing anything (see
`.claude/develop/WORKFLOW.md` for what the parts mean).

## Steps

1. The state:
   ```bash
   bash scripts/develop/state.sh
   ```
2. If a container is running, what it is on: the newest run folder and its
   log's last lines:
   ```bash
   RUN=$(ls -td ~/.haisos-develop/runs/*/*/*/ 2>/dev/null | head -n 1)
   echo "$RUN"; tail -n 3 "$RUN/container.log" 2>/dev/null
   ```
3. Print both as they are, then at most three lines of your own: what is
   running or next, what is blocked and why, and the questions waiting for
   `/develop-plan answer`.

Do not commit, push, pull, switch branches or start anything.
