# Review: search--grep-core (PR #57)
- Verdict: merged
- Merged as: 1ec82ff, version 0.5.15
- Tokens: claude 52791, ollama input 267873, ollama output 123260

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 840e344 | src/components/BuiltinCommands/commands/grep/Grep.cpp:160-173 | 12 not-treated option rows had a 7th `""` feeding `bool hidden`: MSVC C2397 broke build-windows; each row now has BuiltinOption's 6 fields in order |
| high | fixed in 840e344 | src/components/BuiltinCommands/commands/grep/GrepFile.cpp:198 | `offset = end + 1` indexed the per-chunk buffer, so `-b` was wrong after the first 96 KiB read or a short pipe read; now `offset += end - pos + 1`, test GrepByteOffsetsPastTheFirstRead |
| high | fixed in 330379e | tests/unit/components/BuiltinCommands.unittests/GrepTest.cpp:108 | A local named `stdin` (a macro in MSVC's CRT) broke the Windows test build; renamed `piped` |
| medium | commented | src/components/BuiltinCommands/commands/grep/GrepMatcher.cpp:146-151 | `-P -w` wraps the pattern as `\b(?:p)\b` (per the plan); GNU's pcresearch likely uses `(?<!\w)(?:p)(?!\w)`, which differs for patterns starting/ending with a non-word byte; with `-x` both wraps apply. Unchecked against GNU grep 3.11 |
| low | commented | src/components/BuiltinCommands/commands/grep/GrepMatcher.cpp:318 | The `-w` shrink step for `-F` scans every later position (quadratic on long lines); call `FixedAt` at `b` directly |
| low | commented | src/components/BuiltinCommands/commands/grep/GrepFile.cpp:136 | `noLimitZero` means "read the input"; rename to `readInput` |
