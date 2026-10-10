# Review: awk--expressions (PR #77)
- Verdict: merged
- Merged as: a676fb5, version 0.5.35
- Tokens: claude 337325, ollama input 171487, ollama output 102922

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 0455328 | src/components/BuiltinCommands/commands/awk/AwkParser.cpp:131 | gawk --posix takes an assignment as either ternary branch and as the right operand of `|| && ~ !~` and comparisons (`1 && y = 0`, `0 ? 1 : y = 2`, `1 < y = 2`); rejected; now ParseAssignment there, tests |
| high | fixed in 0455328 | src/components/BuiltinCommands/commands/awk/AwkParser.cpp:240 | the plan's `{ '|' getline }` written as an if, not a loop: chained command getline failed; concatenation continues after `"cmd" | getline x`; tests |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkParser.cpp:469 | a parenthesized lvalue is still an lvalue: `(x) = 3`, `(x)++`, `++(x)` accepted; gawk: syntax error |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkParser.cpp:410 | `name[subscripts]` and `$` operand parsing written twice (ParseGetlineTarget, ParsePrimary) |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkAst.h:25 | comment and switch-return alignment off by one |
