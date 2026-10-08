# Develop: Various unix commands used by coding agents

## Metadata
- Created: 2026-10-07T05:57:48Z
- Base: master @ d40f97c

## Settings
- Task models: glm-5.3:cloud
- Attempts per model: 2
- CI fix rounds: 3
- Task timeout minutes: 120
- Review model: opus
- Helper model: sonnet

## Goal
Haisos ships, as builtin commands, the Unix commands coding agents reach for:
the file commands (`cp`, `mv`, `rm`, `rmdir`, `touch`, `chmod`), the small
utilities (`basename`, `dirname`, `realpath`, `which`, `env`, `true`, `false`,
`sleep`, `printf`, `seq`, `date`, `stat`, `du`, `cmp`, `test`/`[`), the text
filters (`head`, `tail`, `sort`, `uniq`, `cut`, `tr`, `tee`, `nl`), search and
edit (`grep`/`egrep`/`fgrep`, `find`, `xargs`, `sed`), `diff` and `patch`, a
POSIX `awk`, `rg`, a `jq` subset, `tar`, and `ps`/`kill`/`pgrep`/`pkill` over Haisos's
own processes. Each is placed with `BUILTIN` like the existing ones, accepts
every argument of the real command, prints exactly what GNU (or ripgrep, jq,
procps) prints, and has `--help`, `--version` and a `man` page. So a script
an agent (or a person) would write for a Linux shell -- `grep -rn TODO src |
sort | head`, `find . -name '*.cpp' | xargs wc -l`, `sed -i 's/a/b/g' f`,
`diff -u a b > p && patch -p0 < p` -- runs unchanged in `hsh` inside Haisos,
with the output it would have on Linux. To support them, `IFileSystem` and
`IFileIO` gain rename and set-times operations, and grep, sed, find, awk and rg
share one regex engine of Haisos's own.

