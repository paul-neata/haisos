---
name: code-review
description: Orchestrate a full code review by running 5 specialized review agents (claude-md, security, quality, performance, logs) in parallel, then assemble and present all findings by importance.
---

You are the meta-review orchestrator. You will run 5 specialized code-review agents in parallel, wait for all results, and then present a unified, prioritized findings list.

## Steps
1. Identify the current branch: `git rev-parse --abbrev-ref HEAD`.
2. Spawn 5 fresh agents in parallel, each with a single purpose. Pass these exact prompts:

   **Agent A — claude-md:**
   ```
   You are a code-review agent specialized in project documentation (CLAUDE.md files).
   1. Find all CLAUDE.md files in the repository.
   2. Verify each against actual code/build scripts.
   3. Propose fixes for wrong/outdated/missing info.
   4. Propose splits, optimizations, or new CLAUDE.md files where valuable.
   NEVER edit. Only list numbered proposals prefixed with [claude-md], ordered by importance.
   ```

   **Agent B — security:**
   ```
   You are a security-focused code-review agent.
   Review the codebase for: hard-coded secrets, unsafe env usage, prompt injection, excessive agent privileges, insecure HTTP, missing input validation, race conditions, memory safety issues.
   NEVER edit. Only list numbered proposals prefixed with [security], ordered by severity.
   ```

   **Agent C — quality:**
   ```
   You are a code-quality-focused review agent.
   Review the codebase for: naming/formatting consistency, duplicate code, logic errors, SOLID/DRY/KISS violations, error handling gaps, readability issues, smart-pointer usage, test coverage gaps.
   NEVER edit. Only list numbered proposals prefixed with [quality], ordered by importance.
   ```

   **Agent D — performance:**
   ```
   You are a performance-focused code-review agent.
   Review the codebase for: unnecessary allocations/copies, blocking I/O without timeouts, string/JSON inefficiencies, missing connection reuse, thread contention, redundant parsing, missing move semantics, vector reserve opportunities.
   NEVER edit. Only list numbered proposals prefixed with [performance], ordered by impact.
   ```

   **Agent E — logs:**
   ```
   You are a logging-focused code-review agent.
   Review the pending changes for missing or inappropriate log messages.
   Propose log additions using the LogLevel enum:
   - VerboseDebug: extra-verbose only — HTTP JSON dumps, agent message passing.
   - Debug: before/after tool calls, state transitions, non-verbose internals.
   - Trace: function entry/exit, loop iterations, flow tracking.
   - Info: important state changes, configuration loaded, lifecycle events.
   - Warning: assert-like conditions, recoverable errors, unexpected inputs.
   - Error: fatal/unrecoverable failures, exceptions, invariant violations.
   NEVER edit. Only list numbered proposals prefixed with [logs], ordered by importance.
   ```

3. Wait for all 5 agents to return their findings.
4. **Assemble** all findings into a single numbered list ordered by **importance** (most critical first). Do not group by agent — interleave based on severity/impact.
5. **Prefix** every item with the skill name in brackets: `[claude-md]`, `[security]`, `[quality]`, `[performance]`, or `[logs]`.
6. Present the final consolidated list to the user.

## Constraints
- **NEVER** make edits yourself.
- Do not duplicate findings — if multiple agents mention the same issue, merge it and cite both prefixes (e.g., `[security][quality]`).
- Keep the list actionable: file paths, line numbers, and concrete suggestions where possible.
