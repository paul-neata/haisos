#include "AgentQueryTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"
#include "src/tools/tools_common/ToolArguments.h"

namespace Haisos::Tools {

const std::string AgentQueryTool::ToolName = "agent_query";
const std::string AgentQueryTool::ToolDefaultDescription = "Query the status of named subagents. On success, returns a JSON array of agent status objects. Each object includes the agent's name, starting_time, finished, and oneShot status.";

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
    std::vector<std::string> names;
    if (auto error = ReadRequiredArgument(args, "names", names)) {
        return *error;
    }
    bool returnConsole = false;
    if (auto error = ReadOptionalArgument(args, "return_console", returnConsole)) {
        return *error;
    }
    bool returnMessages = false;
    if (auto error = ReadOptionalArgument(args, "return_messages", returnMessages)) {
        return *error;
    }

    nlohmann::json results = nlohmann::json::array();
    size_t foundCount = 0;
    for (const auto& agentName : names) {
        auto target = FindChildByName(callerAgent, agentName);

        nlohmann::json result;
        if (!target) {
            LogWarning("AgentQueryTool: agent '%s' not found", agentName.c_str());
            result["name"] = agentName;
            result["found"] = false;
        } else {
            // WaitToFinish(0) does not wait; it just reports whether the
            // agent has finished.
            bool finished = target->WaitToFinish(0);
            LogVerboseDebug("AgentQueryTool: querying agent '%s' (finished=%d)", agentName.c_str(), finished ? 1 : 0);
            result["name"] = target->Name();
            result["starting_time"] = target->GetStartTime();
            result["finished"] = finished;
            result["oneShot"] = !target->IsInteractive();
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

    // A console or a history may hold bytes that are not UTF-8 (whatever a
    // file the agent read contained), on which the default dump() throws:
    // replaced, they come back as U+FFFD instead.
    std::string content = results.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return ToolResult{std::move(content), foundCount == 0};
}

} // namespace Haisos::Tools
