#include "AgentStartTool.h"
#include "src/components/libheaders/SanitizeUserInput.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string AgentStartTool::ToolName = "agent_start";
const std::string AgentStartTool::ToolDefaultDescription = "Start a new subagent with a user prompt and return immediately. On success, returns only the agent name string as a plain unquoted string. The name is alphanumeric and may contain underscores and spaces. For an agent that normally does a job it was delegated and then finishes, pass oneShot=true (this is the common choice). oneShot=true agents finish by themselves; there is no need to stop them, monitor them, or call agent_list_running to check if they are running. If you need the response, just call agent_wait_to_finish with no timeout; it will return as soon as the agent is done. If you need the subagent to keep running and wait for more commands, pass oneShot=false. This tool ONLY starts the agent and does NOT wait for it to finish. Even if the agent finishes quickly, you can still call agent_query to check its status and collect output. If you need to wait for results, use agent_wait_to_finish afterward.";

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
            {"oneShot", {
                {"type", "boolean"},
                {"description", "If true, the subagent finishes after processing its initial task. If false, the subagent keeps running and waits for more commands."}
            }}
        }},
        {"required", nlohmann::json::array({"user_prompt", "oneShot"})}
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

    bool oneShot = false;
    if (args.contains("oneShot") && args["oneShot"].is_boolean()) {
        oneShot = args["oneShot"].get<bool>();
    }

    constexpr int MAX_SUBAGENT_DEPTH = 5;
    if (callerAgent && callerAgent->GetDepth() >= MAX_SUBAGENT_DEPTH) {
        return ToolResult{"Subagent recursion depth limit exceeded", true};
    }

    auto agent = CreateAndStartSubagent(m_factory, callerAgent, userPrompt, systemPrompts, !oneShot);

    if (oneShot) {
        LogDebug("AgentStartTool: subagent '%s' started as one-shot", agent->Name().c_str());
    } else {
        LogDebug("AgentStartTool: subagent '%s' started as long-running", agent->Name().c_str());
    }

    return ToolResult{agent->Name(), false};
}

} // namespace Haisos::Tools
