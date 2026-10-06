# Memory

- kimi-k3 leaves Windows-only breakage (windows.h `min`/`max` macros: use `(std::min)(...)`); reviewers may fix such one-liners directly, and then the PR merges green without the host Windows step.
- Open follow-up for the final-review fixes: FileDescriptorTest.cpp:162 -- add a write attempt through the ReadOnlyFileSystem descriptor (`EXPECT_EQ(read->Write("x", 1), kIOError)`, file unchanged). Also the stale comment at BuiltinCommandsTest.cpp:286 once IFileIO moves off int fds (fd--process-table).
