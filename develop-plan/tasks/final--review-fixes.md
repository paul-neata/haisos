# Task final--review-fixes: the whole-develop review's fixes

- Rock: final
- Depends on: every other task (all merged)
- Size: ~400 changed lines in ~25 files (many one- or two-line doc edits)
- Plan checked against: develop @ 8eca554
- PR title: Final review fixes: hsh/wc match dash/GNU, docs, test hygiene

## Goal

What `develop-plan/final-review.md` promoted, and nothing more:

1. `cd` with a read-only `PWD` or `OLDPWD` behaves as dash: it reports and
   goes on.
2. A `>&`/`<&` target must be exactly one digit, as dash requires.
3. Under `<<-`, a backslash-continued heredoc line keeps its leading tabs, as
   dash does.
4. `wc` prints the total for an empty `--files0-from` list when `--total`
   asks for it, as GNU wc does.
5. hsh's `--help` notes and its manual page list every documented exception.
6. The unbounded pipe drains in linear time.
7. The endless in-shell-pipeline limitation is documented.
8. Stale comments and docs are fixed.
9. Test hygiene: a write through a read-only wrapper is tested; one shared
   `ReleaseCountingDescriptor`; `CreatePipe` under one lock; one `NoExec()`
   helper; `RedirectionScope::Save` scoped to its `Apply`.

hsh goes to version `1.0.1` and wc to `1.0.1`, per the builtin rule: bump the
version whenever a builtin's behaviour changes.

## Context

