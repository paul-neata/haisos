# Review: awk--values (PR #79)
- Verdict: merged
- Merged as: 036998c, version 0.5.37
- Tokens: claude 277418, ollama input 135572, ollama output 44626

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding; every planned declaration, test and doc present.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkInput.cpp:24 | `m_buffer.erase(0, pos+1)` per record moves up to 64 KiB each time (quadratic on short lines), separator search restarts at 0: keep a start offset |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkFields.cpp:114 | a rebuilt $0 after a field/NF assignment is a strnum; gawk --posix: a string (`$1=12; print ($0<9)` prints 1) |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkFields.cpp:143 | SetField fixes CONVFMT at assignment; gawk uses CONVFMT at rebuild (`$2=3.5; CONVFMT="%.2f"; print` -> `a 3.50`) |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkFields.cpp:152 | SetNF grows without limit: `NF=1e12` throws bad_alloc, not an AwkFatal |
| low | commented | tests/unit/components/Awk.unittests/AwkValueTest.cpp:333 | a test comment does not match the test; the plan's "caller changed FS" case cannot be told apart |
