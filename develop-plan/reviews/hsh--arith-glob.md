# Review: hsh--arith-glob (PR #32)
- Verdict: merged
- Merged as: d60a719, version 0.4.14
- Tokens: claude 247262, ollama input 93336, ollama output 49955

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 06ab0e7 | src/components/BuiltinCommands/commands/hsh/HshArithmetic.cpp:84 | Tokenize hung on Windows: on "0x" with no hex digit MSVC's strtoimax reads nothing (end == start), the index never moved and tokens piled up without end (the " 0x " test row; Hsh.unittests killed after 40 s); now takes the "0" as glibc does; "0x" and "0xg" test rows added |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshArithmetic.cpp:184 | ParseAssign reads the left variable before the right-hand side, and also for plain `=`; dash reads it only for compound operators, after the RHS: `x=abc; $((x=3))` fails "Illegal number: abc", `x=5; $((x += (x=2)))` gives 7 (dash 4) |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshGlob.cpp:14 | SplitComponents jumps over a bracket even when it holds a '/': `[a/b]` stays one component and matches file `a`; plan and dash: a bracket never spans '/' |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshPattern.h:39 | PatternBracketEnd is public API the plan does not list |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshPattern.cpp:16 | A `[:` searches `:]` anywhere further on, past the bracket's real closing `]` |
