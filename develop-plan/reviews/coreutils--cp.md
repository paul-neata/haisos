# Review: coreutils--cp (PR #50)
- Verdict: merged
- Merged as: b8937f4, version 0.5.8
- Tokens: claude 172779, ollama input 146774, ollama output 106351

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in 958a88c | src/components/BuiltinCommands/BuiltinCopy.cpp:430,475 | When setting the times failed, the message was "failed to preserve times for"; GNU and the plan say "preserving times for" |
| high | fixed in 958a88c | src/components/BuiltinCommands/BuiltinCopy.cpp:410 | A stop in the middle of copying a file left a partial file but CopyPath still reported success; mv would then have removed its source |
| medium | commented | src/components/BuiltinCommands/commands/cp/Cp.cpp:42-76,448-462 | kDirMode, q, JoinPath, CreateFailedReason and StatMissingReasonOf are copies of the BuiltinCopy.cpp helpers |
| medium | commented | src/components/BuiltinCommands/commands/cp/Cp.cpp:282-298,341-344 | The backup mode is worked out option by option, not once at the end as GNU does: -S alone ignores VERSION_CONTROL, -b resets an earlier --backup=WORD, a bad VERSION_CONTROL is reported "for 'backup type'" instead of "for '$VERSION_CONTROL'" |
| medium | commented | src/components/BuiltinCommands/commands/cp/Cp.cpp:347-361 | --update=WORD given after -n overrides -n; in GNU 9.4, -n wins |
| low | commented | src/components/BuiltinCommands/BuiltinCopy.cpp:331 | cp -r also refuses a device given directly on the command line (as GNU); the help notes should say so |
| low | commented | src/components/BuiltinCommands/BuiltinCopy.cpp:365 | With -i and no prompt object, cp overwrites without asking |
| low | commented | src/components/BuiltinCommands/commands/cp/Cp.cpp:42 | Uses S_IRWXU without including <sys/stat.h> directly (it comes through FilesystemUtils.h) |
| low | commented | src/components/BuiltinCommands/BuiltinCopy.h:61 | No newline at end of file |
