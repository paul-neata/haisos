#include "AgentListRunningTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentListRunningTool::ToolName = "agent_list_running";
const std::string AgentListRunningTool::ToolDefaultDescription = "List running subagents. Optionally filter by names.";

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

std::string AgentListRunningTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    std::vector<std::string> filterNames;
    if (args.contains("names") && args["names"].is_array()) {
        for (const auto& name : args["names"]) {
            if (name.is_string()) {
                filterNames.push_back(name);
            }
        }
    }

    nlohmann::json results = nlohmann::json::array();
    if (!callerAgent) {
        return results.dump();
    }

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
            results.push_back(BuildListRunningEntry(child));
        }
    }

    return results.dump();
}

} // namespace Haisos::Tools
