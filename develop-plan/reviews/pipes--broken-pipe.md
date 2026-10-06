# Review: pipes--broken-pipe (PR #25)
- Verdict: merged
- Merged as: e2246a9, version 0.4.7
- Tokens: claude 279909, ollama input 117716, ollama output 26907

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/HaisosOS/LuaProcess.cpp:619 | print inside a coroutine re-arms the kill hook only on the coroutine's lua_State; coroutine.resume catches the error and the resumer runs up to ~1000 more instructions (an os_* tool call included) before stopping -- same gap as exit() (#23) |
| low | commented | tests/unit/components/HaisosOS.unittests/HaisosOSPipeTest.cpp:296 | No test that a TriggerStop() after the broken write, while the process still runs, keeps the code at 141 rather than 143 |
| low | commented | src/components/HaisosOS/CLAUDE.md:55 (also CLAUDE.md:191, src/components/BuiltinCommands/CLAUDE.md:43) | Doc reflow leaves a short line and overlong lines |
