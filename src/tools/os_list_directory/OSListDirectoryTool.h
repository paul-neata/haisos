#pragma once
#include <memory>
#include "interfaces/IHaisosOS.h"
#include <nlohmann/json.hpp>

namespace Haisos::Tools {

class OSListDirectoryTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    // os is held by reference, not shared: the OS owns the tool factory that
    // creates this tool, so sharing it back would close an ownership cycle.
    static std::shared_ptr<OSListDirectoryTool> Create(IHaisosOS& os) {
        return std::shared_ptr<OSListDirectoryTool>(new OSListDirectoryTool(os));
    }
    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    explicit OSListDirectoryTool(IHaisosOS& os);

    IHaisosOS& m_os;
};

}
