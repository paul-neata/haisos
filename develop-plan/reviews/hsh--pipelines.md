# Review: hsh--pipelines (PR #37)
- Verdict: merged
- Merged as: 7598ffa, version 0.4.19
- Tokens: claude 297337, ollama input 152300, ollama output 68255

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 7a7aaf5 | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:803 | A `$(...)` in the words of a pipeline or background stage started without waiting inherited the no-wait mode: its commands ran concurrently, `$?` inside was 0, the children leaked in m_liveChildren; fixed, test CommandSubstitutionInAStageIsWaitedFor |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:260, :477 | A child stage's `$(...)` is expanded before the earlier in-shell stages run: `: \| /bin/echo $(cat)` deadlocks; a stage holding a substitution should not count as a child stage |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:558 | A background pipeline is expanded in the foreground: `/bin/echo $(/spin.lua) &` blocks the shell |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshSubshell.cpp:22 | A job started inside a subshell is dropped when the job list is restored, but its processes stay in m_liveChildren for good |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:674 | Options reach the child hsh as an invocation argument, not a `set` prelude line (no `set` builtin yet); documented deviation from the plan |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:617 | Option letters built by hand instead of filtering OptionLetters |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:522 | WriteErr in RunSubshell's ShellError handler can throw past the subshell boundary |
| low | commented | tests/unit/components/Hsh.unittests/HshPipelineTest.cpp:18, HshShellTest.cpp:217 | Spin-poll loop written twice; belongs in HshShellFixture.h |
