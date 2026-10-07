# Review: final--review-fixes (PR #41)
- Verdict: merged
- Merged as: af6f65c, version 0.4.23
- Tokens: claude 166629, ollama input 299330, ollama output 56331

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshBuiltinCd.cpp:167-177 | With OLDPWD read-only, dash 0.5.12 reports it and stops (PWD not assigned; with both read-only one line only); hsh goes on to assign PWD and reports both (as planned, unlike dash); return 2 right after the first read-only report |
| medium | commented | src/components/HaisosOS/AgentInputLoop.cpp:70, AgentInputLoop.h:21,35 | Still cite "D7", a goal.md item (goal.md is deleted before master); missed by the plan's list and acceptance grep |
| low | commented | tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp:138 | New CreatePipe test also passes on the old rollback code (the race is invisible in one thread); it only pins the outcome |
| low | commented | tests/unit/components/Hsh.unittests/HshUnboundedPipeTest.cpp:67; HshControlFlowTest.cpp:250-252 | "would take minutes" overstates it (seconds); one comment wraps oddly |
