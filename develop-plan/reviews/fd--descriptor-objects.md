# Review: fd--descriptor-objects (PR #19)
- Verdict: merged
- Merged as: 7fdb376, version 0.4.1
- Tokens: claude 210567, ollama input 156310, ollama output 49663

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in eb829c0 | src/components/Filesystem/windows/WindowsFilesystem.cpp:57,65 | windows.h `min` macro broke `std::min(` (C2589), Windows build red; now `(std::min)(...)` |
| medium | commented | tests/unit/components/Filesystem.unittests/FileDescriptorTest.cpp:162 | WrappersPassTheInnerDescriptorUp never attempts a write through a descriptor opened via ReadOnlyFileSystem; passing the inner descriptor up is safe only because the leaf enforces the access mode |
| low | commented | tests/unit/components/Filesystem.unittests/FileDescriptorTest.cpp:240 | TwoDescriptorsOfABuiltinReadIndependently assumes the rest of the note fits in one 256-byte read |
| low | commented | tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp:286 | Comment "IFileIO keeps int fds in this task" will go stale |
