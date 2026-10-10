# Review: awk--printf-math (PR #83)
- Verdict: merged
- Merged as: bd5af6c, version 0.5.41
- Tokens: claude 208163, ollama input 259953, ollama output 61607

Clean-room re-check before the task (Opus; it hit a Claude session limit at its end, so its count is missing here). Clean-room check: no finding; every plan item present.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in bea5ced | src/components/BuiltinCommands/commands/awk/AwkFormat.cpp:230 | `%c` kept the precision: `%.0c` of "x" printed nothing; gawk prints `x` |
| medium | fixed in bea5ced | src/components/BuiltinCommands/commands/awk/AwkFormat.cpp:80 | `%c` of inf/NaN cast a NaN to int (UB); gawk prints a NUL byte |
| medium | fixed in bea5ced | src/components/BuiltinCommands/commands/awk/AwkFormat.cpp:243 | `%#d` beyond 64 bits picked up a trailing `.` from `%#.0f`; gawk prints none |
| medium | fixed in bea5ced | src/components/BuiltinCommands/commands/awk/AwkBuiltins.cpp:340 | exp warned on subnormal results; gawk 5.2.1 warns only on 0 or inf (the plan's DBL_MIN rule was wrong) |
| medium | fixed in bea5ced | src/components/BuiltinCommands/commands/awk/CLAUDE.md | rand() called "gawk's shape"; the formula is the one published with MT19937 |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkBuiltins.cpp:123 | sprintf reads CONVFMT before its arguments run, missing an assignment in an argument |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:463 | `case Array: fatal();` falls through syntactically (lambda not noreturn): -Wimplicit-fallthrough risk |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:1390 | the "a, from x" string built for every by-name argument |
