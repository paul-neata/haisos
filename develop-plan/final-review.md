# Final review

- Reviewed: develop @ 3e9c9bc (whole develop at 8eca554, then 8eca554..3e9c9bc)

## Overview
The develop does what `goal.md` asks for. Every process gets its own
descriptor table with stdin, stdout and stderr; pipes come from `IPipeService`
through `IFileIO::CreatePipe`. Exit codes follow shell rules: modulo 256, 141
for a broken pipe, 143 for a stop. `haisos` exits with the code of the first
failing `RUN`. The console is raw, with stderr going to the host's stderr. The
develop also adds `wc`, `man`, `cat` reading stdin and `ls` writing to a pipe,
and `hsh`, a dash reimplementation of about 9 000 lines with ~4 000 lines of
unit tests and an end-to-end haisos test for all six scenarios.

Security holds:
- Every process, hsh and its children included, still reaches the OS only
  through `ICurrentProcess`: `Process().OS()` is asked for at each use, and
  pipes are reached through `IFileIO`.
- `man` reads program data only.
- Lua stays sandboxed, with exit/print latching across coroutines and error
  objects rendered inside the protected call.
- No build or test file does more than build or test.
- No text in code, docs or tests is aimed at AI.

All nine gate BLOCKs (`.claude/`, `scripts/develop/`) come from the user's own
host commit 956b65e ("Minor update to develop-* skills"). No task PR touched a
protected path. Its `git_ssh.sh` sets only this clone's `core.sshCommand`, and
only to an agent socket the user owns that is proven to reach origin. That is
benign.

Every acceptance scenario can be met by the code as it stands, with two catches
in `goal.md` itself (findings below, open for the user; `goal.md` is not
changed here). First, scenario 3 runs `hello` against `hi*`, which dash answers
`plain`; the tests run `hi`. Second, the preamble's `/abc.txt` has no final
newline as written.

The risks that remain are dash/GNU differences left over from the task reviews,
plus three new findings:
- an endless in-shell producer piped into an in-shell reader never ends and its
  memory grows without bound, and this is undocumented;
- a quadratic unbounded-pipe read;
- comments that point at `develop-plan/` and at task ids, which `/develop-close`
  deletes.

Several follow-ups noted in memory are already resolved on develop:
- a background `$(...)` is no longer expanded in the foreground (`IsChildStage`
  now sends such a list to a child hsh);
- `read`'s IFS merging, `IsChildStage`'s redirection targets, `"$@$@"` and the
  heredoc-body Incomplete flag are fixed;
- `case x in fi)` matches dash 0.5.12, which accepts `fi` as a pattern.

The dash comparisons below were checked against dash 0.5.12 and GNU coreutils
wc on the host.

