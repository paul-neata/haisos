# Review: builtins--unicode (PR #27)
- Verdict: merged
- Merged as: 35621fc, version 0.4.9
- Tokens: claude 218601, ollama input 48951, ollama output 14071

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| low | commented | src/components/Unicode/Unicode.cpp:17 | DecodeUtf8 reads data[0] before checking size; size 0 is only a documented precondition (return Incomplete or assert) |
| low | commented | tests/unit/components/Unicode.unittests/UnicodeTest.cpp:170 | Zero-width-beats-wide tested only at U+302A; add DisplayWidth(0x3099) == 0 |
| low | commented | tests/unit/components/Unicode.unittests/UnicodeTest.cpp:127 | IsSpace(U+2028/U+2029) never asserted |
| low | commented | CMakeLists.txt:122 | add_subdirectory(src/components/Unicode) placed after the src/tools/* entries, not with the components |
