---
name: develop-plan
description: Plan the develop in progress, in the plan session -- the goal and its clarifications, the big rocks, the small rocks as task plans sized for an Ollama implementer, and the playbook; later amend them on the fly, answer the implement session's questions, give it directions, pause or resume it, set the version or the settings, explore an idea or take a note. Writes only develop-plan/, notes/ and HAISOS_VERSION, and publishes them with /develop-update.
args:
  - name: request
    description: "Empty (continue where the plan stands); the goal (': text' literal, text, a notes/ file name, or a file path); or an amendment such as 'add ...', 'change ...', 'replan <task-id>', 'answer', 'direction <text>', 'pause', 'resume', 'version <M.m.p>', 'set <setting> <value>', 'explore <topic>', 'note <text>'"
    required: false
---

Plan a develop with the user: steps 2-5 of the develop workflow, and every
change to the plan afterwards. Read `.claude/develop/WORKFLOW.md` first (all
of it, once per session) -- the file formats, the sizing rules and the
two-session protocol are defined there.

This skill **never writes code**: only files in `develop-plan/` and
`notes/`, and `HAISOS_VERSION`. Invoking it is the instruction to commit and
push those files on `develop`, always through **`/develop-update`** --
`bash scripts/develop/update.sh -m "<message>"`, with its step 4 when a
rebase conflicts (`.claude/skills/develop-update/SKILL.md`). That is also how
the implement session hears about every change: it takes the plan in again
before each task.

Claude pays for planning, Ollama for implementing: keep this skill's own
reading focused (the CLAUDE.md files and the code the goal touches), and put
the effort where it pays -- clear goals, well-cut tasks, complete plans.

## Steps

### 1. Sync

```bash
git rev-parse --abbrev-ref HEAD
bash scripts/develop/update.sh
bash scripts/develop/state.sh
```

- Not on `develop` in this clone: if `origin/develop` exists, `git switch
  develop` (after checking the tree is clean); if there is no develop at all,
  stop and point to `/develop-create`.
- Then read `develop-plan/goal.md`, `develop-plan/rocks.md` and
  `develop-plan/playbook.md`. The `INCOMING` part of the update tells what
  the implement session did since: tasks started, merged, blocked.

### 2. Open questions first

If the playbook's `## Questions` has entries without `-> answered`, show them
before anything else and ask the user (`AskUserQuestion`, recommended option
first, when the choices are clear; otherwise in plain text). Write each answer
after its question (`-> answered: <answer>`), apply what it implies (a task
back to `todo` for a retry, a task split, a setting changed -- see step 5),
and publish (`"Plan: answer questions"`).

### 3. Decide what to do

- The request names an amendment (`add`, `change`, `replan`, `answer`,
  `direction`, `pause`, `resume`, `version`, `set`, `explore`, `note`, ...):
  go to step 5.
- Otherwise, continue the plan where it stands: the goal still says
  `(to be written ...)` -> step 4a; no rocks yet -> 4b; no tasks in the
  playbook -> 4c; tasks without a plan file -> 4d; everything planned ->
  show the state (`state.sh` output, the playbook) and ask what to change.

### 4. The initial plan

#### 4a. The goal

**The seed** is the request: `: text` (literal), a `notes/` file name with or
without `.md`, a file path with an extension (its exact text), or text to
interpret (from the conversation). No seed: ask the user for the goal.

1. Ground it: read the root `CLAUDE.md` and the `CLAUDE.md` of each component
   or tool the goal touches, and the code where the goal lands. For a large
   unfamiliar area, one `Explore` agent that returns a short map is cheaper
   than reading it all here.
2. Draft: the **title**; the **goal** -- what a Haisos user can do at the end
   that they cannot today; 2-5 **acceptance scenarios** -- a haisosfile (and
   any agent `.md` / `.lua` program it runs) and what the user sees; **out of
   scope**.
3. **Clarify** with the user: the questions that change what gets built --
   behaviour visible to users or agents, syntax, compatibility, what is out --
   in one to three rounds of `AskUserQuestion` (up to 4 questions each,
   recommended option first). Do not ask what the code or the rules in the
   root `CLAUDE.md` already decide; decide it and say so.
4. Write `goal.md` (title, `## Goal`, `## Clarifications` as `- Q: ... -- A:
   ...`, `## Acceptance scenarios`, `## Out of scope`); leave `## Metadata`
   and `## Settings` as they are. Publish: `"Plan: goal"`.

#### 4b. The big rocks

Propose 2-6 **big rocks**: the big categories of work, in dependency order,
each with its purpose (what it gives the user), the components it touches and
a rough size. A rock is not a task; it will hold several. Show them and ask
the user to accept or adjust. Write `rocks.md` (the `Tasks:` lines come in
4c). Publish: `"Plan: big rocks"`.

#### 4c. The small rocks -- the task list

Cut each rock into **tasks** (small rocks) following "Sizing for cost" in
`WORKFLOW.md` -- about 300-800 changed lines each, merge anything under ~150
into a neighbour, split anything over ~1000; a develop of about 6-20 tasks.
For each: its task id (`<rock>--<task>`), one line of purpose, its size
estimate, what it depends on. Order them so each task leaves the build green
and builds on merged work only.

