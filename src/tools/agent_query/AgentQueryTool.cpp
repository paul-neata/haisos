#include "AgentQueryTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentQueryTool::ToolName = "agent_query";
const std::string AgentQueryTool::ToolDefaultDescription = "Query the status of named subagents. On success, returns a JSON array of agent status objects. Each object includes the agent's name, starting_time, killed, finished, and oneShot status.";

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

ToolResult AgentQueryTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("names") || !args["names"].is_array()) {
        return ToolResult{"Missing required field: names", true};
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
    size_t foundCount = 0;
    for (const auto& name : args["names"]) {
        if (!name.is_string()) continue;
        std::string agentName = name;

        auto target = FindChildByName(callerAgent, agentName);

        nlohmann::json result;
        if (!target) {
            LogWarning("AgentQueryTool: agent '%s' not found", agentName.c_str());
            result["name"] = agentName;
            result["found"] = false;
        } else {
            LogVerboseDebug("AgentQueryTool: querying agent '%s' (finished=%d, killed=%d)", agentName.c_str(), target->IsFinished() ? 1 : 0, target->IsKilled() ? 1 : 0);
            result["name"] = target->Name();
            result["starting_time"] = target->GetStartTime();
            result["killed"] = target->IsKilled();
            result["finished"] = target->IsFinished();
            result["oneShot"] = !target->IsLongRunning();
            if (returnConsole) {
                result["console_result"] = target->GetConsoleOutput();
            }
            if (returnMessages) {
                result["messages_result"] = target->GetHistory();
            }
            ++foundCount;
        }
        results.push_back(result);
    }

    if (foundCount == 0) {
        return ToolResult{results.dump(), true};
    }
    return ToolResult{results.dump(), false};
}

} // namespace Haisos::Tools