## Clarifications
- Q: Which commands? -- A: every group: file commands + small utilities, text filters, search & edit (grep, find, sed, xargs), diff & patch; plus, from the user's investigation (claude-code-shell-commands.md, the commands Claude Code's Bash tool relies on), `stat`, `du`, `which`, `cmp`, `nl`, `egrep`/`fgrep`, `test`/`[` as programs, `chmod`, and the larger `rg`, `jq` (subset), `tar`, `ps`/`kill`/`pgrep`, and a POSIX `awk` subset.
- Q: Which tools stay out? -- A: the complicated ones -- `git`, `node`, `python`, `make`, package managers, compilers, `docker`, `gh`; `curl` too (network permissions are not implemented); `bat` (not a standard Unix command).
- Q: How do agents use them? -- A: as builtin commands like the existing ones -- GNU arguments respected, `--help`, `--version`, `man` pages. A tool letting an agent run a command and read its output (`os_run_command`) is out of scope.
- Q: Regex flavours? -- A: one regex engine of Haisos's own (decided: hsh already avoids `<regex>`, std::regex recurses per character and overflows the stack on long lines, and its dialects differ from GNU's): GNU BRE and ERE with `\w \W \s \S \b \B \< \> \+ \? \|`, back-references, POSIX classes, leftmost-longest matching; `grep -P` (and rg's syntax) as a Perl subset -- `\d \D \s \w`, lazy quantifiers, `(?:...)`, `(?i)` -- leftmost-first, no lookaround.
- Q: One develop or several? -- A: one big develop with everything (52 tasks, well beyond the usual 6-20).
- Q: awk? -- A: a POSIX awk subset: patterns and actions, fields and `-F`, `BEGIN`/`END`, variables and `-v`, arrays, `printf`, the common built-in functions, `getline`, output redirection.
- Q: chmod, with no permissions in Haisos? -- A (decided): accepted -- the mode is parsed and validated, the files must exist (GNU's errors otherwise) -- and nothing changes; a documented exception, consistent with `ls -l` showing `rwxrwxrwx`.
- Q: mv between filesystems? -- A (decided): when `Rename` fails across mounts, copy then remove, as GNU mv does across devices.
- Q: printf, test, true, false -- hsh builtins or programs? -- A (decided): `/bin` programs, found by hsh through `PATH`; `test`/`[` share hsh's evaluator rather than copy it.
- Q: tar compression? -- A (decided): ustar archives without compression (no deflate in the tree); `-z`/`-j`/`-J` fail the command with an error, never write an uncompressed archive silently.
- Q: ps/kill/pgrep? -- A (decided): over `IHaisosOS::GetRunningProcesses()`, reached through `ICurrentProcess::OS()`; `kill` stops a process with `TriggerStop()` (TERM, KILL, INT); what Haisos lacks (ttys, CPU time, users) is a documented exception.
- Q: diff side-by-side (`-y`/`-W`)? -- A: not treated; diff--diff-core stays one task (~1050 lines).
- Q: awk's `system()` and `close()` of a pipe? -- A: plain gawk 5's values -- the command's exit status (`system("exit 3")` is 3; 256 + signal when killed) -- not gawk `--posix`'s wait status.
- Q: egrep/fgrep's obsolescence warning? -- A: none, as Ubuntu's scripts (upstream 3.8+ prints one).
- Q: tar member modes, with no permissions in Haisos? -- A: 0644 for files, 0755 for directories (owner `haisos`, uid/gid 0), so archives extract normally on real systems.
- Q: Task model? -- A: `glm-5.3:cloud` for this develop (Settings).

## Acceptance scenarios

Each haisosfile mounts a project directory as root, places the builtins it
uses (`CREATE_DIR /bin`, `BUILTIN rootfs <name> /bin/<name>`, `ENV
PATH=/bin`) and runs `hsh`; the output must equal, byte for byte, what bash
with GNU coreutils/grep/findutils/sed/diffutils/patch/tar, gawk --posix,
ripgrep, jq and procps prints for the same tree (process listings aside).

1. **Search a project.** `RUN /bin/hsh -c 'grep -rn --include="*.cpp"
   TODO src | sort -t: -k1,1 -k2,2n | head -5; find . -name "*.h" -newer
   CMakeLists.txt -print0 | xargs -0 wc -l | tail -1; rg -n -w Create src |
   cut -d: -f1 | uniq -c | sort -rn | head -3'` -- the matches, the line
   total, the files most using `Create`; `grep` exits 1 when nothing matches.
2. **Edit and compare.** `cp -r src /tmp/orig; sed -i -E
   's/(Create)\(\)/\1Instance()/g' src/a.cpp; diff -u /tmp/orig/a.cpp
   src/a.cpp > /tmp/p; cat /tmp/p; cp /tmp/orig/a.cpp src/a.cpp; patch -p0
   < /tmp/p` (paths adjusted) -- the unified diff, then `patching file
   ...`, and the file again as `sed` left it; `cmp` of the two copies is
   silent.
3. **Manage files.** `mkdir -p out/a; touch -d "2024-01-02 03:04" out/a/x;
   cp -a out out2; mv out2/a/x out2/a/y; stat -c "%n %s %y" out2/a/y; du -sh
   out; rm -rv out2; ls out2` -- the stat line with the preserved time, `rm`'s
   `removed ...` lines, and `ls: cannot access 'out2': No such file or
   directory`.
4. **Data one-liners.** `jq -r '.dependencies | keys[]' package.json | sort;
   awk -F, 'NR > 1 { s += $3 } END { printf "%.2f\n", s }' data.csv; seq -w
   1 3 | tr '\n' ' '; printf "%-8s|%5.1f\n" a 2.5; tar -cf /tmp/s.tar src &&
   tar -tvf /tmp/s.tar | head -3` -- the keys, the sum, `01 02 03 `, the
   formatted line, the archive listing.
5. **Processes.** `RUN -i /bin/hsh`, then `sleep 100 &`, `ps`, `pgrep
   sleep`, `kill $!`, `wait`; and `man grep`, `sed --help`,
   `awk --version` for any new command -- the same `--help` shape and `man`
   page as the existing builtins.

## Out of scope
- A tool for agents to run a command and read its output (`os_run_command`); agents keep `os_start_process`.
- `git`, `node`, `python`, `make`, package managers, compilers, `docker`, `gh`, `curl`, `bat`, `less`/pagers, editors.
- gawk extensions beyond POSIX awk (`--re-interval` aside); the full jq language (modules, `@format` strings beyond `@csv @tsv @json @text @sh @base64`, `limit`/`input` streaming, SQL-style builtins); tar compression and formats other than ustar/GNU long names; regex lookaround and Unicode properties; `sed`'s GNU `e` command; `find -fprint*`/`-ls` niceties may be reported as not treated where Haisos lacks data.
- Symbolic and hard links: no command creates one (`ln`, `cp -s/-l` reported as failing -- the root CLAUDE.md forbids creating links on a physical filesystem).
- A `kill` shell builtin in hsh (job specs like `kill %1`): `/bin/kill` takes pids.
- Permissions, owners and users (`chmod` changes nothing; `chown`, `chgrp`, `install` not added).
