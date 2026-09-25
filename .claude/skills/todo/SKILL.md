---
name: todo
description: Write a mid-detail plan (not ready-to-implement) into notes/todo-<words>.md, from a seed -- literal text (": text"), interpreted text, a notes/ file, a file path, or an explore-* file whose open aspects are first settled with the user. "--redo <todo> [:] [text]" re-checks and updates an existing todo. The planning runs in a separate agent.
args:
  - name: seed
    description: "': <text>' (literal), '<text>' (interpreted), a notes/ file name with or without .md, a file path with extension, an explore-* name or path (asks the open questions first), or '--redo <todo name or path> [:] [text]'"
    required: true
---

Write one **todo**: a plan that is on its way to being a very good plan, but
deliberately stops in the middle of the road. It says *what* has to change and
*where*, and settles the decisions that need real thought -- but it does not
work out every signature, file and line. That is done later by `/implement`,
possibly by a newer model and against newer code, and detail written now would
either be redone (paying for the design twice) or be stale by then.

The todo is written to `notes/todo-<words>.md`. `notes/` is shared: `/note`
writes `note-*.md`, `/explore` writes `explore-*.md`, `/implement` writes
`plan-*.md`. This skill only ever writes `todo-*.md` files.

The usual flows (see "Planning skills" in the root `CLAUDE.md`):
`/note` -> `/todo` -> `/implement`, `/note` -> `/explore` -> `/todo` -> `/implement`,
`/explore` -> `/todo` -> `/implement`, or `/todo` -> `/implement`.

## Steps

### 1. Work out the seed

The **seed** is the text the todo is built from. Let `ARGS` be the skill's
argument, trimmed. Take the first rule that matches:

1. **`--redo ...`** -- rework an existing todo: go to "Redoing a todo" below.
2. **Starts with `:`** -- the seed is everything after the `:`, trimmed, taken
   exactly as written. `/todo : add a tee builtin` has the seed
   `add a tee builtin`.
3. **An exploration** -- `ARGS` is a single token naming `notes/explore-*.md`
   (with or without `.md`, with or without the `notes/` prefix), or a path to an
   existing file whose name matches `explore-*`. Go to "Seeding from an
   exploration" below; it produces the seed.
4. **A file in `notes/`** -- `ARGS` is a single token and `notes/<ARGS>` or
   `notes/<ARGS>.md` exists (e.g. `note-log-file-recreation`). The seed is that
   file's exact text.
5. **A file path** -- `ARGS` is a single token with an extension (relative to
   the repo root or absolute) and the file exists. The seed is that file's
   exact text. If it looks like a path but does not exist, say so and stop;
   do not fall back to interpreting it.
6. **Otherwise, interpret `ARGS`.** It is shorthand for something the user has
   in mind -- most often from the current conversation (a problem just found,
   a feature just discussed), sometimes from the code, sometimes general
   knowledge. Work out what it means and write the seed as a short,
   self-contained statement of what is wanted: a reader with none of this
   conversation must understand it. If it matches nothing you can find, or
   could mean quite different things, ask the user rather than guess.

### 2. Choose the file name

`todo-` followed by two or three meaningful words (four only when three cannot
say it), lowercase, joined by `-`, ending in `.md`. When the seed came from a
`notes/` file, reuse its words (`note-tee-builtin.md` / `explore-tee-builtin.md`
-> `todo-tee-builtin.md`). Never `todo-1.md`, `todo-misc.md` or a date. If the
name is taken (`ls notes/`), choose different words; never overwrite -- use
`--redo` to update an existing todo.

Create `notes/` if it is missing, with an empty `notes/.gitkeep`
(`git add notes/.gitkeep`).

### 3. Record where the plan starts from

```bash
git rev-parse --abbrev-ref HEAD
git rev-parse --short HEAD
git status --porcelain | head -20
```

Also measure which files change often -- the planner needs it to tell stable
things from volatile ones:

```bash
git log -n 40 --format= --name-only | grep -v '^$' | sort | uniq -c | sort -rn | head -40
```

