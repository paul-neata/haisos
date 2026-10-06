# Review: hsh--expansion (PR #34)
- Verdict: merged
- Merged as: ca0002b, version 0.4.16
- Tokens: claude 329469, ollama input 528163, ollama output 111701

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 7806dba | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:465, HshExpansion.cpp:326 | Inside double quotes the operand of `#`/`##`/`%`/`%%` was lexed as quoted text: `"${x%/*}"` removed a literal `/*`, `"${x%$y}"` was literal; dash treats both as patterns; operand now lexed as an unquoted word and expanded outside the double quotes; lexer and expansion tests added |
| high | fixed in 7806dba | src/components/BuiltinCommands/commands/hsh/HshExpansion.cpp:480,521; tests/unit/components/Hsh.unittests/HshExpansionTest.cpp:219,223-224 | "Per-op tilde rules" (872c6a0) went against dash 0.5.12 and the tests asserted the wrong results (`a${z:=~/a}` gives `a/h/a`, `${q:+~}` gives `/h` in dash); a leading tilde in an operand now expands for every op unless the `${...}` is in double quotes; tests and hsh CLAUDE.md corrected |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshExpansion.cpp:290 | `"$@$@"` (and `"${@}${@}"`) with no positional parameters gives no field; dash gives one empty field -- only a lone `"$@"` vanishes (fix: require `part.parts.size() == 1`, plus a test) |
