# Review: awk--parser (PR #78)
- Verdict: merged
- Merged as: 1c54151, version 0.5.36
- Tokens: claude 360908, ollama input 197493, ollama output 130203

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 99bb561 | src/components/BuiltinCommands/commands/awk/AwkParser.cpp:322 | a print argument continuing from a grouping was wrapped by the next unary/prefix operator: `print (a) + b` as `(concat (+ a) b)`, `print (1) - 1`, `print (x) !x`; `print (x) ++y` a syntax error (gawk --posix: 3, 0, 01, 13); 4 tests |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkParser.cpp:746 | skipping separators before `else`/`while` accepts what gawk rejects (`if (1) print 1;; else`, `if (1) {} ; else`, `do x++;; while`, `do {} ; while (0)`): after a body accept one `;` only when it did not end in `}`, then only newlines |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkParser.h:24 | VariableUse/ParseResult/ParseAwkProgram sit between Parser's comment and the class |
| low | commented | tests/unit/components/Awk.unittests/AwkParserTest.cpp:145 | parenthesized-lvalue tests in a new suite, not the planned ones (same coverage) |
| low | commented | (commit messages) | the task's commits say "(#77)" instead of #78 |
