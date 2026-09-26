#include "Agent.h"
#include <chrono>
#include <iterator>
#include <optional>
#include <nlohmann/json.hpp>
#include "src/components/Logger/Logger.h"
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"

namespace Haisos {

// How long each pass of the destructor's wait gives the agent's thread before
// reporting that it is still running. Only the reporting interval -- the wait
// itself never gives up.
constexpr uint64_t DESTRUCTION_WAIT_INTERVAL_MS = 5000;

namespace {

// A string field of a JSON object an LLM sent -- a tool call, or its
// "function" -- or "" when the field is missing or not a string, or the value
// is not an object at all. Never throws: what an LLM sends is untrusted, and
// json::value() throws when the field is there with another type.
std::string StringField(const nlohmann::json& object, const char* key) {
    if (!object.is_object()) {
        return std::string();
    }
    const auto it = object.find(key);
    return (it != object.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

// The name of the tool a tool call asks for: under "function" in the shape
// Ollama sends, at the top in the flat one.
std::string ToolCallName(const nlohmann::json& toolCall) {
    if (toolCall.is_object()) {
        const auto function = toolCall.find("function");
        if (function != toolCall.end() && function->is_object()) {
            return StringField(*function, "name");
        }
    }
    return StringField(toolCall, "name");
}

// A tool's result as it goes into the history.
LLMMessage ToolResultMessage(const std::string& toolName, const std::string& content,
                             const std::string& toolCallId, bool isError) {
    LLMMessage toolMsg;
    toolMsg.role = "tool";
    // Tool results are untrusted content, but they are structured data (file
    // contents, JSON) that must reach the LLM verbatim. Instead of filtering
    // them, delimit them the same way user input is delimited so the model
    // can tell data from instructions.
    toolMsg.is_error = isError;
    toolMsg.content = "\n--- BEGIN TOOL RESULT ---\n" + content + "\n--- END TOOL RESULT ---\n";
    toolMsg.name = toolName;
    toolMsg.tool_call_id = toolCallId;
    return toolMsg;
}

} // namespace

std::shared_ptr<Agent> Agent::Create(
    std::shared_ptr<ILLMCommunicator> llmCommunicator,
    std::shared_ptr<IToolFactory> toolFactory,
    std::shared_ptr<IAgentConsole> console,
    const std::vector<std::string>& systemPrompts,
    const std::string& name,
    std::shared_ptr<IAgent> parent,
    const std::string& startTime,
    bool interactive)
{
    // The agent's own thread can hold the last reference to it -- the one it
    // hands each tool it calls -- and the destructor waits for that thread.
    auto agent = std::shared_ptr<Agent>(
        new Agent(
            std::move(llmCommunicator),
            std::move(toolFactory),
            std::move(console),
            systemPrompts,
            name,
            std::move(parent),
            startTime,
            interactive),
        DestroyOffRuntimeThreads<Agent>("Agent '" + name + "'"));
    // Only now that the owning shared_ptr exists may the thread run: it hands
    // shared_from_this() to every tool it calls.
    agent->Start();
    return agent;
}

Agent::Agent(
    std::shared_ptr<ILLMCommunicator> llmCommunicator,
    std::shared_ptr<IToolFactory> toolFactory,
    std::shared_ptr<IAgentConsole> console,
    const std::vector<std::string>& systemPrompts,
    const std::string& name,
    std::shared_ptr<IAgent> parent,
    const std::string& startTime,
    bool interactive)
    : m_llmCommunicator(std::move(llmCommunicator))
    , m_toolFactory(std::move(toolFactory))
    , m_console(std::move(console))
    , m_systemPrompts(systemPrompts)
    , m_name(name)
    , m_startTime(startTime)
    , m_parent(std::move(parent))
    , m_interactive(interactive)
{
    if (m_toolFactory) {
        m_cachedToolDescriptions = m_toolFactory->GetAvailableToolDescriptions();
    }
}

void Agent::Start() {
    // Under the join lock, as every use of m_thread is: IsOwnThread() may be
    // asked from the agent's thread itself.
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    m_thread = std::thread(&Agent::RunThread, this);
}

bool Agent::IsOwnThread() {
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    return m_thread.get_id() == std::this_thread::get_id();
}

Agent::~Agent() {
    LogDebug("Agent '%s': destroying", m_name.c_str());
    TriggerStop();
    // The wait is deliberately unbounded: the thread runs on members this
    // destructor is about to free, so giving up on it is never an option.
    // Every pass says whether the agent has stopped, so one that is wedged
    // shows up in the log as it happens instead of looking like a silent hang.
    while (!WaitToFinish(DESTRUCTION_WAIT_INTERVAL_MS)) {
        LogWarning("Agent '%s' has not stopped after waiting %llums in its destructor, still waiting",
            m_name.c_str(), static_cast<unsigned long long>(DESTRUCTION_WAIT_INTERVAL_MS));
    }
    LogDebug("Agent '%s' stopped, destruction continuing", m_name.c_str());
}

void Agent::Post(const std::string& command) {
    m_commandQueue.Post(command);
}

void Agent::Send(const std::string& command) {
    m_commandQueue.Send(command);
}

void Agent::TriggerStop() {
    // Only a request, and two halves of one: closing the queue stops new
    // commands being taken, while the flag is what a round already in flight
    // notices -- see ExecuteToolCalls. Without the flag a stop would not be
    // seen until the current command was finished with, which for a command
    // that keeps calling tools can be a long way off.
    m_stopRequested = true;
    m_commandQueue.Close();
}

std::shared_ptr<IAgent> Agent::GetParent() const {
    return m_parent;
}

std::string Agent::Name() const {
    return m_name;
}

bool Agent::WaitToFinish(uint64_t timeoutMs) {
    // A wait for the agent, made on the agent's own thread, can only time out:
    // that thread is the one that would have to finish. ~Agent run there once
    // did exactly this, forever; reported loudly in case anything does again.
    if (timeoutMs > 0 && IsOwnThread()) {
        LogError("Agent '%s': waiting %llums for itself on its own thread, which cannot finish while it waits",
            m_name.c_str(), static_cast<unsigned long long>(timeoutMs));
    }
    std::unique_lock<std::mutex> lock(m_finishedMutex);
    bool finished = m_finishedCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_finished.load(); });
    lock.unlock();
    if (finished) {
        std::lock_guard<std::mutex> joinLock(m_joinMutex);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    return finished;
}

void Agent::AddChild(std::shared_ptr<IAgent> child) {
    std::lock_guard<std::mutex> lock(m_childrenMutex);
    m_children.push_back(child);
}

std::vector<std::shared_ptr<IAgent>> Agent::GetChildren(bool onlyDirectChildren) const {
    std::vector<std::shared_ptr<IAgent>> directChildren;
    {
        std::lock_guard<std::mutex> lock(m_childrenMutex);
        for (const auto& wp : m_children) {
            if (auto sp = wp.lock()) {
                directChildren.push_back(std::move(sp));
            }
        }
    }
    if (onlyDirectChildren) {
        return directChildren;
    }

    // Depth-first: each child, then everything below it. m_childrenMutex is
    // already released, so a child's own lock is never taken while holding this
    // agent's. The agent hierarchy is a tree, so the walk always terminates.
    std::vector<std::shared_ptr<IAgent>> subtree;
    for (const auto& child : directChildren) {
        subtree.push_back(child);
        auto descendants = child->GetChildren(/*onlyDirectChildren=*/false);
        subtree.insert(subtree.end(),
            std::make_move_iterator(descendants.begin()),
            std::make_move_iterator(descendants.end()));
    }
    return subtree;
}

nlohmann::json Agent::GetHistory() const {
    std::vector<LLMMessage> localHistory;
    {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        localHistory = m_history;
    }
    nlohmann::json historyArray = nlohmann::json::array();
    for (const auto& msg : localHistory) {
        nlohmann::json entry;
        entry["role"] = msg.role;
        entry["content"] = msg.content;
        entry["name"] = msg.name;
        entry["thinking"] = msg.thinking;
        entry["is_error"] = msg.is_error;
        if (!msg.toolCallsJson.empty()) {
            entry["toolCallsJson"] = msg.toolCallsJson;
        }
        historyArray.push_back(entry);
    }
    return historyArray;
}

std::string Agent::GetConsoleOutput() const {
    return m_messageBuffer.GetContents();
}

bool Agent::IsFinished() const {
    return m_finished.load();
}

std::string Agent::GetStartTime() const {
    return m_startTime;
}

int Agent::GetDepth() const {
    int depth = 0;
    auto p = m_parent;
    while (p) {
        ++depth;
        p = p->GetParent();
    }
    return depth;
}

bool Agent::IsInteractive() const {
    return m_interactive;
}

std::vector<std::tuple<std::string, std::string, std::string, bool>> Agent::ExecuteToolCalls(const LLMMessage& message) {
    std::vector<std::tuple<std::string, std::string, std::string, bool>> toolResults;
    if (!m_toolFactory) {
        return toolResults;
    }

    toolResults.reserve(message.toolCallsJson.size());

    for (const auto& tc : message.toolCallsJson) {
        // Everything here is read without throwing: a tool call is whatever the
        // LLM sent, and json::value() throws on a field of the wrong type --
        // which used to end the agent with the tool call left unanswered.
        const std::string toolCallId = StringField(tc, "id");
        const std::string toolName = ToolCallName(tc);
        nlohmann::json args = nlohmann::json::object();

        // Ollama nests the arguments under "function"; the flat shape has
        // them at the top, next to the name.
        const nlohmann::json* holder = &tc;
        if (tc.is_object()) {
            const auto function = tc.find("function");
            if (function != tc.end() && function->is_object()) {
                holder = &*function;
            }
        }
        if (holder->is_object()) {
            const auto argField = holder->find("arguments");
            if (argField != holder->end()) {
                if (argField->is_string()) {
                    try {
                        args = nlohmann::json::parse(argField->get<std::string>());
                    } catch (const std::exception& e) {
                        LogWarning("Agent '%s' - Failed to parse tool arguments JSON for '%s': %s", m_name.c_str(), toolName.c_str(), e.what());
                        args = nlohmann::json::object();
                    } catch (...) {
                        LogWarning("Agent '%s' - Failed to parse tool arguments JSON for '%s': unknown exception", m_name.c_str(), toolName.c_str());
                        args = nlohmann::json::object();
                    }
                } else {
                    args = *argField;
                }
            }
        }

        if (toolName.empty()) {
            toolResults.emplace_back("", "Error: the tool call names no tool (its name must be a non-empty string)", toolCallId, true);
            continue;
        }
        // No arguments at all is as good as none given; anything else that is
        // not an object has no named arguments for a tool to read.
        if (args.is_null()) {
            args = nlohmann::json::object();
        }
        if (!args.is_object()) {
            LogWarning("Agent '%s' - arguments for tool '%s' are not a JSON object", m_name.c_str(), toolName.c_str());
            const std::string type = args.type_name();
            const char* article = (type == "array" || type == "object") ? "an " : "a ";
            toolResults.emplace_back(toolName, "Error: the arguments of tool " + toolName + " must be a JSON object, not " +
                article + type, toolCallId, true);
            continue;
        }

        // Asked to stop: run no further tools. Every call still gets an answer,
        // because the conversation only stays well-formed with one result per
        // tool call.
        if (m_stopRequested) {
            LogDebug("Agent '%s' - stop requested, not running tool: %s", m_name.c_str(), toolName.c_str());
            toolResults.emplace_back(toolName, "Error: the agent was asked to stop", toolCallId, true);
            continue;
        }

        LogDebug("Agent '%s' - Tool call received: %s", m_name.c_str(), toolName.c_str());
        // A tool that throws fails its own call, not the agent: the exception
        // becomes the call's result, and the conversation goes on.
        std::optional<ToolResult> result;
        try {
            auto tool = m_toolFactory->CreateTool(toolName, shared_from_this());
            if (tool) {
                result = tool->Call(shared_from_this(), args);
            }
        } catch (const std::exception& e) {
            LogError("Agent '%s' - tool '%s' threw: %s", m_name.c_str(), toolName.c_str(), e.what());
            result = ToolResult{"Error: tool " + toolName + " failed: " + e.what(), true};
        } catch (...) {
            LogError("Agent '%s' - tool '%s' threw an unknown exception", m_name.c_str(), toolName.c_str());
            result = ToolResult{"Error: tool " + toolName + " failed: unknown exception", true};
        }
        if (result) {
            toolResults.emplace_back(toolName, result->content, toolCallId, result->isError);
            LogVerboseDebug("Agent '%s' - Tool result: %s", m_name.c_str(), result->content.c_str());
        } else {
            LogWarning("Agent '%s' - Unknown tool: %s", m_name.c_str(), toolName.c_str());
            if (m_console) {
                m_console->Write("Error: Unknown tool - " + toolName);
            }
            m_messageBuffer.Append("[" + m_name + "] Error: Unknown tool - " + toolName + "\n");
            toolResults.emplace_back(toolName, "Error: Unknown tool - " + toolName, toolCallId, true);
        }
    }

    return toolResults;
}

void Agent::RunThread() {
    // A runtime thread (see DestroyOffRuntimeThreads.h): the reference to this
    // agent that it hands each tool it calls can be the last one, and so can a
    // tool's references to the agent's process and OS. Whatever it lets go of
    // last is then destroyed on the destruction thread, not here. It also names
    // this thread in every log line.
    RuntimeThreadScope runtimeThread("agent " + m_name);
    for (const auto& prompt : m_systemPrompts) {
        LLMMessage systemMsg;
        systemMsg.role = "system";
        systemMsg.content = prompt;
        {
            std::lock_guard<std::mutex> lock(m_historyMutex);
            m_history.push_back(systemMsg);
        }
    }

    while (true) {
        std::string command;
        try {
            if (!m_commandQueue.Pop(command)) {
                LogVerboseDebug("Agent '%s' command queue closed, exiting outer loop", m_name.c_str());
                break;
            }
        } catch (const std::exception& e) {
            // A queue that fails can hand out nothing more, and there is no
            // command yet to report the failure against.
            LogError("Agent '%s' - Exception in RunThread while taking its next command: %s", m_name.c_str(), e.what());
            break;
        } catch (...) {
            LogError("Agent '%s' - Unknown exception in RunThread while taking its next command", m_name.c_str());
            break;
        }

        if (command.empty()) {
            LogVerboseDebug("Agent '%s' received empty command, skipping", m_name.c_str());
            continue;
        }

        // A command that fails takes only itself down, never the agent: an
        // exception out of it -- a tool, the LLM round trip, anything -- used
        // to end the agent's thread with nothing but a log line to show for
        // it, so the agent simply stopped answering.
        try {
            ProcessCommand(command);
        } catch (const std::exception& e) {
            OnCommandFailed(e.what());
        } catch (...) {
            OnCommandFailed("unknown exception");
        }

        if (!m_interactive) {
            LogVerboseDebug("Agent '%s' not interactive: finished processing command, exiting outer loop", m_name.c_str());
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_finishedMutex);
        m_finished = true;
    }
    m_finishedCv.notify_all();
    LogDebug("Agent '%s' RunThread finished", m_name.c_str());
}

void Agent::ProcessCommand(const std::string& command) {
    LogDebug("Agent '%s' processing command: %s", m_name.c_str(), command.c_str());

    // The command goes in whole, byte for byte. It is delimited, not filtered:
    // it is the agent's program, a line its operator typed, or a prompt from
    // the agent that started it -- none from an untrusted third party -- and
    // rewriting it (as a denylist of "injection" phrases once did) corrupts
    // code, markup and ordinary prose while stopping nobody. Content that is
    // untrusted, a tool's result, is delimited the same way.
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "\n--- BEGIN USER INPUT ---\n" + command + "\n--- END USER INPUT ---\n";
    {
        std::lock_guard<std::mutex> lock(m_historyMutex);
        m_history.push_back(userMsg);
    }

    constexpr int MAX_LLM_ROUNDS = 20;
    int rounds = 0;
    while (true) {
        if (++rounds > MAX_LLM_ROUNDS) {
            LogWarning("Agent '%s' exceeded maximum LLM rounds (%d), breaking conversation loop", m_name.c_str(), MAX_LLM_ROUNDS);
            break;
        }

        LogVerboseDebug("Agent '%s' LLM round %d starting", m_name.c_str(), rounds);

        std::vector<LLMMessage> localHistory;
        {
            std::lock_guard<std::mutex> lock(m_historyMutex);
            localHistory = m_history;
        }

        LLMResponse response = m_llmCommunicator->Call(localHistory, m_cachedToolDescriptions);

        if (!response.message.content.empty()) {
            if (m_console) {
                m_console->Write(response.message.content);
            }
            m_messageBuffer.Append("[" + m_name + "] " + response.message.content + "\n");
        }

        // ALWAYS push assistant response to history
        {
            std::lock_guard<std::mutex> lock(m_historyMutex);
            m_history.push_back(response.message);
        }

        if (!response.message.toolCallsJson.empty()) {
            LogDebug("Agent '%s' received %zu tool call(s)", m_name.c_str(), response.message.toolCallsJson.size());
            auto toolResults = ExecuteToolCalls(response.message);
            {
                std::lock_guard<std::mutex> lock(m_historyMutex);
                for (const auto& tr : toolResults) {
                    m_history.push_back(ToolResultMessage(std::get<0>(tr), std::get<1>(tr), std::get<2>(tr), std::get<3>(tr)));
                }
            }
            // Nothing was run, so there is nothing for another round to
            // build on: going back to the LLM would only spend calls
            // refusing tools until the round cap ran out.
            if (m_stopRequested) {
                LogDebug("Agent '%s' - stop requested, ending the conversation round", m_name.c_str());
                break;
            }
            continue;
        }

        // No tool calls present: the conversation round is complete.
        LogVerboseDebug("Agent '%s' no tool calls, breaking conversation loop", m_name.c_str());
        break;
    }
}

void Agent::OnCommandFailed(const std::string& what) {
    // Best effort: this runs because something has already failed, and
    // nothing here may throw out of the agent's thread.
    try {
        // "Exception in RunThread" is what this has always been logged as.
        LogError("Agent '%s' - Exception in RunThread while processing a command: %s", m_name.c_str(), what.c_str());
        try {
            AnswerUnansweredToolCalls("Error: the command failed before this tool call was answered: " + what);
        } catch (...) {
            LogError("Agent '%s' - could not answer the tool calls a failed command left open", m_name.c_str());
        }
        // The message buffer first: it is memory, where a console may be the
        // very thing that failed.
        const std::string line = "Error: the command failed: " + what;
        m_messageBuffer.Append("[" + m_name + "] " + line + "\n");
        if (m_console) {
            m_console->Write(line);
        }
        if (m_interactive) {
            LogInfo("Agent '%s' - carrying on with its next command", m_name.c_str());
        } else {
            LogInfo("Agent '%s' - not interactive, so it finishes with the failed command", m_name.c_str());
        }
    } catch (...) {
    }
}

void Agent::AnswerUnansweredToolCalls(const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    size_t assistantIndex = m_history.size();
    for (size_t i = m_history.size(); i-- > 0;) {
        if (m_history[i].role == "assistant") {
            assistantIndex = i;
            break;
        }
    }
    if (assistantIndex == m_history.size()) {
        return;
    }
    // Results follow the message asking for them, in the order of its tool
    // calls, so the ones already there answer its first tool calls.
    size_t answered = 0;
    for (size_t i = assistantIndex + 1; i < m_history.size(); ++i) {
        if (m_history[i].role == "tool") {
            ++answered;
        }
    }
    // Built apart first: appending to the history may move the message whose
    // tool calls are being read.
    std::vector<LLMMessage> answers;
    const auto& toolCalls = m_history[assistantIndex].toolCallsJson;
    for (size_t i = answered; i < toolCalls.size(); ++i) {
        answers.push_back(ToolResultMessage(ToolCallName(toolCalls[i]), reason, StringField(toolCalls[i], "id"), true));
    }
    if (!answers.empty()) {
        LogWarning("Agent '%s' - answering %zu tool call(s) a failed command left open", m_name.c_str(), answers.size());
    }
    m_history.insert(m_history.end(), answers.begin(), answers.end());
}

}
