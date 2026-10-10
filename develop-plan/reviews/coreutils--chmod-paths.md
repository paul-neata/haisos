# Review: coreutils--chmod-paths (PR #52)
- Verdict: merged
- Merged as: 22cefe9, version 0.5.10
- Tokens: claude 175036, ollama input 192956, ollama output 96294

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/chmod/Chmod.cpp:408 | `chmod -w` with no FILE prints `missing operand after '-w'`; GNU 9.4 prints plain `missing operand` when the mode came from an option-like word (the `after` form only for a mode taken from the operands) |
| low | commented | src/components/BuiltinCommands/commands/chmod/Chmod.cpp:353 | Mode letters inside an option cluster (`-vw`, `-Rw`) and an option argument that looks like a mode (`--reference -w`) are not handled as GNU's getopt does (error paths only) |
| low | commented | src/components/BuiltinCommands/commands/realpath/Realpath.cpp:259 | With `-e`, GNU requires the `--relative-to`/`--relative-base` DIR to be a directory (`Not a directory`); a regular file is accepted |
| low | commented | tests/unit/components/BuiltinCommands.unittests/ChmodPathsTest.cpp:17 | ChmodValidatesAndChangesNothing only checks the file's content, which chmod could never alter |
