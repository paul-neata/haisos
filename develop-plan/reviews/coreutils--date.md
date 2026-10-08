# Review: coreutils--date (PR #60)
- Verdict: merged
- Merged as: d032535, version 0.5.18
- Tokens: claude 258863, ollama input 449099, ollama output 144816

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/date/Date.cpp:95 | --help notes don't list the +FORMAT conversions (the plan asked for it) |
| medium | commented | src/components/BuiltinCommands/commands/date/Date.cpp:95, src/components/BuiltinCommands/CLAUDE.md:233 | docs say the set operand parses like -d (ParseDateString); the code uses ParseTouchStamp |
| medium | commented | src/components/BuiltinCommands/BuiltinDate.cpp:1037 | %^P / %#P print PM; GNU prints pm |
| medium | commented | src/components/BuiltinCommands/BuiltinDate.cpp:918 | %_N, %_3N, %-3N don't trim trailing zeros or pad behind as gnulib does |
| medium | commented | src/components/BuiltinCommands/BuiltinDate.cpp:1171 | %_10:z / %_5:::z pad the hours inside the sign (`+ 0:00`) |
| medium | commented | src/components/BuiltinCommands/BuiltinDate.cpp:950 | the `-` flag is sticky (GNU: the last of - _ 0 wins, %-0d is 04); %^#Z gives UTC, GNU utc |
| low | commented | src/components/BuiltinCommands/BuiltinDate.cpp | E/O modifiers accepted on every conversion (GNU writes %5Ed out as it stands) |
| low | commented | src/components/BuiltinCommands/commands/date/Date.cpp:288 | AdjustResolution doesn't skip %% |
| low | commented | src/components/BuiltinCommands/commands/date/Date.cpp:308 | a refused set prints the date before the error (GNU: error first) |
| low | commented | src/components/BuiltinCommands/commands/date/Date.cpp:128 | source conflicts checked during the option loop; GNU checks after it |
| low | commented | tests/unit/components/BuiltinCommands.unittests/DateTest.cpp | no tests for --rfc-email or the --rfc-3339 argmatch error |
