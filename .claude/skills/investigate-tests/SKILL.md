---
name: investigate-tests
description: Run tests, investigate failures with debug logging, fix issues, and confirm all tests pass
---

Run the project tests using `/test`, and if any tests fail, investigate each failure by enabling verbose debug logging to a temporary file, re-running the failing test, analyzing the logs, fixing the underlying code issue, and verifying the fix.

**Arguments:** [platform] [selector] [options] [filter]

Same as `/test`.

## Steps

1. Run `/test` with all provided arguments.
2. If all tests pass, report success and stop.
3. If tests fail, collect the list of failed test names from the output.
4. For each failed test:
   a. Generate a temporary log file path (e.g., `/tmp/investigate_<testname>_<timestamp>.log`).
   b. Export environment variables in the shell before re-running:
      ```bash
      export HAISOS_TEST_LOG_LEVEL=verbose_debug
      export HAISOS_TEST_LOG_FILE=/tmp/investigate_<testname>_<timestamp>.log
      ```
   c. Re-run only the failing test using `/test` with a filter for just that test name and the same platform/selector/options.
      - Example: if `AgentStart.integrationtest` failed on Linux integration tests, run `/test L I AgentStart`.
   d. Read the temporary log file to understand why the test failed.
   e. If the failure is due to a code issue, fix the code.
   f. If code was changed, run `/build` for the current platform to recompile.
   g. Re-run the specific failing test again to verify it passes. Do **not** run all tests at this stage.
   h. If it still fails, repeat investigation until it passes or the root cause is clearly identified.
5. After all originally failing tests have been investigated and fixed, run `/test` again with the original arguments to confirm that fixing one test did not break any others.
6. Report the final results: which tests were fixed, which remain failing (if any), and the path to any relevant log files.

## Environment Variables

The following environment variables are respected by tests and the haisos executable:

- `HAISOS_TEST_LOG_LEVEL` — Sets the minimum log level (`verbose_debug`, `debug`, `trace`, `info`, `warning`, `error`).
- `HAISOS_TEST_LOG_FILE` — Path to a file where logs will be appended.

When investigating, always set `HAISOS_TEST_LOG_LEVEL=verbose_debug` and `HAISOS_TEST_LOG_FILE` to a temporary file path before re-running a failing test.

## Important Notes

- Only run the specific failing test during investigation, not the entire test suite.
- After compiling fixes, only run the specific test that was fixed.
- Always run the full test suite with original arguments at the end to confirm no regressions.
- Do not automatically commit changes.
