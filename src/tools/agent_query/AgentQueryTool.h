#pragma once
#include "interfaces/ITool.h"
#include "interfaces/IAgent.h"
#include "interfaces/IFactory.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace Haisos::Tools {

class AgentQueryTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    std::string Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }
};

} // namespace Haisos::Tools
