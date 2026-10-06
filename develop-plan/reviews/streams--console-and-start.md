# Review: streams--console-and-start (PR #21)
- Verdict: merged
- Merged as: 67e02fd, version 0.4.3
- Tokens: claude 294461, ollama input 181244, ollama output 54354

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | tests/mocks/MockFileDescriptor.h:33 | Forced write results return before `++m_writeCalls`, so `WriteCalls()` misses failed writes despite its comment; `m_writtenCalls` is never read |
| low | commented | src/components/BuiltinCommands/BuiltinProcess.cpp:100 | `internal error` line is one raw `Write` without the partial-write loop; other builtin messages use `BuiltinContext::WriteAll` |
| low | commented | CLAUDE.md:455 | Console row of the component table does not mention the console descriptors |
| low | commented | CLAUDE.md:269 | `RUN -i` wording "(only in front of the path, in front of any program)" is awkward |
| low | commented | tests/unit/components/Console.unittests/ConsoleTest.cpp:100 | `WriteAndWriteErrorDoNotAddNewlines` is a smoke test, but its name claims a check it cannot make |
