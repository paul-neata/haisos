# Review: streams--exit-codes (PR #23)
- Verdict: merged
- Merged as: 2cb3d48, version 0.4.5
- Tokens: claude 334302, ollama input 228366, ollama output 65962

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 89a1a2b | src/components/HaisosOS/LuaProcess.cpp:494 | RenderLuaError ran an error object's `__tostring` via luaL_callmeta after lua_pcall returned, unprotected: a raising `__tostring` (or a kill landing in it) made Lua panic and abort() the whole haisos host, from any untrusted .lua; now rendered by a message handler inside the protected call (as lua.c), test LuaProcessTest.AnErrorObjectsTostringRunsProtected |
| medium | commented | src/components/HaisosOS/LuaProcess.cpp:614 | exit() inside a coroutine re-arms the hook only on the coroutine's thread; coroutine.resume catches the error and the main thread can run up to ~1000 more instructions (tool calls included) before its own count hook fires |
| medium | commented | src/components/Agent/Agent.cpp:507 | No test for a command failing at the LLM-round cap (LastCommandFailed, "maximum of <n> LLM rounds" error line) |
| low | commented | src/components/HaisosOS/AgentProcess.cpp:142 | TriggerStop checks finished, then sets the stop flag: an agent finishing in between reports 143; a self-closed interactive agent still waiting for a line counts as stopped at OS shutdown |
| low | commented | src/components/HaisosOS/LuaProcess.cpp:~600 | exit() casts its 64-bit code to int before modulo 256, and accepts numeric strings (`exit("3")`) |
| low | commented | CLAUDE.md:22, src/haisos/CliParser.cpp:794 | Exit-status docs: OUTCOPY failure exits 1 only when every RUN exited 0; "127 when one or no process could be started" is unclear; root doc omits 143 for a RUN still running at the 24 h limit |
