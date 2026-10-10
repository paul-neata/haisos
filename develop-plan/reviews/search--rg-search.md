# Review: search--rg-search (PR #69)
- Verdict: merged
- Merged as: 4f6c3f6, version 0.5.27
- Tokens: claude 201774, ollama input 482740, ollama output 181372

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 41454b6 | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:356 | the first stdin read (which decides stdin is searched) was never checked for a NUL: piped binary printed raw instead of "binary file matches (...)" (RgBinary) |
| high | fixed in 41454b6 | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:408 | stdin ending in an unterminated line: -m N / -l went on after the stop and printed an extra match (RgStdin) |
| low | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:692 | "No files were searched" printed even with --no-messages (rg suppresses it, exit 2 stays) |
| low | commented | src/components/BuiltinCommands/commands/rg/Rg.cpp:518 | every -f FILE open failure is "No such file or directory (os error 2)", a directory included; should use OpenFailureText(failure) |
| low | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:613 | an unreadable directory during the walk is skipped silently; rg reports it and exits 2 |
