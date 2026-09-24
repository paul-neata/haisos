#pragma once
#include "interfaces/ILLMService.h"
#include "interfaces/IFactory.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace Haisos::Tools {

class AgentListRunningTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    static std::shared_ptr<AgentListRunningTool> Create() {
        return std::shared_ptr<AgentListRunningTool>(new AgentListRunningTool());
    }

    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    AgentListRunningTool() = default;
};

} // namespace Haisos::Tools
