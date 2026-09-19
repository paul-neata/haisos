#pragma once
#include <memory>
#include "interfaces/IHaisosOS.h"
#include <nlohmann/json.hpp>

namespace Haisos::Tools {

class OSWriteFileTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    // os is held by reference, not shared: the OS owns the tool factory that
    // creates this tool, so sharing it back would close an ownership cycle.
    //
    // TODO: take the calling process's ICurrentProcess instead and reach the OS
    // through GetHaisosOS(). ICurrentProcess is meant to be the only door out
    // of a process (see the Security section of the root CLAUDE.md); this tool
    // predates that rule and still holds its own handle on the OS.
    static std::shared_ptr<OSWriteFileTool> Create(IHaisosOS& os) {
        return std::shared_ptr<OSWriteFileTool>(new OSWriteFileTool(os));
    }
    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    explicit OSWriteFileTool(IHaisosOS& os);

    IHaisosOS& m_os;
};

}
