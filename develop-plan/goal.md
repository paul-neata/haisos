# Develop: Follow symbolic links anywhere

## Metadata
- Created: 2026-10-05T07:24:52Z
- Base: master @ f582be0

## Settings
- Task models: kimi-k3:cloud
- Attempts per model: 2
- CI fix rounds: 3
- Task timeout minutes: 120
- Review model: opus
- Helper model: sonnet

## Goal
Symbolic links (and, on Windows, junctions) inside a disk-backed filesystem
(`FS ... PHYSICAL`, a plain-path `ROOT`, `IFactory::CreatePhysicalFileSystem`)
are followed wherever they lead, exactly as the host follows them -- including
out of the filesystem's directory. Whoever declares a physical filesystem in a
haisosfile vouches for the links in it, the way a user who bind-mounts a
directory vouches for its contents. Today a link leading outside is refused
("path escapes root"), a dangling one is refused, and Haisos keeps its own
link bookkeeping (`FileStatus::symbolicLink`, link-aware `DELETE`); all of that
goes, for simpler code (about 150 lines of production code and their tests)
and because it is not needed. What stays is the confinement of paths **as
written**: `..` cannot climb above a filesystem's top, every segment must be a
plain host name, and the top of a physical filesystem can be neither removed
nor created.

## Clarifications
- Q: Remove only the "links stay inside the root" confinement, or every piece of link-specific code? -- A: Everything link-specific: the confinement (`CanonicalWithin`, `IsWithin`), the dangling-link refusal, `O_NOFOLLOW`, the `Follow`/`Keep` resolution modes, `FileStatus::symbolicLink`, `FileSystem::IsLink`, and `DELETE`'s link handling. Accepted consequence: `DELETE` of a directory holding a link to another directory empties that other directory, wherever it lies, then removes the link. (A link to a file is still removed itself: that is what the host's `unlink()` does.)
- Q: A dangling link opened with `O_CREAT` (e.g. `CREATE`, `os_write_file`)? -- A: Followed, as on Linux: the target is created wherever the link points.
- Q: What does this develop hold? -- A: Only this change; one task, then close.
- Q: Security? -- A: Accepted by the user (decided, not asked again): a physical filesystem is only as narrow as the links inside it. A mounted directory with `docs -> ~/.ssh` lets an agent read the keys (and send them to the LLM endpoint); `notes -> ~/.bashrc` lets it write there. Nothing inside Haisos can create a link -- no builtin, tool or directive does -- and the root `CLAUDE.md` says from now on that none may, on a physical filesystem, without bringing a confinement back. `RO` still blocks writes (it acts on the path, before the disk); `MEM` is untouched.
- Q: Keep `FS ... PHYSICAL` as its own `PhysicalFileSystem` rather than a `SubFileSystem` of the full disk? -- A: Yes, decided: it still has to take UNC paths on Windows, which the full filesystem does not hold. Only the comments that justify it by links change.

## Acceptance scenarios
1. **Reading through a link out of the directory.** `project/haisosfile`:
   ```
   FS rootfs PHYSICAL .
   ROOT rootfs
   CREATE_DIR /bin
   BUILTIN rootfs cat /bin/cat
   RUN /bin/cat /data/readme.txt
   ```
   with `project/data -> ../shared` and `shared/readme.txt` holding `hello`.
   The console shows `hello`. Today: `cat: /data/readme.txt: No such file or directory`.
2. **Writing through a link.** Same root, `project/out -> ../results`, and
   `RUN /w.lua` where `w.lua` calls `os_write_file` on `/out/r.txt` with
   `done`: `results/r.txt` holds `done` afterwards.
3. **Creating through a dangling link.** `project/new.txt -> ../made.txt`,
   `made.txt` missing; the directive `CREATE /new.txt 'hi'` succeeds, and
   `made.txt` (outside `project/`) holds `hi`.
4. **`DELETE` follows a link to a directory.** `project/work/cache -> /tmp/c`
   holding `a.txt`; `DELETE /work` succeeds: `project/work` is gone, `/tmp/c`
   still exists but is empty. `DELETE /f` where `f -> ../keep.txt` removes the
   link only; `keep.txt` stays.
5. **Paths as written are still confined.** `RUN /bin/cat /../x.txt` (or
   `os_read_file` on `../x.txt` from `/`) still cannot reach the directory
   above the root; `DELETE /` on a physical root still fails without removing
   the root directory.

## Out of scope
- Creating links: no `ln` builtin, no tool, no directive (and a rule that none is added without a confinement).
- Showing links as links: `ls -l` keeps reporting the target's type, with no `l` or `-> target`; `ls -R` keeps descending into links (the existing low finding about cycles stays open).
- `pwd -P` / `-L` (Haisos's working directory stays logical).
- `SubFileSystem`, `RO`, `MEM`, `DEV`, mounts: unchanged.
- Hard links: nothing was done about them, nothing changes.
