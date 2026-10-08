# Review: coreutils--sort-orders (PR #48)
- Verdict: merged
- Merged as: cf1f0cd, version 0.5.6
- Tokens: claude 180415, ollama input 115052, ollama output 89051

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in a792dd9 | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:59 | -R folded case on every key, even without -f, so `a` and `A` became one group (GNU keeps them apart) |
| high | fixed in a792dd9 | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:156 | Bug from #47: the -d/-i text comparison folded case without -f (`sort -d` on a,B gave `a B`; GNU gives `B a`) |
| high | fixed in a792dd9 | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:135 | -V ignored -f/-d/-i, which GNU applies before its version comparison (`sort -fV`, `-dV`) |
| medium | commented | src/components/BuiltinCommands/commands/sort/Sort.cpp:281 | -C with -o reports '-co' (GNU '-Co'), and the -o conflict is reported before GNU's extra-operand error |
| low | commented | tests/unit/components/BuiltinCommands.unittests/SortTest.cpp (SortMerge) | The tie test merges identical lines, so it cannot show that the earlier input wins a tie |
| low | commented | src/components/BuiltinCommands/commands/sort/SortKeys.cpp:97 | -R builds two transformed strings for every comparison |
| low | commented | src/components/BuiltinCommands/BuiltinCompare.cpp:150 | CompareMonth skips only space and tab, not newline as GNU's blanks do |
| low | commented | src/components/BuiltinCommands/commands/sort/Sort.cpp:259 | std::random_device is created on every run, even without -R |
