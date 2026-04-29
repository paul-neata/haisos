#include "AgentStopTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentStopTool::ToolName = "agent_stop";
const std::string AgentStopTool::ToolDefaultDescription = "Initiate graceful stop or force-kill on named subagents. On success, returns an empty string. Stopping an already stopped agent is not an error. Use agent_wait_to_finish to confirm completion.";

nlohmann::json AgentStopTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"names", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "List of agent names to stop"}
            }},
            {"kill", {
                {"type", "boolean"},
                {"description", "If true, kill the agents instead of stopping them gracefully"}
            }}
        }},
        {"required", nlohmann::json::array({"names"})}
    };
}

ToolResult AgentStopTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("names") || !args["names"].is_array()) {
        return ToolResult{"", false};
    }

    bool kill = false;
    if (args.contains("kill") && args["kill"].is_boolean()) {
        kill = args["kill"];
    }

    for (const auto& name : args["names"]) {
        if (!name.is_string()) continue;
        std::string agentName = name;

        auto target = FindChildByName(callerAgent, agentName);

        if (!target) {
            LogWarning("AgentStopTool: agent '%s' not found", agentName.c_str());
        } else {
            LogDebug("AgentStopTool: %s agent '%s' (finished=%d)", kill ? "killing" : "stopping", agentName.c_str(), target->IsFinished() ? 1 : 0);
            if (kill) {
                target->Kill();
            } else {
                target->Stop(0);
            }
        }
    }

    return ToolResult{"", false};
}

} // namespace Haisos::Tools
