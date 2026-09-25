---
name: todo
description: Write a todo note into the repo's todo/ folder. The argument is interpreted into a self-contained note; if it starts with ':', the text after the ':' is written exactly as given.
args:
  - name: text
    description: What the note is about (e.g. "no stdout and no exit status"), or ':' followed by the exact note text (e.g. ": abc xyz")
    required: true
---

Write one todo note, as a Markdown file in the `todo/` folder at the repo root.

## Steps

### 1. Decide the note's text

Let `TEXT` be the skill's argument.

- **`TEXT` starts with `:`** -- the note is literal. Its content is everything
  after the `:`, with only the whitespace around it trimmed, and nothing else:
  no heading, no rewording, no added context. `/todo : abc xyz` writes a note
  whose whole content is `abc xyz`.

- **Otherwise, interpret `TEXT`.** It is shorthand for something the user has
  in mind -- most often something from the current conversation (a limitation
  just reported, a follow-up just agreed on), sometimes something in the code.
  Work out what it refers to, looking back through the conversation first and
  then at the code if needed, and write a note that stands on its own: someone
  reading it months later, with none of this conversation, must understand
  what is to be done and why. Name the files, classes or commands involved.
  Keep it short -- a heading, then a sentence or a few bullets per item. If
  `TEXT` covers several items, the note covers each of them.

  For example, if the conversation had just reported

  > - **No exit status:** the builtins have exit statuses, but `IProcess` has nowhere to report them yet.
  > - **No stdout:** output goes to the shared console, so several `RUN` builtins print interleaved lines.

  then `/todo no stdout and no exit status` writes a note holding those two
  items, each as a bullet.

  If `TEXT` matches nothing you can find, or could mean several quite
  different things, ask the user what they meant rather than guess.

### 2. Choose the file name

Name the note after what it is about: two or three meaningful words (four
only when three cannot say it), lowercase, joined by `-`, ending in `.md` --
e.g. `builtin-stdout-exit-status.md`, `ls-owner-columns.md`. The words must
say what the note is about; never `note.md`, `todo-1.md` or a date.

The name must not already be taken in `todo/` (check with `ls todo/`). If it
is, choose different words rather than overwriting the existing note.

### 3. Write the note

Create `todo/` if it is missing, with an empty `todo/.gitkeep` so the folder
is kept in git (`git add todo/.gitkeep`).

Write the note to `todo/<name>.md`, ending with a newline:

- literal note: exactly the text from step 1;
- interpreted note: a `# <Title>` heading line, a blank line, then the text.

Do not commit the note; leave it for the user's next commit.

### 4. Report

Print the note's path and its content.
