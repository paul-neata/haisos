# Review: awk--functions (PR #82)
- Verdict: merged
- Merged as: 62f732b, version 0.5.40
- Tokens: claude 425298, ollama input 808465, ollama output 154918

Clean-room re-check before the task (Opus, in the Claude count). Clean-room check: no finding; every planned test present with the plan's expectations.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:453 | a parameter bound to a caller variable that later became an array reads as an empty scalar (`function f(a){x[1]=1; print "[" a "]"} BEGIN{f(x)}` prints `[]`; gawk: ``attempt to use array `a (from x)' in a scalar context``); same in length (AwkBuiltins.cpp:113, `length: received array argument`) and split's message (AwkBuiltins.cpp:184) |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.h:22 | 200 nested calls (kAwkMaxCallDepth) may overflow Windows' 1 MB thread stack (and WASM's) before the depth fatal; Windows CI fails in Awk.unittests and BuiltinCommands.unittests |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:510 | ScalarRefOf builds the "a (from x)" name on every access to a local |
| low | commented | src/components/BuiltinCommands/commands/awk/CLAUDE.md | the split(s, a[i]) exception bullet has a stray backtick |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp | RunBeginItems/RunEndItems treat any caught FlowUnwind as an exit (correct today, implicit) |
