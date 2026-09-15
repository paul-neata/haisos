#pragma once
#include "interfaces/IHaisosOS.h"
#include <nlohmann/json.hpp>

namespace Haisos::Tools {

class OSListDirectoryTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    explicit OSListDirectoryTool(IHaisosOS& os);
    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    IHaisosOS& m_os;
};

}
