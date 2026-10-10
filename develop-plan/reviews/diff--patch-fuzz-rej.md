# Review: diff--patch-fuzz-rej (PR #75)
- Verdict: merged
- Merged as: 2968e7a, version 0.5.33
- Tokens: claude 628641, ollama input 920606, ollama output 588199

Clean-room re-plan before the task (Opus, in the Claude count). Clean-room: one test comment naming a gnulib macro reworded; nothing copied, ported or paraphrased. Every plan item implemented and tested.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 60b2c38 | tests/unit/components/BuiltinCommands.unittests/PatchFuzzRejTest.cpp:80 | clean room: a test comment named gnulib's internal macro (XARGMATCH); reworded as behaviour |
| high | fixed in 60b2c38 | src/components/BuiltinCommands/commands/patch/Patch.cpp:898 | a backup moved the input file to the written file's backup name: on a git copy the source was renamed away (with -b or a mismatch backup); now the written file is backed up, a rename's source to its own .orig, a copy's source left alone; PatchGitCopyAndRenameBackups |
| high | fixed in 60b2c38 | src/components/BuiltinCommands/commands/patch/Patch.cpp:822 | regression: "Not deleting file X as content differs" printed for a deletion skipped at its reversed-patch question; now `!skipped`; test |
| medium | fixed in 60b2c38 | out/summary.md | the implementer's run summary committed into the repository; removed |
| medium | commented | src/components/BuiltinCommands/commands/patch/PatchParse.cpp:872 | a context hunk whose new part exceeds its `--- C,D ----` range is accepted; GNU: `context mangled in hunk at line N`, exit 2 |
| low | commented | src/components/BuiltinCommands/commands/patch/PatchParse.cpp:785 | a star line at the very end of input reports a made-up `malformed patch at line N` |
| low | commented | src/components/BuiltinCommands/commands/patch/PatchParse.cpp:997 | normal hunks after a unified/context file patch's hunks (ORIGFILE given) join it and switch its reject format |
| low | commented | src/components/BuiltinCommands/BuiltinText.h:26 | older text (BuiltinText.h, BuiltinCommands CLAUDE.md:69) still names gnulib's XARGMATCH |
