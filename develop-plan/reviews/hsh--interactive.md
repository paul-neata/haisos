# Review: hsh--interactive (PR #40)
- Verdict: merged
- Merged as: 0ad7006, version 0.4.22
- Tokens: claude 385565, ollama input 299809, ollama output 118779

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | commented | src/components/BuiltinCommands/commands/hsh/Hsh.cpp:75 | --help notes still say only "PS4 is not expanded"; PS1 and PS2 not being expanded is a new documented exception and belongs in the --help notes too (builtin rule) |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:178 | With `-i -c` an error moves on to the next command in the string; dash drops the rest and exits 2 (the plan asked for hsh's behaviour) |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshManPage.cpp:213 | DIFFERENCES FROM DASH is one paragraph, not one line per item; leaves out the read-only-PWD `cd` and `test -ef` exceptions |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshManPage.cpp:194 | `readonly nm.` should read `readonly names` |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:56 | New branch for an empty redirection target tested for reading only; the create case ("Directory nonexistent") untested |
