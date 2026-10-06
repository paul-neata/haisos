# Review: hsh--shell-builtins (PR #38)
- Verdict: merged
- Merged as: 83f2e46, version 0.4.20
- Tokens: claude 325091, ollama input 399315, ollama output 173318

| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | fixed in c27d525 | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:380 | The -x trace went to the command's redirected stderr (`echo a 2>f` traced into f); plan and dash trace to the stderr in place before the redirections; fixed through Shell::WriteTo, doc corrected, test added |
| high | fixed in c27d525 | tests/unit/components/Hsh.unittests/HshBuiltinsTest.cpp | Planned tests missing: cd affecting children (`cd /docs; hsh -c pwd`), PWD/OLDPWD, cd sub/../HOME, export/unset reaching children, a read-only prefix assignment, `set -C`, `set -- -x`, `set - a`, several test/[ grammar cases; added, each checked against dash 0.5.12 |
| low | fixed in c27d525 | src/components/BuiltinCommands/commands/hsh/HshBuiltinVariables.cpp:1 | std::numeric_limits used without <limits> |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshBuiltinCd.cpp:115 | cd with a read-only PWD/OLDPWD is fatal; dash prints `cd: PWD: is read only` and carries on with status 2 (documented deviation) |
| medium | commented | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:150,169,338 | The noexec condition is written out three times; one NoExec() helper |
| low | commented | src/components/BuiltinCommands/commands/hsh/HshBuiltinSet.cpp:105 | `set -i`/`set -s` change options.interactive/stdin mid-run, and `interactive` is what lets noexec be bypassed |
| low | commented | src/components/BuiltinCommands/commands/hsh/Hsh.cpp:9 | HshVersion() declared in HshBuiltins.h but defined in Hsh.cpp |
