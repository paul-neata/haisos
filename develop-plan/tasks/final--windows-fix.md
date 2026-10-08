# Task final--windows-fix: the whole develop built and tested on Windows

- Rock: final
- Depends on: every other task of this develop (it runs last)
- Size: unknown until the Windows run; expected ~100-600 changed lines
- Plan checked against: develop @ 5b3ed53
- PR title: Fix the Windows build and tests of the develop

## How this task runs

Unlike the others, it is not run in the task container: the container
cannot build Windows. For this develop, the Windows checks of task PRs were
skipped (playbook, Directions), so this task is where Windows catches up:

1. In the final phase, CI's `build-windows` job (`.github/workflows/ci.yml`:
   `.github/scripts/build-windows.bat`, then
   `.github/scripts/run-tests-windows.bat`) runs on a PR or branch of the
   whole develop. Its failed logs are the input of this task.
2. The fixes are made on the host, as in "Windows" of
   `.claude/develop/WORKFLOW.md`, by a fresh agent on the helper model --
   code already reviewed, so it may run there. With WSL interop broken on the
   host, `scripts/develop/windows.sh build|test` may not work: then each
   round is verified on CI (push to `task/final--windows-fix`, wait for the
   checks with `scripts/develop/wait_ci.sh`), and Linux in the container
   (`task.sh verify`).
3. **Best effort.** Up to 3 CI rounds. What is still red after them is
   recorded (the failing tests, the cause where known) in the PR
   description, and the implement session copies it to `final-review.md`
   (task branches never touch `notes/` or `develop-plan/`), and left for a later PR; the develop
   closes without it. A round that would need a redesign stops there.

## Goal

The develop builds with MSVC and its unit tests pass on Windows, or what
still fails is written down with its cause.

## Context

- Read: root `CLAUDE.md` (platforms, the "Objects released last on their own
  threads" rule), `src/components/Filesystem/CLAUDE.md` (Windows
  filesystems), `src/components/BuiltinCommands/CLAUDE.md`,
  `src/components/HaisosOS/CLAUDE.md`.
- Windows code lives in `src/components/Filesystem/windows/`
  (`WindowsFilesystem.cpp`, `WindowsFullPhysicalFileSystem`) and behind
  `_WIN32` guards; the builtins added in this develop are portable C++17 but
  were never compiled by MSVC (except grep's, fixed in #57's review).
- Likely MSVC issues, from the earlier reviews: narrowing and sign-compare
  warnings treated as errors, `ssize_t`/POSIX names, `<unistd.h>`-only calls,
  `std::min`/`std::max` vs the `min`/`max` macros, deep recursion on the
  smaller 1 MB Windows stack (the regex nesting limit is 250 for this
  reason), time functions (`gmtime_r`/`localtime_r` vs `_s`), path
  separators in test expectations.
- **The known flake**: `HaisosOS.unittests` failed or hung on Windows CI on
  #44, #51 and #52 (on #52 killed after its 30 s limit), each time passing on
  a re-run; none of those PRs touched HaisosOS. Suspect a stop/destruction
  timing race: `HaisosOS`'s 5 s stop timeout, the `DestructionThread`, a
  test waiting on a process's exit with a fixed sleep. Find the test that
  hangs (run the binary with `--gtest_repeat` and per-test timing in CI if
  needed), and fix the race, not the timeout.

## Changes

Whatever the Windows logs require, keeping to:

- Fix causes, never silence a test (no `GTEST_SKIP` on Windows, no `#ifdef`
  around an assertion) -- except where the behaviour is a documented Windows
  difference already in a `CLAUDE.md`.
- Portable fixes over `_WIN32` branches; a `_WIN32` branch only for an OS
  call.
- No change to behaviour on Linux: the Linux unit tests stay green
  (`task.sh verify`).
- Builtin output stays GNU's on both platforms (`\n` line ends, `/`
  separators inside Haisos).
- Nothing under `.github/`, `scripts/`, `.claude/`.

## Tests

- Windows: CI's `build-windows` job green, or the remaining failures
  recorded in the PR description.
- Linux: `bash ./scripts/build_linux_on_linux.sh` and
  `bash ./scripts/test_linux.sh L U` in the container (`task.sh verify`).
- For the HaisosOS hang: the fixed test passes 20 times in a row on Windows
  CI (`--gtest_repeat=20 --gtest_filter=<the test>` in one diagnostic round,
  reverted before merge if it needed a CI-script change -- prefer running it
  from a test-only commit instead).

## Docs

A Windows difference found and kept goes to the component's `CLAUDE.md`
(and, for a builtin, its `--help` notes and the BuiltinCommands table).

## Acceptance

- [ ] CI `build-windows` green, or the PR description lists
      every remaining failure with its cause and a proposed fix.
- [ ] The HaisosOS.unittests hang found and fixed, or its investigation
      recorded in the PR description.
- [ ] Linux build and unit tests green.
- [ ] No test skipped or weakened on Windows to get green.

## Out of scope

- Windows-specific features; WASM; the host's WSL interop.
- Anything still red after 3 CI rounds: a later PR.
