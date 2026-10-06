# Memory

- kimi-k3 leaves Windows-only breakage (windows.h `min`/`max` macros: use `(std::min)(...)`); reviewers may fix such one-liners directly, and then the PR merges green without the host Windows step.
- kimi-k3 writes tests racing against short-lived processes (a `.lua` that ends at once releases its descriptors); reviewers catch it -- worth a line in later plans' Tests sections.
- MockFileDescriptor (tests/mocks/MockFileDescriptor.h:33, PR #21) undercounts forced/failed writes in WriteCalls(); folded into streams--runtime-streams' plan, which reuses it.
- Open follow-ups for the final-review fixes: FileDescriptorTest.cpp:162 -- add a write attempt through the ReadOnlyFileSystem descriptor (`EXPECT_EQ(read->Write("x", 1), kIOError)`, file unchanged); ReleaseCountingDescriptor fake copied 3x (ProcessFileIOTest.cpp, HaisosOSTest.cpp, BuiltinCommandsTest.cpp) -> one tests/mocks/ header, ideally before streams--* copies it again.
- Token tally per task: plan-check agent + task agent + review agent (+ Windows agent), from their last notifications.
