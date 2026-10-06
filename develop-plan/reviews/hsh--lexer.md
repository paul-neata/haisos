# Review: hsh--lexer (PR #30)
- Verdict: merged
- Merged as: 3ccc87c, version 0.4.12
- Tokens: claude 280991, ollama input 121098, ollama output 75276

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in ba45289 | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:420 | `${#:-x}` lexed as Bad; plan rule 3 and dash read it as `$#` with `:-`; test added |
| high | fixed in ba45289 | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:835 | `${...}` operands in an unquoted heredoc body read as unquoted text: `${x:-it's}` threw "Unterminated quoted string" (dash prints `it's`); now read as inside double quotes; test added |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:849 | An error lexing a heredoc body whose delimiter line was found (e.g. `${x`) still says Incomplete(): an interactive shell would show PS2 forever |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:522 | Inside double quotes, `'` in a `#` `##` `%` `%%` pattern operand is a plain character; dash treats it as quoting (`x=abc; echo "${x#'a'}"` prints `bc`) |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:862 | Heredoc delimiter matched on raw lines: unquoted body `a\<nl>E<nl>E` ends at the first `E`, dash gives `aE` (as planned, but differs from dash and is undocumented) |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.h:80 | Declared `~Lexer()` suppresses implicit moves: a Lexer can be neither copied nor moved |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshLexer.h:90 | Private constructor (shared source, start, line, options) defined but never used |
