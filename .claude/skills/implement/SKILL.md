---
name: implement
description: Implement a plan -- usually a notes/todo-*.md, but any seed works (": text" literal, interpreted text, a notes/ file, a file path). First turns it into a very detailed plan (notes/plan-<words>.md) with parallel planning agents, then implements it in one or a few parts, each by its own agent and each committed with /commit. Needs a task branch and a clean tree; "--no-commit" as the first argument skips the commits and the clean-tree check.
args:
  - name: seed
    description: "Optional '--no-commit' first, then ': <text>' (literal), '<text>' (interpreted), a notes/ file name with or without .md (typically todo-*), or a file path with extension"
    required: true
---

Implement one piece of work end to end: from a seed -- typically a todo written
by `/todo`, which stops in the middle of the road on purpose -- to working,
tested, committed code.

The usual flows (see "Planning skills" in the root `CLAUDE.md`):
`/note` -> `/todo` -> `/implement`, `/note` -> `/explore` -> `/todo` -> `/implement`,
`/explore` -> `/todo` -> `/implement`, or `/todo` -> `/implement`.

The detailed plan is written to `notes/plan-<words>.md`. `notes/` is shared:
`/note` writes `note-*.md`, `/explore` writes `explore-*.md`, `/todo` writes
`todo-*.md`. This skill only ever writes `plan-*.md` files.

## Steps

### 1. Read the arguments

Let `ARGS` be the skill's argument, trimmed. If its first token is
`--no-commit`, set `NO_COMMIT` and drop that token. What is left is the seed
argument.

### 2. Check the branch and the working tree

```bash
git rev-parse --abbrev-ref HEAD
./scripts/base_branch.sh
git status --porcelain
```

- The current branch must already be the task branch: if it is `master`
  (or `main`, or the base branch), stop and tell the user to create one first,
  e.g. with `/begin task/<name>`.
- Unless `NO_COMMIT`: the working tree must be clean, apart from files under
  `notes/` (a todo or exploration just written is expected there, and is
  committed with the first part). Anything else: list it, stop, and tell the
  user to commit or stash it -- or to run `/implement --no-commit ...`.

### 3. Work out the seed

The **seed** is what is to be implemented. Take the first rule that matches:

1. **Starts with `:`** -- the seed is everything after the `:`, trimmed, taken
   exactly as written.
2. **A file in `notes/`** -- the seed argument is a single token and
   `notes/<token>` or `notes/<token>.md` exists (typically a `todo-*`). The
   seed is that file's exact text.
   - An `explore-*` file still has open choices: stop and tell the user to run
     `/todo <name>` first, which asks about them.
   - A `plan-*` file is an earlier detailed plan: reuse it as the detailed plan
     (re-check it against the code as in step 4, but skip the planning agents
     when it is still accurate) and continue from step 5.
3. **A file path** -- a single token with an extension (relative to the repo
   root or absolute) and the file exists. The seed is its exact text. If it
   looks like a path but does not exist, say so and stop.
4. **Otherwise, interpret it.** It is shorthand for something the user has in
   mind -- most often from the current conversation. Write the seed as a
   short, self-contained statement of what is to be done. If it matches
   nothing you can find, or could mean quite different things, ask the user.

If the seed has a `## Base` commit (todos and explorations do), collect what
changed since, for the planners:

```bash
git log --oneline <commit>..HEAD
git diff --stat <commit>
```

### 4. Turn the seed into a very detailed plan

Choose the file name: `plan-<words>.md`, reusing the seed's words when it came
from `notes/` (`todo-tee-builtin.md` -> `plan-tee-builtin.md`), otherwise two
or three meaningful words. If taken, choose different words.

**Split the planning into areas**, each given to its own agent, all spawned
**in parallel** (in one message), using the `Plan` agent type (read-only; it
returns the plan text). One agent is enough for a small change. For a larger
one, 2-4 agents over areas that touch different files, for example:

- interfaces (`interfaces/`) and the mocks in `tests/mocks/` -- the contracts;
- the haisosfile and builtin commands;
- the source changes, split by component group if large;
- the tests (unit, integration, haisos).

A todo's sections suggest the areas directly.

Prompt for each planning agent (fill in the `<...>`):

````
You are turning a seed into a very detailed implementation plan for ONE area
of a change in the Haisos repo (current directory). Think hard. Read the root
CLAUDE.md and the CLAUDE.md of every component or tool you touch, and read the
actual code -- the seed may be vague or out of date, and the code is the truth.

Seed: <the seed text, or "read <path>; its exact text is the seed">
Changes since the seed was written: <git log / diff --stat output, or "n/a">
Your area: <area>. Other areas, planned by other agents at the same time:
<list>. Stay in your area; where you depend on another area (a new interface
method, a new directive), state exactly what you assume it provides.

