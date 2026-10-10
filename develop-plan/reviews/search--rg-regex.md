# Review: search--rg-regex (PR #70)
- Verdict: merged
- Merged as: 53609bf, version 0.5.28
- Tokens: claude 201644, ollama input 829601, ollama output 357356

Clean-room check (rule added after this PR was cut): no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 3c66bba | src/components/BuiltinCommands/commands/rg/Rg.cpp:84 | `-F` turned bytes above 0x7f into `\xhh`, which the Rust-syntax translator re-encoded as UTF-8: `rg -F 'é'` never matched; now kept raw (RgCaseFlags), CLAUDE.md -F/-S wording fixed |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgRegex.cpp:164 | a 0-minimum quantifier on an assertion keeps it (`^*abc`, `\b?x`, `^{0,2}`); Rust makes it optional; RgRegexTest.cpp:207-209 pass either way |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgRegex.cpp:221 | a quantifier after a multi-byte UTF-8 literal or `\u{..}` repeats only its last byte (`é+`) |
| medium | commented | src/components/BuiltinCommands/commands/rg/RgSearch.cpp:602 | the unreadable-directory plan item (`Permission denied (os error 13)`, exit 2) is not implemented: ReadDirectory cannot tell a refused listing from an empty one |
| low | commented | src/components/BuiltinCommands/commands/rg/RgRegex.cpp:919 | ParseClassEscape returns false without an error (unreachable) |
| low | commented | src/components/BuiltinCommands/commands/rg/RgRegex.cpp:658 | FailUnicodeClass's `kind` parameter unused |
| low | commented | src/components/BuiltinCommands/CLAUDE.md | rg row: the refusal list runs into the context clause |
| low | commented | src/components/BuiltinCommands/commands/rg/RgRegex.h:39 | no newline at end of file |
