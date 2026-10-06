# Review: fd--process-table (PR #20)
- Verdict: merged
- Merged as: 42cec68, version 0.4.2
- Tokens: claude 248684, ollama input 117273, ollama output 30383

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 4295cd8 | tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp:556 | OpenFileHandsBackAnUnnumberedDescriptor added descriptors to the table of a short-lived `script.lua` process whose ReleaseAllDescriptors could empty slot 0 before `GetDescriptor(0)->Write` (racy null dereference); the test now waits for the script to finish |
| medium | commented | tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp:13, HaisosOSTest.cpp:161, BuiltinCommands.unittests/BuiltinCommandsTest.cpp:65 | ReleaseCountingDescriptor fake duplicated three times; belongs in one tests/mocks/ header |
| low | commented | src/components/BuiltinCommands/commands/Cat.cpp:217 | Comment "the table needs no Close" implies the descriptor was in the table; it never is |
