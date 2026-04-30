---
name: code-review-logs
description: Review pending changes for missing or inappropriate log messages across all LogLevel types. Proposes only — never edits.
---

Review the pending changes on the current branch for missing, misplaced, or inadequate log messages. Consider all files in the diff, not just those touching the Logger component.

## LogLevel Guidelines

The project uses `LogLevel` enum (ordered lowest to highest severity):

| Level | Usage | Examples |
|-------|-------|----------|
| **VerboseDebug** | Extra-verbose only. Never for normal debugging. | Dumping raw HTTP request/response JSON, agent message-passing payloads, full buffer contents. |
| **Debug** | Before/after significant operations, state transitions. | Before/after tool calls, before/after LLM API requests, factory creation, agent start/stop. |
| **Trace** | Function entry/exit, loop progress, flow tracking. | Entering `ProcessMessage()`, loop iteration N of M, queue depth changes. |
| **Info** | Important lifecycle and configuration events. | Configuration loaded, component initialized, connection established, file opened. |
| **Warning** | Assert-like conditions, recoverable errors, unexpected inputs. | Null/empty where value expected, fallback triggered, retry attempted, deprecated API used. |
| **Error** | Fatal or unrecoverable failures, invariant violations. | Exception caught, allocation failed, HTTP error response, tool execution failed, parse error. |

## What to Look For

1. **Missing logs at Error level** around:
   - Exception catch blocks
   - Failed allocations or resource creation
   - HTTP/client errors
   - Parse/deserialization failures
   - Tool execution failures

2. **Missing logs at Warning level** around:
   - Fallback/default-value usage
   - Unexpected/null/empty inputs that are handled gracefully
   - Retries
   - Configuration overrides

3. **Missing logs at Info level** around:
   - Component construction / destruction
   - Connection open/close
   - File I/O open/close
   - State transitions that affect external behavior

4. **Missing logs at Debug level** around:
   - Before/after tool calls
   - Before/after LLM API requests/responses
   - Significant internal state changes (agent started, message queued, etc.)

5. **Missing logs at Trace level** around:
   - Function entry and exit in non-trivial methods
   - Loop iterations in processing loops
   - Queue push/pop operations

6. **Missing logs at VerboseDebug level** around:
   - Raw JSON payloads sent/received over HTTP
   - Full message dumps in agent communication
   - Complete buffer/vector contents

7. **Misplaced severity:**
   - VerboseDebug used for normal debug info → downgrade to Debug
   - Debug used for raw JSON dumps → upgrade to VerboseDebug
   - Info used for trivial loop iterations → downgrade to Trace
   - Warning used for expected behavior → downgrade to Info or Debug

8. **Redundant or noisy logs:**
   - Duplicate Error logs for the same failure path
   - VerboseDebug in hot loops without gating
   - Info-level logs that fire on every iteration

## Output Format

NEVER edit files. Only list numbered proposals prefixed with `[logs]`, ordered by importance (missing Error/Warning first, then misplaced severity, then missing lower-severity logs). For each proposal include:
- File path and approximate line number
- Suggested log level
- Suggested log message or pattern
- Brief rationale
