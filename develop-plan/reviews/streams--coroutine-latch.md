# Review: streams--coroutine-latch (PR #33)
- Verdict: merged
- Merged as: b98ff82, version 0.4.15
- Tokens: claude 167180, ollama input 76660, ollama output 17139

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 70232ce | src/components/HaisosOS/LuaProcess.cpp:355 | LuaWrapTrampoline always ran `lua_call(L, 1, 1)` whatever it got: `coroutine.wrap(f, x)` called f(x) on the current thread instead of making a coroutine, and `coroutine.wrap()` broke the Lua API's rules (undefined behaviour any script can trigger); now checks the argument and drops extras; test added |
| high | fixed in 70232ce | src/components/HaisosOS/LuaProcess.cpp:338 | Errors from wrapped coroutines lost their caller's `file:line:` prefix (plan: ordinary coroutines behave exactly as before); prefix added back in LuaWrapCallTrampoline; test added |
| high | fixed in 70232ce | tests/unit/components/Agent.unittests/AgentTest.cpp:790 | Round-cap test was flaky: read console errors as soon as `LastCommandFailed()` was true, but the error line is written just after the flag; now waits for both |
| medium | fixed in 70232ce | src/components/HaisosOS/LuaProcess.cpp:320 | Bad-argument errors from resume/wrap named the function `'?'`; wrappers now check their arguments as the originals do; test added |
| medium | commented | src/components/HaisosOS/LuaProcess.cpp:356 | A memory error inside a wrapped coroutine now gets a position prefix the original leaves off (negligible; noted in a code comment) |
| low | commented | src/components/HaisosOS/CLAUDE.md:267 | Edited paragraph leaves one over-long line |
