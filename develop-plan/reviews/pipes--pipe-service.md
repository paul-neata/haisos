# Review: pipes--pipe-service (PR #24)
- Verdict: merged
- Merged as: 4e48555, version 0.4.6
- Tokens: claude 499370, ollama input 157878, ollama output 54555

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | fixed in 0ad1530 | tests/unit/components/HaisosOS.unittests/HaisosOSPipeTest.cpp:265 | Windows CI killed HaisosOS.unittests at its 30 s limit (31.9 s): the new tests' unreachable endpoint 127.0.0.1:9999 takes ~2 s to refuse on Windows, plus fixed 500/200 ms waits; now 0.0.0.0:9999 and 50 ms waits (29.5 s; the suite ~2.3 s -> ~0.15 s), on the host |
| medium | commented | src/components/HaisosOS/ProcessFileIO.cpp:366-373 | CreatePipe takes the table lock separately for each AddDescriptor and the CloseDescriptor rollback; a concurrent Dup2 onto readSlot would make the rollback close someone else's descriptor |
| low | commented | src/components/HaisosOS/ProcessFileIO.cpp:361-362 | Garbled comment ("ends goes away here") |
| low | commented | tests/unit/components/PipeService.unittests/PipeServiceTest.cpp:202 (also :91, :112, :278) | ASSERT inside a read loop returns before helper threads are joined: a failure ends in std::terminate, not a test failure |
