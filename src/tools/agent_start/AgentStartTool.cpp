#include "AgentStartTool.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentStartTool::ToolName = "agent_start";
const std::string AgentStartTool::ToolDefaultDescription = "Start a new subagent with a user prompt. Optionally wait for it to finish and return results.";

AgentStartTool::AgentStartTool(IFactory& factory)
    : m_factory(factory) {}

nlohmann::json AgentStartTool::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"user_prompt", {
                {"type", "string"},
                {"description", "The user prompt to send to the new subagent"}
            }},
            {"system_prompt", {
                {"type", "string"},
                {"description", "Optional system prompt for the subagent"}
            }},
            {"wait_to_finish", {
                {"type", "boolean"},
                {"description", "If true, block until the subagent finishes before returning"}
            }},
            {"wait_to_finish_timeout_ms", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds when wait_to_finish is true (0 = poll current status, omit = block forever)"}
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
        {"required", nlohmann::json::array({"user_prompt"})}
    };
}

std::string AgentStartTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("user_prompt") || !args["user_prompt"].is_string()) {
        return nlohmann::json({{"error", "Missing required field: user_prompt"}}).dump();
    }

    std::string userPrompt = args["user_prompt"];
    std::vector<std::string> systemPrompts;
    if (args.contains("system_prompt") && args["system_prompt"].is_string()) {
        systemPrompts.push_back(args["system_prompt"]);
    }

    bool waitToFinish = false;
    if (args.contains("wait_to_finish") && args["wait_to_finish"].is_boolean()) {
        waitToFinish = args["wait_to_finish"];
    }

    bool hasTimeout = false;
    uint64_t timeout_ms = 0;
    if (args.contains("wait_to_finish_timeout_ms") && !args["wait_to_finish_timeout_ms"].is_null()) {
        hasTimeout = true;
        try {
            timeout_ms = args["wait_to_finish_timeout_ms"].get<uint64_t>();
        } catch (const nlohmann::json::exception&) {
            return nlohmann::json({{"error", "Invalid wait_to_finish_timeout_ms: must be a non-negative integer"}}).dump();
        }
        constexpr uint64_t MAX_TIMEOUT_MS = 24ULL * 60 * 60 * 1000;
        if (timeout_ms > MAX_TIMEOUT_MS) {
            return nlohmann::json({{"error", "wait_to_finish_timeout_ms exceeds maximum allowed value of 86400000 ms (24 hours)"}}).dump();
        }
    }

    bool returnConsole = false;
    if (args.contains("return_console") && args["return_console"].is_boolean()) {
        returnConsole = args["return_console"];
    }

    bool returnMessages = false;
    if (args.contains("return_messages") && args["return_messages"].is_boolean()) {
        returnMessages = args["return_messages"];
    }

    constexpr int MAX_SUBAGENT_DEPTH = 5;
    if (callerAgent && callerAgent->GetDepth() >= MAX_SUBAGENT_DEPTH) {
        return nlohmann::json({{"error", "Subagent recursion depth limit exceeded"}}).dump();
    }

    auto agent = CreateAndStartSubagent(m_factory, callerAgent, userPrompt, systemPrompts);

    if (waitToFinish) {
        if (!hasTimeout) {
            agent->WaitToFinish();
        } else if (timeout_ms > 0) {
            agent->WaitToFinish(timeout_ms);
        }
    }

    return BuildStartResult(agent, waitToFinish, returnConsole, returnMessages).dump();
}

} // namespace Haisos::Tools
