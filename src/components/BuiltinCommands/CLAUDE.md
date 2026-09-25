# BuiltinCommands

The commands compiled into Haisos itself -- `cat`, `echo`, `ls`, `mkdir`,
`pwd` -- and what places them on filesystems. Implements `IBuiltinCommands`
and `IBuiltinConfigurator` (`interfaces/IBuiltinCommands.h`); both are created
through `IFactory` (`CreateBuiltinCommands`, `CreateBuiltinConfigurator`).

## How a builtin gets run

1. `IBuiltinConfigurator::AddBuiltinCommand(fs, path, name)` places the builtin
   on a filesystem, after checking that its directory exists and that nothing
   is at the path yet. The filesystem records it in its own builtin list (see
   the Filesystem component): the path then lists as a file, reads as a note
   (`BuiltinCommandFileContent`), cannot be written or removed, and pins its
   directory in place. The haisosfile's `BUILTIN` directive goes through here.
2. `IHaisosOS::StartProcess` asks its root filesystem `IsBuiltinCommand(path)`
   before looking at the extension. If it names a builtin, the OS fills in a
   `BuiltinCommandHost` -- its own weak self, a fresh pid, itself as parent, the
   program path, and a console tagged `<name>_<pid>` -- and calls
   `IBuiltinCommands::RunCommand`, whose other parameters are those of
   `StartProcess` with the builtin's name in place of the path.
3. `RunCommand` builds a `BuiltinProcess` and returns at once; the command runs
   on the process's own thread.

## Key Classes

- `BuiltinCommands` - the `IBuiltinCommands`: a name -> command map filled once
  from `CreateStandardBuiltinCommands()` (`BuiltinCommandList.h`)
- `BuiltinConfigurator` - the `IBuiltinConfigurator`; stateless
- `BuiltinProcess` - the `ICurrentProcess` a builtin runs as, shaped like
  `LuaProcess`: a thread started by `Create()` once the object is whole,
  `TriggerStop()` sets a flag commands check between steps, the destructor
  joins. Its `IO()` is a `ProcessFileIO` (the separate `ProcessFileIO` library
  in `src/components/HaisosOS/`), so a builtin reaches files exactly as every
  other runtime does -- `ICurrentProcess` is still the only door out.
  `ExitStatus()` is for tests: `IProcess` has no exit status yet.
- `IBuiltinCommand` / `BuiltinContext` (`BuiltinCommand.h`) - one command, and
  what a run of it is handed: its process, arguments, output, and the stop flag.
  Commands are stateless, so one instance serves every process running it.
- `ParseBuiltinArgs` - a GNU `getopt_long`-style parser shared by the commands:
  clustered short options, unambiguous long-option prefixes, options mixed with
  operands, `--`. `--help`/`--version` are recognized for every command.

## Output

There is no stdin, stdout or stderr yet. `BuiltinContext::Out` buffers text
into lines and writes each as one console `Write` (a final unterminated line
is flushed when the command ends); `Error` writes `<name>: <message>` to the
same console. Messages follow the GNU coreutils wording.

## The commands

The rules every builtin follows -- the real command's output format, every
real argument accepted (untreated ones reported as `Parameter --xyz is not
treated by HaisosOS <command> v. <version>`), and one `--help` shape ending in
the `Not treated arguments:` line -- are in the root `CLAUDE.md` (Builtin
Commands). Here they are built from `IBuiltinCommand::Options()`: one table
per command listing every option of the real one, `id` `kBuiltinNotTreated`
for those Haisos does not act on. `ParseBuiltinArgs`, `BeginBuiltin` (help,
version, usage errors, not-treated reports) and `BuiltinHelpText` all read
that table, so the help can never disagree with what is parsed.

| Command | Version | Treated | Documented exceptions |
|---------|---------|---------|-----------------------|
| `cat` | 1.1.0 | every option of GNU cat | no stdin: at least one FILE, and not `-` |
| `echo` | 1.1.0 | `-n -e -E`, the `-e` escapes; `--help`/`--version` only as the sole argument, as GNU echo | -- |
| `ls` | 1.2.0 | `-a -A -B -c -C -d -f -g -G -h -k -l -m -N -o -p -r -R -s -S -t -u -U -w -x -X -1`, `--file-type --format --full-time --group-directories-first --sort --time --time-style` | owner and group are `haisos`, permissions `rwxrwxrwx` (no users or permissions yet), so a device shows as `crwxrwxrwx`, with its major and minor numbers in the size column as GNU ls shows them; columns are padded with spaces, not tabs; the width is 80 unless `-w` says otherwise; `--sort=version/width` and `--time=birth` are reported as not treated |
| `mkdir` | 1.1.0 | `-p -v` | `-m`/`--mode`, `-Z`/`--context` not treated (no permissions or security contexts) |
| `pwd` | 1.1.0 | `-L -P` (the same: no symlinks) | -- |

`ls` follows GNU ls as it prints to a terminal: the column layout (a line
kept shorter than the width), `-l` with a `total` line in 1K blocks, link
counts, owner, group, right-aligned sizes and the locale time style (`Mon dd
HH:MM` for the last six months, `Mon dd  YYYY` otherwise, or `--time-style`),
shell-escape quoting of names (`'with space'`, `"it's"`) with unquoted names
shifted by one to line up, and the sort orders. Sizes, blocks, link counts and
times come from `IFileIO::Stat`.

## Adding a builtin

Write `commands/<Name>.cpp` implementing `IBuiltinCommand` (it needs only
`BuiltinCommand.h`), declare its factory in `BuiltinCommandList.h`, add it to
`CreateStandardBuiltinCommands()`, and list the source in `CMakeLists.txt`.
That is all: `GetCommands()` then reports it, and since `haisos --init`
builds its template from `GetCommands()`, the generated haisosfile shows a
`# BUILTIN rootfs <name> /bin/<name>` line for it automatically. Add it to the
table above and give it tests in `tests/unit/components/BuiltinCommands.unittests/`.
