# Review: awk--lexer (PR #76)
- Verdict: merged
- Merged as: 4cae914, version 0.5.34
- Tokens: claude 324205, ollama input 328739, ollama output 128141

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 77c1fa4 | tests/unit/components/Awk.unittests/AwkCommandTest.cpp:65 | OptionsStopAtTheProgram passed "BEGIN{} -x --lint" as one argument, so it could never fail; now separate arguments, plus "-- BEGIN{} -x" |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkLexer.cpp:459 | a lone `&` reported "invalid char '&' in expression"; gawk --posix says "syntax error"; undocumented |
| medium | commented | src/components/BuiltinCommands/commands/awk/Awk.cpp:123 | --help notes overstate two exceptions: gawk also says "invalid char" for other stray bytes (only `@` differs); gawk --posix accepts backslash-newline in a regex (only strings refused) |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkLexer.cpp:265 | a comment ending in a backslash gets an extra NL token, EOF on line 2 column 0 (wrongly adds a final newline) |
| low | commented | src/components/BuiltinCommands/BuiltinCommandList.h:68 | CreateAwkCommand() before `[` (plan: after); harmless, a map |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkLexer.cpp:211 | comment says nullptr, returns TokenKind::Name |
| low | commented | tests/unit/components/Awk.unittests/AwkLexerTest.cpp | untested: `[.`/`[=` bracket items, a byte >= 0x80 as invalid char, backslash-newline in a regex |
| low | commented | tests/unit/components/Awk.unittests/CMakeLists.txt:7 | no final newline |
