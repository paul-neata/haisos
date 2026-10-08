#pragma once
#include <memory>
#include <vector>
#include "BuiltinCommand.h"

namespace Haisos {

// One factory per builtin command, each defined in a directory of its own,
// commands/<name>/ -- ready for a builtin made of several files. Only the
// registry (BuiltinCommands), the man builtin (whose pages are these command
// objects' -- program data compiled into Haisos, so man may reach them here
// without breaking the ICurrentProcess rule) and tests need this list; any
// other command itself needs only BuiltinCommand.h.
std::shared_ptr<IBuiltinCommand> CreateBasenameCommand();
std::shared_ptr<IBuiltinCommand> CreateCatCommand();
std::shared_ptr<IBuiltinCommand> CreateChmodCommand();
std::shared_ptr<IBuiltinCommand> CreateCpCommand();
std::shared_ptr<IBuiltinCommand> CreateDirnameCommand();
std::shared_ptr<IBuiltinCommand> CreateEchoCommand();
std::shared_ptr<IBuiltinCommand> CreateEnvCommand();
std::shared_ptr<IBuiltinCommand> CreateFalseCommand();
std::shared_ptr<IBuiltinCommand> CreateHshCommand();
std::shared_ptr<IBuiltinCommand> CreateLsCommand();
std::shared_ptr<IBuiltinCommand> CreateManCommand();
std::shared_ptr<IBuiltinCommand> CreateMkdirCommand();
std::shared_ptr<IBuiltinCommand> CreatePwdCommand();
std::shared_ptr<IBuiltinCommand> CreateRealpathCommand();
std::shared_ptr<IBuiltinCommand> CreateRmCommand();
std::shared_ptr<IBuiltinCommand> CreateRmdirCommand();
std::shared_ptr<IBuiltinCommand> CreateSleepCommand();
std::shared_ptr<IBuiltinCommand> CreateSortCommand();
std::shared_ptr<IBuiltinCommand> CreateTrueCommand();
std::shared_ptr<IBuiltinCommand> CreateWcCommand();
std::shared_ptr<IBuiltinCommand> CreateWhichCommand();

// Every builtin Haisos has. Adding one here is all it takes for it to be
// runnable, listed by IBuiltinCommands::GetCommands() -- and so written into
// the haisosfile `haisos --init` generates.
inline std::vector<std::shared_ptr<IBuiltinCommand>> CreateStandardBuiltinCommands() {
    return {
        CreateBasenameCommand(),
        CreateCatCommand(),
        CreateChmodCommand(),
        CreateCpCommand(),
        CreateDirnameCommand(),
        CreateEchoCommand(),
        CreateEnvCommand(),
        CreateFalseCommand(),
        CreateHshCommand(),
        CreateLsCommand(),
        CreateManCommand(),
        CreateMkdirCommand(),
        CreatePwdCommand(),
        CreateRealpathCommand(),
        CreateRmCommand(),
        CreateRmdirCommand(),
        CreateSleepCommand(),
        CreateSortCommand(),
        CreateTrueCommand(),
        CreateWcCommand(),
        CreateWhichCommand(),
    };
}

} // namespace Haisos
