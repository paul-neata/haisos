# Review: hsh--executor (PR #35)
- Verdict: merged
- Merged as: a4f9a29, version 0.4.17
- Tokens: claude 334981, ollama input 194896, ollama output 65930

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | tests/unit/components/Hsh.unittests/HshShellTest.cpp:214 | StopEndsTheShellAndItsChild waits a fixed 200 ms before TriggerStop: on a slow runner the stop arrives before /spin.lua starts and the test passes without testing the stop reaching the child; poll GetRunningProcesses until /spin.lua is listed |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshInvocation.cpp:573 | After `o` in a cluster of option letters the rest is dropped (`break`); dash keeps parsing (`-oe errexit`); `continue`, plus a test |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:426 | After the 5 s grace, a child that ignored TriggerStop is left running, nothing logged; LogWarning with pid and path |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:255 | For a child command the read-only check runs before the assignment value is expanded; dash expands first |
