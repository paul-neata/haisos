---
name: explore
description: Explore an issue in depth -- its variants and aspects, checked against the code -- and write the findings to notes/explore-<words>.md, from a seed (": text" literal, interpreted text, a notes/ file, or a file path). Clear winners are recorded with why the rest were dropped; undecided aspects are listed most probable first, ready for /todo to ask about. "--redo <exploration> [:] [text]" re-checks and extends one. The exploring runs in a separate agent.
args:
  - name: seed
    description: "': <text>' (literal), '<text>' (interpreted), a notes/ file name with or without .md, a file path with extension, or '--redo <explore name or path> [:] [text]'"
    required: true
---

Explore one issue -- a feature, a design question, a problem -- before
anything is planned. Where `/todo` settles on one approach, `/explore` opens
the question up: it looks for the different ways the thing could be done, the
aspects it touches, and checks each against the code, the architecture and
general knowledge. Whatever that settles, it records with its reasons; whatever
stays a genuine choice, it lists for the user to decide.

The exploration is written to `notes/explore-<words>.md`. `notes/` is shared:
`/note` writes `note-*.md`, `/todo` writes `todo-*.md`, `/implement` writes
`plan-*.md`. This skill only ever writes `explore-*.md` files.

An exploration usually feeds `/todo explore-<words>`, which asks the user about
each open aspect and plans from the answers (see "Planning skills" in the root
`CLAUDE.md`).

## Steps

### 1. Work out the seed

The **seed** is the text the exploration starts from. Let `ARGS` be the
skill's argument, trimmed. Take the first rule that matches:

1. **`--redo ...`** -- rework an existing exploration: go to "Redoing an
   exploration" below.
2. **Starts with `:`** -- the seed is everything after the `:`, trimmed, taken
   exactly as written.
3. **A file in `notes/`** -- `ARGS` is a single token and `notes/<ARGS>` or
   `notes/<ARGS>.md` exists. The seed is that file's exact text.
4. **A file path** -- `ARGS` is a single token with an extension (relative to
   the repo root or absolute) and the file exists. The seed is that file's
   exact text. If it looks like a path but does not exist, say so and stop.
5. **Otherwise, interpret `ARGS`.** It is shorthand for something the user has
   in mind -- most often from the current conversation, sometimes from the
   code, sometimes general knowledge. Write the seed as a short,
   self-contained statement of the issue to explore. If it matches nothing you
   can find, or could mean quite different things, ask the user rather than
   guess.

### 2. Choose the file name

`explore-` followed by two or three meaningful words (four only when three
cannot say it), lowercase, joined by `-`, ending in `.md`. When the seed came
from a `notes/` file, reuse its words (`note-tee-builtin.md` ->
`explore-tee-builtin.md`). Never a number, `misc` or a date. If the name is
taken (`ls notes/`), choose different words; never overwrite -- use `--redo`.

Create `notes/` if it is missing, with an empty `notes/.gitkeep`
(`git add notes/.gitkeep`).

### 3. Record where the exploration starts from

```bash
git rev-parse --abbrev-ref HEAD
git rev-parse --short HEAD
git log -n 40 --format= --name-only | grep -v '^$' | sort | uniq -c | sort -rn | head -40
```

### 4. Spawn the exploring agent

Explore in a **separate agent** (`general-purpose`), so the digging gets a
fresh, deep look and does not fill this conversation. It writes the file
itself. When the seed came from a file, give the agent the path rather than
pasting the text.

Prompt for the agent (fill in the `<...>`):

````
You are exploring an issue before anything is planned. EXPLORE: look for every
reasonable way this could be done and every aspect it touches, and check each
one -- read the code, the interfaces, the CLAUDE.md files, the git history;
reason about the architecture; use web search or documentation when the issue
involves an outside tool, library, protocol or standard (e.g. what the GNU
command does). Think hard. A variant you did not think of is a hole in the
exploration; a variant you listed but did not check is half an exploration.

Seed (<literal | interpreted | from file <path>>):
<the seed text, or "read <path>, its exact text is the seed">

Repository: the Haisos repo in the current directory. Read the root CLAUDE.md
first, then the CLAUDE.md of each component or tool involved.
Branch: <branch>   Commit: <short hash>
Churn over the last 40 commits (count  path):
<output of the churn command>

Read every file in notes/ first (`ls notes/`, then each file in full; skip only
the file you are writing; in a redo you read that one anyway). Notes,
explorations, todos and plans there may be linked to the seed: an earlier
finding, a decision already taken, an aspect already explored, work already
planned or done. Use what is relevant -- build
on it, stay consistent with it, or say explicitly where and why this
exploration departs from it -- and list the related files under "Related notes".