Show the whole ordered list -- the draft playbook -- and let the user adjust
it **before** the plans are written (writing plans is the expensive step).
Then fill the playbook table (one row per task: `#`, the task linked as
`[<id>](tasks/<id>.md)`, `todo`, an empty Version -- the implement session
numbers each task when it starts it -- the `#`s it depends on, empty cells)
and the `Tasks:` lines of `rocks.md`. Publish: `"Plan: task list"`.

#### 4d. The task plans

Spawn the planning agents **in parallel** (one message), `general-purpose`,
one per rock (or per group of at most 4 tasks of a big rock). Each writes its
tasks' plan files itself, so the plans do not pass through this
conversation. Prompt (fill in the `<...>`):

````
You are writing the task plans of one big rock of a Haisos develop. Each plan
will be implemented by a weaker model -- an Ollama model running Claude Code
in a container, alone, with no one to ask -- and reviewed afterwards. It must
be complete and unambiguous, but it says what, where and why, not the code.

Read first: .claude/develop/WORKFLOW.md (sections "The develop-plan/ folder"
and "Sizing for cost"), develop-plan/goal.md, develop-plan/rocks.md,
develop-plan/playbook.md, the root CLAUDE.md, and the CLAUDE.md of every
component or tool your tasks touch. Then read the code they change: the code
is the truth.

Your rock: <rock> -- <title>. Your tasks, in order:
<task id: purpose; size estimate; depends on>
Tasks of other rocks that yours depend on, or that depend on yours:
<task id: what it provides or needs>
develop is at <short hash>.

For each task, write develop-plan/tasks/<task id>.md in the task plan
template of WORKFLOW.md ("Plan checked against: develop @ <short hash>"):
- Changes: every file to create or change; exact signatures of new
  interfaces, classes, methods, haisosfile directives, builtin options; the
  logic step by step only where it is not obvious; the root CLAUDE.md rules
  that bite, restated (ICurrentProcess as the only door out of a process,
  private constructors + Create() returning shared_ptr, builtins matching GNU
  output and options and the --help shape, the --init template, CMakeLists
  entries for new files and tests).
- Tests: files, test names, what each sets up and asserts; the exact commands
  (`bash ./scripts/build_linux_on_linux.sh`, `bash ./scripts/test_linux.sh L U
  <filter>`) with filters that select at least one test.
- What earlier tasks provide, by exact name, as if already on develop.
- No function bodies, except exact signatures, a tricky algorithm, or an
  output format to match byte for byte.
- A task that comes out over ~1000 changed lines or under ~150: say so and
  propose the split or merge instead of writing it as is.
Write nothing else and do not commit.

Reply, per task, in at most 10 lines: the file written; the interfaces,
classes, directives, options it introduces (exact names); what it needs from
other tasks; the files it touches; its size estimate; anything the user
should decide.
````

Wait for all of them (you are notified; do not poll). Then **reconcile** from
their replies: where two tasks name the same contract differently, settle it
-- `interfaces/` wins -- and fix the affected plans (a small edit here, or
`SendMessage` to the agent that wrote it). Apply proposed splits or merges
(update the playbook and `rocks.md`). Ask the user what the agents flagged
for decision. Publish: `"Plan: task plans"`.

Report: the playbook, and that `/develop-implement` in the implement session
starts the work.

### 5. Amendments

Always within the ownership rules of `WORKFLOW.md` ("Two sessions, one
develop"): the Status/PR/Tries/Review cells, `reviews/` and
`final-review.md` belong to the implement session. A task `in-progress` or
later is never re-planned; add a follow-up task instead.

- **add / change / remove** a task, a rock or the goal: edit the files; a new
  or changed task gets a full plan (4d's prompt, one agent, or directly here
  when small); removing a `todo` task marks it `obsolete`, with a reason in
  Notes.
- **replan `<task-id>`** (a `todo` or `blocked` task): re-check its plan
  against the current `develop`, rewrite what changed, update `Plan checked
  against`; a `blocked` task goes back to `todo`, with an Adjustments line
  `(plan) <why>`.
- **answer**: step 2.
- **direction `<text>`**: an instruction to the implement session, which
  reads it before its next task -- e.g. "stop after parser--tokens", "run
  redirection--append before pipes--*", "use minimax-m2.7:cloud for
  pipes--three-stage". Add `- (plan, <date>) <text>` under `## Directions`;
  it marks it `-> done: ...` when acted on.
- **pause / resume**: set `Pause: yes` / `Pause: no` in the playbook. The
  implement session stops at its next task boundary.
- **version `<M.m.p>`**: write it to `HAISOS_VERSION` (any version, e.g.
  `1.0.0`). The develop PR's title follows (`[M.m]`), and the next task
  counts its patch on from there.
- **set `<setting>` `<value>`**: edit the line under `## Settings` in
  `goal.md` (e.g. `Task models: kimi-k3:cloud, minimax-m2.7:cloud`).
- **explore `<topic>`**: invoke `/explore <topic>` (it writes
  `notes/explore-*.md`), then offer to fold its conclusions into the plan.
- **note `<text>`**: invoke `/note <text>`.

Publish each amendment on its own, with a message saying what changed
(`"Plan: add task parser--heredoc"`), and report it in a line or two, with
what came in from the implement session.
