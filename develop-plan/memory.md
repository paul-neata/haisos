# Memory

- kimi-k3 leaves Windows-only breakage (windows.h `min`/`max` macros: use `(std::min)(...)`); reviewers may fix such one-liners directly, and then the PR merges green without the host Windows step.
- kimi-k3 writes tests racing against short-lived processes (a `.lua` that ends at once releases its descriptors); reviewers catch it -- worth a line in later plans' Tests sections.
- MockFileDescriptor WriteCalls fix (PR #21 finding) was folded into streams--runtime-streams (#22).
- Open follow-ups for the final-review fixes: FileDescriptorTest.cpp:162 -- add a write attempt through the ReadOnlyFileSystem descriptor (`EXPECT_EQ(read->Write("x", 1), kIOError)`, file unchanged); ReleaseCountingDescriptor fake copied 3x (ProcessFileIOTest.cpp, HaisosOSTest.cpp, BuiltinCommandsTest.cpp) -> one tests/mocks/ header, ideally before streams--* copies it again.
- tests/haisos agent_*.js (PR #22) lost their exit-status check (spawnSync); fold into streams--exit-codes: fail on `result.status !== 0 || result.signal`.
- Token tally per task: plan-check agent + task agent + review agent (+ Windows agent), from their last notifications.
