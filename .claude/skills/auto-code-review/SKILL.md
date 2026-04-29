---
name: auto-code-review
description: Run the full /code-review skill, then automatically implement its findings via fresh agents, and commit the resulting changes.
---

You are the auto-code-review orchestrator. You will run the `/code-review` skill unconditionally, automatically implement the actionable findings it produces by spawning dedicated agents, and commit any resulting changes.

## Steps

### 1. Run the code review

Invoke the `/code-review` skill. Wait for it to complete and capture the full consolidated findings list.

### 2. Filter actionable findings

Parse the findings and keep only items that can be turned into concrete code changes (e.g., fix a typo, refactor duplicated logic, add a missing include, replace a raw pointer with a smart pointer, add input validation). Discard purely architectural or debatable suggestions that cannot be implemented in a single focused edit.

### 3. Implement findings automatically

For each actionable finding, spawn a **fresh** agent in parallel with a precise prompt. Use one agent per finding. The prompt must include the exact finding text and instruct the agent to make only the minimal edit required to address that specific item.

Example prompt for an agent:
```
Implement the following code-review finding:
[finding text]

Steps:
1. Locate the relevant file(s) and line(s).
2. Make the minimal edit needed to fix the issue described above.
3. Do NOT change anything unrelated.
4. Report what file(s) you modified and a brief description of the change.
```

Wait for all implementation agents to finish.

### 4. Build and verify (if changes were made)

If any agent reported modifications:

1. Invoke the `/build` skill for the current platform.
2. If the build succeeds, invoke the `/test` skill for the current platform and all test types (`*`).
3. If the build or tests fail, diagnose the failure, edit the relevant source files to fix it, and re-invoke `/build` and `/test` until they pass.

### 5. Commit automatically

Check whether there are now pending changes:

```bash
git status --short
```

If there are changes:

1. Derive a one-line summary of what was done from the diff stat. Keep it under 72 characters.

2. Build the commit message body from the implemented review findings (top 3-5 items):

   ```
   Code review findings:
   - [security] <item>
   - [quality] <item>
   - [performance] <item>
   ```

3. Combine into the final message:

   ```
   <One-line summary>

   <Body with review findings>
   ```

4. Stage all changes and commit without confirmation:
   ```bash
   git add -A
   cat > /tmp/auto_cr_commit_msg.txt <<'EOF'
   <generated commit message>
   EOF
   git commit -F /tmp/auto_cr_commit_msg.txt
   ```

5. Report the commit hash (`git rev-parse HEAD`) and the full commit message.

If there are no pending changes after implementation, report the review findings and note that no commit was needed because nothing was modified.

## Constraints

- Let `/code-review` produce findings first; do not pre-filter before it runs.
- Spawn one fresh agent per actionable finding. Do not try to implement everything yourself.
- Each agent must make only the minimal edit required for its assigned finding.
- Keep the commit message first line under 72 characters.
- Do not ask for confirmation before committing. Proceed autonomously.
