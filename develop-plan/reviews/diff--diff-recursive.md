# Review: diff--diff-recursive (PR #73)
- Verdict: merged
- Merged as: f1b3afa, version 0.5.31
- Tokens: claude 305749, ollama input 398689, ollama output 180085

Clean-room: two identifiers matching GNU diffutils internals, taken from the plan, renamed; the logic itself is original.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in f1e4ee8 | src/components/BuiltinCommands/commands/diff/DiffDirectories.h:20 | clean room: `switchString` and `DiffDirs` matched GNU's internal `switch_string` / `diff_dirs` (from the plan); renamed `echoedOptions`, `CompareDirectoryEntries`, test DiffDirOptionEcho |
| high | fixed in f1e4ee8 | src/components/BuiltinCommands/commands/diff/DiffDirectories.cpp:103 | two devices in a -r walk compared by content: /dev/zero vs /dev/zero never ended; now reported by type (`... is a character special file while ...`, exit 1) as GNU 3.10; test, CLAUDE.md row |
| high | fixed in 5370444 | tests/unit/components/BuiltinCommands.unittests/DiffTest.cpp | planned tests missing (-P, --unidirectional-new-file, `diff - ra`, --from-file/--to-file, conflicting -S, unreadable -X, -ruN epoch header, --exclude quoting): DiffDirOperandRules, checked against GNU 3.10 |
| medium | commented | src/components/BuiltinCommands/commands/diff/Diff.cpp:655 | `diff -e n1 n2` on two distinct identical files without a final newline exits 0 silently; GNU warns and exits 2 (only the same file is silent); compare resolved paths |
| low | commented | src/components/BuiltinCommands/commands/diff/Diff.cpp:82 | DiffFindLongOption returns null when two prefix matches precede a later exact match: look for an exact match first |
| low | commented | src/components/BuiltinCommands/commands/diff/DiffDirectories.cpp | files lack a final newline |
