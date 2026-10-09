# Review: coreutils--du-cmp (PR #62)
- Verdict: merged
- Merged as: 47a0439, version 0.5.20
- Tokens: claude 314202, ollama input 329244, ollama output 140514

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 090f01b | src/components/BuiltinCommands/commands/cmp/Cmp.cpp:86 | an interrupted read (kIOInterrupted) was an I/O error (exit 2); the plan treats it as a stop |
| medium | commented | src/components/BuiltinCommands/commands/cmp/Cmp.cpp:262 | the same-file shortcut ignores skips and existence: `cmp f f 0 1` equal, `cmp missing missing` exits 0 not 2 |
| medium | commented | src/components/BuiltinCommands/commands/du/Du.cpp:333 | a directory given twice (`du a a`) is walked twice, the second pass without file sizes; GNU skips it |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/DuTest.cpp | no tests for -S, --files0-from, -X, --time-style, a negative -t, -l |
| low | commented | src/components/BuiltinCommands/commands/du/Du.cpp:446 | -t/-B messages for a bad suffix, too-large value and -0 differ from GNU's xstrtol_fatal |
| low | commented | src/components/BuiltinCommands/BuiltinSize.cpp:126 | `0Z`/`0EB` overflow (gnulib: 0); `B`/`iB` after b/c/w refused |
| low | commented | src/components/BuiltinCommands/commands/cmp/Cmp.cpp:201 | the missing-operand message always says 'cmp'; diffutils names the last argument |
| low | commented | src/components/BuiltinCommands/commands/cmp/Cmp.cpp:301 | a read error while skipping exits 1 silently; diffutils reports it, exit 2 |
| low | commented | src/components/BuiltinCommands/commands/cmp/Cmp.cpp:250 | -n refuses an overflowing value; diffutils takes the maximum |
| low | commented | src/components/BuiltinCommands/commands/du/Du.cpp:267 | `-s -d 0` lacks GNU's "summarizing is the same as using --max-depth=0" warning |
