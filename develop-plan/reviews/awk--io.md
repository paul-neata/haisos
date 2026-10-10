# Review: awk--io (PR #84)
- Verdict: merged
- Merged as: e566579, version 0.5.42
- Tokens: claude 411174, ollama input 398414, ollama output 123726

Clean-room re-check before the task (Opus, in the Claude count). Security: the gate's 17 REVIEW lines benign -- files only through context.IO(), pipes through IFileIO::CreatePipe, commands through StartProgram/RunProgramAndWait; no popen/system()/host shell. Clean-room check: no finding.

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 6942f23 | src/components/BuiltinCommands/commands/awk/AwkStreams.cpp:77 | `print | "/dev/stderr"` (and `| "-"`, `| "/dev/stdout"`) wrote to awk's own stream instead of running the command (only `>`/`>>` are special), and registered a new stream entry on every print; AwkIoTest.SpecialFiles |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkInterpreter.cpp:1289 | the redirection target is evaluated after the print arguments; gawk first (`print (x="A") > ("f" x)` writes `f` in gawk, `fA` here) |
| medium | commented | src/components/BuiltinCommands/commands/awk/AwkStreams.cpp:127 | after a quietly failed write, close() of that file gives 0, not the plan's -1 |
| low | commented | src/components/BuiltinCommands/commands/awk/AwkStreams.cpp:288 | fflush("-") after `print > "-"` returns 0 without flushing awk's stdout |
| low | commented | src/components/BuiltinCommands/commands/awk/CLAUDE.md | "hsh mapping its own children the same way" inaccurate (awk maps 271/269); "awk--io's redirections" stale |
