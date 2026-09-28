# Exploration: A develop-branch workflow -- one big feature at a time, tasks implemented by an Ollama-backed Claude Code

## Seed

> I want to change the way of working. Until now I used skills like /begin, implement a leaf punctual thing (like: add root fs for Windows, code-review, add support for 3 haisossfile commands, extend again the haissosfile, etc.). Now what I want is essentaly develop one big, meaningfull and usable (from the haisos persona user 'spoint of view) feature at a time by making use of a develop branch and like this:
> 1. create develop branch from origin/master - by using a /develop-create skill
> 2. I define the goal of this development (the 'develop goal')
> 3. you help me clarify aspects of the develop goal
> 4. you help me split the develop goal into 'big develop rocks'. These are the big categories of things that will be done on this development , they don't translate each into a develop task but into a set of development tasks
> 5. split each big develop rock into multiple 'small develop rocks' - each of these will be a development task, happening of a branch taken from develop, with its own PR to be merged into develop, develop independently by a claude instance that will be using an ollama model. The tasks I want to have the plan done by you, each written into a file named with big-develop-rock name and with small-develop-task name. you will define the sequential order in development of the small task. Each task will have its plan done on a file and written its file name into a playbook of tasks.
> 6. items 2-5 will be done by a skill /develop-plan and will write and commit those files representing plans, traking etc into 'develop-plan' repo folder
> 7. implement those task sequentialy, by the playbook, each punctual implementation will be done by a skill /develop-task that will create the branch, develop, test, create PR, wait for PR CI to finish (and fix it if it's the case) all this on a fresh new parallel agent. The orchestrator of development will be done by /develop-implement skill. It will work on the main claude agent keeping all its context there but will delegate some/most of its work to skills running on different fresh-started agent. It will loop and: refetch / update local develop branch (the plan may suffer changes done and commited in paralel by re-using /develop-plan skill), take informations from github for branches (with base develop) and PRs created after develop branch creation time, review the past execution of the plan, review the future execution of the plan, see if the implementation plan needs some automatic adjustment and make those adjusment if it's the case (and commit and push them on develop), update the playbook on develop (and commit and push), break if notin is to do, take next item to implement and hand it to /develop-task, after it is finished run a /develop-code-review skill that will work on a parallel fresh agent and will code-review the PR (and only it) by implementing the high/critical part directly as a commit and will comment the medium/low parts at the github PR, will merge the PR and continue to loop (next PR will use the previoust PR and that was merged in develop). Then it will code-review per ansable the whole develop branch, it will implement all the high/critical parts, run local tests, create develop merge PR into master, make a good summary of what happened and mention each PR done and for each what items resulted from code-review (fixed or not) and also mention the high-overview code-review. in the future add some exploratory testing here (like testing the tools added, fuzzy-testing haisosfile, inventing 10 haisosfiles with recent changes and test them etc.).
> 8. /develop-close that will delete develop-plan (and comit and push) and then merge the develop pr into master, delete develop and be prepared for a new /develop-create

## Base
- Branch: `task/physical_fs_windows_and_notes`
- Commit: `b65d885` (Added more notes)
- Related notes:
  - `notes/note-review-medium-findings.md` -- its section 3 ("CI and the test
    suite cannot catch HTTP/LLM regressions") is the ground this workflow's
    "wait for CI" stands on: Windows CI runs no tests at all (it calls a
    runner script that does not exist), Linux CI runs unit tests only, the
    integration tests pass with no LLM, and the test runner prints "No tests
    found." and exits 0 when a filter matches no executable. D8 and D9 build on
    it; the Windows CI fix becomes a prerequisite here.
  - `notes/note-review-low-findings.md` -- section 8 (CI and scripts:
    `.github/scripts/run-tests-windows.bat` control flow, `scripts/pack.sh`
    shipping packages without binaries, `scripts/internal/test.js` never
    escalating to SIGKILL) -- same CI ground, lower stakes.
  - `notes/note-release-testing-workflow.md` -- asks what must pass before a
    release, whether a version gate / tag is needed, where WASM fits. A
    develop is a natural release unit (one minor version, D6), so its
    "coverage matrix" and "tag" questions land on `/develop-close`; this
    exploration does not settle them, it only gives them a home (A5, D14).
  - `notes/note-faster-unit-test-runs.md` -- every task builds and runs the
    unit tests, sequentially, on `/mnt/c` (DrvFs); the task loop's wall time is
    dominated by that. It motivates one persistent task worktree with
    incremental builds (D5) and names the WSL-native-filesystem speed-up this
    exploration leaves for later.
  - `notes/note-build-arm-x64.md` -- would widen the CI matrix (ARM64 Linux
    runner); not needed for this workflow, but "wait for CI" gets slower if it
    lands.
  - The other notes are about Haisos runtime code and do not bear on the
    workflow.

## The issue

The way of working moves from one leaf change per task branch (`/begin` ->
work -> `/end`, a PR straight into `master`, squash-merged) to one big,
user-visible feature per **develop**: a `develop` branch cut from
`origin/master`, a goal clarified with the user, split into big rocks and then
into small rocks, each small rock a task branch off `develop` with its own PR,
implemented by a headless Claude Code driven by an Ollama model, reviewed by a
fresh Anthropic agent that fixes critical/high findings and comments the rest,
merged into `develop`, and finally the whole develop reviewed, summed up and
merged into `master`. "Done" means: a precise definition of six skills
(`/develop-create`, `/develop-plan`, `/develop-task`, `/develop-code-review`,
`/develop-implement`, `/develop-close`) and their boundaries; the
`develop-plan/` folder layout and playbook format; how a task runs on Ollama,
where it works, how it is bounded and how it fails; the CI, merge and version
rules; the resumability and concurrency protocol; and the edits to the root
`CLAUDE.md`, the existing skills, `scripts/` and `.github/` that the workflow
needs -- with every choice either settled below or left for the user.

## What exists today

- **Task-branch skills.** `/begin <branch>` stashes, updates `master`,
  branches, bumps the **minor** of `HAISOS_VERSION` (left uncommitted), prunes.
  `/end` builds Linux and Windows (from WSL), `/commit`s, pushes with
  `git push -u origin`, then `/create-pr` or `/update-pr-description`.
  `/sync` auto-commits, fetches, `/rebase`s. `/rebase` resolves a
  `HAISOS_VERSION` conflict as "base's version + 1 minor", then builds and
  tests. `/squash`, `/log`, `/fetch` (force-sets local `master`),
  `/prune-old-branches` (deletes only branches whose tip is an ancestor of
  `origin/master`, so squash-merged branches are always "kept").
- **Base-branch detection** (`scripts/base_branch.sh`), used by `/rebase`,
  `/squash`, `/log`, `/sync`, `/create-pr`, `/implement`: `GITHUB_BASE_REF`,
  then `gh pr view`'s base, then the upstream (ignored when it is the branch
  itself, as after `git push -u origin <branch>`), then
  `branch.<name>.merge`, then `origin/HEAD` (= `master`), then ancestry. **A
  task branch off `develop` that has been pushed with `-u` but has no PR yet
  is detected as based on `master`**, so `/create-pr` would open it against
  `master`.
- **`/propose-pr-description`** diffs against "the default branch", not the
  base: for a branch off `develop` it would describe every earlier task too.
  It forbids emojis and Markdown headers; no past PR body carries a
  "Generated with Claude Code" footer (checked on PR #15).
- **Planning skills** (root `CLAUDE.md`, "Planning skills"): `/note`,
  `/explore` (this file), `/todo`, `/implement`, all writing to `notes/`,
  taking a seed in four forms. `/implement` writes `notes/plan-*.md` with
  parallel `Plan` agents (interfaces, haisosfile/builtins, source, tests),
  then implements it in 1-4 parts, one fresh `general-purpose` agent and one
  `/commit` each; it refuses to run on `master`/the base branch. Its plan
  layout (Goal, Interface changes, haisosfile, builtins, Source, Docs, Tests,
  Decisions, Parts) and its implementer prompt (build, `test_linux.sh L U`,
  never weaken a test, report deviations) are the closest existing model of a
  task plan.
- **Review skills.** `/code-review` spawns five parallel agents (claude-md,
  security, quality, performance, logs) over **the whole codebase** and
  merges their findings **by importance, with no severity labels**; each
  review agent only proposes. `/auto-code-review` implements the actionable
  findings, one fresh agent each, builds, tests, and commits without asking.
  `code-review-llm-protocol` exists but is not part of `/code-review`. None of
  them is scoped to a PR diff, none posts to GitHub.
- **Test skills.** `/test` passes through to `scripts/test_linux.sh` /
  `scripts/test_windows.bat`; `/investigate-tests` re-runs a failing test with
  `HAISOS_TEST_LOG_LEVEL=verbose_debug`; `/auto-investigate-tests` commits the
  fix. `/build` wraps the platform build scripts.
- **Build in a fresh checkout works**: `extern/` is gitignored (not
  submodules), and `scripts/build_linux_on_linux.sh` shallow-clones the three
  dependencies when missing, then configures once and builds incrementally
  in `build/temp_linux`.
- **CI** (`.github/workflows/ci.yml`): `on: push` with **no branch filter** and
  no `pull_request` trigger -- every pushed branch, `develop` and task
  branches included, gets `build-linux` (build + unit tests; integration and
  haisos tests only if recordings exist, and none are committed),
  `build-windows` (build; its test step calls a non-existent
  `scripts\run_tests_windows.bat` and still ends green), and `package`. Push
  runs show up as the PR's checks (`gh pr checks 16` lists the three jobs,
  `event: push`). The last 15 runs took 2.8-4.8 minutes. No `concurrency`
  group, no `paths-ignore`, WASM not built.
- **GitHub repository settings** (read with `gh`): default branch `master`,
  public, **no branch protection**, squash/merge/rebase all allowed,
  **`deleteBranchOnMerge: true`** (a merged PR's head branch is deleted
  remotely), `allow_auto_merge: false` (so `gh pr merge --auto` is not
  available), squash commit title = PR title, squash body = blank. All 15
  merged PRs (#1-#15) targeted `master` and were **squash-merged** (single
  parent, subject ending in `(#N)`). `gh` 2.92 is logged in as `paul-neata`
  with `repo` scope.
- **`gh` behaviour relied on**: `gh pr checks --watch --fail-fast --interval N`
  (exit 8 while pending, non-zero on failure; right after a push it can
  report "no checks" until the runs register), `gh pr merge --squash|--merge
  --subject --body --match-head-commit <sha>` (refuses if the head moved),
  `gh pr list --base develop --state all --search "created:>=<date>"`.
  GitHub keeps `refs/pull/<N>/head` after a branch is deleted, and when a
  merged PR's head branch is auto-deleted it **retargets open PRs based on
  it** to the merged PR's base.
- **Claude Code settings**: `.claude/settings.local.json` (gitignored) allows
  `Bash(*)`, `Read/Write/Edit(*)`, `Task(*)`, `Skill(*)`, web tools;
  `.claude/settings.json` has only an empty hook. In a git worktree, Claude
  Code uses the main checkout's `settings.local.json`.
- **Ollama here**: `ollama` 0.21.0, models `kimi-k2.6:cloud` (1T, the
  project's `HAISOS_MODEL`), `minimax-m2.7:cloud`, `gpt-oss:120b-cloud`; the
  Anthropic-compatible `/v1/messages` endpoint answers. Ollama documents Claude
  Code with `ANTHROPIC_BASE_URL=http://localhost:11434`,
  `ANTHROPIC_AUTH_TOKEN=ollama`, `ANTHROPIC_API_KEY=""`, `claude --model
  <model>`, and a headless form `ollama launch claude --model <m> --yes -- -p
  "..."`. Its Anthropic API lacks prompt caching, `tool_choice`,
  `count_tokens` and batches. Anthropic states it does not support routing
  Claude Code to non-Claude models (it works, unsupported).
- **Verified in this exploration** (Claude Code 2.1.281): a headless
  `claude -p` launched from Bash **inside this Claude Code session**, with the
  env above and `--model kimi-k2.6:cloud`, answered in 4 s; a second run with
  `--allowedTools "Read,Bash(git rev-parse *)" --permission-mode dontAsk` used
  both tools correctly (3 turns, 28 s) and returned `0.3.0
  task/physical_fs_windows_and_notes`. It warns "unrecognized_model" and
  assumes a 200k context window; it waits 3 s for stdin unless given
  `< /dev/null`.
- **Claude Code facts** (docs): subagents can nest 3 levels by default
  (`CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH`); a subagent's model is **Anthropic
  only** (alias or Claude id); `isolation: worktree` makes a temporary
  worktree **branched from the default branch, not HEAD**, cleaned up if
  unchanged; background subagents lack `AskUserQuestion`; a skill with
  `context: fork` (plus `agent:`, `model:`, `background:`) always runs in a
  fresh subagent; in `-p` mode `/skill-name` in the prompt is expanded and
  project skills, `CLAUDE.md` and settings load (unless `--bare`);
  `--output-format json|stream-json` reports turns, cost and session id;
  `--permission-mode auto|dontAsk|acceptEdits|bypassPermissions`,
  `--permission-prompts none`, `--settings <json>` (higher precedence than
  project settings), `-w/--worktree`. A background subagent inside `-p` keeps
  the process open (10 min idle ceiling by default). The Bash tool's own
  timeout is at most 10 minutes; `run_in_background` re-invokes the caller
  when the command exits. The `attribution` setting (`{"commit": "", "pr":
  ""}`) removes the Co-Authored-By trailer and PR footer (a bug report says
  it was not always honoured).
- **Root `CLAUDE.md` rules that bind the new skills**: build only on the local
  platform, release by default, run unit tests after building, never commit
  or push unless the prompt explicitly instructs it, never add
  Co-Authored-By lines, only repo-relative paths in edits, docs, commit
  messages and skill prompts.

## Open aspects

### A1. How does `/develop-task` run on the Ollama model?
The heart of the workflow: who drives branch/build/test/PR/CI -- the Ollama
model alone, or the Ollama model supervised by a thin Anthropic layer. It
decides reliability, Anthropic spend, and how much the orchestrator must
verify afterwards.
Combinable: no

1. **Headless Ollama runs the whole skill** -- `/develop-implement` launches,
   with `run_in_background`, `claude -p "/develop-task <task-file>"` in the
   task worktree, `ANTHROPIC_BASE_URL`/`ANTHROPIC_AUTH_TOKEN`/model env set
   (also `ANTHROPIC_DEFAULT_{OPUS,SONNET,HAIKU}_MODEL` and
   `CLAUDE_CODE_SUBAGENT_MODEL` to the Ollama model, so nothing inside asks
   Ollama for a Claude alias), `--permission-mode dontAsk` (or `auto`),
   `--permission-prompts none`, deny rules via `--settings`,
   `--output-format stream-json` to a log file, `< /dev/null`, wrapped in
   `timeout`. The Ollama session creates the branch, implements, builds,
   tests, `/commit`s, pushes, opens the PR, waits for CI and fixes it. The
   orchestrator then checks post-conditions (D11). For: exactly what the seed
   asks ("a claude instance that will be using an ollama model"); zero
   Anthropic tokens per task besides the orchestrator's check; verified to
   work here. Against: the weaker model also does the outward-facing steps
   (push, PR) and the CI loop, where mistakes are costly; its only safety net
   is the post-check and the review.
2. **Anthropic wrapper, Ollama coder** -- `/develop-task` is a forked skill
   (`context: fork`, a cheap model such as `sonnet`/`haiku`): it creates the
   branch and worktree, launches the headless Ollama run for the
   implementation only (build and unit tests included), verifies the result
   itself (diff scope, build, tests, test counts), then commits, pushes,
   opens the PR, waits for CI and hands CI failures back to a new Ollama run.
   For: the outward and bookkeeping steps are done reliably; the Ollama model
   gets a narrower, better-specified job. Against: an extra layer and some
   Anthropic spend per task (mostly waiting); two places to debug.
3. **Ollama codes, a script does the rest** -- the headless Ollama run only
   implements and tests; `/develop-implement` (or a script in `scripts/`)
   commits, pushes, opens the PR and runs the CI wait; CI failures go back to
   another Ollama run. For: deterministic outward steps, no extra agent.
   Against: grows the orchestrator's own work and context, the opposite of
   the seed's "delegate most of its work".

### A2. What happens when a task fails?
A task can fail to build/test locally, time out, leave CI red after the
allowed fix rounds, or produce a diff the post-check rejects. The policy
decides whether a develop can finish unattended and what it costs.
Combinable: yes (a ladder of steps, in order)

1. **Retry Ollama with the failure** -- one more headless run on the same
   branch, given the log excerpt and the failure summary. For: most failures
   are fixable with the error in hand; still no Anthropic spend. Against: a
   model that got it wrong once may loop.
2. **Try another Ollama model** -- the same retry with the configured
   fallback model (`minimax-m2.7:cloud` or `gpt-oss:120b-cloud`). For:
   different failure modes. Against: `gpt-oss:120b` has a 128k window, below
   the 200k Claude Code assumes for an unknown model.
3. **Fall back to an Anthropic agent** -- a fresh `general-purpose` agent
   (e.g. `sonnet`) finishes the task on the same branch, and the playbook
   records "done by fallback". For: the develop keeps moving. Against:
   spends Anthropic tokens the user meant to save; masks weak plans.
4. **Split or re-plan the task** -- the orchestrator splits the small rock
   into two smaller ones (allowed by D12) and retries. For: often the real
   cause is a too-big task. Against: slower; needs the orchestrator's
   judgement.
5. **Stop and ask the user** -- mark the task `blocked`, stop the loop,
   report. For: the user stays in control. Against: the loop halts, maybe
   overnight.

### A3. How is `develop` merged into `master`?
Decides what `master`'s history looks like after a develop, whether the task
commits and their `(#N)` subjects survive on `master`, and what `git bisect`
can find.
Combinable: no

1. **Merge commit** -- `gh pr merge --merge`: `master` gets each task's squash
   commit plus one merge commit whose message is the develop summary.
   `git log --first-parent master` still shows one entry per develop. For:
   per-task commits stay bisectable (valuable when the code was written by a
   weaker model); the develop's history, including the `develop-plan/`
   commits, stays reachable from `master`; local `develop` becomes an
   ancestor, so `/prune-old-branches` removes it. Against: breaks the
   all-squash, linear history of #1-#15.
2. **Squash** -- one commit per develop on `master`, as every PR so far. For:
   the familiar linear history, one line per feature. Against: `master`
   loses the per-task commits (they live on only in the PR and
   `refs/pull/<N>/head`); a regression can be bisected only to "the whole
   develop"; the plan files never appear in `master`'s history.
3. **Rebase** -- replay the task commits onto `master`. For: linear and
   per-task. Against: rewrites every hash (the `(#N)` squash commits get new
   ones), no single place for the develop summary; no benefit over a merge
   commit here.

### A4. What happens to medium and low review findings besides the PR comment?
A PR comment is buried once the PR is merged. The seed says to comment them;
the question is whether anything else tracks them.
Combinable: yes

1. **Recorded in the task and the summary** -- the finding lines (severity,
   file, one line) go into the task's outcome in `develop-plan/` and into the
   final develop PR summary, as the seed asks. For: nothing is lost before
   the develop ends; no extra work. Against: after `/develop-close` they live
   only in GitHub.
2. **Follow-up small rocks** -- mediums become new small rocks at the end of
   the playbook (a D12 adjustment), implemented within the same develop.
   For: the develop leaves no known medium debt. Against: grows the develop;
   some mediums are not worth a task.
3. **A notes/ file per develop** -- the unfixed findings are written to
   `notes/note-<version>-review-findings.md` on `develop`, so they reach
   `master` and stay in the project's memory, ready for `/todo`. For:
   consistent with `notes/note-review-medium-findings.md` /
   `note-review-low-findings.md`; survives `/develop-close`. Against: one more
   file per develop.
4. **GitHub issues** -- one issue per unfixed finding. For: a tracker.
   Against: the project uses `notes/`, not issues; noise.

### A5. What keeps a record of the plan once `/develop-close` deletes `develop-plan/`?
The seed deletes `develop-plan/` before the merge; the goal, rocks, task
plans and playbook are the develop's design record.
Combinable: yes

1. **The develop PR body** -- the final summary (goal, rocks, every task PR
   with its review outcome, the whole-develop review) is the PR's
   description and, with a merge commit (A3), the merge commit's body. For:
   written anyway (the seed asks for it); GitHub also keeps the PR's commits,
   plan files included, under `refs/pull/<N>/head`. Against: only a summary,
   not the task plans.
2. **A tag** -- `/develop-close` tags the last `develop` commit that still has
   `develop-plan/` (e.g. `develop-plan/<version>`), and optionally `master`'s
   merge as `v<version>` (the tag question of
   `notes/note-release-testing-workflow.md`). For: the full plan stays one
   `git checkout` away, independent of GitHub. Against: tags accumulate;
   pushing tags is another outward step.
3. **Archive into notes/** -- move the goal and playbook (not every task
   plan) into `notes/develop-<version>.md` instead of deleting them. For: the
   project's memory is `notes/`; later `/explore` and `/todo` runs read it.
   Against: `notes/` grows with history rather than live ideas; departs from
   the seed's plain delete.
4. **Nothing beyond git history** -- with a merge commit (A3.1) the plan is in
   `master`'s history (added, then deleted). For: no extra step. Against:
   hard to find; gone with a squash merge.

### A6. Which default Ollama model, and where is it configured?
Changes quality and speed of every task; the choice may change per develop.
Combinable: no

1. **`kimi-k2.6:cloud`, per-develop setting** -- the default written by
   `/develop-create` into the develop's metadata in `develop-plan/`, with a
   fallback model next to it; overridable per task in the playbook (a task
   can also be marked "Anthropic only"). For: already the project's
   `HAISOS_MODEL`, verified with tools here, a large context. Against: one
   cloud model means Ollama cloud usage limits hit every task.
2. **`minimax-m2.7:cloud` default** -- positioned by its vendor for agentic
   coding. For: possibly stronger at long tool loops. Against: not verified
   here.
3. **Environment only** -- the orchestrator takes `HAISOS_MODEL` (or a new
   variable) at run time. For: nothing to write. Against: not reproducible
   across restarts; the model a task ran on is not recorded.

## Decided aspects

### D1. The skill set and its boundaries
**Chosen:** six new skills, one job each, plus changes to existing ones;
nothing is retired.
- `/develop-create` -- refuse if `develop` exists locally or on `origin`, or
  the tree is dirty; branch `develop` from a freshly fetched
  `origin/master`; bump the minor of `HAISOS_VERSION` (D6); write the
  `develop-plan/` skeleton with the develop's metadata (creation time and
  commit, version, model settings -- D2); commit; `git push -u origin
  develop`. The metadata is what later lets the orchestrator filter "PRs with
  base `develop` created after this develop began", since the name `develop`
  is reused by every develop.
- `/develop-plan [seed | --redo ...]` -- steps 2-5 of the seed. Runs **in the
  main conversation** (it asks the user with `AskUserQuestion`, which
  background agents do not have); spawns parallel `Plan` agents per big rock
  for the task plans, as `/implement` does per area; writes and commits
  `develop-plan/` and pushes, with the concurrency protocol of D10. Re-run
  later, it amends the plan (new rocks, re-planned tasks) and is the user's
  way to change what the orchestrator may not (D12). It takes its seed in the
  four forms `/todo` and `/explore` use, so a `notes/explore-*`, `todo-*` or
  `note-*` can be the develop goal's starting point.
- `/develop-task <task-file>` -- one small rock, from branch to green PR (A1
  decides who runs which part). It never edits `develop-plan/` or
  `HAISOS_VERSION`, never merges, never pushes to `develop` or `master`.
- `/develop-code-review <PR>` -- a forked skill (`context: fork`,
  `general-purpose`, Anthropic model): reviews that PR's diff only, fixes
  critical/high as commits on the PR branch, comments medium/low, waits for
  CI, merges (D9, D13).
- `/develop-implement` -- the orchestrator loop (D11), in the main
  conversation, ending with the whole-develop review and the develop -> 
  `master` PR (D14).
- `/develop-close` -- the human gate before `master` (D15).
- Existing skills: `/begin`, `/end`, `/sync`, `/implement` stay for leaf work
  on `master` (a hotfix during a develop) and are the building blocks
  `/develop-task` reuses (`/build`, `/test`, `/commit`, `/create-pr`,
  `/investigate-tests`). `/note`, `/explore`, `/todo` stay and feed
  `/develop-plan`. `/code-review`'s agent prompts are reused by
  `/develop-code-review`. A read-only `/develop-status` (playbook + GitHub
  state, no writes) is cheap and useful while the orchestrator runs for
  hours; recommended, not required.

**Ignored:**
- One monolithic `/develop` skill -- the seed wants a separate `/develop-plan`
  that can run in parallel with the loop, and a separate close gate.
- `/develop-task` calling `/implement` -- `/implement` re-plans with parallel
  `Plan` agents; the task plan is already that detailed plan, so a second
  planning pass on the weaker model is pure cost and drift.
- Retiring `/begin`/`/end` -- hotfixes and small leaf changes on `master`
  still need them; the develop workflow is for big features.

### D2. The `develop-plan/` layout and naming
**Chosen:** a flat, predictable folder on `develop`:
- `develop-plan/goal.md` -- the develop goal, the clarifications (question ->
  answer), the user-visible acceptance scenarios (a few example haisosfiles
  and what the user sees -- this is what "meaningful and usable from the
  Haisos user's point of view" means, and later the seed of the exploratory
  tests), and the develop's metadata (creation commit and time, version,
  Ollama model and fallback).
- `develop-plan/rocks.md` -- the big rocks, each with its purpose and its
  small rocks (names only, one line each).
- `develop-plan/tasks/<big-rock>--<small-rock>.md` -- one plan per small
  rock; both names 1-3 lowercase words joined by `-`, joined by `--`. The file
  name is the task's identity everywhere: the task branch is
  `task/<big-rock>--<small-rock>`, so GitHub state maps back to the plan with
  no extra metadata. The **order is not in the file name** -- re-ordering
  would rename files; it lives in the playbook only.
- `develop-plan/playbook.md` -- one Markdown table, **one line per task**
  (line-local edits merge cleanly): order, task file, big rock, status,
  branch, PR number, attempts, runner (model / fallback), review outcome
  (critical/high fixed, medium/low commented, as counts), a short note.
  Status is a fixed vocabulary: `planned`, `running`, `pr-open`,
  `in-review`, `merged`, `failed`, `blocked`, `skipped`, `obsolete`. Below the
  table, an append-only "Adjustments" list: each automatic change with its
  reason (D12).
- Per-task outcome: after the merge the orchestrator appends an "Outcome"
  section to the task file (PR, deviations reported, review findings fixed or
  commented, with one line each); the whole-develop review goes to
  `develop-plan/final-review.md`.

**GitHub is the truth for what GitHub knows** (branch exists, PR number and
state, checks, merged), the playbook mirrors it and adds what only the plan
knows (order, attempts, blocked reasons, review outcome).
**Ignored:**
- A JSON/YAML playbook, or JSON plus generated Markdown -- the only readers
  and writers are LLM agents, which read a Markdown table reliably; two files
  are two sources of truth, and a generated file conflicts whenever its
  source does.
- One file per big rock holding its task plans -- parallel planning agents
  and the per-task outcome would all edit one file; per-task files keep edits
  separate and make "the plan for this task" a single path to hand over.
- `develop-plan/` inside `notes/` -- `notes/` is permanent, shared with
  `master` and written by `/note`/`/explore`/`/todo`/`/implement`; the plan
  is transient and deleted at close (the seed's point 8).

### D3. How detailed a task plan is
**Chosen:** at least the detail of `/implement`'s `notes/plan-*.md` -- a todo
is deliberately too loose for a weaker model working alone. Each task plan
has: goal (what is true after it, visible behaviour); context (what earlier
tasks did, which files and `CLAUDE.md` files to read first); every file to
create or change with exact signatures and step-by-step logic where it is
not obvious, following the root `CLAUDE.md` rules restated where they apply
(`ICurrentProcess` as the only door out, private constructors + `Create()`,
builtin rules, `--init` template); tests by name, what they set up and assert;
docs to update; the exact build and test commands, including the filters
that must select at least one test (the runner prints "No tests found." and
exits 0 otherwise -- `notes/note-review-medium-findings.md`); acceptance
criteria as a checklist; out of scope and must-not-touch (`develop-plan/`,
`HAISOS_VERSION`, other rocks' files); the commit message and PR title.
**Size:** one small rock is one reviewable PR that leaves the build green and
the unit tests passing -- typically one component or one directive plus its
tests, a few hundred changed lines at most; bigger means split. Plans are
written up front by `/develop-plan` (the seed asks for it), and **the next
task's plan is re-checked against the current `develop` just before it
starts** (the seed's "review the future execution of the plan"), because
earlier tasks rename and move things.
**Ignored:**
- Todo-level plans refined just in time by the Ollama model itself -- the
  weaker model would be making the design decisions the seed wants made by
  Claude.
- Only just-in-time plans (none up front) -- the seed wants the whole
  playbook planned and reviewable before implementation starts.

### D4. The Ollama run mechanics (whichever variant of A1)
**Chosen:** a headless `claude -p` launched through Bash with
`run_in_background` (the Bash tool's 10-minute limit rules out a foreground
call; the background run notifies the orchestrator when it exits), wrapped
in `timeout` (a few hours per task, configurable), with:
`ANTHROPIC_BASE_URL=http://localhost:11434`, `ANTHROPIC_AUTH_TOKEN=ollama`,
`ANTHROPIC_API_KEY=""`, `--model <ollama model>`, the
`ANTHROPIC_DEFAULT_*_MODEL` and `CLAUDE_CODE_SUBAGENT_MODEL` variables set to
the same model, `--permission-mode dontAsk` plus an allow list (or `auto`),
`--permission-prompts none`, deny rules passed with `--settings` (no `gh pr
merge`, no force push, no push to `develop`/`master`, no edits under
`develop-plan/`), `--output-format stream-json --verbose` to a gitignored
log file per task and attempt, stdin from `/dev/null`, and **not** `--bare`
(the run needs the project's skills, `CLAUDE.md` and settings). Its cwd is the
task worktree (D5). The json result gives turns and a (meaningless) cost.
Local models would need `OLLAMA_CONTEXT_LENGTH` of 64k or more; the three
cloud models present do not.
**Ignored:**
- An Agent-tool subagent "on Ollama" -- subagent models are Anthropic only.
- `ollama launch claude ... -- -p` -- the same thing with less control over
  flags and environment; fine by hand, not for a skill.
- `--dangerously-skip-permissions` / `bypassPermissions` -- nothing would
  stop a force push or a merge; deny rules plus `dontAsk` give the same
  autonomy with guard rails.
- Launching the run in the foreground -- hits the 10-minute Bash limit.

### D5. Where tasks work: one persistent task worktree
**Chosen:** tasks are sequential, so **one long-lived git worktree**, a
sibling of the main checkout (e.g. `../<repo-dir>-task`), created once by
`/develop-implement` (or `/develop-task`) with `git worktree add`, and for each
task `git checkout -B task/<big>--<small> origin/develop` inside it. Its
`build/` and `extern/` persist, so each task is an incremental build, not a
clean one. The same worktree serves `/develop-code-review` for that PR (it is
still on the PR branch), and after the merge the local task branch is
deleted (it was squash-merged, so `/prune-old-branches` would otherwise keep
it forever). The main checkout stays on `develop` for the orchestrator and is
never switched while a task runs.
**Ignored:**
- The main checkout -- the orchestrator commits plan updates to `develop`
  while a task runs; one working tree cannot be on two branches.
- A fresh worktree per task (or `claude -w`, or the Agent tool's
  `isolation: worktree`) -- a clean build plus three dependency clones per
  task; `isolation: worktree` also branches from `master`, not `develop`.
- A WSL-native location for speed -- faster I/O
  (`notes/note-faster-unit-test-runs.md`), but outside the Windows side's
  reach; worth measuring later, not needed for the workflow.

### D6. `HAISOS_VERSION`
**Chosen:** bumped **once per develop**, by `/develop-create` (minor +1, patch
0, as `/begin` does), committed as the first commit on `develop`. Task
branches never touch it, so task PRs cannot conflict on it and `/rebase`'s
"base + 1 minor" rule is never triggered inside a develop. If `master` moves
during a develop and `develop` merges it (D7), a conflict on
`HAISOS_VERSION` resolves to `master`'s version + 1 minor -- the same rule
`/rebase` already applies.
**Ignored:**
- A patch bump per task -- every task PR would touch the same line; no
  consumer of the patch number exists (`scripts/pack.sh` already adds the
  branch, minute and hash to the package name).
- A minor bump per task (as `/begin` does) -- a develop would ship as
  "0.12.0" for one feature; the develop is the release unit.

### D7. Branch names and master moving underneath
**Chosen:** one `develop` at a time, named `develop`; task branches
`task/<big-rock>--<small-rock>`. If `master` gains commits during a develop
(a hotfix via `/begin`/`/end`), the orchestrator **merges** `origin/master`
into `develop` (a merge commit, pushed), never rebases `develop`: it is
shared and pushed, and task PRs are based on it. `/rebase` and `/sync` must
not be used on `develop` itself; their docs say so.
**Ignored:**
- `develop/<name>` per develop -- the seed wants one develop at a time and
  "delete develop" at close; the metadata in `develop-plan/` already tells
  develops apart.
- Rebasing `develop` onto `master` -- rewrites a pushed branch under open PRs.

### D8. CI for PRs into `develop`
**Chosen:** the existing `on: push` trigger already runs CI for `develop` and
for every task branch, and its runs show as the PR's checks, so **no new
trigger is needed**. Two changes: (1) fix the Windows test step
(`.github/scripts/run-tests-windows.bat` calls a runner that does not exist
and still succeeds -- `notes/note-review-medium-findings.md`), because a
green check must mean something before a workflow gates merges on it; (2) add
a `concurrency` group per ref with `cancel-in-progress`, so a burst of
playbook commits on `develop` does not queue runs. Expect 3-5 minutes per
run. Windows is checked by CI, not locally (root `CLAUDE.md`: build only on
the local platform).
**Ignored:**
- Adding a `pull_request` trigger -- doubles every run; the push run already
  tests the exact head the PR would merge (task branches start from the
  current `develop`, and tasks are sequential).
- `paths-ignore` for `develop-plan/**` and `notes/**` -- a head commit that
  only touches them (the plan deletion in `/develop-close`) would then have no
  checks at all, and "wait for checks" would need a special case; the runs are
  free on a public repository and short.
- Branch protection with required checks on `develop` -- a single user, and
  the skills already gate on checks; revisit if others contribute.
- Running integration/haisos tests in CI -- they need recordings, which
  do not replay deterministically yet (medium findings, section 3); separate
  work.

### D9. Waiting for CI and fixing it
**Chosen:** after the push, poll until the checks exist (right after a push
`gh pr checks` can report none), then `gh pr checks <N> --watch --fail-fast
--interval 30` inside a loop that respects the 10-minute Bash limit; on
failure, read the failed job's log (`gh run view --log-failed`), fix, commit,
push, and wait again -- **at most 3 fix rounds** (configurable), then the
task fails into A2. The same loop runs in `/develop-code-review` after it
pushes its own fixes, and before every merge.
**Ignored:**
- Unbounded fixing -- a model that cannot fix a Windows-only compile error
  (it cannot build Windows locally) would loop forever.
- Treating CI as optional because local tests passed -- CI is the only
  Windows build in the loop.

### D10. Concurrent edits of `develop-plan/`
**Chosen:** every writer of `develop-plan/` (`/develop-create`,
`/develop-plan`, `/develop-implement`) follows one protocol: fetch; start
from `origin/develop`; edit; commit only `develop-plan/` paths; push; on a
non-fast-forward rejection, fetch, rebase the plan commit onto
`origin/develop`, resolve conflicts (playbook conflicts are line-local and
merged by reading both sides), push again. Nobody keeps uncommitted plan
edits across a wait. A parallel `/develop-plan` runs from another session in
**its own checkout or worktree**, never in the orchestrator's main checkout.
The orchestrator re-reads the plan from `origin/develop` at the top of every
iteration, so a parallel re-plan is picked up at the next task boundary.
Task runs and reviews never touch `develop-plan/` (D1), so plan commits never
conflict with task PRs.
**Ignored:**
- Locking (a lock file or a lock commit) -- a crashed holder blocks
  everyone; push-rejection already detects the race.
- Keeping the plan on a separate branch -- the seed puts it on `develop`,
  and a separate branch would not make the plan part of the develop PR's
  history.

### D11. The orchestrator loop and resumability
**Chosen:** `/develop-implement` keeps **no state it cannot rebuild**: each
iteration starts from `origin/develop:develop-plan/` plus GitHub (`gh pr list
--base develop --state all --search "created:>=<develop creation time>"`,
`git ls-remote` for `task/*` branches), reconciles the playbook with them
(e.g. a PR merged while the orchestrator was down becomes `merged`; a branch
with no PR and no running process becomes `failed` with attempt +1), then:
reviews the past (merged tasks' reported deviations, review outcomes) and the
future (re-checks the next task's plan against current `develop`, D3), makes
any allowed adjustments (D12), commits and pushes the playbook, stops if
nothing is left (or a task is `blocked`), otherwise runs the next task (A1),
checks its **post-conditions** -- PR exists, base `develop`, head
`task/<big>--<small>`, checks green, the diff does not touch `develop-plan/`
or `HAISOS_VERSION`, no Co-Authored-By lines, the tests the plan names were
actually selected -- then runs `/develop-code-review` (D13) and loops. A
restart of `/develop-implement` after a crash, `/clear` or compaction resumes
at the right step. A task already running (the log file is still growing)
is waited for, not restarted.
**Ignored:**
- Keeping loop state in the conversation -- lost on restart or compaction,
  which a multi-hour loop will hit.
- A local state file outside git -- not visible to a parallel `/develop-plan`,
  lost with the checkout.
- Parallel tasks or stacked PRs (task N+1 branched from task N before N is
  merged) -- the seed wants sequential tasks, each starting from a `develop`
  that holds the previous one; the review takes minutes, stacking would buy
  little and cost rebases.

### D12. What the orchestrator may adjust on its own
**Chosen:** the seed asks for automatic adjustments, and the boundary follows
from who owns what. **The user owns** the goal (`goal.md`) and the big rocks:
changing, adding or dropping them, or dropping a small rock that delivers
user-visible behaviour, stops the loop and asks (or waits for a
`/develop-plan` re-run). **The orchestrator may**: re-order `planned` tasks
(a dependency discovered), refresh a task plan to the current code (renames,
moved files), split a task that failed or is too big into smaller ones
within the same rock, add follow-up small rocks for review findings within an
existing rock (A4), mark a task `obsolete` when an earlier one already did its
work, and record each change with its reason under "Adjustments" in the
playbook, in the same commit.
**Ignored:**
- Full autonomy (rocks and goal too) -- the goal is the user's decision by
  the seed's step 2.
- No autonomy (every change asks) -- contradicts the seed's "make those
  adjustments", and would stop the loop at every rename.

### D13. `/develop-code-review` per PR
**Chosen:** a forked skill on an Anthropic model, in the task worktree
(already on the PR branch). It reviews **the PR's diff only** (`gh pr diff`,
plus the surrounding code it needs to judge it), reusing the specialised
prompts of the `code-review-*` skills scoped to the diff: quality, security,
performance, claude-md, logs, and llm-protocol only when the diff touches the
LLMCommunicator or Agent components -- as parallel sub-agents (nesting depth
2 of the default 3). It ranks every finding **critical / high / medium /
low** (today's `/code-review` only orders by importance; the 2026-09-26 review
notes used critical/medium/low/latent), with the rule of
`/auto-code-review`: no race or memory-safety claim without evidence.
Critical and high are fixed by the review agent itself as a commit on the PR
branch (build, unit tests, push, CI wait -- D9); medium and low become PR
review comments (`gh api` line comments where a line is known, a single
`gh pr review --comment` otherwise). It reports the findings, fixed or not,
for the task's outcome, then merges with `gh pr merge <N> --squash
--match-head-commit <sha> --subject "<PR title> (#N)" --body ""`. Squash, as
every PR so far: one commit per task on `develop`, the review fixes folded
in. No `--delete-branch`: the repository already deletes the remote head
branch on merge, and deleting the local branch from `gh` would try to switch
the worktree to `develop`, checked out in the main checkout; the skill
deletes the local branch itself.
**Ignored:**
- Running the whole-codebase `/code-review` per PR -- slow, and findings
  unrelated to the PR would be fixed on the wrong branch (the seed: "the PR
  (and only it)").
- Letting the Ollama model review -- the review is the quality gate for the
  weaker model's code.
- `--auto` merge -- disabled on the repository, and the skill gates on checks
  itself.
- Merge commits into `develop` -- each task would bring its intermediate
  commits (CI fixes, review fixes) into `develop`'s history.

### D14. The end of `/develop-implement`: whole-develop review and the develop PR
**Chosen:** when every task is `merged`/`skipped`/`obsolete`: (1) a
whole-develop review ("per ansamblu" -- as a whole) of `git diff
origin/master...develop` by the same reviewers plus one looking across tasks
(duplicated helpers from different tasks, inconsistent names, docs that
disagree, `CLAUDE.md` tables missing an entry); (2) its critical/high fixed as
**one more PR into `develop`** (`task/final--review-fixes`), implemented by an
Anthropic agent, CI-gated, and listed in the summary like any task -- so every
code change reaches `develop` through a PR; (3) all local tests, all types
(`/test`), on the local platform; (4) the develop -> `master` PR, created with
`--base master`, whose body is **self-contained** (it must survive the
deletion of `develop-plan/`): the goal, the rocks, every task PR with its
review outcome (fixed / commented), the whole-develop review in brief, test
results, deviations and fallbacks; plain text as `/propose-pr-description`
requires. (5) A reserved **exploratory-testing step** between (3) and (4):
empty at first, later inventing haisosfiles from `goal.md`'s acceptance
scenarios and the tools/directives added, fuzzing the haisosfile parser, and
driving the release binary against a scripted fake LLM endpoint as the
2026-09-26 review did; its results go into the PR body.
**Ignored:**
- Committing the final fixes directly to `develop` -- skips CI gating and
  the per-PR record the summary is built from.
- Letting `/develop-implement` merge into `master` -- the seed gives that to
  `/develop-close`, the one step the user triggers by hand.

### D15. `/develop-close` ordering
**Chosen:** (1) refuse unless the develop PR exists, is green, and no task PR
into `develop` is still open (after the merge GitHub would **retarget open
PRs based on `develop` to `master`**) -- close or finish them first; (2)
merge `origin/master` into `develop` if it moved (D7); (3) keep the record
chosen in A5 (e.g. tag); (4) delete `develop-plan/`, commit, push; (5) wait
for the checks of that new head (it is a new commit, D8); (6) merge the PR
with `--match-head-commit` using the strategy of A3; (7) `develop` is deleted
on `origin` by the repository setting -- verify, then delete it locally,
remove the task worktree, check out `master`, pull, `/prune-old-branches`;
(8) report. It refuses while the playbook has unfinished tasks unless told
to close anyway.
**Ignored:**
- Merging first and deleting `develop-plan/` on `master` afterwards -- a
  direct commit to `master`, which the workflow otherwise never makes.
- `gh pr merge --delete-branch` -- the repository already deletes the remote
  branch, and the local delete would fight the checkout.

### D16. Authorization, attribution and permissions
**Chosen:** each develop skill states in its text that invoking it is the
explicit instruction to commit, push, open PRs, comment on and merge them
within its scope -- the way `/auto-code-review` states "commit without
confirmation" -- which satisfies the root `CLAUDE.md` rule "never commit or
push unless explicitly instructed". The one outward step on `master` is in
`/develop-close`, invoked by the user. No Co-Authored-By trailer and no PR
footer: set `"attribution": {"commit": "", "pr": ""}` in the committed
`.claude/settings.json` (it applies to the headless Ollama runs too, which
load project settings), say so in every develop skill and in the task prompt,
check it in the post-conditions (D11), and pass an explicit squash subject
and empty body when merging so no trailer reaches `develop`. PR bodies follow
the existing convention (no footer; PR #15 has none). Permissions: the
gitignored `settings.local.json` already allows `Bash(*)`; the headless runs
add deny rules via `--settings` (D4). The Ollama cloud models' usage limits
and the orchestrator's Anthropic context (long, many iterations) are the
real costs; the json output's cost figure for Ollama models is meaningless.
**Ignored:**
- Relying on the attribution setting alone -- a bug report says it was not
  always honoured; the post-check and explicit squash message are cheap.

### D17. Changes to existing skills, scripts and docs the workflow needs
**Chosen:**
- `scripts/base_branch.sh` learns a branch's recorded base (a per-branch git
  config key set when the task branch is created -- name left to `/todo`),
  checked before the `origin/HEAD` fallback, so `/create-pr`, `/rebase`,
  `/squash`, `/log` and `/propose-pr-description` see `develop` for a task
  branch before its PR exists. `/develop-task` also passes `--base develop`
  explicitly.
- `/propose-pr-description` diffs against the detected base, not the default
  branch.
- `.github/scripts/run-tests-windows.bat` and `ci.yml` as in D8.
- `.gitignore`: the per-task log folder (D4).
- Root `CLAUDE.md`: "Directory Structure" gains `develop-plan/` (transient,
  on `develop` only); "Planning skills" gains a "Develop skills" table and the
  flow `/develop-create` -> `/develop-plan` -> `/develop-implement` ->
  `/develop-close`, with `/note`/`/explore`/`/todo` feeding `/develop-plan`;
  "Automatic Development Rules" gains the develop rules (the develop skills
  are the explicit instruction to commit/push/PR/merge within their scope;
  tasks never touch `develop-plan/` or `HAISOS_VERSION`; `/rebase`/`/sync`
  are not for `develop` itself).
**Ignored:**
- Pushing task branches without `-u` to keep `origin/develop` as upstream
  (so the existing upstream step detects it) -- `/end` and a plain `git push`
  would reset or refuse it; an explicit record is robust.

## Checked

- Every skill in `.claude/skills/`: `begin`, `end`, `sync`, `rebase`,
  `squash`, `commit`, `fetch`, `prune-old-branches`, `create-pr`,
  `propose-pr-description`, `update-pr-description`, `implement`, `todo`,
  `explore`, `note`, `code-review` (and `references/`), `auto-code-review`,
  the six `code-review-*`, `investigate-tests`, `auto-investigate-tests`,
  `test`, `build`, `llm-cache`, `log`, `stage`, `status`.
- `scripts/base_branch.sh`, `scripts/prune_old_branches.sh`,
  `scripts/build_linux_on_linux.sh`, `scripts/test_linux.sh`,
  `scripts/pack.sh`, `scripts/internal/smoke_tests.txt`.
- `.github/workflows/ci.yml` and every file in `.github/scripts/`.
- `.claude/settings.json`, `.claude/settings.local.json`, `.gitignore`,
  `HAISOS_VERSION` (0.3.0).
- Root `CLAUDE.md` ("Planning skills", "Automatic Development Rules",
  "Directory Structure", build and test sections).
- Every file in `notes/` (the runtime ones skimmed for relevance).
- Git: `git log --oneline -60` (all PRs squash-merged, subjects ending in
  `(#N)`), `git log -- .claude/skills`, `git worktree list`.
- GitHub (read-only): `gh auth status`, `gh repo view` (merge settings,
  `deleteBranchOnMerge`), `gh api` for branch protection (none) and
  `allow_auto_merge` (false), `gh pr list --state all` (#1-#16, all base
  `master`), `gh pr checks 16` (push-event checks on a PR), `gh pr view 15`
  (body style), `gh run list` (run durations 2.8-4.8 min), `gh pr checks
  --help`, `gh pr merge --help`.
- Local tools: `claude --version` (2.1.281) and `--help`, `ollama --version`
  (0.21.0), `ollama launch --help`, `/api/tags` (three cloud models),
  `/v1/messages` (present); two headless `claude -p` runs against
  `kimi-k2.6:cloud`, one with tools.
- External docs: Ollama's Claude Code integration and Anthropic
  compatibility pages (docs.ollama.com); Claude Code docs on subagents
  (nesting depth, Anthropic-only models, `isolation: worktree` from the
  default branch, background limits), skills (`context: fork`, `model`,
  `background`), headless mode (`-p`, skills in `-p`, permission modes,
  `--permission-prompts none`, `--bare`, background waits, output formats),
  LLM gateways (non-Claude models unsupported), settings precedence; the
  `attribution` setting and its reported bug (GitHub issue
  anthropics/claude-code#18253).