Produce, for your area:
- every file to create or change, with its full path;
- for each: the classes, functions and members to add or change, with exact
  signatures, and what each does -- step by step where the logic is not
  obvious; the includes and CMakeLists.txt entries needed;
- how it follows the rules in the root CLAUDE.md (ICurrentProcess as the only
  door out of a process, private constructors + Create() returning
  shared_ptr, builtin rules, --init template, ...);
- the CLAUDE.md files (root and per component/tool) to update, and how;
- the tests: file, test name, what it sets up and asserts, and category
  (unit / integration / haisos); mocks to add or extend;
- decisions the seed left open, each with your choice and why -- and,
  separately, any choice that changes behaviour visible to users or agents and
  that you think the user should make;
- where the seed is wrong or out of date against the code, say so.

Reply with the plan text only (Markdown). Do not edit any file.
````

Wait for all of them (you are notified; do not poll). Then **merge** their
answers into `notes/plan-<words>.md`:

- Make the areas agree: where two assumed different things (a signature, a
  name, a directive's syntax), settle on one -- the contract in `interfaces/`
  wins -- and fix the other.
- If an agent flagged a choice the user should make, or the seed is ambiguous
  in a way that changes behaviour, ask the user with `AskUserQuestion`
  (recommended option first) and write the answer into the plan.

Layout of the plan file:

```
# Plan: <title>

## Seed
<"From `<path>`" for a file seed, otherwise the seed quoted with "> ">

## Base
- Branch: `<branch>`
- Commit: `<short hash>` (<commit subject>)

## Goal
## Interface changes (`interfaces/`)
## haisosfile changes
## Builtin command changes
## Source changes (`src/`)          <- by area, in dependency order
## Documentation (CLAUDE.md files)
## Tests                             <- by category: unit, integration, haisos, mocks
## Decisions                         <- choices made, and why; user answers

## Parts
### Part 1: <commit message, under 72 characters>
<which sections / files of the plan it covers; what must build and pass>
### Part 2: ...
```

**Splitting into parts:** usually one part for a small or medium change. Split
only a large change, into a few parts (rarely more than 4) that each make sense
as one commit, in order, each leaving the build green and the unit tests
passing (e.g. interfaces + mocks + a first implementation; then the next
component; then the haisosfile directive and its tests). Each part's heading is
its commit message.

### 5. Implement the parts

Implement the parts **one after another** -- each builds on the commit of the
one before -- each by its own fresh agent (`general-purpose`), so each part
gets a clean context. For each part:

1. Spawn the agent with this prompt (fill in the `<...>`):

   ````
   Implement Part <n> of the plan in notes/plan-<words>.md, in the Haisos repo
   (current directory). Read the whole plan first for context, then do only
   Part <n>. <Parts done so far: 1..n-1, already in the working tree / commits.>

   Rules:
   - Follow the root CLAUDE.md and the CLAUDE.md of each component or tool you
     touch; match the style of the surrounding code.
   - The plan is detailed, but the code is the truth: where the plan is wrong
     or incomplete, do what the plan intends, and report the deviation.
   - Update the CLAUDE.md files the part affects.
   - Build (release, local platform only): ./scripts/build_linux_on_linux.sh
   - Run the unit tests: ./scripts/test_linux.sh L U
     Also the integration / haisos tests the part adds or affects:
     ./scripts/test_linux.sh L I <filter>, ./scripts/test_linux.sh L H <filter>
   - Fix failures until the build and those tests pass. If one cannot be fixed
     within the part, stop and report it -- never disable or weaken a test.
   - Do not commit, stage, stash or switch branches.

   Reply with: files changed, deviations from the plan and why, and the build
   and test results (pass/fail counts; failing test names).
   ````

2. Wait for it. Check its report and `git status --short`. If it reports a
   build or test failure it could not fix, stop and report to the user; do not
   commit a broken part.

3. Unless `NO_COMMIT`: invoke the `/commit` skill, passing the part's heading
   from the plan as the commit message to use. The first part's commit also picks up the `notes/`
   files (the seed's todo and `plan-<words>.md`).

### 6. Final check

After the last part, run all test types once for the local platform with the
`/test` skill. Integration and haisos tests replay recorded LLM traffic (see
the `llm-cache` skill); a failure caused by a missing recording, rather than by
the change, is reported as such. Investigate any other failure; if fixing it
needs a code change, make it and -- unless `NO_COMMIT` -- commit it with
`/commit`.

### 7. Report

- The plan file.
- Each part: its commit hash and message (or "not committed" with
  `--no-commit`).
- Deviations from the plan the agents reported.
- Test results, stated plainly: what passed, what failed and why.
