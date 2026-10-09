# Review: search--sed-core (PR #67)
- Verdict: merged
- Merged as: d3d1f13, version 0.5.25
- Tokens: claude 234888, ollama input 536379, ollama output 291535

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedExecutor.cpp:353 | `addr,~0` divided by zero (SIGFPE, crashes haisos) |
| high | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedExecutor.cpp:323 | a range never started again after ending (`/a/,/b/p` matched only the first block) |
| high | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedExecutor.cpp:331 | `addr,+0` / `addr,~0` did not end on the line that started them |
| high | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedParser.cpp:499 | backslash-newline dropped instead of kept as a newline |
| high | fixed in 51a11d4, 1a2560f | tests/unit/components/BuiltinCommands.unittests/SedTest.cpp | many planned tests and error-table rows missing (acceptance scenario 2 among them) |
| medium | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedParser.cpp:360 | address regex used its own scanner (`/a\\/p`, `/[/]/p` failed); now ScanDelimited |
| medium | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedParser.cpp:524 | bracket tracking broke on `[[:alpha:]/]` and a leading `]` |
| medium | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedParser.cpp:84 | `\dNNN` required exactly 3 digits (GNU takes 1-3) |
| low | fixed in 51a11d4 | src/components/BuiltinCommands/commands/sed/SedParser.cpp:826 | `{p;1}` wrong message (GNU: "`}' doesn't want any addresses") |
| medium | commented | src/components/BuiltinCommands/commands/sed/SedParser.cpp:711 | `s///w FILE` accepted but ignored (plan: "unknown option" for now); its filename stops at `;`/`}`/blank, GNU's runs to end of line |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/SedTest.cpp:611 | SedIsStoppedPromptly reads a big file, not a held-open stdin pipe: can race |
| medium | commented | src/components/Regex/RegexParser.cpp:490 | the M flag change (REG_NEWLINE: `.` and `[^...]` no longer match `\n`) is outside the plan's files and has no Regex unit test |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/SedTest.cpp | the plan's `/[/p` row (char 4) untested |
| low | commented | src/components/BuiltinCommands/commands/sed/SedParser.cpp:333 | ParseNumber saturates at 1e9: a huge `q N` exits with the wrong code |
