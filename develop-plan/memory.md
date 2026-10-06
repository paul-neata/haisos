# Memory

- kimi-k3 leaves Windows-only breakage (windows.h `min`/`max` macros: use `(std::min)(...)`); reviewers may fix such one-liners directly, and then the PR merges green without the host Windows step.
- kimi-k3 writes tests racing against short-lived processes (a `.lua` that ends at once releases its descriptors); reviewers catch it -- worth a line in later plans' Tests sections.
- MockFileDescriptor WriteCalls fix (PR #21 finding) was folded into streams--runtime-streams (#22).
- Open follow-ups for the final-review fixes: FileDescriptorTest.cpp:162 -- add a write attempt through the ReadOnlyFileSystem descriptor (`EXPECT_EQ(read->Write("x", 1), kIOError)`, file unchanged); ReleaseCountingDescriptor fake copied 3x (ProcessFileIOTest.cpp, HaisosOSTest.cpp, BuiltinCommandsTest.cpp) -> one tests/mocks/ header, ideally before streams--* copies it again.
- streams--exit-codes (#23) / pipes--broken-pipe (#25) mediums: Lua exit() and print in a coroutine (LuaProcess.cpp:614, :619) re-arm the hook on the coroutine only and no LLM-round-cap failure test (Agent.cpp:507) -- asked the user (Question) whether to add a follow-up task.
- Lua runtime is where security bugs show (#23: unprotected __tostring -> host abort): reviews of Lua changes need the panic/protection check.
- Windows: HaisosOS.unittests sits at ~29.5-33 s against the 30 s CI limit (HaisosOSTest uses 127.0.0.1:9999, ~2 s to refuse on Windows; 0.0.0.0:9999 fails at once) -- asked the user. A red Windows check on a PR not touching HaisosOS: check the log (HaisosOS.unittests killed), re-run the failed job (`gh run rerun <id> --failed`), merge if green (#27).
- builtins--unicode took ~1.9 h of a 120 min timeout for a small diff and few Ollama tokens: watch kimi-k3 latency.
- An API session limit can kill an agent mid-review (#24): check the PR for commits/comments, then simply restart the review.
- Token tally per task: plan-check agent + task agent + review agent (+ Windows agent), from their last notifications.
