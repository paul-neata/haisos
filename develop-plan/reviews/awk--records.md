# Review: awk--records (PR #81)
- Verdict: merged
- Merged as: 097f83f, version 0.5.39
- Tokens: claude 418051, ollama input 525165, ollama output 185774

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in af91c5c | src/components/BuiltinCommands/commands/awk/AwkInput.cpp:92 | with `RS = ""` a single leading newline stayed in the first record; gawk skips every leading newline; reader and run tests |
| medium | fixed in af91c5c | src/components/BuiltinCommands/commands/awk/AwkRegex.cpp:308 | a bracket range starting with a leading `-` (`[--/]`) became `[.-.]`, matching only `-`; test |
| medium | fixed in af91c5c | src/components/BuiltinCommands/commands/awk/Awk.cpp:45 | `\<`/`\>` unknown C++ escapes (GCC and MSVC warn): --help printed `no < >`; test |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:991 | a regex FS that does not compile is reported only when a record is split, with FILENAME/FNR; gawk is fatal when FS is assigned (`BEGIN{FS="a("}{print "hi"}` exits 2) |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkFields.cpp:140 | assigning `$0` keeps the read record's paragraph flag (`RS=""` mid-record, `$0="p\nq r"`, FS `x`: NF 1, gawk 2) |
| low | commented | tests/unit/components/Awk.unittests/AwkInterpreterTest.cpp:610 | NotYetAvailable runs `length("ab")` twice, ignoring the first |
