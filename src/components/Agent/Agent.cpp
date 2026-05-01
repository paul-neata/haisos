#include "Agent.h"
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include "src/components/Logger/Logger.h"
#include "src/components/libheaders/SanitizeUserInput.h"

namespace Haisos {

constexpr size_t MAX_HISTORY_SIZE = 100;
constexpr size_t MAX_MESSAGE_CONTENT_SIZE = 100 * 1024;

static void TrimHistory(std::vector<LLMMessage>& history) {
    if (history.size() <= MAX_HISTORY_SIZE) {
        return;
    }

    // Count system messages at the beginning
    size_t systemCount = 0;
    for (const auto& msg : history) {
        if (msg.role == "system") {
            ++systemCount;
        } else {
            break;
        }
    }

    size_t excess = history.size() - MAX_HISTORY_SIZE;
    size_t removable = history.size() - systemCount;
    size_t toRemove = std::min(excess, removable);

    // Never trim in the middle of a tool-call round. If the erase boundary
    // falls inside a block of tool result messages, extend removal to include
    // the entire block so no orphaned tool messages remain.
    while (systemCount + toRemove < history.size() && history[systemCount + toRemove].role == "tool") {
        ++toRemove;
    }

    history.erase(history.begin() + static_cast<std::ptrdiff_t>(systemCount),
                  history.begin() + static_cast<std::ptrdiff_t>(systemCount + toRemove));
}

static void TruncateIfNeeded(std::string& content) {
    constexpr size_t truncatedNoticeSize = 11; // strlen("[truncated]")
    if (content.size() > MAX_MESSAGE_CONTENT_SIZE) {
        content.resize(MAX_MESSAGE_CONTENT_SIZE - truncatedNoticeSize);
        content += "[truncated]";
    }
}

Agent::Agent(
    std::shared_ptr<ILLMCommunicator> llmCommunicator,
    std::shared_ptr<IToolFactory> toolFactory,
    std::shared_ptr<IConsole> console,
    const std::vector<std::string>& systemPrompts,
    const std::string& name,
    std::shared_ptr<IAgent> parent,
    const std::string& startTime,
    const SystemCallbacks& callbacks,
    bool longRunning)
    : m_llmCommunicator(std::move(llmCommunicator))
    , m_toolFactory(std::move(toolFactory))
    , m_console(std::move(console))
    , m_systemPrompts(systemPrompts)
    , m_name(name)
    , m_startTime(startTime)
    , m_parent(std::move(parent))
    , m_callbacks(callbacks)
    , m_longRunning(longRunning)
{
    m_thread = std::thread(&Agent::RunThread, this);
}

Agent::~Agent() {
    Stop(0);
    if (!WaitToFinish(5000)) {
        LogWarning("Agent '%s' thread did not finish within 5s during destruction, waiting indefinitely", m_name.c_str());
    }
    WaitToFinish();
}

void Agent::Post(const std::string& command) {
    m_commandQueue.Post(command);
}

void Agent::Send(const std::string& command) {
    m_commandQueue.Send(command);
}

bool Agent::Stop(unsigned timeoutMs) {
    m_commandQueue.Close();
    if (timeoutMs > 0) {
        return WaitToFinish(timeoutMs);
    }
    return false;
}

void Agent::Kill() {
    m_killed = true;
    Stop(0);
}

std::shared_ptr<IAgent> Agent::GetParent() const {
    return m_parent;
}

std::string Agent::Name() const {
    return m_name;
}

void Agent::WaitToFinish() {
    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool Agent::WaitToFinish(uint64_t timeoutMs) {
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

std::vector<std::shared_ptr<IAgent>> Agent::GetChildren() const {
    std::lock_guard<std::mutex> lock(m_childrenMutex);
    std::vector<std::shared_ptr<IAgent>> result;
    for (const auto& wp : m_children) {
        if (auto sp = wp.lock()) {
            result.push_back(sp);
        }
    }
    return result;
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

bool Agent::IsKilled() const {
    return m_killed.load();
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

bool Agent::IsLongRunning() const {
    return m_longRunning;
}

std::vector<std::tuple<std::string, std::string, std::string, bool>> Agent::ExecuteToolCalls(const LLMMessage& message) {
    std::vector<std::tuple<std::string, std::string, std::string, bool>> toolResults;
    if (!m_toolFactory) {
        return toolResults;
    }

    toolResults.reserve(message.toolCallsJson.size());

    for (const auto& tc : message.toolCallsJson) {
        std::string toolName;
        std::string toolCallId = tc.value("id", "");
        nlohmann::json args = nlohmann::json::object();

        if (tc.contains("function") && tc["function"].is_object()) {
            const auto& func = tc["function"];
            toolName = func.value("name", "");
            if (func.contains("arguments")) {
                const auto& argField = func["arguments"];
                if (argField.is_string()) {
                    try {
                        args = nlohmann::json::parse(argField.get<std::string>());
                    } catch (const std::exception& e) {
                        LogWarning("Agent '%s' - Failed to parse tool arguments JSON for '%s': %s", m_name.c_str(), toolName.c_str(), e.what());
                        args = nlohmann::json::object();
                    } catch (...) {
                        LogWarning("Agent '%s' - Failed to parse tool arguments JSON for '%s': unknown exception", m_name.c_str(), toolName.c_str());
                        args = nlohmann::json::object();
                    }
                } else {
                    args = argField;
                }
            }
        } else {
            toolName = tc.value("name", "");
            if (tc.contains("arguments")) {
                const auto& argField = tc["arguments"];
                if (argField.is_string()) {
                    try {
                        args = nlohmann::json::parse(argField.get<std::string>());
                    } catch (const std::exception& e) {
                        LogWarning("Agent '%s' - Failed to parse tool arguments JSON for '%s': %s", m_name.c_str(), toolName.c_str(), e.what());
                        args = nlohmann::json::object();
                    } catch (...) {
                        LogWarning("Agent '%s' - Failed to parse tool arguments JSON for '%s': unknown exception", m_name.c_str(), toolName.c_str());
                        args = nlohmann::json::object();
                    }
                } else {
                    args = argField;
                }
            }
        }

        if (toolName.empty()) {
            toolResults.emplace_back("", "Error: Tool name is empty", toolCallId, true);
            continue;
        }

        LogInfo("Agent '%s' - Tool call received: %s", m_name.c_str(), toolName.c_str());
        auto tool = m_toolFactory->CreateTool(toolName, shared_from_this());
        if (tool) {
            ToolResult result = tool->Call(shared_from_this(), args);
            toolResults.emplace_back(toolName, result.content, toolCallId, result.isError);
            LogTrace("Agent '%s' - Tool result: %s", m_name.c_str(), result.content.c_str());
        } else {
            LogError("Agent '%s' - Unknown tool: %s", m_name.c_str(), toolName.c_str());
            if (m_console) {
                m_console->Write(*this, "Error: Unknown tool - " + toolName);
            }
            m_messageBuffer.Append("[" + m_name + "] Error: Unknown tool - " + toolName + "\n");
            toolResults.emplace_back(toolName, "Error: Unknown tool - " + toolName, toolCallId, true);
        }
    }

    return toolResults;
}

void Agent::RunThread() {
    SystemCallbacks llmCallbacks;
    if (m_callbacks.on_send_with_name) {
        llmCallbacks.on_send = [this](const std::string& json) {
            LogDebug("Agent '%s' sending JSON (%zu bytes)", m_name.c_str(), json.size());
            m_callbacks.on_send_with_name(m_name, json);
        };
    } else if (m_callbacks.on_send) {
        llmCallbacks.on_send = [this](const std::string& json) {
            LogDebug("Agent '%s' sending JSON (%zu bytes)", m_name.c_str(), json.size());
            m_callbacks.on_send(json);
        };
    }
    if (m_callbacks.on_received_with_name) {
        llmCallbacks.on_received = [this](const std::string& json) {
            LogDebug("Agent '%s' received JSON (%zu bytes)", m_name.c_str(), json.size());
            m_callbacks.on_received_with_name(m_name, json);
        };
    } else if (m_callbacks.on_received) {
        llmCallbacks.on_received = [this](const std::string& json) {
            LogDebug("Agent '%s' received JSON (%zu bytes)", m_name.c_str(), json.size());
            m_callbacks.on_received(json);
        };
    }

    for (const auto& prompt : m_systemPrompts) {
        LLMMessage systemMsg;
        systemMsg.role = "system";
        systemMsg.content = prompt;
        {
            std::lock_guard<std::mutex> lock(m_historyMutex);
            m_history.push_back(systemMsg);
            TrimHistory(m_history);
        }
    }

    while (true) {
        try {
            std::string command;
            if (!m_commandQueue.Pop(command)) {
                LogVerboseDebug("Agent '%s' command queue closed, exiting outer loop", m_name.c_str());
                break;
            }

            if (command.empty()) {
                LogVerboseDebug("Agent '%s' received empty command, skipping", m_name.c_str());
                continue;
            }

            LogDebug("Agent '%s' processing command: %s", m_name.c_str(), command.c_str());

            std::string sanitizedCommand = SanitizeUserInput(command);
            LLMMessage userMsg;
            userMsg.role = "user";
            userMsg.content = "\n--- BEGIN USER INPUT ---\n" + sanitizedCommand + "\n--- END USER INPUT ---\n";
            {
                std::lock_guard<std::mutex> lock(m_historyMutex);
                m_history.push_back(userMsg);
                TrimHistory(m_history);
            }

            constexpr int MAX_LLM_ROUNDS = 20;
            int rounds = 0;
            while (true) {
                if (++rounds > MAX_LLM_ROUNDS) {
                    LogWarning("Agent '%s' exceeded maximum LLM rounds (%d), breaking conversation loop", m_name.c_str(), MAX_LLM_ROUNDS);
                    break;
                }

                LogVerboseDebug("Agent '%s' LLM round %d starting", m_name.c_str(), rounds);

                std::vector<std::tuple<std::string, std::string, nlohmann::json>> tools;
                if (m_toolFactory) {
                    tools = m_toolFactory->GetAvailableToolDescriptions();
                }

                std::vector<LLMMessage> localHistory;
                {
                    std::lock_guard<std::mutex> lock(m_historyMutex);
                    localHistory = m_history;
                }

                LLMResponse response = m_llmCommunicator->Call(localHistory, tools, llmCallbacks);
                TruncateIfNeeded(response.message.content);

                if (!response.message.content.empty()) {
                    if (m_console) {
                        m_console->Write(*this, response.message.content);
                    }
                    m_messageBuffer.Append("[" + m_name + "] " + response.message.content + "\n");
                }

                // ALWAYS push assistant response to history
                {
                    std::lock_guard<std::mutex> lock(m_historyMutex);
                    m_history.push_back(response.message);
                    TrimHistory(m_history);
                }

                if (!response.message.toolCallsJson.empty()) {
                    LogDebug("Agent '%s' received %zu tool call(s)", m_name.c_str(), response.message.toolCallsJson.size());
                    auto toolResults = ExecuteToolCalls(response.message);
                    {
                        std::lock_guard<std::mutex> lock(m_historyMutex);
                        for (const auto& tr : toolResults) {
                            LLMMessage toolMsg;
                            toolMsg.role = "tool";
                            toolMsg.content = std::get<1>(tr);
                            toolMsg.is_error = std::get<3>(tr);
                            if (!toolMsg.is_error) {
                                TruncateIfNeeded(toolMsg.content);
                            }
                            toolMsg.name = std::get<0>(tr);
                            toolMsg.tool_call_id = std::get<2>(tr);
                            m_history.push_back(toolMsg);
                        }
                        TrimHistory(m_history);
                    }
                    continue;
                }

                // No tool calls present: the conversation round is complete.
                LogVerboseDebug("Agent '%s' no tool calls, breaking conversation loop", m_name.c_str());
                break;
            }

            if (!m_longRunning) {
                LogVerboseDebug("Agent '%s' short-running: finished processing command, exiting outer loop", m_name.c_str());
                break;
            }
        } catch (const std::exception& e) {
            LogError("Agent '%s' - Exception in RunThread: %s", m_name.c_str(), e.what());
            break;
        } catch (...) {
            LogError("Agent '%s' - Unknown exception in RunThread", m_name.c_str());
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

}