Read first:
- the root `CLAUDE.md` ("Builtin Commands" rules, "Exit codes");
- `src/components/BuiltinCommands/CLAUDE.md` (the commands table);
- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` ("Pipelines,
  subshells, jobs", "Redirections");
- the files named below.

hsh tests use `ExpectSh({script, out, err, status}, "<Name>")` from
`tests/unit/components/Hsh.unittests/HshShellFixture.h`. wc tests use
`RunCaptured` from `BuiltinCommandsFixture.h`.

Expected outputs were checked on dash 0.5.12 and GNU coreutils wc 9.4. hsh
writes `hsh:` where dash writes `dash:`.

## Changes

### 1. `cd` and a read-only PWD/OLDPWD
File: `src/components/BuiltinCommands/commands/hsh/HshBuiltinCd.cpp`, lines
~113-118.

Today `shell.AssignVariable(...)` throws `ShellError` (fatal) for a read-only
variable. Change it as follows:

1. Once `ChangeDirectory` has succeeded, the change stands.
2. For each of `OLDPWD` (first) and `PWD`, in that order:
   - if `shell.State().variables.IsReadonly(name)`, call
     `shell.Report("cd: " + name + ": is read only")` and remember that it
     failed;
   - otherwise assign and export it as today.
3. Print the CDPATH line as today.
4. Return 2 if either variable was read-only, else 0.

No exception is thrown. A `-e` shell then exits with 2, as for any failing
command, because the status is not 0 in an untested context.

Remove the matching exception from:
- the `hsh` row of `src/components/BuiltinCommands/CLAUDE.md` ("`cd` with a
  read-only PWD is a fatal error where dash only warns");
- hsh's `CLAUDE.md`, if it mentions it.

### 2. A redirection fd: exactly one digit
File: `src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:170-177`
(DupInput/DupOutput).

Replace the `strtol` check with this rule: the target is valid only if
`target.size() == 1 && target[0] >= '0' && target[0] <= '9'`. Anything else
(except `-`) is `m_shell.Fail("Syntax error: Bad fd number")`, as today. The
source is then `target[0] - '0'`. `<cstdlib>` may go if nothing else uses it.

### 3. `<<-` and continuation lines
File: `src/components/BuiltinCommands/commands/hsh/HshLexer.cpp:917-923`.

Delete the `if (hd->stripTabs) { ... next.erase(0, i); }` block that strips
the joined continuation line (`next`). Leave the strip at :878, which applies
to the start of each physical body line. dash strips only at the start of a
line it reads fresh: a line reached through a backslash-newline is part of the
same logical line. The delimiter comparison is unchanged.

### 4. wc: total for an empty --files0-from list
File: `src/components/BuiltinCommands/commands/wc/Wc.cpp:426-430`.

When `names` is empty after `SplitNames(list)`:
- with `--total=always` or `--total=only`, print the total line of zero counts
  with the selected counts. The width is 1, since there are no files to size
  from. `always` prints the `total` label; `only` prints the counts alone,
  joined as for any line;
- with `--total=auto` or `never`, still print nothing.

Keep any error already reported (and status 1) for zero-length names in the
list.

GNU's outputs, byte for byte:
- `--files0-from=empty --total=always` gives `0 0 0 total\n`, status 0;
- `--total=only` gives `0 0 0\n`;
- `-l --total=only` gives `0\n`;
- `-lc --total=always` gives `0 0 total\n`;
- a list holding just one NUL, `--total=always`: stderr
  `wc: <list>:1: invalid zero-length file name\n`, stdout `0 0 0 total\n`,
  status 1.

Bump the wc version to `1.0.1` in `Wc.cpp`, and in the BuiltinCommands
`CLAUDE.md` table.

### 5. hsh's --help notes and manual page
File: `src/components/BuiltinCommands/commands/hsh/Hsh.cpp:73-76`
(`help.notes`).

The notes must state every documented exception of the `hsh` table row. Keep
them short, a few lines.
- Replace "PS4 is not expanded" with "PS1, PS2 and PS4 are not expanded".
- Add a line: "test's -r -w -x -O -G only test that the file exists; -h and -L
  are never true; -ef compares resolved paths."
- Add a line: "Two in-shell pipeline stages run one after the other: an
  endless one before another in-shell stage never ends." This is the
  limitation in item 7.

Bump `HshVersion()` to `"1.0.1"`.

File: `src/components/BuiltinCommands/commands/hsh/HshManPage.cpp`.
- Line 194: change `readonly nm.` to `readonly names` (keep the column
  alignment).
- DIFFERENCES FROM DASH (:213-224): write one item per line (an item that runs
  past ~70 columns continues on an indented line). Add an item for `-ef`
  (compares resolved paths) and one for the in-shell pipeline limitation
  (item 7). Keep every existing item.
- The section headings must not change: `ManTest.cpp:51` and the haisos test
  look for them.

Update every test asserting hsh's or wc's version string or `--help` text,
for example `WcTest.cpp:355` (`wc v. 1.0.0`) and any hsh `--version`/`--help`
expectation.

### 6. The unbounded pipe: linear draining
File: `src/components/BuiltinCommands/commands/hsh/HshUnboundedPipe.cpp:40-46`.

Keep a read offset (`size_t m_readOffset`) into `m_bytes` instead of
`m_bytes.erase(0, n)`:
- a read copies from `m_bytes.data() + m_readOffset` and advances the offset;
- when the offset reaches the size, clear both;
- when the offset passes half of `m_bytes.size()` (and at least 64 KiB),
  erase the consumed prefix once.

Every place that tests "bytes available" uses `m_bytes.size() > m_readOffset`.
The behaviour is unchanged (same results, same end-of-file and interruption
rules).

### 7. Document the in-shell pipeline limitation
In `src/components/BuiltinCommands/commands/hsh/CLAUDE.md` ("Pipelines,
subshells, jobs", after the unbounded-pipe paragraph near :460-474), add a
short paragraph:
- the in-shell stages run one after the other;
- so an in-shell stage that never ends, written into an in-shell reader
  (`while :; do echo y; done | while read l; do break; done`), never ends, and
  its output is held in memory as it grows, where dash ends at once;
- a child stage (a program) does not have this limit, because it runs
  concurrently through a bounded pipe.

Add the same, in a few words, to the `hsh` row's exceptions in
`src/components/BuiltinCommands/CLAUDE.md`. This is in addition to the
`--help` and man page lines of item 5. No code change.

### 8. Stale text
Each of these is a one- or two-line edit; nothing else in these files
changes:
- `interfaces/IFileDescriptor.h:29-34`: write the current truth. Only a pipe
  (`IPipeService`, or hsh's in-process unbounded pipe), console input or
  console output may block. Only a pipe returns `kIOBrokenPipe` and
  `kIOInterrupted`; no filesystem descriptor does. Drop "so far" and the task
  name.
- `src/components/Console/ConsoleDescriptors.cpp:79` and
  `ConsoleDescriptors.h:64`: replace "goal.md D7" / "goal.md's D7" with the
  reason itself: console input cannot be interrupted, because a line being
  read from the host's terminal cannot be abandoned.
- `src/components/BuiltinCommands/commands/hsh/HshShell.cpp:1032`: drop "and
  from hsh--control-flow".
- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md:485`: drop "Since
  hsh--control-flow".
