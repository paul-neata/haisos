# Review: tools--jq-json (PR #86)
- Verdict: merged
- Merged as: 607c60c, version 0.5.44
- Tokens: claude 353116, ollama input 516937, ollama output 259825

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding (nothing from jq, nlohmann/json or a dtoa). Every expected output confirmed on jq 1.7.1.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in d7ecab7 | src/components/BuiltinCommands/commands/jq/JqJsonReader.cpp | `nan` read as null and `infinity` as 1.797e308; jq 1.7.1 reads a NaN number and a real infinity |
| high | fixed in d7ecab7, c7e12f4 | tests/unit/components/Jq.unittests | many planned cases missing (invalid tokens, error table, line counting across feeds, value delivery, RepairUtf8, number pairs) |
| medium | fixed in f7eed0b | src/components/BuiltinCommands/commands/jq/CLAUDE.md | said the reader accepts comments (neither it nor jq 1.7.1 does) |
| medium | commented | tests/unit/components/Jq.unittests/JqJsonWriterTest.cpp:55 | `0.0 / 0.0` does not compile on MSVC (C2124) -- the Windows CI failure; use quiet_NaN() |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqJsonReader.h | signatures differ from the plan (JsonReader output vector in the constructor, Feed/Finish/Error; WriteJson out-parameter; QuoteJsonString const std::string&) |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqValue.cpp:94 | objects have no key index: building a large object member by member is quadratic |
| medium | commented | src/components/BuiltinCommands/commands/jq/JqJsonReader.cpp:121 | the duplicate-key check scans every member per key: O(n^2) |
| low | commented | src/components/BuiltinCommands/commands/jq/JqValue.cpp:208 | dead size comparison, misleading comment |
| low | commented | src/components/BuiltinCommands/commands/jq/JqValue.h:81 | a comment points to "the plan's number layout" |
| low | commented | tests/unit/components/Jq.unittests | suites named JqValue/JqJsonWriter/JqJsonReader, not ...Test |
| low | commented | src/components/BuiltinCommands/commands/jq/CLAUDE.md | colour text says "bold white"; 1;39 is bold in the default colour |
