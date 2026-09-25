#pragma once
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "ReopeningLogFile.h"

namespace Haisos {

// How --log-agent-to-file writes each agent's LLM traffic.
enum class AgentTrafficLogType {
    // Extreme diff (the default): JSON-like, like Diff, but shorter -- only
    // what changed since the same agent's previous request, tools only by
    // name and description, tool calls as "path = value" lines, text wrapped
    // to 80 characters, and fields that say nothing left out (see
    // ComputeExtremeDiff and FormatExtremeResponse).
    XDiff,
    // Each request is written as its difference from the previous request of
    // the same agent (the first one in full); responses are written in full,
    // since each is new rather than a growing version of the last.
    Diff,
    // Every request and response is written in full.
    Full,
};

// Parses "xdiff", "diff" or "full". Returns false, leaving outType untouched,
// for anything else.
bool ParseAgentTrafficLogType(const std::string& name, AgentTrafficLogType& outType);

// The name ParseAgentTrafficLogType accepts for type.
const char* AgentTrafficLogTypeName(AgentTrafficLogType type);

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

// The extreme difference between two request bodies, written JSON-like:
// - only fields that changed are written; an array that only grew (an
//   agent's "messages", written as "m") shows only its new entries, after a
//   line saying which ones are as before;
// - "tools" is each tool's name with its description wrapped under it, and no
//   parameter schema;
// - a message's "tool_calls" is one line per field, named by its full path:
//     m[1].tool_calls[0].function.name = "get_current_date_time"
//   (a call's "arguments" given as a JSON string is expanded the same way);
// - a string with a line break, or longer than 80 characters, is a """-fenced
//   block of its lines, wrapped to 80 characters and indented;
// - empty strings and a tool call's "index" are left out.
// An empty previousJson means there is nothing to diff against: everything is
// new. Anything that is not a JSON object is written as FormatExtremeResponse
// would.
std::string ComputeExtremeDiff(const std::string& previousJson, const std::string& currentJson);

// A response body written the way ComputeExtremeDiff writes a request (tool
// calls under "message.tool_calls..."), leaving out "model", "created_at" and
// the "*_duration" timings. Anything that is not JSON is returned as it is.
std::string FormatExtremeResponse(const std::string& json);

// Indents every line of text for an agent at the given depth in its agent
// tree: nothing at depth 0, and two tabs and a '|' per level below that, so a
// subagent's traffic hangs under its parent's.
std::string IndentForAgentDepth(const std::string& text, size_t depth);

// Writes agent traffic to a stream or file it owns, one entry per request or
// response, each headed by its direction, agent path and time, and indented by
// the agent's depth. Thread-safe: agents report from their own threads.
// Register OnSend/OnReceive as the Logger's agent callbacks
// (RegisterLogAgentSendCallback/RegisterLogAgentReceiveCallback).
class AgentTrafficLog {
public:
    AgentTrafficLog(std::unique_ptr<std::ostream> out, AgentTrafficLogType type);

    // Writes to the file at path (emptied first), and re-creates it if it is
    // deleted while Haisos runs (see ReopeningLogFile). The re-created file
    // starts afresh -- in the diff modes each agent's next request is written in
    // full -- so it never refers back to entries that went with the old one.
    AgentTrafficLog(std::shared_ptr<ReopeningLogFile> file, AgentTrafficLogType type);

    AgentTrafficLog(const AgentTrafficLog&) = delete;
    AgentTrafficLog& operator=(const AgentTrafficLog&) = delete;

    void OnSend(const std::vector<std::string>& agentPath, const std::string& json);
    void OnReceive(const std::vector<std::string>& agentPath, const std::string& json);

private:
    // Writes one entry: its header line, then body, indented for the agent.
    void WriteEntry(const char* direction, const std::vector<std::string>& agentPath, const std::string& body);
    // Called first by OnSend/OnReceive, with m_mutex held: if the file was
    // re-created, forgets the requests the next diffs would refer to.
    void StartAfreshIfRecreated();
    // Writes one whole entry, with m_mutex held.
    void Emit(const std::string& entry);

    std::mutex m_mutex;
    // Exactly one of the two is set.
    std::unique_ptr<std::ostream> m_out;
    std::shared_ptr<ReopeningLogFile> m_file;
    AgentTrafficLogType m_type;
    // Diff/XDiff modes: the last request each agent (by path) sent, to diff
    // the next one against.
    std::unordered_map<std::string, std::string> m_lastSent;
};

} // namespace Haisos