- `src/components/Unicode/CLAUDE.md:40`: drop ", from the builtins--wc task".
- `tests/haisos/hsh.haisostest/hsh.haisostest.js:4,87` and
  `tests/unit/components/Hsh.unittests/HshControlFlowTest.cpp:250,262`: refer
  to "the develop's acceptance scenarios" (or describe the scenario), not
  `develop-plan/goal.md`, which is deleted before master. The comment's
  content about `hello` vs `hi*` stays.
- `src/components/Factory/CLAUDE.md:8`: "jailed at" becomes "rooted at" (links
  inside are followed; see the root CLAUDE.md, Security).
- `src/components/Filesystem/FilesystemUtils.h:107`: "rooted/jailed" becomes
  "rooted".

### 9. Test hygiene and small robustness
- **New `tests/mocks/ReleaseCountingDescriptor.h`**: the class exactly as the
  three copies have it, in `namespace Haisos`, with `#pragma once` and the
  includes it needs (`<atomic>`, `<memory>`, `interfaces/IFileDescriptor.h`).
  Remove the copies from:
  - `tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp:13`;
  - `tests/unit/components/HaisosOS.unittests/HaisosOSTest.cpp:183`;
  - `tests/unit/components/BuiltinCommands.unittests/BuiltinCommandsTest.cpp:35`.

  Each includes `"tests/mocks/ReleaseCountingDescriptor.h"` instead, as these
  files already include `tests/mocks/MockFileDescriptor.h`. Use
  `using Haisos::ReleaseCountingDescriptor;` if a copy lived in an anonymous
  namespace.