Write the exploration to notes/<explore-words>.md.

## Deciding or not

For each aspect, try to settle it: check it against the code and the rules in
the root CLAUDE.md (ICurrentProcess as the only door out of a process, private
constructors + Create(), builtins copying GNU behaviour, ...), debate it, and
if one variant is a clear winner, record it as decided -- and every other
variant as ignored, with why. Only what remains a genuine choice (a matter of
taste, of product direction, or of trade-offs the user must weigh) stays open.
Do not leave something open that you could have checked.

Order open aspects from most important first, and within each aspect order the
variants from most probable / most natural first to least probable,
least important or technically uglier last.

## Stable vs. volatile references

Name the stable parts of the code freely: interfaces in interfaces/ and their
main types and methods, component and tool folders, the architectural rules in
CLAUDE.md, haisosfile directives, builtin names, test folders, scripts/.
Describe volatile parts (implementation files, helpers, members, exact test
names) by what they do, or mark the name as current ("currently `X` in
`Y.cpp`"). Judge by the churn list and `git log`. Never cite line numbers.

## File layout (exactly these sections, in this order)

# Exploration: <title>

## Seed
<the seed quoted with "> " on every line, exactly as given; for a file seed,
first a line "From `<path>`:".>

## Base
- Branch: `<branch>`
- Commit: `<short hash>` (<commit subject>)
- Related notes: <each related notes/ file, with a few words on how it
  relates -- or "none">

## The issue
<what is being explored, in a paragraph, and what "done" would mean.>

## What exists today
<the facts the aspects rest on: what the code, interfaces and docs already do
or prevent, with stable references. Bullets.>

## Open aspects
<"None." if everything was decided. Otherwise, most important first, each as:

### A<n>. <the aspect as a question, ending in "?">
<1-3 lines: why it matters, what it affects.>
Combinable: <yes | no>   (yes when several variants can be chosen together)

1. **<short variant name>** -- <what it is>. For: <...>. Against: <...>.
2. **<short variant name>** -- ...

The variant names must be short (a few words): /todo offers them as choices.>

## Decided aspects
<"None." or, for each settled aspect:

### D<n>. <the aspect>
**Chosen:** <variant> -- <why it wins: the evidence and reasoning>.
**Ignored:**
- <variant> -- <why not>.>

## Checked
<a short list of what you looked at: files, interfaces, commits, external
references -- so a later reader knows what the conclusions rest on.>

When done, reply with the file path and a short summary: the open aspects (one
line each) and the decided ones (one line each). Do not commit.
````

Wait for the agent to finish (you are notified; do not poll).

### 5. Report

Print the exploration's path and the agent's summary, and remind the user that
`/todo explore-<words>` asks about the open aspects and plans from the answers.
Do not commit it; leave it for the user's next commit.

## Redoing an exploration

`/explore --redo <target> [:] [text]` re-checks an existing exploration
against the current code and extends it with new input, in place.

1. **Target** -- the first token after `--redo`: an `explore-*` name in
   `notes/` (with or without `.md`), or a path to an exploration file. If it
   does not resolve to an existing `explore-*.md` file, say so and stop.
2. **New input** -- the rest of the arguments, trimmed. If it starts with `:`,
   it is literal (the text after the `:`); if it is empty, the exploration is
   only re-checked; otherwise interpret it as in step 1, rule 5.
3. **What changed since** -- read the exploration's `## Base` commit and run
   `git log --oneline <commit>..HEAD` and `git diff --stat <commit>`.
4. **Spawn the exploring agent** with the same instructions as step 4, plus:
   the exploration's path, the new input, and the output of step 3; tell it to
   - re-check every fact, reference and decision against the current code: a
     decided aspect whose evidence changed is re-debated, and may become open;
     an open aspect the code has since settled becomes decided;
   - explore the new input fully -- new aspects, new variants of existing
     aspects, new evidence -- and fold it in where it belongs, keeping the
     section layout and the ordering rules (renumber the aspects if needed);
   - keep the original seed, and append the new input under it as
     `Redo (<YYYY-MM-DD>): ` followed by the input quoted with `> `
     (or `Redo (<YYYY-MM-DD>): re-check only`);
   - replace the `## Base` lines with the current branch and commit, keeping
     the old commit as `- Previously: <old hash>`;
   - report what changed, in a few lines.
5. Report the path and the agent's summary of what changed.