## Findings
| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| high | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshBuiltinCd.cpp:115-118 | A read-only PWD or OLDPWD makes `cd` a fatal error (`hsh: 1: PWD: is read only`, script ends). dash prints `dash: 1: cd: PWD: is read only`, keeps the new directory, returns 2 and goes on (from #38) |
| high | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:174 | `strtol` accepts leading blanks and a sign, so `x=' 3'; echo >&$x` and `>&+1` duplicate fd 3/1. dash says `Syntax error: Bad fd number` (exactly one digit) (from #36) |
| high | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:917-923 | Under `<<-`, leading tabs are also stripped from a backslash-continued line. dash strips only at the start of a physical body line: `cat <<-E` / `\ta\` / `\tb` / `\tE` gives `a\tb` in dash and `ab` in hsh (from #31) |
| high | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/wc/Wc.cpp:426-430 | An empty `--files0-from` list returns before the total rule. GNU still prints the total: `0 0 0 total` with `--total=always`, `0 0 0` with `--total=only`, `0 0 total` with `-lc --total=always` (from #28; builtin rule: GNU byte for byte) |
| high | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/Hsh.cpp:73-76 | The `--help` notes leave out documented exceptions that the BuiltinCommands table lists: PS1/PS2 are not expanded (only PS4 is mentioned); `test -r -w -x -O -G` only check existence; `-h`/`-L` are never true; `-ef` compares resolved paths. The root CLAUDE.md requires every exception in both places (from #40) |
| medium | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshUnboundedPipe.cpp:44 | `m_bytes.erase(0, n)` on every read shifts the whole remaining buffer, so draining N bytes in 4 KiB reads costs O(N^2/4096): `x=$(cat big)` or an in-shell stage pipe with tens of MB takes minutes to hours |
| medium | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:375-440 (ExecutePipelinedStages, pass 2 at :432) | In-shell stages run one after another through an unbounded pipe. An endless in-shell writer before an in-shell reader (`while :; do echo y; done \| while read l; do break; done`) never ends and its buffered output grows without bound, where dash ends at once. This is not a documented exception (`--help` notes, man DIFFERENCES FROM DASH, BuiltinCommands table) |
| medium | promoted -> final--review-fixes | interfaces/IFileDescriptor.h:29-34; src/components/Console/ConsoleDescriptors.cpp:79, .h:64; src/components/BuiltinCommands/commands/hsh/HshShell.cpp:1032; hsh/CLAUDE.md:485; src/components/Unicode/CLAUDE.md:40; tests/haisos/hsh.haisostest/hsh.haisostest.js:4,87; tests/unit/components/Hsh.unittests/HshControlFlowTest.cpp:250,262; src/components/Factory/CLAUDE.md:8; src/components/Filesystem/FilesystemUtils.h:107 | Stale text. Some contradicts the code: "only the console descriptors exist so far", "reserved for pipes (the pipes--pipe-service task)", "jailed". Some points at `develop-plan/goal.md` ("D7", "scenario 3") or at task ids, all of which `/develop-close` deletes before master |
| medium | promoted -> final--review-fixes | tests/unit/components/Filesystem.unittests/FileDescriptorTest.cpp:162 | Nothing attempts a write through a descriptor opened read-only via `ReadOnlyFileSystem`. Passing the inner descriptor up is safe only because the leaf enforces the access mode, and no test pins that (from #19) |
| medium | promoted -> final--review-fixes | tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp:13, HaisosOSTest.cpp:183, BuiltinCommands.unittests/BuiltinCommandsTest.cpp:35 | `ReleaseCountingDescriptor` is copied three times, identically; it belongs in one `tests/mocks/` header (from #20) |
| medium | promoted -> final--review-fixes | src/components/HaisosOS/ProcessFileIO.cpp:178-201 | `CreatePipe` takes the table lock separately for each `AddDescriptor` and for the rollback `CloseDescriptor`. A concurrent `Dup2` onto the read slot would make the rollback close another descriptor, and the two slots are not "the two lowest free" atomically (from #24) |
| medium | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:280,300,654 | The noexec condition `noexec && !interactive` is written out three times; it should be one `NoExec()` helper (from #38) |
| medium | open (question for the user) | develop-plan/goal.md:88-92 (preamble) | `CREATE /abc.txt multiline END` with no empty line before `END` writes `one\ntwo\nthree`, 13 bytes with no final newline (root CLAUDE.md: the newline before the marker is not part of the text). Scenarios 1 and 2 as written would then print `2` and `13 /out.txt`, not `3` and `14 /out.txt`. hsh.haisostest.js adds an empty line before `END`. Fix the preamble in goal.md, or accept the test's version |
| low | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshManPage.cpp:194,213-224 | DIFFERENCES FROM DASH is one paragraph rather than one line per item, and leaves out `-ef` (and, once fixed, nothing about cd); `readonly nm.` should read `readonly names` (from #40) |
| low | promoted -> final--review-fixes | src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:62 | `Save` skips a slot already saved anywhere in the scope, so a failing second `Apply` on the same scope would not put back the slots its first `Apply` changed. There is no reachable trigger today (one `Apply` per scope); scoping the de-duplication to the current `Apply` makes it safe (from #36) |
| low | open | src/components/BuiltinCommands/commands/hsh/HshShell.cpp:608 | `const std::vector<std::string> keepPositional = std::move(...)` makes both the save and the restore copies (from #39) |
| low | open | CLAUDE.md (Exit codes) | "127 when one or no process could be started" is unclear; it means 127 for a RUN that never started, and also when none could (from #23) |

## Review of 8eca554..3e9c9bc

### Overview
One task was merged after the whole-develop review: final--review-fixes (#41,
af6f65c, 0.4.23). It fixes every finding promoted above:
- `cd` with a read-only PWD/OLDPWD now reports it and goes on, status 2;
- a `>&`/`<&` target must be exactly one digit;
- `<<-` no longer strips the tabs of a backslash-continued line;
- an empty `--files0-from` list still prints the total that `--total=always`
  or `--total=only` asks for;
- the hsh `--help` notes and the man page's DIFFERENCES FROM DASH list every
  documented exception, including the in-shell pipeline limitation, which is
  now also in both `CLAUDE.md` files;
- the unbounded pipe drains in linear time (a read offset, compacted once the
  consumed prefix passes 64 KiB and half of the buffer);
- `ProcessFileIO::CreatePipe` finds and fills both slots under one lock, with
  no rollback;
- `RedirectionScope::Save` de-duplicates within the current `Apply` only;
- `NoExec()` is one helper;
- `ReleaseCountingDescriptor` is in `tests/mocks/`;
- the stale text and `goal.md`/task-id references are gone, except those
  already known.

Each behaviour fix comes with a test that fails on the old code. The
one-free-slot `CreatePipe` test only pins the outcome; that is already known.
Versions are bumped and the tables match: hsh 1.0.1, wc 1.0.1.

Security holds:
- the changes touch only builtin internals, the descriptor table, comments and
  tests;
- nothing reaches out of a process other than through `ICurrentProcess`;
- the six gate REVIEW lines (five `CLAUDE.md` files and
  `hsh.haisostest.js`) are benign. The changed lines are documentation of the
  code and two comments, and none of them is aimed at AI.

A search of `src`, `interfaces` and `tests` for text pointing at `goal.md`,
`D<n>` items, `develop-plan/` or task ids finds only the three
`AgentInputLoop` lines already known. The two open mediums of the task review
are worth fixing before master:
- `cd` should stop after the first read-only report;
- the `AgentInputLoop` comments still cite D7.

dash 0.5.12 on the host also shows a third side of the cd one: dash prints
nothing for `cd -` once a read-only variable has been reported (the error
leaves cd before the print), and hsh still prints the directory.

### Findings
| Severity | Status | Where | Finding |
|----------|--------|-------|---------|
| medium | open | src/components/BuiltinCommands/commands/hsh/HshBuiltinCd.cpp:130-132 | After a read-only OLDPWD/PWD report, `cd -` still prints the new directory. dash 0.5.12 prints nothing: `cd /usr; readonly OLDPWD; cd -` gives only `dash: 1: cd: OLDPWD: is read only`, status 2. Fix it together with the known "stop after the first report" finding of #41: return 2 right after the report, before the print |
| low | open | src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:58-60 | `Keep()` clears `m_saved` but leaves `m_applyFirst`, so a `Save` after a `Keep` with no new `Apply` would start past the end (UB). Today it cannot happen (`Save` runs only inside `Apply`, which resets it first). Reset `m_applyFirst = 0` in `Keep()` |
