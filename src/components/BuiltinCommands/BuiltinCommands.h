#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "interfaces/IBuiltinCommands.h"
#include "BuiltinCommand.h"

namespace Haisos {

// The IBuiltinCommands every OS is given: the standard set of commands (see
// BuiltinCommandList.h), each run as a BuiltinProcess.
class BuiltinCommands : public IBuiltinCommands {
public:
    static std::shared_ptr<BuiltinCommands> Create();
    ~BuiltinCommands() override;

    std::vector<std::string> GetCommands() const override;
    std::string GetBuiltinVersion(const std::string& builtinName) const override;
    std::shared_ptr<IProcess> RunCommand(
        const BuiltinCommandHost& host,
        std::shared_ptr<IEnvironment> environment,
        const std::string& builtinName,
        const std::vector<std::string>& args,
        const std::string& workingDirectory,
        const StartProcessOptions& options) override;

private:
    BuiltinCommands();

    // Filled in once, by the constructor, and only read after: no lock needed.
    std::map<std::string, std::shared_ptr<IBuiltinCommand>> m_commands;
};

} // namespace Haisos
