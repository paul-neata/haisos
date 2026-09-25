#include "BuiltinCommands.h"
#include "BuiltinProcess.h"
#include "BuiltinCommandList.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<BuiltinCommands> BuiltinCommands::Create() {
    return std::shared_ptr<BuiltinCommands>(new BuiltinCommands());
}

BuiltinCommands::BuiltinCommands() {
    for (auto& command : CreateStandardBuiltinCommands()) {
        const std::string name = command->Name();
        m_commands.emplace(name, std::move(command));
    }
}

BuiltinCommands::~BuiltinCommands() = default;

std::vector<std::string> BuiltinCommands::GetCommands() const {
    std::vector<std::string> names;
    names.reserve(m_commands.size());
    for (const auto& command : m_commands) {
        names.push_back(command.first);
    }
    return names;
}

std::string BuiltinCommands::GetBuiltinVersion(const std::string& builtinName) const {
    auto it = m_commands.find(builtinName);
    return it == m_commands.end() ? std::string() : it->second->Version();
}

std::shared_ptr<IProcess> BuiltinCommands::RunCommand(
    const BuiltinCommandHost& host,
    std::shared_ptr<IEnvironment> environment,
    const std::string& builtinName,
    const std::vector<std::string>& args,
    const std::string& workingDirectory,
    const StartProcessOptions& options)
{
    auto it = m_commands.find(builtinName);
    if (it == m_commands.end()) {
        LogWarning("BuiltinCommands: there is no builtin named '%s'", builtinName.c_str());
        return nullptr;
    }
    // As for IHaisosOS::StartProcess: a process runs with what it is handed.
    if (!environment) {
        LogError("BuiltinCommands: refusing to run '%s': no environment was passed", builtinName.c_str());
        return nullptr;
    }
    if (host.os.expired()) {
        LogError("BuiltinCommands: refusing to run '%s': it has no OS to run under", builtinName.c_str());
        return nullptr;
    }
    if (options.interactiveAgent) {
        LogDebug("BuiltinCommands: interactiveAgent only applies to agents; ignoring it for '%s'", builtinName.c_str());
    }
    LogDebug("BuiltinCommands: running '%s' as '%s' with %zu arg(s)",
        builtinName.c_str(), host.programPath.c_str(), args.size());
    return BuiltinProcess::Create(host, std::move(environment), it->second, args, workingDirectory);
}

} // namespace Haisos
