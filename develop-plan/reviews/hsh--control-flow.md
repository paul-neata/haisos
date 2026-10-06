# Review: hsh--control-flow (PR #39)
- Verdict: merged
- Merged as: 949d6cd, version 0.4.21
- Tokens: claude 352444, ollama input 234741, ollama output 89550

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in cd6fd27 | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:410 | break/continue thrown from a loop's condition (`while break; do :; done; echo after`) was never caught by that loop: at top level it escaped `Shell::Run` (shell ended silently, status 1), inside another loop it broke the outer one; tests added |
| high | fixed in cd6fd27 | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:600 | Use after free: a function defined on an earlier line is owned only by `m_functions`; a body redefining or `unset -f`-ing its own name freed the body it was running; the definition is now held for the call; test FunctionRedefinedOrUnsetWhileRunning |
| medium | fixed in cd6fd27 | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:431,463 | A loop body ended by break/continue returned the previous command's `$?` instead of 0, unlike dash; tests added |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshBuiltinRead.cpp:134-147 | IFS white space before a non-white delimiter not merged into it: `IFS=" :"` on `a : b` gives `[a][][b]`, dash `[a][b][]` |
| medium | commented | develop-plan/goal.md:115-117 | Acceptance scenario 3 contradicts itself: `hello` does not match `hi*`, dash prints `plain`, not `greeted`; the implementer tests `hi` -> greeted, `hello` -> plain (asked the user) |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:768 | A `$(...)` in a child stage's redirection target (`: \| /bin/cat < $(cat)`) can still deadlock (IsChildStage checks words only) |
| low | commented | tests/unit/components/Hsh.unittests/HshPipelineTest.cpp:255 | Detached thread captures `this`: a timed-out test would crash the suite instead of failing |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:1100 | Test-only live-child counter decremented even when the child was already removed |
| low | commented | tests/unit/components/Hsh.unittests/HshControlFlowTest.cpp:96 | `exit() {...}` accepted and ignored; dash rejects it with `Bad function name`; the test locks in the difference |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp | `const` saved positional parameters make the restore a copy, not a move |
