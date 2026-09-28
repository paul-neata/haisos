# Builtin commands: the same names on both interfaces

The builtin commands have two interfaces, and they name the same things
differently:

- `IBuiltinCommands` (`interfaces/IBuiltinCommands.h`), the whole set, used by
  the OS, `haisos --init` and the tests: `GetCommands()`,
  `GetBuiltinVersion(builtinName)`, `RunCommand(host, environment, builtinName, ...)`.
- `IBuiltinCommand` (`src/components/BuiltinCommands/BuiltinCommand.h`), one
  command, inside the BuiltinCommands component: `Name()`, `Version()`,
  `Options()`, `Help()`, `Run(context)`.

`IBuiltinCommands` even mixes its own words: its version method says "Builtin"
where its neighbours say "Command".

To do: rename `IBuiltinCommands::GetBuiltinVersion` to `GetCommandVersion`, so
that interface says "command" throughout (`GetCommands`, `GetCommandVersion`,
`RunCommand`), and settle one spelling per idea across the two interfaces --
e.g. `IBuiltinCommand::Name()`/`Version()` becoming `GetName()`/`GetVersion()`
(or the set's getters dropping their `Get`), and the `builtinName` parameters
becoming `commandName`. To update: `BuiltinCommands` (the implementation) and
the BuiltinCommands and Factory unit tests; no documentation names the method.
The word "builtin" stays where it names the concept: the haisosfile's
`BUILTIN`, `IFileSystem::AddBuiltinCommand`, `IBuiltinConfigurator`.
