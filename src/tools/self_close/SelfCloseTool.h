#pragma once
#include "interfaces/ILLMService.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace Haisos::Tools {

// Closes the agent that calls it. What an interactive agent uses to end its
// own session, since nothing else it does ever finishes it.
class SelfCloseTool : public ITool {
public:
    static const std::string ToolName;
    static const std::string ToolDefaultDescription;

    static std::shared_ptr<SelfCloseTool> Create() {
        return std::shared_ptr<SelfCloseTool>(new SelfCloseTool());
    }

    ToolResult Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) override;
    static nlohmann::json GetDefaultParametersSchema();
    nlohmann::json GetParametersSchema() const override { return GetDefaultParametersSchema(); }

private:
    SelfCloseTool() = default;
};

} // namespace Haisos::Tools
