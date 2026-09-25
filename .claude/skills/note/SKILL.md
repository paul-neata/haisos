---
name: note
description: Write a note into the repo's notes/ folder as notes/note-<words>.md, or list the notes with "/note list". The text is interpreted into a self-contained note; if it starts with ':', the text after the ':' is written exactly as given.
args:
  - name: text
    description: What the note is about (e.g. "why log files are re-created"), ':' followed by the exact note text (e.g. ": abc xyz"), or the single word "list"
    required: true
---

Write one note, as a Markdown file in the `notes/` folder at the repo root --
or, with `list`, show the notes already there.

A note records something worth keeping -- a finding, a decision and its
reason, how something works, a gotcha, or work still to be done. `notes/` is
shared: a plan skill writes `plan-*.md` there. This skill only ever writes,
names and lists `note-*.md` files.

## Steps

### 1. Decide what was asked

Let `TEXT` be the skill's argument, with surrounding whitespace trimmed.

- **`TEXT` is exactly the single word `list`** -- list the notes: go to
  "Listing the notes" below and stop there. (A note whose whole text is "list"
  is written with `/note : list`.)
- **`TEXT` starts with `:`** -- the note is literal. Its content is everything
  after the `:`, with only the whitespace around it trimmed, and nothing else:
  no heading, no rewording, no added context. `/note : abc xyz` writes a note
  whose whole content is `abc xyz`.
- **Otherwise, interpret `TEXT`.** It is shorthand for something the user has
  in mind -- most often something from the current conversation (an
  explanation just given, a cause just found, a choice just made), sometimes
  something in the code. Work out what it refers to, looking back through the
  conversation first and then at the code if needed, and write a note that
  stands on its own: someone reading it months later, with none of this
  conversation, must understand it. Name the files, classes or commands
  involved. Keep it short -- a heading, then a few sentences or bullets. If
  `TEXT` covers several things, the note covers each of them.

  For example, right after finding out why a deleted log file stopped being
  written to, `/note why log files are re-created` writes a note explaining
  that an open file outlives its deleted name on POSIX, and that
  `src/haisos/ReopeningLogFile.h` re-creates the file when its path is gone.

  If `TEXT` matches nothing you can find, or could mean several quite
  different things, ask the user what they meant rather than guess.

### 2. Choose the file name

`note-` followed by two or three meaningful words (four only when three
cannot say it), lowercase, joined by `-`, ending in `.md` -- e.g.
`note-log-file-recreation.md`, `note-builtin-fd-namespace.md`. The words (not
counting the `note-` prefix) must say what the note is about; never
`note-1.md`, `note-misc.md` or a date.

The name must not already be taken in `notes/` (check with `ls notes/`). If it
is, choose different words rather than overwriting the existing file.

### 3. Write the note

Create `notes/` if it is missing, with an empty `notes/.gitkeep` so the folder
is kept in git (`git add notes/.gitkeep`).

Write the note to `notes/note-<words>.md`, ending with a newline:

- literal note: exactly the text from step 1;
- interpreted note: a `# <Title>` heading line, a blank line, then the text.

Do not commit it; leave it for the user's next commit.

### 4. Report

Print the note's path and its content.

## Listing the notes

1. Find the notes, most recently modified first, with their times:
   ```bash
   for f in $(ls -t notes/note-*.md 2>/dev/null); do
       echo "$(date -r "$f" '+%Y-%m-%d %H:%M')  $f"
   done
   ```
   If there are none, print `(no notes in notes/)` and stop.

2. Read each file and sum it up in one sentence: what the note is about, in
   your own words (not just its heading).

3. Print one line per note, most recent first, as a table without borders:
   three columns, padded with spaces so they line up, under a header line
   naming them, inside a fenced code block so the alignment survives:
   ```
   time              filepath                            what it does
   2026-09-25 15:02  notes/note-log-file-recreation.md   Explains why deleted log files are re-created and where that happens.
   2026-09-24 11:40  notes/note-builtin-fd-namespace.md  Records why synthetic fds come from one program-wide counter.
   ```
   The file path is relative to the current directory, as `ls` printed it.
   Print nothing else besides the table.
