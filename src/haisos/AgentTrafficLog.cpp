#include "AgentTrafficLog.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace Haisos {

namespace {

std::string CurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

std::string DisplayName(const std::string& agentName) {
    return agentName.empty() ? "(unnamed)" : agentName;
}

} // namespace

bool ParseAgentTrafficLogType(const std::string& name, AgentTrafficLogType& outType) {
    if (name == "diff") { outType = AgentTrafficLogType::Diff; return true; }
    if (name == "full") { outType = AgentTrafficLogType::Full; return true; }
    return false;
}

std::string PrettyPrintJson(const std::string& json) {
    auto parsed = nlohmann::ordered_json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        return json;
    }
    return parsed.dump(2);
}

std::string ComputeSmartDiff(const std::string& previousJson, const std::string& currentJson) {
    auto previous = nlohmann::ordered_json::parse(previousJson, nullptr, /*allow_exceptions=*/false);
    auto current = nlohmann::ordered_json::parse(currentJson, nullptr, /*allow_exceptions=*/false);
    if (!previous.is_object() || !current.is_object()) {
        return PrettyPrintJson(currentJson);
    }

    nlohmann::ordered_json result = nlohmann::ordered_json::object();

    // Scalars first, "model" and "stream" leading, then the arrays: the
    // unchanging settings read at a glance, the conversation after them.
    auto emitScalar = [&](const std::string& key) {
        auto it = current.find(key);
        if (it != current.end() && !it->is_array()) {
            result[key] = *it;
        }
    };
    emitScalar("model");
    emitScalar("stream");
    for (const auto& [key, value] : current.items()) {
        if (key != "model" && key != "stream" && !value.is_array()) {
            result[key] = value;
        }
    }

    for (const auto& [key, currentArray] : current.items()) {
        if (!currentArray.is_array()) {
            continue;
        }
        auto previousIt = previous.find(key);
        if (previousIt == previous.end() || !previousIt->is_array()) {
            result[key] = currentArray;
            continue;
        }
        const auto& previousArray = *previousIt;
        if (previousArray == currentArray) {
            result[key] = "-- same --";
            continue;
        }
        bool grewOnly = previousArray.size() <= currentArray.size();
        for (size_t i = 0; grewOnly && i < previousArray.size(); ++i) {
            grewOnly = previousArray[i] == currentArray[i];
        }
        if (!grewOnly) {
            result[key] = currentArray;
            continue;
        }
        nlohmann::ordered_json added = nlohmann::ordered_json::array();
        added.push_back("-- precedent array --");
        for (size_t i = previousArray.size(); i < currentArray.size(); ++i) {
            added.push_back(currentArray[i]);
        }
        result[key] = std::move(added);
    }

    return result.dump(2);
}

AgentTrafficLog::AgentTrafficLog(std::unique_ptr<std::ostream> out, AgentTrafficLogType type)
    : m_out(std::move(out))
    , m_type(type)
{
}

void AgentTrafficLog::WriteHeader(const char* direction, const std::string& agentName) {
    *m_out << direction << " [" << DisplayName(agentName) << "] " << CurrentTimestamp() << "\n";
}

void AgentTrafficLog::OnSend(const std::string& agentName, const std::string& json) {
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteHeader(">>>>>>>> SEND   ", agentName);
    if (m_type == AgentTrafficLogType::Diff) {
        auto it = m_lastSent.find(agentName);
        if (it != m_lastSent.end()) {
            *m_out << "(diff against the previous send of [" << DisplayName(agentName) << "])\n"
                   << ComputeSmartDiff(it->second, json) << "\n\n";
            it->second = json;
            m_out->flush();
            return;
        }
        m_lastSent.emplace(agentName, json);
    }
    *m_out << PrettyPrintJson(json) << "\n\n";
    m_out->flush();
}

void AgentTrafficLog::OnReceive(const std::string& agentName, const std::string& json) {
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteHeader("<<<<<<<< RECEIVE", agentName);
    *m_out << PrettyPrintJson(json) << "\n\n";
    m_out->flush();
}

} // namespace Haisos
