#include "AgentTrafficLog.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include "src/components/Logger/Logger.h"

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

using OrderedJson = nlohmann::ordered_json;

OrderedJson ParseOrDiscard(const std::string& json) {
    return OrderedJson::parse(json, nullptr, /*allow_exceptions=*/false);
}

// How wide xdiff lets a line of text grow before wrapping it, counted in
// characters, not counting the indentation in front of it.
constexpr size_t kWrapWidth = 80;

std::string Spaces(size_t count) {
    return std::string(count, ' ');
}

// A key as a path segment: bare if it reads as a name, ["quoted"] otherwise,
// so a key holding a '.' or a space cannot be mistaken for two segments.
std::string PathSegment(const std::string& path, const std::string& key) {
    bool bare = !key.empty();
    for (char c : key) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            bare = false;
            break;
        }
    }
    if (bare) {
        return path.empty() ? key : path + "." + key;
    }
    return path + "[" + OrderedJson(key).dump() + "]";
}

std::string IndexSegment(const std::string& path, size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

// A control character as a visible escape; a tab stays as it is.
void AppendVisible(std::string& out, char c) {
    switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
            out += buf;
        }
    }
}

bool IsControl(char c) {
    return static_cast<unsigned char>(c) < 0x20 && c != '\t';
}

// The number of characters (UTF-8 code points) in text.
size_t CharCount(const std::string& text) {
    size_t count = 0;
    for (char c : text) {
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }
    return count;
}

// The byte offset at which text's character number `column` starts (or its
// size, if it is shorter).
size_t ByteOffsetOfColumn(const std::string& text, size_t column) {
    size_t seen = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (seen == column) {
                return i;
            }
            ++seen;
        }
    }
    return text.size();
}

// text split into lines at its own line breaks (a "\r\n" is one), then each
// line wrapped to at most kWrapWidth characters, breaking at the last space
// that fits (or mid-word, for a word longer than a whole line). Control
// characters other than a tab are escaped.
std::vector<std::string> WrapText(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    auto flush = [&lines](std::string current) {
        while (CharCount(current) > kWrapWidth) {
            size_t limit = ByteOffsetOfColumn(current, kWrapWidth);
            size_t space = current.rfind(' ', limit);
            if (space == std::string::npos || space == 0) {
                lines.push_back(current.substr(0, limit));
                current.erase(0, limit);
            } else {
                lines.push_back(current.substr(0, space));
                current.erase(0, space + 1);
            }
        }
        lines.push_back(std::move(current));
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        if (c == '\n') {
            flush(std::move(line));
            line.clear();
        } else if (IsControl(c)) {
            AppendVisible(line, c);
        } else {
            line += c;
        }
    }
    flush(std::move(line));
    return lines;
}

