---
name: develop-plan
description: "Plan mode for the develop in progress, in the plan session. `/develop-plan begin` enters it: from then on every prompt in this conversation plans -- the goal and its clarifications, the big rocks, the small rocks as task plans sized for an Ollama implementer, the playbook, and later amendments, answers, directions, pause/resume, version, settings, notes and explorations -- editing only develop-plan/, notes/ and HAISOS_VERSION, uncommitted, until `/develop-plan end`. With no argument it says whether the mode is on and, if so, shows the plan's uncommitted diff with a few words on what it changes. Publishing is by hand, with /develop-update."
args:
  - name: mode
    description: "begin (enter plan mode), end (leave it), or nothing (show the mode and the uncommitted plan diff)"
    required: false
---

Plan mode for a develop: steps 2-5 of the develop workflow, and every change
to the plan afterwards, made in conversation with the user. Read
`.claude/develop/WORKFLOW.md` once per session, all of it -- the file formats,
the sizing rules and the two-session protocol are defined there.

## The mode

**Plan mode is on** from a `/develop-plan begin` in this conversation until
the next `/develop-plan end`; otherwise -- no `begin` yet, or `end` came last
-- it is off. The conversation itself is the state: look for the last
`begin` or `end` (or, after a summary, the last `develop-plan mode: on/off`
it mentions). Nothing is written to disk for it.

While it is on:

- **Every prompt of the user is planning**, unless it plainly is not: a goal,
  an answer to a clarification, "add a task for ...", "split X", "pause",
  "tell the implement session to ...", "bump the version to 1.0.0". Each one
  ends as edits to the plan files (see "Planning" below). A question about
  the plan or the code is answered -- and edits the plan only if the answer
  changes it.
- **Write only** `develop-plan/`, `notes/` and `HAISOS_VERSION`. Never code,
  tests, docs, scripts or skills: asked for that, say it needs
  `/develop-plan end` first.
- **Never commit, push or run `scripts/develop/update.sh`.** The changes stay
  uncommitted, so the user can iterate on them (`/develop-plan` shows the
  diff), and publishes them by hand with `/develop-update`.
- `/note` and `/explore` may be used: they write in `notes/`.
- End each reply that changed the plan with one line:
  `Plan: <n> file(s) changed, unpublished -- /develop-plan shows the diff, /develop-update publishes.`

Claude pays for planning, Ollama for implementing: keep your own reading
focused (the CLAUDE.md files and the code the goal touches), and put the
effort where it pays -- clear goals, well-cut tasks, complete plans.

## The argument

Only `begin`, `end` or nothing; anything else: say so and change nothing.

### `begin`

1. Be on `develop`:
   ```bash
   bash scripts/develop/git_ssh.sh
   git fetch -q origin
   git rev-parse --abbrev-ref HEAD
   git status --porcelain
   ```
   `git_ssh.sh` says `FAIL`: stop and show its line (the mode stays off).
   Not on `develop`: if `origin/develop` exists and the tree is clean,
   `git switch develop`; if there is no develop at all, stop and point to
   `/develop-create` (the mode stays off).
2. Read `develop-plan/goal.md`, `develop-plan/rocks.md` and
   `develop-plan/playbook.md`, and run `bash scripts/develop/state.sh`.
3. If `develop` is behind `origin/develop` (`git rev-list --count
   HEAD..origin/develop`), say so: the implement session or a merged task
   moved it, and `/develop-update` takes that in -- best before planning.
4. If the playbook's `## Questions` has entries without `-> answered`, show
   them: they are the first thing to answer in the mode.
5. Reply, starting with **`develop-plan mode: on`**: where the plan stands
   and the next step -- the goal still says `(to be written ...)`: ask for
   the goal; no rocks: propose them; no tasks: cut them; tasks without a plan
   file: write them; everything planned: ask what to change.

### `end`

Reply, starting with **`develop-plan mode: off`**: the plan files still
uncommitted (`git status --short -- develop-plan notes HAISOS_VERSION`), and
that `/develop-update` publishes them. From then on, behave as usual.

### Nothing

Reply, starting with **`develop-plan mode: on`** or **`develop-plan mode:
off`**. Then, when on (when off, only the number of uncommitted plan files):

```bash
git status --short -- develop-plan notes HAISOS_VERSION
git diff HEAD -- develop-plan notes HAISOS_VERSION
git ls-files --others --exclude-standard -- develop-plan notes
```

and, for each new file, `git diff --no-index /dev/null <file>`. Command
output does not reach the user: show the diff in your reply, in a `diff`
block -- all of it up to about 300 lines; beyond that the `--stat`, the
hunks that matter most, and a note of what was left out. Then, in two to
four short lines, what the changes mean for the develop: tasks added, cut or
re-planned, decisions taken, what the implement session will do differently.
No changes: say the plan is as published.

## Planning

What a prompt in plan mode leads to. Within the ownership rules of
`WORKFLOW.md` ("Two sessions, one develop"): the Status/PR/Tries/Review
cells, `reviews/` and `final-review.md` belong to the implement session. A
task `in-progress` or later is never re-planned; add a follow-up task
instead.

### The initial plan

In order, each step agreed with the user before the next.

#### The goal

**The seed** is the user's prompt: text to interpret, a `notes/` file name
with or without `.md`, or a file path with an extension (its exact text). No
seed yet: ask the user for the goal.

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
   and `## Settings` as they are.

#### The big rocks

Propose 2-6 **big rocks**: the big categories of work, in dependency order,
each with its purpose (what it gives the user), the components it touches and
a rough size. A rock is not a task; it will hold several. Show them and ask
the user to accept or adjust. Write `rocks.md` (the `Tasks:` lines come with
the task list).

#### The small rocks -- the task list

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
and the `Tasks:` lines of `rocks.md`.

#### The task plans

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
for decision.

Report: the playbook; once it is published (`/develop-update`),
`/develop-implement` in the implement session starts the work.

### Amendments

Later prompts change the plan:

- **add / change / remove** a task, a rock or the goal: edit the files; a new
  or changed task gets a full plan (the prompt of "The task plans", one agent, or directly here
  when small); removing a `todo` task marks it `obsolete`, with a reason in
  Notes.
- **replan `<task-id>`** (a `todo` or `blocked` task): re-check its plan
  against the current `develop`, rewrite what changed, update `Plan checked
  against`; a `blocked` task goes back to `todo`, with an Adjustments line
  `(plan) <why>`.
- **answer** (the implement session's Questions): write each answer after
  its question (`-> answered: <answer>`) and apply what it implies -- a task
  back to `todo` for a retry, a task split, a setting changed.
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

Report each change in a line or two. Several changes may pile up before the
user publishes them; `/develop-update` derives one message from them all.
