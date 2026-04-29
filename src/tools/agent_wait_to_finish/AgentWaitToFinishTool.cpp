#include "AgentWaitToFinishTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentWaitToFinishTool::ToolName = "agent_wait_to_finish";
const std::string AgentWaitToFinishTool::ToolDefaultDescription = "Wait for named subagents to finish. On success, returns an empty string. On error, returns an error message about agents that could not be found or waited for.";

nlohmann::json AgentWaitToFinishTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"names", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "List of agent names to wait for"}
            }},
            {"timeout_ms", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds (0 = poll current status, omit = block forever)"}
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

ToolResult AgentWaitToFinishTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
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

    bool hasTimeout = false;
    uint64_t timeout_ms = 0;
    if (args.contains("timeout_ms") && !args["timeout_ms"].is_null()) {
        hasTimeout = true;
        try {
            timeout_ms = args["timeout_ms"].get<uint64_t>();
        } catch (const nlohmann::json::exception&) {
            return ToolResult{"Invalid timeout_ms: must be a non-negative integer", true};
        }
        constexpr uint64_t MAX_TIMEOUT_MS = 24ULL * 60 * 60 * 1000;
        if (timeout_ms > MAX_TIMEOUT_MS) {
            return ToolResult{"timeout_ms exceeds maximum allowed value of 86400000 ms (24 hours)", true};
        }
    }

    bool anyNotFound = false;
    std::string errorMessage;
    for (const auto& name : args["names"]) {
        if (!name.is_string()) continue;
        std::string agentName = name;

        auto target = FindChildByName(callerAgent, agentName);

        if (!target) {
            LogWarning("AgentWaitToFinishTool: agent '%s' not found", agentName.c_str());
            anyNotFound = true;
            if (!errorMessage.empty()) {
                errorMessage += ", ";
            }
            errorMessage += agentName + " not found";
        } else {
            LogDebug("AgentWaitToFinishTool: waiting for agent '%s' (finished=%d)", agentName.c_str(), target->IsFinished() ? 1 : 0);
            if (!target->IsFinished()) {
                target->Stop(0);
            }
            if (!hasTimeout) {
                target->WaitToFinish();
            } else if (timeout_ms > 0) {
                target->WaitToFinish(timeout_ms);
            }
            LogDebug("AgentWaitToFinishTool: agent '%s' wait completed (finished=%d)", agentName.c_str(), target->IsFinished() ? 1 : 0);
        }
    }

    if (anyNotFound) {
        return ToolResult{errorMessage, true};
    }
    return ToolResult{"", false};
}

} // namespace Haisos::Tools
