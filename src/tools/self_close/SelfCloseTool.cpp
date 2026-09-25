#include "SelfCloseTool.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

const std::string SelfCloseTool::ToolName = "self_close";
const std::string SelfCloseTool::ToolDefaultDescription = "Close yourself: end this agent's session. Use it when the user wants to end an interactive session, or when your work is complete and nothing more is expected of you. Once called, no further tool calls in this turn are run and no more messages will reach you.";

nlohmann::json SelfCloseTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()}
    };
}

ToolResult SelfCloseTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& /*args*/) {
    if (!callerAgent) {
        return ToolResult{"No calling agent to close", true};
    }
    LogInfo("SelfCloseTool: agent '%s' is closing itself", callerAgent->Name().c_str());
    // Only a request, made from the agent's own thread: it cannot wait for
    // itself to finish. The agent finishes once this round is over.
    callerAgent->TriggerStop();
    return ToolResult{"Closing: this agent will stop once the current turn ends.", false};
}

} // namespace Haisos::Tools
