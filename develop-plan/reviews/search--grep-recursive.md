# Review: search--grep-recursive (PR #58)
- Verdict: merged
- Merged as: f2d3be1, version 0.5.16
- Tokens: claude 215775, ollama input 374769, ollama output 154779

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in ea3ae75 | src/components/BuiltinCommands/commands/grep/GrepFile.cpp:266 | -m N with trailing context printed a matching context line as a match (`5:five TODO`); GNU 3.11 prints it as context (`5-five TODO`); the test asserted the wrong output |
| high | fixed in ea3ae75 | src/components/BuiltinCommands/commands/grep/GrepFile.cpp:205 | -o printed matches of any context line; GNU prints them only in lines where selected XOR -v |
| high | fixed in ea3ae75 | src/components/BuiltinCommands/commands/grep/Grep.cpp:497 | -d lower-cased its value, so `-d READ` was accepted; GNU's argmatch is case-sensitive |
| medium | commented | src/components/BuiltinCommands/commands/grep/Grep.cpp:913 | after a -q match inside a recursive walk, later file operands are still opened and searched (exit status still right); fix: `if (stopped) break;` after the walk |
| medium | commented | src/components/BuiltinCommands/commands/grep/Grep.cpp:845 | the walk recurses on the stack per directory level; a symlink loop on a physical FS recurses until the host's ELOOP/ENAMETOOLONG |
| low | commented | src/components/BuiltinCommands/commands/grep/Grep.cpp:975 | no GREP_COLOR deprecation warning when GREP_COLORS sets only ms or only mc |
| low | commented | src/components/BuiltinCommands/commands/grep/Grep.cpp:356 | the -NUM digit accumulation can wrap at the size_t boundary |
| low | commented | src/components/BuiltinCommands/BuiltinFnmatch.cpp:244 | each bracket is re-parsed (two vectors allocated) for every byte tried against it |