- **`src/components/HaisosOS/ProcessFileIO.cpp:178-201` (`CreatePipe`)**: take
  `m_descriptorsMutex` once around finding and filling the two lowest free
  slots. Factor the slot search into a private helper that expects the lock
  held (e.g. `int AddDescriptorLocked(std::shared_ptr<IFileDescriptor>)`),
  used by `AddDescriptor`, `Dup` and `CreatePipe`. Then fewer than two free
  slots leaves the table untouched without any rollback. Release the pipe ends
  that were not placed outside the lock (destroy them after the
  `lock_guard`'s scope ends).
- **`src/components/BuiltinCommands/commands/hsh/HshShell.cpp:280,300,654`**:
  add `bool Shell::NoExec() const { return m_state.options.noexec &&
  !m_state.options.interactive; }` (declared private in `HshShell.h`, with a
  one-line comment saying dash re-checks -n at every evaltree and an
  interactive shell ignores it). Use it in all three places.
- **`src/components/BuiltinCommands/commands/hsh/HshRedirection.cpp:62`
  (`Save`)**: de-duplicate only within the current `Apply`. Add a member
  `size_t m_applyFirst = 0;` set to `m_saved.size()` at the start of `Apply`,
  and have `Save` search `m_saved.begin() + m_applyFirst` to the end. Restore
  is LIFO, so a slot saved twice in one scope ends at its first saved value.
  Declare it in `HshRedirection.h`.

## Tests

Every command is to be run in the container (`task.sh` does it).

- `tests/unit/components/Hsh.unittests/HshBuiltinsTest.cpp`: replace the
  read-only PWD case (:45-48) with these checks:
  - `readonly PWD; cd /docs; echo "st $?"; pwd` gives out `st 2\n/docs\n`, err
    `hsh: 1: cd: PWD: is read only\n`, status 0;
  - `readonly OLDPWD; cd /docs; echo "st $?"` gives out `st 2\n`, err
    `hsh: 1: cd: OLDPWD: is read only\n`;
  - `set -e; readonly PWD; cd /docs; echo still` gives no out, the same err
    line, status 2.

  The test uses the fixture's `/docs` and `/bin/pwd`, if present; otherwise use
  `echo $PWD` and expect the old value.
- `tests/unit/components/Hsh.unittests/HshRedirectionTest.cpp`:
  `RedirectionFdMustBeOneDigit` checks that `x=' 3'; echo hi >&$x; echo after`
  and `echo hi >&+1; echo after` each give err
  `hsh: 1: Syntax error: Bad fd number\n` and status 2, with nothing on out.
  `echo hi >&1` still prints `hi`.
- `tests/unit/components/Hsh.unittests/HshLexerTest.cpp` (or HshShellTest): a
  `<<-` body with a continuation line. The script `cat <<-E\n\ta\\\n\tb\n\tE\n`
  prints `a\tb\n`. The plain `<<-` stripping (`\tx\n\tE`, giving `x\n`) still
  holds.
- `tests/unit/components/BuiltinCommands.unittests/WcTest.cpp`: extend
  `WcFilesFromAFile` (or add `WcEmptyFilesFromStillTotals`) with the five GNU
  outputs of item 4, using `l3` (empty) and a new `l5` holding one NUL. The
  existing `l3` case without `--total` stays empty.
- `tests/unit/components/Hsh.unittests/HshUnboundedPipeTest.cpp`:
  `LargeTransferDrainsInOrder` writes 8 MiB of a known pattern and reads it
  back in 4096-byte reads. The bytes must be identical and the reads must end
  in end of file. It runs well under a second.
- `tests/unit/components/Filesystem.unittests/FileDescriptorTest.cpp:162`: after
  `read` is opened through `readOnly`, `EXPECT_EQ(read->Write("x", 1),
  kIOError)`, and `ReadAll(*inner, "/d/f")` is unchanged.
- `tests/unit/components/HaisosOS.unittests/ProcessFileIOTest.cpp`:
  `CreatePipeWithOneFreeSlotLeavesTheTableAsItWas` fills the table to
  `kMaxDescriptors - 1` with `ReleaseCountingDescriptor`s. `CreatePipe()`
  returns nullopt, the last slot is still empty, and no counted descriptor was
  released. (Keep or adapt the existing CreatePipe tests.)
- The ManTest and haisos tests stay green with the reworded man page. Add one
  check in `ManTest.cpp` that the hsh page contains a line with `-ef` under
  DIFFERENCES FROM DASH. Add one check that `hsh --help` contains `PS1, PS2
  and PS4 are not expanded`.

Commands:
- `./scripts/build_linux_on_linux.sh`;
- `./scripts/test_linux.sh L U Hsh`;
- `./scripts/test_linux.sh L U BuiltinCommands`;
- `./scripts/test_linux.sh L U Filesystem`;
- `./scripts/test_linux.sh L U HaisosOS`;
- then `./scripts/test_linux.sh L "*"`.

## Docs

- `src/components/BuiltinCommands/CLAUDE.md`: the hsh and wc versions
  (`1.0.1`); drop the cd exception; add the in-shell pipeline limitation.
- `src/components/BuiltinCommands/commands/hsh/CLAUDE.md`: the limitation
  paragraph (item 7); the `NoExec()` and `RedirectionScope` notes if those
  sections describe them; drop "Since hsh--control-flow".
- The stale lines of item 8.

## Acceptance
- [ ] `readonly PWD; cd /docs; echo $?` reports `cd: PWD: is read only`, prints
      2 and goes on; the same for OLDPWD; `set -e` exits 2
- [ ] `>&' 3'` and `>&+1` are `Syntax error: Bad fd number`
- [ ] `<<-` keeps the tabs of a continuation line (`a\tb`)
- [ ] wc's five empty-list outputs match GNU byte for byte
- [ ] `hsh --help` notes list PS1/PS2/PS4, test's -r/-w/-x/-O/-G/-h/-L/-ef
      and the in-shell pipeline limitation; the man page DIFFERENCES list has
      one item per line, including those; `readonly names`
- [ ] hsh `1.0.1`, wc `1.0.1`, in code, tests and the table
- [ ] The unbounded pipe has no per-read `erase(0, n)`, and the 8 MiB test
      passes quickly
- [ ] No comment, doc or test under `src/`, `interfaces/` or `tests/` names
      `goal.md`, `develop-plan` or a task id (`grep -rnE
      'goal\.md|develop-plan|hsh--|builtins--|pipes--|streams--|fd--' src
      interfaces tests` finds nothing); no "jailed"
- [ ] One `ReleaseCountingDescriptor`, in `tests/mocks/`
- [ ] `CreatePipe` holds the table lock once and has no rollback
- [ ] `NoExec()` replaces the three copies
- [ ] `RedirectionScope::Save` de-duplicates within one `Apply`
- [ ] All Linux unit, integration and haisos tests pass

## Out of scope
- Running in-shell pipeline stages concurrently (the limitation is only
  documented).
- `goal.md` itself: the scenario 3 `hello`/`hi*` mismatch and the preamble's
  missing final newline are questions for the user, not code changes.
- The other low findings left open in `develop-plan/reviews/*.md` and
  `final-review.md`.
- Anything under `develop-plan/`, `notes/`, `.claude/`, `.github/`,
  `scripts/` or `HAISOS_VERSION`.
