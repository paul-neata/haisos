#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "interfaces/IHaisosOS.h"
#include "src/components/libheaders/CurrentProcessHandle.h"

namespace Haisos {

// The OS-level tool set (read/write files, list directories, start/list
// processes, ...) -- distinct from ILLMService's agent-management tool set.
// Merged with an agent's own tools via CompositeToolFactory when an
// agent-backed process is started by an IHaisosOS.
//
// Built once per process, around that process rather than around an OS:
// ICurrentProcess is the only door out of a process (see the Security section
// of the root CLAUDE.md), so every tool here reaches exactly the OS its calling
// process was given, and no more. It is also what lets a tool resolve a
// relative path against the calling process's working directory.
//
// The process is reached through a CurrentProcessHandle rather than directly,
// because an agent needs its tools before the process wrapping it can be built;
// whoever builds the process fills the handle in before it goes live.
class OSToolFactory : public IToolFactory {
public:
    static std::shared_ptr<OSToolFactory> Create(std::shared_ptr<CurrentProcessHandle> process) {
        return std::shared_ptr<OSToolFactory>(new OSToolFactory(std::move(process)));
    }

    OSToolFactory(const OSToolFactory&) = delete;
    OSToolFactory& operator=(const OSToolFactory&) = delete;

    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent> callerAgent = nullptr) override;
    bool HasTool(const std::string& name) const override;
    std::vector<std::string> GetAvailableTools() const override;
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override;

private:
    explicit OSToolFactory(std::shared_ptr<CurrentProcessHandle> process);

    struct ToolEntry {
        std::string name;
        std::function<std::string()> getDescription;
        std::function<nlohmann::json()> getSchema;
        std::function<std::shared_ptr<ITool>()> create;
    };

    std::vector<ToolEntry> m_registry;
    std::shared_ptr<CurrentProcessHandle> m_process;
};

}
