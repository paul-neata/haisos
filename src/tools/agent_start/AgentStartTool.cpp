#include "AgentStartTool.h"
#include "src/components/libheaders/SanitizeUserInput.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentStartTool::ToolName = "agent_start";
const std::string AgentStartTool::ToolDefaultDescription = "Start a new subagent with a user prompt and return immediately. On success, returns only the agent name string. For an agent that normally does a job it was delegated and then finishes, pass long_running=false (this is the common choice and the default). If you need the subagent to keep running and wait for more commands, pass long_running=true. This tool ONLY starts the agent and does NOT wait for it to finish. If you need to wait for results, use agent_wait_to_finish afterward.";

AgentStartTool::AgentStartTool(IFactory& factory)
    : m_factory(factory) {}

std::shared_ptr<IAgent> CreateAndStartSubagent(
    IFactory& factory,
    std::shared_ptr<IAgent> parent,
    const std::string& userPrompt,
    const std::vector<std::string>& systemPrompts,
    bool longRunning)
{
    std::string name = GenerateAgentName();
    std::string startTime = GetCurrentTimestamp();

    LogDebug("CreateAndStartSubagent: creating subagent '%s' with prompt '%s' longRunning=%d", name.c_str(), userPrompt.c_str(), longRunning ? 1 : 0);

    auto httpClient = factory.CreateHTTPClient();
    auto toolFactory = factory.CreateToolFactory(factory);
    auto console = factory.CreateConsole(false);
    std::string endpoint = std::getenv("HAISOS_ENDPOINT") ? std::getenv("HAISOS_ENDPOINT") : "http://localhost:11434/api/chat";
    std::string model = std::getenv("HAISOS_MODEL") ? std::getenv("HAISOS_MODEL") : "llama3";
    std::string apiKey = std::getenv("HAISOS_API_KEY") ? std::getenv("HAISOS_API_KEY") : "";

    auto llmCommunicator = factory.CreateLLMCommunicator(
        std::move(httpClient),
        endpoint,
        model,
        apiKey);

    auto agent = factory.CreateAgent(
        std::move(llmCommunicator),
        std::move(toolFactory),
        std::move(console),
        systemPrompts,
        name,
        parent,
        startTime,
        longRunning);

    agent->Post(userPrompt);
    LogDebug("CreateAndStartSubagent: subagent '%s' posted prompt", name.c_str());
    return agent;
}

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
            {"long_running", {
                {"type", "boolean"},
                {"description", "If true, the subagent keeps running and waits for more commands. If false (default), the subagent finishes after processing its initial task."}
            }}
        }},
        {"required", nlohmann::json::array({"user_prompt"})}
    };
}

ToolResult AgentStartTool::Call(std::shared_ptr<IAgent> callerAgent, const nlohmann::json& args) {
    if (!args.contains("user_prompt") || !args["user_prompt"].is_string()) {
        return ToolResult{"Missing required field: user_prompt", true};
    }

    std::string userPrompt = SanitizeUserInput(args["user_prompt"]);
    std::vector<std::string> systemPrompts;
    if (args.contains("system_prompt") && args["system_prompt"].is_string()) {
        systemPrompts.push_back(SanitizeUserInput(args["system_prompt"]));
    }

    bool longRunning = false;
    if (args.contains("long_running") && args["long_running"].is_boolean()) {
        longRunning = args["long_running"].get<bool>();
    }

    constexpr int MAX_SUBAGENT_DEPTH = 5;
    if (callerAgent && callerAgent->GetDepth() >= MAX_SUBAGENT_DEPTH) {
        return ToolResult{"Subagent recursion depth limit exceeded", true};
    }

    auto agent = CreateAndStartSubagent(m_factory, callerAgent, userPrompt, systemPrompts, longRunning);

    if (longRunning) {
        LogDebug("AgentStartTool: subagent '%s' started as long-running", agent->Name().c_str());
    } else {
        LogDebug("AgentStartTool: subagent '%s' started as short-running", agent->Name().c_str());
    }

    return ToolResult{agent->Name(), false};
}

} // namespace Haisos::Tools
