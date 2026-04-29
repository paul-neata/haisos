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
