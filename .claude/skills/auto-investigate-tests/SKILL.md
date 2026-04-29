---
name: auto-investigate-tests
description: Automatically investigate failing tests and commit any fixes
---

Run `/investigate-tests` with the provided arguments, automatically commit any code changes made during investigation, and report the results.

**Arguments:** [platform] [selector] [options] [filter]

Same as `/test` and `/investigate-tests`.

## Steps

1. Run `/investigate-tests` with all provided arguments.
2. After `/investigate-tests` completes, check `git status` to see if any files were modified.
3. If there are changes:
   a. Stage all modified and new files with `git add -A`.
   b. Draft a concise one-line commit message summarizing the fix.
   c. If the changes are significant (multiple files, complex fixes, or architectural changes), add an empty line after the one-liner and then a brief description of what was done on subsequent lines.
   d. Commit with `git commit -m "..."`.
   e. Report which files were changed and the commit hash.
4. If no changes were made, report that all tests passed without needing fixes.

## Commit Message Examples

- One-line fix:
  ```
  Fix AgentStart.integrationtest blocking on LLM timeout
  ```

- Fix with description:
  ```
  Fix integration test timeouts and Agent thread cleanup

  - Add timeout to Agent::WaitToFinish to prevent indefinite blocking
  - Update integration tests to use timed wait
  - Add verbose debug logging around tool execution
  ```
