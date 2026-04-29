#include "AgentStopTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentStopTool::ToolName = "agent_stop";
const std::string AgentStopTool::ToolDefaultDescription = "Stop or kill one or more named subagents.";

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

std::string AgentStopTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("names") || !args["names"].is_array()) {
        return nlohmann::json({{"error", "Missing required field: names"}}).dump();
    }

    bool kill = false;
    if (args.contains("kill") && args["kill"].is_boolean()) {
        kill = args["kill"];
    }

    nlohmann::json results = nlohmann::json::array();
    for (const auto& name : args["names"]) {
        if (!name.is_string()) continue;
        std::string agentName = name;

        auto target = FindChildByName(callerAgent, agentName);

        nlohmann::json result;
        result["name"] = agentName;
        if (!target) {
            result["error"] = "agent " + agentName + " not found";
        } else {
            if (kill) {
                target->Kill();
            } else {
                target->Stop(0);
            }
            result = BuildStopResult(target);
        }
        results.push_back(result);
    }

    return results.dump();
}

} // namespace Haisos::Tools
