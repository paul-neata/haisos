#include "AgentWaitToFinishTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentWaitToFinishTool::ToolName = "agent_wait_to_finish";
const std::string AgentWaitToFinishTool::ToolDefaultDescription = "Wait for one or more named subagents to finish.";

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

std::string AgentWaitToFinishTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
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

    bool hasTimeout = false;
    uint64_t timeout_ms = 0;
    if (args.contains("timeout_ms") && !args["timeout_ms"].is_null()) {
        hasTimeout = true;
        try {
            timeout_ms = args["timeout_ms"].get<uint64_t>();
        } catch (const nlohmann::json::exception&) {
            return nlohmann::json({{"error", "Invalid timeout_ms: must be a non-negative integer"}}).dump();
        }
        constexpr uint64_t MAX_TIMEOUT_MS = 24ULL * 60 * 60 * 1000;
        if (timeout_ms > MAX_TIMEOUT_MS) {
            return nlohmann::json({{"error", "timeout_ms exceeds maximum allowed value of 86400000 ms (24 hours)"}}).dump();
        }
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
            if (!hasTimeout) {
                target->WaitToFinish();
            } else if (timeout_ms > 0) {
                target->WaitToFinish(timeout_ms);
            }
            result = BuildWaitResult(target, returnConsole, returnMessages);
        }
        results.push_back(result);
    }

    return results.dump();
}

} // namespace Haisos::Tools
