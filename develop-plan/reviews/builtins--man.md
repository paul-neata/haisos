# Review: builtins--man (PR #29)
- Verdict: merged
- Merged as: 68a6ceb, version 0.4.11
- Tokens: claude 260719, ollama input 95984, ollama output 26957

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| low | commented | src/components/BuiltinCommands/commands/man/Man.cpp:39 | `isdigit` used without `#include <cctype>` (compiles only through another header) |
| low | commented | src/components/BuiltinCommands/commands/man/Man.cpp:142 | Help note says -f searches names and summaries; -f matches names only |
| low | commented | CLAUDE.md:587 | `man` row mentions "a full page for `hsh`", which does not exist yet (drop the clause if hsh does not land in this develop) |