### 4. Spawn the planning agent

Plan in a **separate agent** (`general-purpose`), so the design gets a fresh,
deep look and the exploration of the code does not fill this conversation. Pass
it everything below; it writes the file itself. When the seed came from a file,
give the agent the path and let it read the file rather than pasting it.

Prompt for the agent (fill in the `<...>`):

````
You are writing a todo: a plan that stops in the middle of the road toward a
very good plan. Think hard before writing -- the value of this document is in
the decisions it gets right, not in its volume.

Seed (<literal | interpreted | from file <path>>):
<the seed text, or "read <path>, its exact text is the seed">
<for an exploration seed: the exploration file path and the user's answers>

Repository: the Haisos repo in the current directory. Read the root CLAUDE.md
first, then the CLAUDE.md of each component or tool you touch.
Branch: <branch>   Commit: <short hash>   Uncommitted changes: <none | list>
Churn over the last 40 commits (count  path):
<output of the churn command>

Read every file in notes/ first (`ls notes/`, then each file in full; skip only
the file you are writing; in a redo you read that one anyway). Notes,
explorations, todos and plans there may be linked to the seed: an earlier
finding, a decision already taken, an aspect already explored, work already
planned or done. Use what is relevant -- build
on it, stay consistent with it, or say explicitly where and why this
plan departs from it -- and list the related files under "Related notes".

Write the todo to notes/<todo-words>.md.

## How detailed

- Decide what needs real thought: the approach, where the change belongs
  (which component, which interface), the boundaries between parts, the
  behaviour visible to users and agents, risks and ordering constraints.
- Do NOT write code, full signatures for implementation classes, line numbers,
  or step-by-step edits. A later implementer, maybe a stronger model, will
  have the code in front of them; leave to them whatever they would decide in
  a minute. Typical length: 60-200 lines.
- Where you leave a choice open on purpose, say so in one line
  ("Left to /implement: ...").

## Stable vs. volatile references

Lean on the stable parts of the code and name them; stay loose about the
volatile parts.
- Stable -- name them freely: the interfaces in interfaces/ and their main
  types and methods; component and tool folder names under src/components/
  and src/tools/; the architectural rules in the root CLAUDE.md
  (ICurrentProcess as the only door out of a process, private constructors +
  Create() returning shared_ptr, per-process OS tools); the haisosfile
  directives; builtin command names and the GNU behaviour they copy; the test
  folders (tests/unit, tests/integration, tests/haisos, tests/mocks); scripts/.
- Volatile -- describe what the code does rather than pinning its name:
  implementation files inside a component, helper and private functions,
  members, file splits, exact test names, counts. When naming one is really
  useful, mark it as such: "currently `X` in `Y.cpp`", "(the parser's RUN
  handling -- name may have changed)". Never cite line numbers.
- Judge by evidence, not by guess: the churn list above, `git log --format=%cr
  -n 1 -- <path>` for when a file last changed, and whether a thing is part of
  an interface or an implementation detail.

## File layout (exactly these sections, in this order; write "None." under a
## section with nothing to change)

# Todo: <title>

## Seed
<the seed: quote it with "> " on every line, exactly as given. For a file seed,
first a line "From `<path>`:" then the quoted text. For an exploration seed:
"From `<explore path>`", the exploration's own seed quoted, then the user's
answers as a list "<aspect question> -> <chosen variant>".>

## Base
- Branch: `<branch>`
- Commit: `<short hash>` (<commit subject>)
- Related notes: <each related notes/ file, with a few words on how it
  relates -- or "none">

## Goal
<2-5 sentences: what will be true when this is done, and why. Then any
"Left to /implement: ..." lines.>

## Interface changes (`interfaces/`)
<new or changed interfaces, types and methods -- the contracts. This is where
precision pays: a signature sketch is fine here. Say which implementations and
mocks (tests/mocks) must follow.>

## haisosfile changes
<new or changed directives, their syntax and meaning, ordering rules, errors
reported, and whether the `haisos --init` template must change.>

