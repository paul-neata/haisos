# Review: coreutils--sort (PR #47)
- Verdict: merged
- Merged as: 1e24d21, version 0.5.5
- Tokens: claude 196577, ollama input 287765, ollama output 101720

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 9953cf2 | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:88 | `-f` compared folded bytes as signed char, so bytes >= 0x80 sorted before ASCII (GNU puts them after) |
| high | fixed in 9953cf2 | src/components/BuiltinCommands/commands/sort/Sort.cpp:123 | `-t '\0'` (GNU's NUL tab) was rejected as a multi-character tab; the test passed a raw NUL with no key, so it could never fail |
| high | fixed in 9953cf2 | src/components/BuiltinCommands/BuiltinText.cpp:154 | BuiltinLineReader erased its buffer after every line (quadratic per 64 KiB read) and re-scanned long lines from the start after each read |
| medium | commented | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:45,343 | `-d` with `-i`: GNU keeps only `-d` (tab and newline still compared, error says `'-dn'`), Haisos applies both and says `'-din'` -- fits coreutils--sort-orders |
| low | commented | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:115 | KEYDEF counts don't skip leading whitespace as xstrtoumax does |
| low | commented | src/components/BuiltinCommands/commands/sort/Sort.cpp:343 | The whole output is built in one string before writing (double the peak memory) |
| low | commented | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:38 | A comment block with no code under it |
| low | commented | new files and tests CMakeLists.txt | No final newline |
