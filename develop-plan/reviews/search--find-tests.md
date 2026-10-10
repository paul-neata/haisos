# Review: search--find-tests (PR #64)
- Verdict: merged
- Merged as: 087edfc, version 0.5.22
- Tokens: claude 347022, ollama input 1302128, ollama output 492209

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in b7e30f5 | src/components/BuiltinCommands/commands/find/Find.cpp:196 | `find ''`: Stat("") resolved to the working directory and `file.path.back()` ran on an empty string (UB); now `'': No such file or directory`, exit 1, as GNU |
| high | fixed in b7e30f5 | src/components/BuiltinCommands/commands/find/FindParser.cpp:407 | with -warn, -d printed the global-option warning before its deprecation warning (the test asserted it); GNU prints the deprecation first |
| high | fixed in b7e30f5 | src/components/BuiltinCommands/commands/find/FindTests.cpp:509 | `-perm u=`, `=`, `-u=` (empty permission list) refused; GNU accepts them |
| medium | fixed in b7e30f5 | src/components/BuiltinCommands/commands/find/FindTests.cpp:449 | `-perm ''` and `-perm -` taken as mode 0; GNU: invalid mode |
| medium | fixed in b7e30f5 | src/components/BuiltinCommands/commands/find/FindTests.cpp:1165,1186 | `-user +5` / `-group -5` taken as numeric ids; GNU refuses them as unknown names |
| low | commented | src/components/BuiltinCommands/commands/find/FindTests.cpp:713 | `-type D` never matches (per the plan); GNU 4.9 on Linux refuses it ("Solaris doors are not supported"), exit 1 |
| low | commented | src/components/BuiltinCommands/commands/find/FindTests.cpp:59 | ParseTimeCount refuses forms strtod accepts (`-mtime 1e3`, `-mmin ' 1'`) |
| low | commented | src/components/BuiltinCommands/commands/find/FindTests.cpp:263 | time-window boundaries flipped: [N,N+1) vs GNU's (N,N+1]; differs only at exact boundaries |
| low | commented | src/components/BuiltinCommands/commands/find/FindExpression.h:92 | FindParseState::firstNonOption unused; the parser keeps a local copy |
| low | commented | src/components/BuiltinCommands/BuiltinDate.cpp:45 | outside the plan's file list but harmless: names a temporary before strtoll; date/touch unchanged |