## Builtin command changes
<new builtins or changes to existing ones: command lines (options treated,
options accepted but not treated), output format versus the GNU command,
--help/--version, version bumps, registration (the --init template lists every
builtin automatically once registered).>

## Source changes (`src/`)
<systematic: group by area, in dependency order -- src/components/libheaders,
then each component under src/components/ (lower layers first:
Filesystem/FileSystemService, Environment, Console, Logger, HTTPClient/
NetworkService, LLMCommunicator, Agent/LLMService/ToolFactory, BuiltinCommands,
HaisosOS, ServicesCreator/Factory), then src/tools/, then src/haisos/. One
subsection per area touched: what changes and why, in a few bullets. Include
the CLAUDE.md files that must be updated.>

## Tests
<what tests are needed, grouped by category: unit (tests/unit/...), integration
(tests/integration/..., note if new LLM recordings are needed via the llm-cache
skill), haisos (tests/haisos/...), mocks to add or extend. Describe what each
test proves, not its exact name.>

When done, reply with the file path and a 3-5 line summary of the approach and
of what you left to /implement. Do not commit.
````

Wait for the agent to finish (you are notified; do not poll).

### 5. Report

Print the todo's path and the agent's short summary. Do not print the whole
file. Do not commit it; leave it for the user's next commit (`/implement` picks
it up with its first commit).

## Seeding from an exploration

An `explore-*.md` file (written by `/explore`) lists aspects it could not
decide under `## Open aspects`, each as `### A<n>. <question>`, with its
variants numbered most probable first, and marks each aspect `Combinable: yes`
or `no`. Settle them with the user before planning:

1. Read the exploration. If it has no open aspects, tell the user so and use
   the exploration as the seed as it is.
2. Ask with `AskUserQuestion`, up to 4 aspects per call, in the file's order,
   calling it again until every open aspect has been asked:
   - `question`: the aspect's question, ending in `?`;
   - `header`: a 1-2 word label for the aspect (at most 12 characters);
   - `options`: its variants, in the file's order, the first labelled
     `<name> (Recommended)`; `description` is the variant's one-line gist
     with its main pro and con. At most 4 options: when there are more, show
     the first 4 and name the rest in the question ("... or: X, Y"), since the
     user can type any of them as "Other";
   - `multiSelect`: true when the aspect is `Combinable: yes`.
3. The seed is the exploration file plus the answers, one per aspect
   (`<question> -> <answer>`, with any notes the user typed). The exploration's
   `## Decided aspects` stand as they are; pass them on to the planner.

Then continue at step 2, naming the todo after the exploration's words.

## Redoing a todo

`/todo --redo <target> [:] [text]` re-checks an existing todo against the
current code and folds in new input, in place.

1. **Target** -- the first token after `--redo`: a `todo-*` name in `notes/`
   (with or without `.md`), or a path to a todo file. If it does not resolve to
   an existing `todo-*.md` file, say so and stop.
2. **New input** -- the rest of the arguments, trimmed. If it starts with `:`,
   it is literal (the text after the `:`); if it is empty, there is no new
   input and the todo is only re-checked; otherwise interpret it as in step 1,
   rule 6.
3. **What changed since** -- read the todo's `## Base` commit and run
   `git log --oneline <commit>..HEAD` and `git diff --stat <commit>` (both
   limited to the paths the todo mentions, when that is practical).
4. **Spawn the planning agent** with the same instructions as step 4, plus:
   the todo's path, the new input, and the output of step 3; tell it to
   - re-check every reference and claim in the todo against the current code,
     fixing what moved, was renamed or already got done;
   - fold the new input in where it belongs -- update sections, append to them,
     or remove what the input makes obsolete -- keeping the section layout;
   - keep the original seed, and append the new input under it as
     `Redo (<YYYY-MM-DD>): ` followed by the input quoted with `> `
     (or `Redo (<YYYY-MM-DD>): re-check only`);
   - replace the `## Base` lines with the current branch and commit, keeping
     the old commit as `- Previously: <old hash>`;
   - report what changed in the todo, in a few lines.
5. Report the path and the agent's summary of what changed.
