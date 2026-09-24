#include "AgentWaitToFinishTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentWaitToFinishTool::ToolName = "agent_wait_to_finish";
const std::string AgentWaitToFinishTool::ToolDefaultDescription = "Wait for named subagents to finish. On success, returns an empty string. On error, returns an error message about agents that could not be found or waited for. For oneShot=true agents, it is fine to omit timeout_ms; they finish as soon as they can and this call will return promptly. For oneShot=false agents timeout_ms is required, because such an agent keeps waiting for more commands and never finishes on its own.";

// The longest an agent may be waited for in one call, and what an omitted
// timeout_ms falls back to. There is no unbounded wait: an agent cannot be told
// to stop through IAgent, so "forever" would be exactly that.
constexpr uint64_t MAX_TIMEOUT_MS = 24ULL * 60 * 60 * 1000;

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
                {"description", "Timeout in milliseconds (0 = poll current status, omit = wait as long as allowed). Required for oneShot=false agents."}
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
        } else if (!hasTimeout && target->IsInteractive()) {
            // An interactive agent goes back to waiting for commands once it is
            // done, so it never finishes by itself and nothing here can tell it
            // to. Say so rather than sitting on the caller's thread until the
            // cap runs out.
            LogWarning("AgentWaitToFinishTool: agent '%s' is interactive; timeout_ms is required", agentName.c_str());
            anyNotFound = true;
            if (!errorMessage.empty()) {
                errorMessage += ", ";
            }
            errorMessage += agentName + " is a oneShot=false agent and never finishes on its own; pass timeout_ms";
        } else {
            // WaitToFinish(0) does not wait; it just reports whether the agent
            // has finished.
            LogDebug("AgentWaitToFinishTool: waiting for agent '%s' (finished=%d)", agentName.c_str(), target->WaitToFinish(0) ? 1 : 0);
            uint64_t waitMs = hasTimeout ? timeout_ms : MAX_TIMEOUT_MS;
            bool finished = target->WaitToFinish(waitMs);
            LogDebug("AgentWaitToFinishTool: agent '%s' wait completed (finished=%d)", agentName.c_str(), finished ? 1 : 0);
        }
    }

    if (anyNotFound) {
        return ToolResult{errorMessage, true};
    }
    return ToolResult{"", false};
}

} // namespace Haisos::Tools