// A string that fits on one line, quoted, with quotes, backslashes and
// control characters escaped.
std::string QuoteString(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (IsControl(c)) {
            AppendVisible(out, c);
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// A string as xdiff writes it, for a value whose line starts `indent` spaces
// in: quoted if it fits on one line, otherwise a """-fenced block of its
// lines, wrapped and indented four more, so it reads as the text it holds.
// An empty line gets no indent, so the block carries no trailing blanks.
std::string RenderString(const std::string& text, size_t indent) {
    if (text.find('\n') == std::string::npos && CharCount(text) <= kWrapWidth) {
        return QuoteString(text);
    }
    std::string out = "\"\"\"\n";
    for (const auto& line : WrapText(text)) {
        if (!line.empty()) {
            out += Spaces(indent + 4) + line;
        }
        out += '\n';
    }
    return out + Spaces(indent) + "\"\"\"";
}

// A leaf value (or an empty object or array) as xdiff writes it.
std::string RenderLeaf(const OrderedJson& value, size_t indent) {
    if (value.is_string()) {
        return RenderString(value.get_ref<const std::string&>(), indent);
    }
    if (value.is_object()) {
        return "{}";
    }
    if (value.is_array()) {
        return "[]";
    }
    return value.dump();
}

// An object's fields in the order xdiff writes them: "role" and "name" lead,
// so a message opens with who is speaking and a tool call with the tool.
// Empty strings (an assistant message's "content" when it only calls tools)
// and a tool call's "index" say nothing, and are left out.
std::vector<std::pair<std::string, const OrderedJson*>> FieldsToWrite(const OrderedJson& object) {
    std::vector<std::pair<std::string, const OrderedJson*>> fields;
    auto keep = [&fields](const std::string& key, const OrderedJson& value) {
        if (value.is_string() && value.get_ref<const std::string&>().empty()) {
            return;
        }
        if (key == "index") {
            return;
        }
        fields.emplace_back(key, &value);
    };
    for (const char* leading : {"role", "name"}) {
        auto it = object.find(leading);
        if (it != object.end()) {
            keep(leading, *it);
        }
    }
    for (const auto& [key, value] : object.items()) {
        if (key != "role" && key != "name") {
            keep(key, value);
        }
    }
    return fields;
}

// Appends value as one "path = value" line per leaf, each `indent` spaces in.
// OpenAI-style tool calls carry their arguments as a JSON string; spelled out,
// they read the same as Ollama's arguments object.
void Flatten(std::string& out, const std::string& path, const OrderedJson& value, size_t indent) {
    if (value.is_object() && !value.empty()) {
        for (const auto& [key, childPtr] : FieldsToWrite(value)) {
            const OrderedJson& child = *childPtr;
            std::string childPath = PathSegment(path, key);
            if (key == "arguments" && child.is_string()) {
                auto decoded = ParseOrDiscard(child.get_ref<const std::string&>());
                if (decoded.is_object() || decoded.is_array()) {
                    Flatten(out, childPath, decoded, indent);
                    continue;
                }
            }
            Flatten(out, childPath, child, indent);
        }
    } else if (value.is_array() && !value.empty()) {
        for (size_t i = 0; i < value.size(); ++i) {
            Flatten(out, IndexSegment(path, i), value[i], indent);
        }
    } else {
        if (!out.empty()) {
            out += '\n';
        }
        out += Spaces(indent) + path + " = " + RenderLeaf(value, indent);
    }
}

// One entry of an object or array being written: its text (every line but
// the first already indented), and whether a ',' may follow it -- not after a
// run of "path = value" lines, which are not JSON to begin with.
struct Chunk {
    std::string text;
    bool takesComma = true;
};

std::string JoinChunks(const std::vector<Chunk>& chunks) {
    std::string out;
    for (size_t i = 0; i < chunks.size(); ++i) {
        out += chunks[i].text;
        if (i + 1 < chunks.size()) {
            out += chunks[i].takesComma ? ",\n" : "\n";
        }
    }
    return out;
}

std::string RenderValue(const OrderedJson& value, size_t indent, const std::string& path, size_t from = 0);

// An object or array's field, `"key": value`, `indent` spaces in. A message's
// "tool_calls" is written as "path = value" lines rather than as JSON: a call
// is read for what it asks, not for its shape.
Chunk RenderField(const std::string& key, const OrderedJson& value, size_t indent, const std::string& path, size_t from = 0) {
    if (key == "tool_calls" && value.is_array() && !value.empty()) {
        Chunk chunk;
        Flatten(chunk.text, path, value, indent);
        chunk.takesComma = false;
        return chunk;
    }
    return Chunk{Spaces(indent) + OrderedJson(key).dump() + ": " + RenderValue(value, indent, path, from)};
}

// value as xdiff writes it, its first line starting `indent` spaces in (the
// caller has written those). An array's entries before `from` are left out,
// with a line saying so: they are the same as in the previous request.
std::string RenderValue(const OrderedJson& value, size_t indent, const std::string& path, size_t from) {
    if (value.is_object() && !value.empty()) {
        std::vector<Chunk> chunks;
        for (const auto& [key, child] : FieldsToWrite(value)) {
            chunks.push_back(RenderField(key, *child, indent + 2, PathSegment(path, key)));
        }
        return "{\n" + JoinChunks(chunks) + "\n" + Spaces(indent) + "}";
    }
    if (value.is_array() && !value.empty()) {
        std::vector<Chunk> chunks;
        if (from > 0) {
            chunks.push_back(Chunk{Spaces(indent + 2) + "-- " + path + "[0.." + std::to_string(from - 1) + "] as before --", false});
        }
        for (size_t i = from; i < value.size(); ++i) {
            chunks.push_back(Chunk{Spaces(indent + 2) + RenderValue(value[i], indent + 2, IndexSegment(path, i))});
        }
        return "[\n" + JoinChunks(chunks) + "\n" + Spaces(indent) + "]";
    }
    return RenderLeaf(value, indent);
}

const OrderedJson& ToolDescribed(const OrderedJson& tool) {
    auto it = tool.find("function");
    return it != tool.end() && it->is_object() ? *it : tool;
}

// A request's "tools": each tool's name, then its description wrapped under
// it; their parameter schemas are left out.
std::string RenderTools(const OrderedJson& tools, size_t indent) {
    std::string out = "[";
    for (const auto& tool : tools) {
        const auto& described = ToolDescribed(tool);
        auto name = described.find("name");
        out += "\n" + Spaces(indent + 2) + (name != described.end() && name->is_string() ? name->get<std::string>() : tool.dump());
        auto description = described.find("description");
        if (description != described.end() && description->is_string()) {
            out += ":";
            for (const auto& line : WrapText(description->get<std::string>())) {
                out += "\n" + (line.empty() ? "" : Spaces(indent + 6) + line);
            }
        }
    }
    return out + "\n" + Spaces(indent) + "]";
}

} // namespace

bool ParseAgentTrafficLogType(const std::string& name, AgentTrafficLogType& outType) {
    if (name == "xdiff") { outType = AgentTrafficLogType::XDiff; return true; }
    if (name == "diff") { outType = AgentTrafficLogType::Diff; return true; }
    if (name == "full") { outType = AgentTrafficLogType::Full; return true; }
    return false;
}

const char* AgentTrafficLogTypeName(AgentTrafficLogType type) {
    switch (type) {
        case AgentTrafficLogType::XDiff: return "xdiff";
        case AgentTrafficLogType::Diff: return "diff";
        case AgentTrafficLogType::Full: return "full";
    }
    return "?";
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

std::string ComputeExtremeDiff(const std::string& previousJson, const std::string& currentJson) {
    auto current = ParseOrDiscard(currentJson);
    if (!current.is_object()) {
        return FormatExtremeResponse(currentJson);
    }
    auto previous = previousJson.empty() ? OrderedJson() : ParseOrDiscard(previousJson);
    const bool havePrevious = previous.is_object();

    std::vector<Chunk> chunks;
    for (const auto& [key, value] : current.items()) {
        const OrderedJson* before = nullptr;
        if (havePrevious) {
            auto it = previous.find(key);
            if (it != previous.end()) {
                before = &*it;
            }
        }
        if (before && *before == value) {
            continue;
        }
        const std::string name = key == "messages" ? "m" : key;
        if (key == "tools" && value.is_array()) {
            chunks.push_back(Chunk{"  \"tools\": " + RenderTools(value, 2)});
            continue;
        }
        size_t from = 0;
        if (value.is_array() && before && before->is_array() && before->size() <= value.size()) {
            bool grewOnly = true;
            for (size_t i = 0; grewOnly && i < before->size(); ++i) {
                grewOnly = (*before)[i] == value[i];
            }
            from = grewOnly ? before->size() : 0;
        }
        chunks.push_back(RenderField(name, value, 2, PathSegment("", name), from));
    }
    if (havePrevious) {
        for (const auto& [key, value] : previous.items()) {
            if (!current.contains(key)) {
                chunks.push_back(Chunk{"  " + OrderedJson(key == "messages" ? "m" : key).dump() + ": \"-- removed --\""});
            }
        }
    }
    return chunks.empty() ? "{}" : "{\n" + JoinChunks(chunks) + "\n}";
}

std::string FormatExtremeResponse(const std::string& json) {
    auto parsed = ParseOrDiscard(json);
    if (!parsed.is_object()) {
        return parsed.is_discarded() ? json : RenderValue(parsed, 0, "");
    }
    // What was asked of which model is in the request; when it came back is in
    // the entry's header; how long each stage took is rarely what one reads
    // this log for.
    for (auto it = parsed.begin(); it != parsed.end();) {
        const std::string& key = it.key();
        bool noise = key == "model" || key == "created_at"
            || (key.size() > 9 && key.compare(key.size() - 9, 9, "_duration") == 0);
        it = noise ? parsed.erase(it) : std::next(it);
    }
    return RenderValue(parsed, 0, "");
}

std::string IndentForAgentDepth(const std::string& text, size_t depth) {
    if (depth == 0) {
        return text;
    }
    std::string prefix;
    for (size_t i = 0; i < depth; ++i) {
        prefix += "\t\t|";
    }
    std::string out;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        bool terminated = lineEnd != std::string::npos;
        if (!terminated) {
            lineEnd = text.size();
        }
        out += prefix;
        if (lineEnd > lineStart) {
            out += ' ';
            out.append(text, lineStart, lineEnd - lineStart);
        }
        if (terminated) {
            out += '\n';
        }
        lineStart = lineEnd + 1;
    }
    return out;
}

AgentTrafficLog::AgentTrafficLog(std::unique_ptr<std::ostream> out, AgentTrafficLogType type)
    : m_out(std::move(out))
    , m_type(type)
{
}

AgentTrafficLog::AgentTrafficLog(std::shared_ptr<ReopeningLogFile> file, AgentTrafficLogType type)
    : m_file(std::move(file))
    , m_type(type)
{
}

void AgentTrafficLog::StartAfreshIfRecreated() {
    if (m_file && m_file->EnsureOpen()) {
        m_lastSent.clear();
        m_file->Write("(" + m_file->Path() + " was deleted while Haisos was running and has been re-created; "
            "each agent's next request is written in full)\n\n");
    }
}

void AgentTrafficLog::Emit(const std::string& entry) {
    if (m_file) {
        m_file->Write(entry);
    } else if (m_out) {
        *m_out << entry;
        m_out->flush();
    }
}

void AgentTrafficLog::WriteEntry(const char* direction, const std::vector<std::string>& agentPath, const std::string& body) {
    std::string entry = std::string(direction) + " [" + FormatAgentPath(agentPath) + "] " + CurrentTimestamp() + "\n"
        + body + "\n\n";
    Emit(IndentForAgentDepth(entry, agentPath.empty() ? 0 : agentPath.size() - 1));
}

void AgentTrafficLog::OnSend(const std::vector<std::string>& agentPath, const std::string& json) {
    std::lock_guard<std::mutex> lock(m_mutex);
    StartAfreshIfRecreated();
    if (m_type == AgentTrafficLogType::Full) {
        WriteEntry(">>>>>>>> SEND   ", agentPath, PrettyPrintJson(json));
        return;
    }
    const bool extreme = m_type == AgentTrafficLogType::XDiff;
    const std::string agentName = FormatAgentPath(agentPath);
    std::string body;
    auto it = m_lastSent.find(agentName);
    if (it == m_lastSent.end()) {
        body = extreme ? ComputeExtremeDiff("", json) : PrettyPrintJson(json);
        m_lastSent.emplace(agentName, json);
    } else {
        body = "(diff against the previous send of [" + agentName + "])\n"
            + (extreme ? ComputeExtremeDiff(it->second, json) : ComputeSmartDiff(it->second, json));
        it->second = json;
    }
    WriteEntry(">>>>>>>> SEND   ", agentPath, body);
}

void AgentTrafficLog::OnReceive(const std::vector<std::string>& agentPath, const std::string& json) {
    std::lock_guard<std::mutex> lock(m_mutex);
    StartAfreshIfRecreated();
    WriteEntry("<<<<<<<< RECEIVE", agentPath,
        m_type == AgentTrafficLogType::XDiff ? FormatExtremeResponse(json) : PrettyPrintJson(json));
}

} // namespace Haisos
