#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <random>
#include <nlohmann/json.hpp>
#include "interfaces/IAgent.h"
#include "interfaces/IFactory.h"

namespace Haisos::Tools {

inline std::string GenerateAgentName() {
    const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(sizeof(chars) - 2));

    std::string name;
    for (int i = 0; i < 8; ++i) {
        name += chars[dist(gen)];
    }
    return name;
}

inline std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

inline std::string GetCurrentTimestampISO8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &time);
#else
    gmtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

inline std::shared_ptr<IAgent> FindChildByName(std::shared_ptr<IAgent> parent, const std::string& name) {
    if (!parent) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        if (child->Name() == name) {
            return child;
        }
    }
    return nullptr;
}

inline std::shared_ptr<IAgent> CreateAndStartSubagent(
    IFactory& factory,
    std::shared_ptr<IAgent> parent,
    const std::string& userPrompt,
    const std::vector<std::string>& systemPrompts)
{
    std::string name = GenerateAgentName();
    std::string startTime = GetCurrentTimestamp();

    auto virtualConsole = factory.CreateVirtualConsole();
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
        "",
        parent,
        std::move(virtualConsole),
        startTime);

    agent->Post(userPrompt);
    return agent;
}

inline nlohmann::json BuildStartResult(std::shared_ptr<IAgent> agent, bool finished, bool returnConsole, bool returnMessages) {
    nlohmann::json result;
    result["name"] = agent->Name();
    result["start_time"] = agent->GetStartTime();
    result["killed"] = agent->IsKilled();
    result["finished"] = finished;

    if (finished) {
        if (returnConsole && agent->GetVirtualConsole()) {
            result["console_result"] = agent->GetVirtualConsole()->GetContents();
        }
        if (returnMessages) {
            result["messages_result"] = agent->GetHistory();
        }
    }

    return result;
}

inline nlohmann::json BuildWaitResult(std::shared_ptr<IAgent> agent, bool returnConsole, bool returnMessages) {
    nlohmann::json result;
    result["name"] = agent->Name();
    result["killed"] = agent->IsKilled();
    result["finished"] = agent->IsFinished();
    if (agent->IsFinished()) {
        result["end_time"] = GetCurrentTimestampISO8601();
        if (returnConsole && agent->GetVirtualConsole()) {
            result["console_result"] = agent->GetVirtualConsole()->GetContents();
        }
        if (returnMessages) {
            result["messages_result"] = agent->GetHistory();
        }
    }
    return result;
}

inline nlohmann::json BuildQueryResult(std::shared_ptr<IAgent> agent, bool returnConsole, bool returnMessages) {
    nlohmann::json result;
    result["name"] = agent->Name();
    result["killed"] = agent->IsKilled();
    result["finished"] = agent->IsFinished();
    if (returnConsole) {
        auto vc = agent->GetVirtualConsole();
        result["console_result"] = vc ? vc->GetContents() : "";
    }
    if (returnMessages) {
        result["messages_result"] = agent->GetHistory();
    }
    return result;
}

inline nlohmann::json BuildStopResult(std::shared_ptr<IAgent> agent) {
    nlohmann::json result;
    result["name"] = agent->Name();
    result["killed"] = agent->IsKilled();
    result["finished"] = agent->IsFinished();
    return result;
}

inline nlohmann::json BuildListRunningEntry(std::shared_ptr<IAgent> agent) {
    nlohmann::json entry;
    entry["name"] = agent->Name();
    entry["start_time"] = agent->GetStartTime();
    return entry;
}

} // namespace Haisos::Tools
