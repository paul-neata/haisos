# Review: diff--patch-core (PR #74)
- Verdict: merged
- Merged as: 887cc1d, version 0.5.32
- Tokens: claude 448903, ollama input 498538, ollama output 663779

Clean-room re-plan before the task (Opus, in the Claude count). Clean-room check: no finding; every plan item present.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in e60dc37 | src/components/BuiltinCommands/commands/patch/PatchApply.cpp:93 | LocateHunk stepped line by line from a far-off stated line (`@@ -1000000000000,3`): an effective hang, no stop check; now starts at the nearest candidate; test against GNU's output |
| high | fixed in e60dc37 | src/components/BuiltinCommands/commands/patch/Patch.cpp:694 | a failed temp-file write was ignored and the partial result renamed over the target (data loss); now removed and fatal |
| high | fixed in e60dc37 | src/components/BuiltinCommands/commands/patch/Patch.cpp:919 | `-i FILE` overrode the PATCHFILE operand; GNU 2.7.6 takes the operand (plan wrong); test |
| high | fixed in af27d12 | src/components/BuiltinCommands/commands/patch/Patch.cpp:703 | with `-o`, a git rename deleted its old file and printed `(read from ...)`; GNU keeps it, `patching file out (renamed from keep)`; test |
| low | fixed in e60dc37 | tests/unit/components/BuiltinCommands.unittests/PatchTest.cpp:182 | test lambda named after a GNU helper (`ask`) renamed `applyP1` |
| medium | commented | src/components/BuiltinCommands/commands/patch/Patch.cpp:954 | int64 `-p` narrowed to int (`-p 4294967296` strips 0) |
| medium | commented | src/components/BuiltinCommands/commands/patch/PatchParse.cpp:385 | `rawOldStart + 1` overflows (UB) for a `-9223372036854775807,0` range |
| medium | commented | src/components/BuiltinCommands/commands/patch/Patch.cpp:326 | open/write failures of `.rej`, `.orig` and `-o` silently ignored |
| medium | commented | src/components/BuiltinCommands/commands/patch/Patch.cpp:400 | the ORIGFILE operand ignored for a git rename/copy |
| low | commented | src/components/BuiltinCommands/commands/patch/PatchParse.cpp:27 | a local `strlen` shadows libc's; header helpers have external linkage |
| low | commented | src/components/BuiltinCommands/commands/diff/Diff.cpp:268 | the exact-match pass does not skip an empty longName |
