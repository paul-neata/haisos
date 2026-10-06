# Review: hsh--redirections (PR #36)
- Verdict: merged
- Merged as: 6c7185e, version 0.4.18
- Tokens: claude 311235, ollama input 129265, ollama output 49476

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 982b291 | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:893 | The new heredoc line-joining loop called `raw.back()` unchecked: under `<<-` a last body line of only tabs, without newline, strips to empty (undefined behaviour; the old `raw.size() >= 2` guard was dropped); `raw.empty()` guard and a lexer test added |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:174 | `strtol` accepts leading whitespace and a sign: `>&$x` with `x=' 3'` or `x=+3` reads 3; dash wants exactly one digit |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:53 | `Restore` goes through `PlaceDescriptor`, which needs a free slot: with a full table the restore fails silently and the redirection stays |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:62 | `Save` skips duplicates across the whole scope: a second failing `Apply` on one scope does not put back slots saved by the first |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshGlob.cpp:23 | `ComponentBracketEnd` skips a `[:...:]` class with `find(":]")`, which can run past a `/` (`[[:a/b:]]`) |
| low | commented | src/components/BuiltinCommands/CLAUDE.md:118 | "heredocs `<<`-`<<-`" should read "`<<` and `<<-`" |
