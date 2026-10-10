# Review: diff--diff-core (PR #72)
- Verdict: merged
- Merged as: ca6969e, version 0.5.30
- Tokens: claude 468647, ollama input 400645, ollama output 595657

Clean-room re-plan before the task (Opus, included in the Claude count). Clean-room check: no finding. Built over 4 container runs (two died on the 32000 output-token cap, one on an Ollama quota).

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 35fad12 | src/components/BuiltinCommands/commands/diff/DiffEngine.cpp:245 | forward/backward search steps read past both ends of the diagonal tables (k+1 > N, k-1 < -M) when one file is much longer (5 lines vs 1): UB on ordinary input |
| high | fixed in 35fad12 | src/components/BuiltinCommands/commands/diff/DiffEngine.cpp:347 | LinesUp never lined up a block at the region's end: `a\na\n` vs `c\na\nc\n` gave `1c1`/`2a3`, GNU `0a1`/`2c3`; regression test |
| high | fixed in 9d52cfe | tests/unit/components/BuiltinCommands.unittests/DiffTest.cpp | most planned tests missing (Horizon, PicksMyersScript, PlacesBlocks, IsMinimalWhereGnuDefaultDiffers, random LCS/apply, -Z/-E/-B/-t CR/quoting/errors); added, checked against GNU 3.10 |
| medium | fixed in 35fad12 | src/components/BuiltinCommands/commands/diff/DiffOutput.cpp:403 | unified context line marked `\ No newline` when only file 1 lacked it; GNU marks file 0's only |
| medium | fixed in 35fad12 | CLAUDE.md:629 | both docs had `-E` over `-Z` (code and GNU: `-Z` over `-E`); newline-marker wording wrong |
| low | fixed in 35fad12 | tests/unit/components/BuiltinCommands.unittests/CMakeLists.txt:1 | DiffTest.cpp listed twice |
| medium | commented | src/components/BuiltinCommands/commands/diff/DiffEngine.cpp:397 | block sliding recounts every position from the region start: quadratic; the plan keeps j updated as the block moves |
| low | commented | src/components/BuiltinCommands/commands/diff/DiffEngine.cpp:231 | both search tables copied in full on every step D |
| low | commented | src/components/BuiltinCommands/commands/diff/Diff.cpp:466 | `diff -e f f` on a file without a final newline warns and exits 2; GNU exits 0 silently |
