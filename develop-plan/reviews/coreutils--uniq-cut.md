# Review: coreutils--uniq-cut (PR #55)
- Verdict: merged
- Merged as: 972338b, version 0.5.13
- Tokens: claude 57404, ollama input 150970, ollama output 85538

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/cut/Cut.cpp:116 | The "invalid field value"/"invalid byte/character position" message quotes the rest of the item; GNU quotes the rest of the whole LIST (`-f 1x,2` gives 'x,2' on GNU 9.4) |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/CutTest.cpp | No test for `-d ''` or `--output-delimiter=''` being the NUL byte, though the docs and plan describe it |
| low | commented | src/components/BuiltinCommands/commands/cut/Cut.cpp:77 | `-f 0x` gives "fields are numbered from 1"; GNU gives "invalid field value 'x'" |
| low | commented | src/components/BuiltinCommands/commands/cut/Cut.cpp:38 | List positions are size_t, so on 32-bit WASM `-f 4294967296` is "too large"; GNU accepts it (uintmax_t) |
| low | commented | src/components/BuiltinCommands/commands/uniq/Uniq.cpp:75 | `-f`/`-s`/`-w` reject a leading blank or `+`, which GNU accepts |
| low | commented | src/components/BuiltinCommands/commands/uniq/Uniq.cpp:245 | The `--group`/`-c -D` conflict errors are checked before the operands; GNU reports "extra operand" first |
| low | commented | src/components/BuiltinCommands/commands/uniq/Uniq.cpp:386 | The read-error message quotes with GnuQuote, unlike line 297 (GNU uses quoteaf for both) |
| low | commented | tests/unit/components/BuiltinCommands.unittests/CutTest.cpp:76 | The adjacent-range case `1-2,3-4 --output-delimiter=X` -> abXcd (in the plan) is not tested |
