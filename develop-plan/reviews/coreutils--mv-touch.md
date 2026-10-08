# Review: coreutils--mv-touch (PR #59)
- Verdict: merged
- Merged as: 2be9adb, version 0.5.17
- Tokens: claude 378851, ollama input 265414, ollama output 112948

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 3bcca84 | src/components/BuiltinCommands/CMakeLists.txt | BuiltinDate.cpp, Mv.cpp and Touch.cpp were never compiled (the first run, cut short by Ollama's usage limit, left them out) |
| high | fixed in 3bcca84 | src/components/BuiltinCommands/BuiltinCommandList.h | mv and touch were not registered (nor in the --init template) |
| high | fixed in 3bcca84 | src/components/BuiltinCommands/BuiltinDate.cpp:364 | a signed number counted as a zone after any earlier time, not only directly after it (test ZoneOnlyDirectlyAfterTime) |
| high | fixed in 4fe3532 | tests/unit/components/BuiltinCommands.unittests/ | no tests: BuiltinDateTest, MvTest, TouchTest, the CpTest review-fix tests and the builtin list test added |
| high | fixed in 2f750b2 | CLAUDE.md, src/components/BuiltinCommands/CLAUDE.md | docs had no mv/touch rows and no BuiltinDate entry |
| medium | commented | src/components/BuiltinCommands/BuiltinDate.cpp:338 | after a zone attached to a T-time, previousWasTime stays true, so `2024-01-02T03:04Z +1 hour` fails; GNU adds one hour |
| medium | commented | src/components/BuiltinCommands/commands/touch/Touch.cpp:95-108 | error order differs from GNU: missing operand is checked before the time-source errors |
| medium | commented | src/components/BuiltinCommands/commands/mv/Mv.cpp:133-176 | a backup made before a cross-device copy is not shown by -v and not restored when the move fails |
| low | commented | src/components/BuiltinCommands/commands/touch/Touch.cpp:50 | help note "times are set to the second" is untrue: -d keeps nanoseconds |
