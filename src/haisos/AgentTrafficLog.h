#pragma once
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>

namespace Haisos {

// How --log-agent-to-file writes each agent's LLM traffic.
enum class AgentTrafficLogType {
    // Each request is written as its difference from the previous request of
    // the same agent (the first one in full); responses are written in full,
    // since each is new rather than a growing version of the last.
    Diff,
    // Every request and response is written in full.
    Full,
};

// Parses "diff" or "full". Returns false, leaving outType untouched, for
// anything else.
bool ParseAgentTrafficLogType(const std::string& name, AgentTrafficLogType& outType);

// Pretty-prints a JSON document, preserving its key order. Anything that is
// not JSON is returned as it is.
std::string PrettyPrintJson(const std::string& json);

// The difference between two request bodies, as JSON: scalar fields are shown
// as they are now; an array equal to before is shown as "-- same --"; an array
// that only grew is shown as "-- precedent array --" followed by the new
// entries (which is how an agent's "messages" grows round by round); any other
// array is shown in full. Falls back to the pretty-printed current body if
// either is not a JSON object.
std::string ComputeSmartDiff(const std::string& previousJson, const std::string& currentJson);

// Writes agent traffic to a stream it owns, one entry per request or
// response, each headed by its direction, time and agent. Thread-safe: agents
// report from their own threads. Register OnSend/OnReceive as the Logger's
// agent callbacks (RegisterLogAgentSendCallback/RegisterLogAgentReceiveCallback).
class AgentTrafficLog {
public:
    AgentTrafficLog(std::unique_ptr<std::ostream> out, AgentTrafficLogType type);

    AgentTrafficLog(const AgentTrafficLog&) = delete;
    AgentTrafficLog& operator=(const AgentTrafficLog&) = delete;

    void OnSend(const std::string& agentName, const std::string& json);
    void OnReceive(const std::string& agentName, const std::string& json);

private:
    void WriteHeader(const char* direction, const std::string& agentName);

    std::mutex m_mutex;
    std::unique_ptr<std::ostream> m_out;
    AgentTrafficLogType m_type;
    // Diff mode: the last request each agent sent, to diff the next one against.
    std::unordered_map<std::string, std::string> m_lastSent;
};

} // namespace Haisos
