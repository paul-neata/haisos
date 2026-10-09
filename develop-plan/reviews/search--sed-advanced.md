# Review: search--sed-advanced (PR #68)
- Verdict: merged
- Merged as: 25fe497, version 0.5.26
- Tokens: claude 204899, ollama input 806241, ollama output 382264

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in ee86da6 | src/components/BuiltinCommands/commands/sed/SedParser.cpp:764 | each w/W/s///w opened its own truncating descriptor, even for the same file, so writes overwrote each other; now one shared file per name (GNU, the plan), with a test |
| high | fixed in ee86da6 | src/components/BuiltinCommands/commands/sed/Sed.cpp:253 | the static mt19937 behind TempFileName was unguarded: concurrent sed -i threads raced; now a mutex |
| medium | commented | src/components/BuiltinCommands/commands/sed/Sed.cpp:212 | `run == 4` taken as a read error: `sed -i q4 f` keeps the original and drops the temp file (GNU renames and exits 4) |
| medium | commented | src/components/BuiltinCommands/commands/sed/SedExecutor.cpp:122 | `l` wraps only at column N-1, so a multi-char escape overruns the width (67 a's + \001 -> 71 chars); fix `column + unit.size() > wrap - 1` |
| medium | commented | src/components/BuiltinCommands/commands/sed/Sed.cpp:206 | temp-file creation failure should say GNU's `couldn't open temporary file X: <reason>` |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/SedTest.cpp:1049 | planned tests missing: y different-lengths error, `l 0` / `l 1` / default 80 wrap, `N;s/^b/X/M`, `s///w a;b`, `-z 'N;l;d'` |
| low | commented | src/components/BuiltinCommands/commands/sed/SedExecutor.cpp:953 | `y` with a repeated source byte: first mapping wins, GNU uses the last |
| low | commented | src/components/BuiltinCommands/commands/sed/SedExecutor.cpp:1010 | -u does not flush after w writes (the plan asks for it) |
| low | commented | src/components/BuiltinCommands/commands/sed/Sed.cpp:236 | a failed rename always reports "No such file or directory" |
