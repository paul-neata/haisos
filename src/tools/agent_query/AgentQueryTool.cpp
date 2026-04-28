#include "AgentQueryTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentQueryTool::ToolName = "agent_query";
const std::string AgentQueryTool::ToolDefaultDescription = "Query the status of one or more named subagents.";

nlohmann::json AgentQueryTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"names", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "List of agent names to query"}
            }},
            {"return_console", {
                {"type", "boolean"},
                {"description", "Whether to include the subagent's console output in the result"}
            }},
            {"return_messages", {
                {"type", "boolean"},
                {"description", "Whether to include the subagent's message history in the result"}
            }}
        }},
        {"required", nlohmann::json::array({"names"})}
    };
}

std::string AgentQueryTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("names") || !args["names"].is_array()) {
        return nlohmann::json({{"error", "Missing required field: names"}}).dump();
    }

    bool returnConsole = false;
    if (args.contains("return_console") && args["return_console"].is_boolean()) {
        returnConsole = args["return_console"];
    }

    bool returnMessages = false;
    if (args.contains("return_messages") && args["return_messages"].is_boolean()) {
        returnMessages = args["return_messages"];
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
            result = BuildQueryResult(target, returnConsole, returnMessages);
        }
        results.push_back(result);
    }

    return results.dump();
}

} // namespace Haisos::Tools
