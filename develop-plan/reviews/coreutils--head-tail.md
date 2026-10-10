# Review: coreutils--head-tail (PR #63)
- Verdict: merged
- Merged as: 76bca30, version 0.5.21
- Tokens: claude 268978, ollama input 274801, ollama output 136020

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in bde90ed | src/components/BuiltinCommands/commands/tail/Tail.cpp:604 | following started with the last FILE that printed as "last printed", so `tail -n0 -f a b` printed a second `==> b <==` header; GNU starts with the last FILE (test TailFollowStartsWithTheLastFileAsTheLastPrinted) |
| high | fixed in bde90ed | tests/unit/components/BuiltinCommands.unittests/TailTest.cpp:200 | TailFollowEndsWithThePidProcess could never fail; it now checks the follower still follows while the --pid process runs |
| medium | commented | src/components/BuiltinCommands/commands/tail/Tail.cpp:700 | name mode: a path coming back untailable (a directory) prints "has appeared;  following new file" every round; GNU prints "replaced with an untailable file" once |
| medium | commented | src/components/BuiltinCommands/commands/tail/Tail.cpp:715 | descriptor mode with --retry: a file appearing later is picked up without GNU's "has appeared" message |
| medium | commented | src/components/BuiltinCommands/commands/head/Head.cpp:163, src/components/BuiltinCommands/commands/tail/Tail.cpp:244 | the -c byte rings erase from the front of a string on every 64 KiB read (O(N) per read) |
| low | commented | src/components/BuiltinCommands/commands/tail/Tail.cpp:726 | a comment about the path being gone sits on the truncation branch |
| low | commented | src/components/BuiltinCommands/commands/tail/Tail.cpp:99 | `+` alone (GNU's obsolete form) is taken as a file name |
| low | commented | src/components/BuiltinCommands/commands/tail/Tail.cpp:156 | `-s inf` is refused; GNU accepts it |
| low | commented | src/components/BuiltinCommands/commands/tail/Tail.cpp:152 | errno/ERANGE used without `<cerrno>` (a possible cause of the Windows red) |
| low | commented | src/components/BuiltinCommands/commands/head/Head.cpp:98 | the obsolete-form overflow message leaves out the b/k/m letter GNU quotes |
