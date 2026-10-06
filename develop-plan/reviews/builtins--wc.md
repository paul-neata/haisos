# Review: builtins--wc (PR #28)
- Verdict: merged
- Merged as: 23e7ccb, version 0.4.10
- Tokens: claude 284222, ollama input 137113, ollama output 57447

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in ad3978b | tests/unit/components/BuiltinCommands.unittests/WcTest.cpp | Nothing tested a multibyte character split across the 16 KiB read chunks (the carry in Wc.cpp:65-76); added WcCharacterSplitAcrossReadChunksCountsOnce |
| medium | commented | src/components/BuiltinCommands/commands/wc/Wc.cpp:428 | An empty --files0-from list returns before the total rule, so --total=always/only print nothing; GNU 9.4 still prints the total line (0 0 0 total / 0 0 0) |
| low | commented | src/components/BuiltinCommands/commands/wc/Wc.cpp:185,476 | A read error on operand `-` is reported as 'standard input'; GNU names it `-` |
| low | commented | src/components/BuiltinCommands/commands/wc/Wc.cpp:65 | WcFeed copies every 16 KiB chunk into a new string just to prepend a carry of at most 3 bytes |
| low | commented | src/components/BuiltinCommands/commands/wc/Wc.cpp:457-488 | The loop inside `else {` is not indented |
| low | commented | interfaces/IBuiltinCommands.h:32 | Rewrapped class comment has an overlong line |
