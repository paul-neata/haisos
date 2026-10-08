# Review: coreutils--names-env (PR #51)
- Verdict: merged
- Merged as: 59685dd, version 0.5.9
- Tokens: claude 191377, ollama input 227571, ollama output 71533

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in dee73f6 | src/components/BuiltinCommands/commands/env/Env.cpp:378 (+351) | `env -S` error messages did not match GNU: a bad escape printed `invalid sequence '\q in -S` without its closing quote; a trailing backslash printed `invalid sequence '\' in -S` (GNU: `invalid backslash at end of string in -S`). Tests added |
| medium | commented | src/components/BuiltinCommands/commands/env/Env.cpp:229 | After a `-S` re-parse an option's argument is written back as a separate word; for an Optional long option (`--block-signal=INT -S ...`) it becomes COMMAND |
| medium | commented | src/components/BuiltinCommands/commands/env/Env.cpp:181 | With `-C DIR`, a relative COMMAND is resolved against the caller's directory; GNU resolves it in DIR |
| low | commented | src/components/BuiltinCommands/commands/env/Env.cpp:246 | A not-treated option is reported twice when `-S` is used |
| low | commented | src/components/BuiltinCommands/commands/env/Env.cpp:142 | `-C ''` is taken as no `-C`; GNU fails with `cannot change directory to ''` |
| low | commented | src/components/BuiltinCommands/commands/env/Env.cpp:133 | An operand `=VALUE` is taken as COMMAND (exit 127); GNU prints `cannot set '': Invalid argument`, exit 125 (the code follows the plan) |
| low | commented | src/components/BuiltinCommands/commands/env/Env.cpp:277 | `-S` lacks some GNU rules: `\\` and `\'` inside '...', `\c` inside "..." an error, an invalid `${NAME}` an error |
| low | commented | tests/unit/components/BuiltinCommands.unittests/EnvTest.cpp:217 | EnvStopStopsTheChild would pass even if the child sleep kept running |
