#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdarg>

namespace Haisos {

// Log level enum ordered by severity (lowest to highest)
enum class LogLevel {
    VerboseDebug = 0,
    Debug = 1,
    Trace = 2,
    Info = 3,
    Warning = 4,
    Error = 5
};

// Log message struct
struct LogMessage {
    LogLevel level;
    std::string message;
    std::string timestamp;
};

// Log message receiver callback type
using LogMessageReceiver = std::function<void(const LogMessage&)>;

inline std::string FormatLogMessage(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list args2;
    va_copy(args2, args);
    int len = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string msg(len, '\0');
    vsnprintf(&msg[0], msg.size() + 1, fmt, args2);
    va_end(args2);
    return msg;
}

// Implementation function
void LogImpl(LogLevel level, const char* file, int line, const std::string& message);

// Register a message receiver, returns a token for unregistering
int LogRegisterMessageReceiver(LogMessageReceiver receiver);

// Unregister a message receiver by token
void LogUnregisterMessageReceiver(int token);

// Clear all registered message receivers
void LogClearMessageReceivers();

// Set minimum log level
void LogSetMinimumLevel(LogLevel level);

// Get current minimum log level
LogLevel LogGetMinimumLevel();

// Enable or disable default stderr console output
void LogSetConsoleOutput(bool enabled);

// --- Agent traffic ---
//
// Every JSON body an agent sends to its LLM, and every one it gets back, is
// reported here with the name of the agent it belongs to. This is separate from
// the leveled log above: it carries whole JSON documents, is not filtered by
// level, and goes only to the callback registered for its direction (see
// `--log-agent-to-file` in the haisos CLI). With no callback registered, a call
// costs a check and nothing more.
//
// The callbacks run on the calling agent's thread, and several agents can call
// at once, so a callback must be thread-safe.
using LogAgentCallback = std::function<void(const std::string& agentName, const std::string& json)>;

// Reports a request body about to be sent to the LLM by agentName.
void LogAgentSend(const std::string& agentName, const std::string& json);

// Reports what came back from the LLM for agentName: the response body, or, if
// the request failed with no body, a line saying why.
void LogAgentReceive(const std::string& agentName, const std::string& json);

// Sets the one callback for each direction, replacing any earlier one; pass
// nullptr to remove it.
void RegisterLogAgentSendCallback(LogAgentCallback callback);
void RegisterLogAgentReceiveCallback(LogAgentCallback callback);

// Convenience macros using do-while(false) pattern
#define Log(level, ...) \
    do { \
        if (level < Haisos::LogGetMinimumLevel()) break; \
        Haisos::LogImpl(level, __FILE__, __LINE__, Haisos::FormatLogMessage(__VA_ARGS__)); \
    } while (false)
#define LogError(...)   Log(Haisos::LogLevel::Error, __VA_ARGS__)
#define LogWarning(...) Log(Haisos::LogLevel::Warning, __VA_ARGS__)
#define LogInfo(...)    Log(Haisos::LogLevel::Info, __VA_ARGS__)
#define LogTrace(...)   Log(Haisos::LogLevel::Trace, __VA_ARGS__)
#define LogDebug(...)   Log(Haisos::LogLevel::Debug, __VA_ARGS__)
#define LogVerboseDebug(...) Log(Haisos::LogLevel::VerboseDebug, __VA_ARGS__)

}
