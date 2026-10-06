# Review: hsh--parser (PR #31)
- Verdict: merged
- Merged as: d26cf22, version 0.4.13
- Tokens: claude 294357, ollama input 158416, ollama output 94423

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 696979d | src/components/BuiltinCommands/commands/hsh/HshParser.cpp:267,665 | sourceText of items inside a compound list, and of a function with more on its line, was cut before the heredoc body was read, so the body was missing (`{ cat <<E & }`, `case x in a) cat <<E;; esac`, `f() { cat <<E; } && x`); round-trip tests compared dumps, which never show bodies; texts now filled once the complete command is read; exact-text tests added |
| high | fixed in 696979d | src/components/BuiltinCommands/commands/hsh/HshParser.cpp:721 | An error inside a closed `$(...)` or backquote was flagged incomplete, so an interactive shell would keep asking for input after `echo $(if)`; now never incomplete; interactive test added |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:885 | Heredoc continuation checks only the last backslash, so an escaped `\\` before a newline joins with the next line, unlike dash; join only on an odd number of trailing backslashes |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshAst.h:73,128,133 | Comments point to "Source text" in HshParser.h and "AST dump" in the hsh CLAUDE.md; neither section exists, the dump format is documented nowhere |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:897 | Under `<<-`, leading tabs are also stripped from a continuation line; dash probably does not (to check) |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshParser.cpp:509 | Only `esac` is reserved at the start of a case pattern; what dash does with `case x in fi)` to check |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshParser.cpp:109 | Source copied twice in `Impl` and again in `ParseProgram` |
