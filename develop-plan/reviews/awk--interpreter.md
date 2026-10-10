# Review: awk--interpreter (PR #80)
- Verdict: merged
- Merged as: 7d77dee, version 0.5.38
- Tokens: claude 386645, ollama input 493557, ollama output 123957

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding; every plan item present.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 12a3aa3 | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:470 | post `++`/`--` returned the old Value, not the old number (`x="abc"; y=x++; print y` printed `abc`, gawk 0); test |
| high | fixed in 12a3aa3 | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:266 | compound assignment and `++`/`--` evaluated the subscript/field index twice (`i=1; a[i++]+=5; print i` printed 3, gawk 2); PlaceOf/ReadPlace/WritePlace; test |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:173 | `-v` decodes escapes before checking the name: `-v 'a\x41=1'` sets `aA`, gawk refuses the name |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:378 | `k in x` leaves an untyped x untyped; gawk makes it an array (a later `x = 1` fatal) |
| low | commented | tests/unit/components/Awk.unittests/AwkInterpreterTest.cpp:616 | StopsPromptly stops right after starting: the loop's stop check may go untested |
