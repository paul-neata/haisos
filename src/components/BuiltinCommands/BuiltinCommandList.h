#pragma once
#include <memory>
#include <vector>
#include "BuiltinCommand.h"

namespace Haisos {

// One factory per builtin command, each defined in its own file under
// commands/. Only the registry (BuiltinCommands) and tests need this list; a
// command itself needs only BuiltinCommand.h.
std::shared_ptr<IBuiltinCommand> CreateCatCommand();
std::shared_ptr<IBuiltinCommand> CreateEchoCommand();
std::shared_ptr<IBuiltinCommand> CreateLsCommand();
std::shared_ptr<IBuiltinCommand> CreateMkdirCommand();
std::shared_ptr<IBuiltinCommand> CreatePwdCommand();

// Every builtin Haisos has. Adding one here is all it takes for it to be
// runnable, listed by IBuiltinCommands::GetCommands() -- and so written into
// the haisosfile `haisos --init` generates.
inline std::vector<std::shared_ptr<IBuiltinCommand>> CreateStandardBuiltinCommands() {
    return {
        CreateCatCommand(),
        CreateEchoCommand(),
        CreateLsCommand(),
        CreateMkdirCommand(),
        CreatePwdCommand(),
    };
}

} // namespace Haisos
