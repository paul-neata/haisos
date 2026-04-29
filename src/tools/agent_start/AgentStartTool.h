#pragma once
#include "interfaces/ITool.h"
#include "interfaces/IAgent.h"
#include "interfaces/IFactory.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace Haisos::Tools {

class AgentStartTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    AgentStartTool(IFactory& factory);
    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    IFactory& m_factory;
};

std::shared_ptr<IAgent> CreateAndStartSubagent(
    IFactory& factory,
    std::shared_ptr<IAgent> parent,
    const std::string& userPrompt,
    const std::vector<std::string>& systemPrompts,
    bool longRunning = true);

} // namespace Haisos::Tools
