# Review: search--find-actions (PR #65)
- Verdict: merged
- Merged as: 5974e5c, version 0.5.23
- Tokens: claude 297522, ollama input 352413, ollama output 138683

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 59e1243 | src/components/BuiltinCommands/commands/find/FindActions.cpp:782 | the program was looked up once per primary: `-exec {} \;` ran the first file's program for every file, `-execdir` looked for ./NAME in find's directory; now looked up again when the command changes, -execdir from the file's directory (FindExecSemicolon, FindExecdir) |
| medium | commented | src/components/BuiltinCommands/commands/find/FindActions.cpp:737 | when OpenEmptyInput returns null, the -ok child gets find's own stdin (where the prompt answers come from) |
| low | commented | src/components/BuiltinCommands/commands/find/FindParser.cpp:454 | the -delete/-prune clash checks token spellings: `-name -prune -delete` is wrongly refused |
| low | commented | src/components/BuiltinCommands/commands/find/FindActions.cpp:596 | a failed -delete always says "Permission denied", even where GNU says "No such file or directory" |
| low | commented | src/components/BuiltinCommands/commands/find/FindActions.cpp:811 | a program found but not started is always "Permission denied" |
| low | commented | src/components/BuiltinCommands/commands/find/FindActions.cpp:167 | -ls reads the clock per file for the six-month window; GNU uses find's start time |
