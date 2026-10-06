# Big rocks

## links -- Follow symbolic links anywhere
Removes every piece of link-specific code from the disk-backed filesystems:
links and junctions on the disk are followed wherever they lead, as the host
follows them, and `DELETE` treats a link like whatever it points at.
Done directly on `develop` in 76079bd; its task is obsolete.
Tasks: links--follow-anywhere

## fd -- File descriptors as objects
Open files become `shared_ptr<IFileDescriptor>` everywhere: every filesystem
returns its own descriptor objects (no more `int` descriptors, synthetic
descriptor ranges or mount translation), and every process gets a descriptor
table of its own on its `IFileIO`, with `Dup`/`Dup2`, released when its
program ends. Touches `interfaces/IFileSystemService.h`, `interfaces/IFileIO.h`,
the new `interfaces/IFileDescriptor.h`, the Filesystem component (all of it),
`ProcessFileIO`, `HaisosFileOperations`, `cat`, the `os_*` file tools, and
their tests. About 1750 changed lines.
Tasks: fd--descriptor-objects, fd--process-table

## streams -- Standard streams and exit codes
Every process starts with stdin, stdout and stderr in slots 0/1/2 -- given by
the starter in `StartProcessOptions`, else the console's (raw, untagged, stderr
on the host's stderr) -- and every runtime writes there; every process has an
exit code, and `haisos` returns the first failing `RUN`'s. Touches
`IPhysicalConsole`, `IHaisosOS` (`StartProcessOptions`), `IProcess`,
`IBuiltinCommands` (`BuiltinCommandHost`), the Console, HaisosOS,
BuiltinCommands and Agent components, `src/haisos/main.cpp`, the haisos tests.
About 2150 changed lines.
Tasks: streams--console-and-start, streams--runtime-streams, streams--exit-codes, streams--coroutine-latch

## pipes -- Unnamed pipes
`IPipeService` makes bounded, blocking, unidirectional pipes whose ends are
descriptors; a process makes one with `IFileIO::CreatePipe`; a blocked read or
write is interrupted when its process is asked to stop; a write nobody reads
ends a program quietly with 141. Touches the new `interfaces/IPipeService.h`,
`IServicesCreator`, `IHaisosOS`, a new PipeService component, libheaders, the
runtimes. About 1250 changed lines.
Tasks: pipes--pipe-service, pipes--broken-pipe

## builtins -- Builtins in pipelines, wc and man
Each builtin in a directory of its own, with a manual page; a `Unicode` library (UTF-8 decoding, character classes, display widths); `cat` reads stdin,
`ls` prints one name per line when stdout is not a terminal; a new GNU-exact
`wc` and a `man` that prints the compiled-in pages. Touches the BuiltinCommands
component, the `--init` template (automatically), the BuiltinCommands
CLAUDE.md and the root `CLAUDE.md` tables, the new Unicode component. About 2000 changed lines.
Tasks: builtins--directories, builtins--unicode, builtins--wc, builtins--man

## hsh -- The Haisos shell
`hsh`, a builtin reimplementing dash: lexer, parser, expansions, an executor
that starts processes with pipes and redirections, control flow and functions,
an interactive mode for `RUN -i /bin/hsh`, and its full manual page. Touches
`src/components/BuiltinCommands/commands/hsh/` (new), the `--init` template
(`ENV PATH=/bin`), the haisos tests, docs. About 9750 changed lines.
Tasks: hsh--lexer, hsh--parser, hsh--arith-glob, hsh--expansion, hsh--executor, hsh--redirections, hsh--pipelines, hsh--shell-builtins, hsh--control-flow, hsh--interactive
