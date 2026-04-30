#include "AgentListRunningTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentListRunningTool::ToolName = "agent_list_running";
const std::string AgentListRunningTool::ToolDefaultDescription = "List currently running subagents by name. On success, returns a comma-separated string of agent names without spaces, like 'agent1,agent2'.";

nlohmann::json AgentListRunningTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"names", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Optional filter: only include agents with these names"}
            }}
        }},
        {"required", nlohmann::json::array()}
    };
}

ToolResult AgentListRunningTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    std::vector<std::string> filterNames;
    if (args.contains("names") && args["names"].is_array()) {
        for (const auto& name : args["names"]) {
            if (name.is_string()) {
                filterNames.push_back(name);
            }
        }
    }

    if (!callerAgent) {
        return ToolResult{"no caller agent", true};
    }

    std::string commaSeparatedNames;
    size_t runningCount = 0;
    for (const auto& child : callerAgent->GetChildren()) {
        std::string name = child->Name();
        if (!filterNames.empty()) {
            bool found = false;
            for (const auto& n : filterNames) {
                if (n == name) {
                    found = true;
                    break;
                }
            }
            if (!found) continue;
        }

        if (!child->IsFinished() && !child->IsKilled()) {
            LogVerboseDebug("AgentListRunningTool: found running agent '%s'", name.c_str());
            if (!commaSeparatedNames.empty()) {
                commaSeparatedNames += ",";
            }
            commaSeparatedNames += name;
            ++runningCount;
        }
    }

    LogDebug("AgentListRunningTool: returning %zu running agent(s)", runningCount);

    return ToolResult{commaSeparatedNames, false};
}

} // namespace Haisos::Tools
