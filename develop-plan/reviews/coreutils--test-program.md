# Review: coreutils--test-program (PR #54)
- Verdict: merged
- Merged as: 435d22f, version 0.5.12
- Tokens: claude 199087, ollama input 491458, ollama output 248163

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in cfe54ef | src/components/BuiltinCommands/BuiltinTestExpression.cpp:618-630 | Inside "(", term narrowed the window (m_argc) as well as the count; GNU narrows only the count, so its descent can read past the ")": `( a -a ! ) )` gave 2 (GNU 1), `( a -a ! ) -o b` gave "missing argument after 'b'" (GNU "')' expected"); checked against host LC_ALL=C /usr/bin/test |
| high | fixed in cfe54ef | tests/unit/components/BuiltinCommands.unittests/TestTest.cpp | Tests for several acceptance items were missing (`<`/`>` rejected, long integers, `+1`, `-z`/`-t` alone, nested parens, `a = b -o c`, `-ef ./notes.txt`, the error cases); added, expected values checked against GNU coreutils 9.4 |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/TestTest.cpp (TestProgramReachedThroughPath) | The plan's hsh -c case (a slash path in hsh runs the program, not the shell builtin) is not tested; the test starts the programs directly |
| low | commented | tests/unit/components/BuiltinCommands.unittests/ManTest.cpp:27 | `test` is skipped instead of comparing `man test` with BuiltinHelpText |
| low | commented | src/components/BuiltinCommands/BuiltinTestExpression.h:30 | No newline at end of file |
