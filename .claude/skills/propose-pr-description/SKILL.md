---
name: propose-pr-description
description: Compute a PR title and description for the current branch, write them to a temporary .pr.md file, and print the contents to the console.
---

You are a PR description writer. When the user invokes this skill, generate a concise PR title and a detailed PR description for the current branch, save them to a temporary `.pr.md` file, and print the file contents.

## Steps

1. Determine the current branch name with `git rev-parse --abbrev-ref HEAD`.
2. Collect every commit message on this branch that is not on the default branch with `git log --format="%s" <default-branch>..HEAD`.
3. Gather the list of changed files and a high-level diff-stat with `git diff --stat <default-branch>..HEAD`.
4. Compute the PR title:
   - Start from the branch name.
   - Incorporate key themes from the commit messages.
   - Keep it short, clear, and concise (ideally under 70 characters).
5. Compute the PR description:
   - Summarize the skeleton of what was done.
   - Mention new environment variables, new command-line parameters, and new interface/classes.
   - Categorize test changes into three groups: unit tests, integration tests, and haisos tests. Mention any added, removed, or renamed tests.
   - Use only plain text, paragraphs, ordered lists (1. 2. 3.), or unordered bullet lists (-).
   - Do **not** use emojis, HTML, markdown headers (`#`), or font-size changes.
   - Wrap inline code, variable names, function names, class names, and file names in single back-ticks (e.g. `Agent`).
   - For multi-line code blocks, use triple back-ticks on their own lines (```) before and after the block.
6. Write the result to a temporary file:
   - Sanitize the branch name by replacing `/` with `_` (or another safe character) and append `.pr.md`.
   - Use `/tmp/<sanitized_branch>.pr.md` as the file path.
   - Do **not** add leading spaces to any line in the file.
7. Print the exact contents of the file to the console.
8. After the file contents, print one blank line, then:
   `PR title and description: /tmp/<sanitized_branch>.pr.md`

## Output format

The file (and console output) must look exactly like this:

```
PR Title: <Concise PR title here>

PR Description:
<Paragraphs and/or lists describing what was done, new env vars, new CLI params, and test changes.>
```

## Constraints
- Do not indent any line with spaces.
- Keep the title under 70 characters if possible.
- Do not invent changes; derive everything from the git history and diff-stat.
- If there are no test changes, explicitly state that no tests were modified.
