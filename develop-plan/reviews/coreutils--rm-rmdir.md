# Review: coreutils--rm-rmdir (PR #49)
- Verdict: merged
- Merged as: bbc8a48, version 0.5.7
- Tokens: claude 54576, ollama input 128262, ollama output 46191

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 5daa1e4 | src/components/BuiltinCommands/BuiltinRemove.cpp:153 | `rm -ri` with a declined "descend into directory" still prompted for the ancestors and, on yes, failed with "Directory not empty" (status 1); GNU 9.4 leaves them alone silently (status 0). RemoveEntry now returns Done/Skipped/Failed, with a test |
| medium | commented | src/components/BuiltinCommands/BuiltinRemove.cpp:147 | `rm -r` uses Stat, which follows links: on a PHYSICAL filesystem it descends into a symlinked directory and empties its target, even outside the tree; GNU removes only the link. DELETE already behaves so, but rm makes it reachable from any process |
| low | commented | src/components/BuiltinCommands/commands/rmdir/Rmdir.cpp:147 | `rmdir -p a//b` names and removes `a/`, where GNU says `a` |
| low | commented | src/components/BuiltinCommands/BuiltinRemove.cpp:160 | `rm -r --no-preserve-root /` builds children as `//bin`, where GNU shows `/bin` |
| low | commented | src/components/BuiltinCommands/commands/rmdir/Rmdir.cpp:47 | The `-v` line hard-codes `rmdir: ` instead of using context.Name() |
