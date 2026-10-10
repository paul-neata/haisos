# Review: search--xargs (PR #66)
- Verdict: merged
- Merged as: fedcc74, version 0.5.24
- Tokens: claude 206158, ollama input 435562, ollama output 291432

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| critical | fixed in 195f143 | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:407 | run state kept as members of the one shared XargsCommand: concurrent xargs raced, `xargs xargs echo` left m_settings dangling; now one instance per run (XargsRunningXargsKeepsItsOwnState) |
| high | fixed in 195f143 | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:1148 | --process-slot-var counted 0, 1, 2...; GNU 4.9.0 gives 0 every time |
| high | fixed in 195f143 | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:1120 | -p/-o opened /dev/tty and failed; now stdin is the terminal (-p via BuiltinPrompt, -o hands the child xargs's stdin) |
| medium | commented | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:1179 | after a stop, the rest of the current line's items (-n 1) can still start children |
| medium | commented | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:487 | -a: an existing file that will not open is taken as empty; every Stat failure reported as "No such file or directory" |
| medium | commented | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:1160 | with -a GNU leaves the child's stdin as xargs's own; here it is empty (the plan's choice), undocumented |
| medium | commented | tests/unit/components/BuiltinCommands.unittests/XargsTest.cpp:393 | the plan's two-run -I empty-child-input case is missing |
| low | commented | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:735 | strtol overflow not detected for -n/-L/-s |
| low | commented | src/components/BuiltinCommands/commands/xargs/Xargs.cpp:11 | unused includes DescriptorLineReader.h and <climits> |
