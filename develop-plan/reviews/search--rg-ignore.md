# Review: search--rg-ignore (PR #71)
- Verdict: merged
- Merged as: a1e08ae, version 0.5.29
- Tokens: claude 214424, ollama input 368878, ollama output 298365

Clean-room check: no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 5f14d2b | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:620 | `--no-ignore-parent` still applied the `.gitignore` files above the operand (rg's manual: every ignore file in a parent directory) |
| high | fixed in 5f14d2b | tests/unit/components/BuiltinCommands.unittests/RgIgnoreTest.cpp | untested behaviour: `.rgignore` over `.ignore`, `.git/info/exclude`, `--no-ignore-exclude/-dot/-vcs`, `--no-ignore --ignore`, `--no-ignore-parent`, `-uuu` (RgIgnoreKinds, RgIgnoreNoParent, RgIgnoreUnrestrictedBinary) |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:577 | each directory listed twice; the three ignore files opened blindly in every directory |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp | globs matched against the printed path: with an absolute or `../` operand an anchored `-g 'src/*.c'` never matches |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:627 | `--no-require-git` inside a repository still skips `.gitignore` files above its root (plan: every ancestor); check against rg |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:631 | `.git/info/exclude` above the operand still applies under `--no-ignore-parent`; check against rg, add a test |
| low | commented | src/components/BuiltinCommands/commands/rg/Rg.cpp:729 | `--type-list` returns before later flags are checked; `-u` applied after the loop, so a later `--ignore`/`--no-hidden` cannot undo it |
| low | commented | src/components/BuiltinCommands/commands/rg/RgIgnore.cpp:61 | misleading comment about `\#` |
| low | commented | src/components/BuiltinCommands/commands/rg/RgIgnore.h:46 | comment names `pi..`/`qi..`, not the parameters; new files lack a final newline |
