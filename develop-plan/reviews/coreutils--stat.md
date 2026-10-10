# Review: coreutils--stat (PR #61)
- Verdict: merged
- Merged as: 630baac, version 0.5.19
- Tokens: claude 318500, ollama input 146678, ollama output 131409

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in a915fff | src/components/BuiltinCommands/commands/stat/Stat.cpp:72 | the grouping flag `'` was not accepted as a directive flag (`%'s` printed `?s`), though the plan, stat.c's printf_flags and the CLAUDE.md row include it; now accepted, tested |
| medium | commented | src/components/BuiltinCommands/commands/stat/Stat.cpp:194 | a --printf format ending in a backslash prints `\` without GNU's `warning: backslash at end of format` |
| medium | commented | src/components/BuiltinCommands/commands/stat/Stat.cpp:140 | negative times with nanoseconds in %X/%Y/%Z: -1.5 s prints `-2.500000000`; GNU's out_epoch_sec prints `-1.500000000` |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/StatTest.cpp | no tests for %t %T %r %R, the --printf octal/hex escapes, -L, or --cached's not-treated report |
| low | commented | src/components/BuiltinCommands/commands/stat/Stat.cpp:78 | a width up to INT_MAX builds the padded text in memory (also in the shared BuiltinPrintf) |
| low | commented | CLAUDE.md:542, src/components/BuiltinCommands/CLAUDE.md:7 | the builtin lists were re-wrapped badly, leaving a lone "`wc`," / "`true`," line |
