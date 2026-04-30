#include "Logger.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace Haisos {

namespace {
    struct ReceiverEntry {
        int token;
        LogMessageReceiver receiver;
    };
    std::vector<ReceiverEntry> g_receivers;
    std::atomic<LogLevel> g_minLevel{LogLevel::VerboseDebug};
    std::atomic<bool> g_consoleOutput{true};
    std::mutex g_mutex;
    int g_nextToken = 1;
}

void LogImpl(LogLevel level, const char* file, int line, const std::string& message) {
    // Check if level is >= minimum
    if (static_cast<int>(level) < static_cast<int>(g_minLevel.load())) {
        return;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    // Create log message
    LogMessage msg;
    msg.level = level;
    msg.message = message;
    msg.timestamp = oss.str();

    // Print to stderr
    const char* levelStr =
        level == LogLevel::VerboseDebug ? "VERBOSE_DEBUG" :
        level == LogLevel::Debug ? "DEBUG" :
        level == LogLevel::Trace ? "TRACE" :
        level == LogLevel::Info ? "INFO" :
        level == LogLevel::Warning ? "WARNING" :
        level == LogLevel::Error ? "ERROR" : "UNKNOWN";

    // Notify receivers
    std::vector<ReceiverEntry> receivers_copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        receivers_copy = g_receivers;
    }
    for (const auto& entry : receivers_copy) {
        entry.receiver(msg);
    }

    if (!g_consoleOutput.load()) {
        return;
    }

    fprintf(stderr, "[%s][%s][%s:%d] %s\n",
            msg.timestamp.c_str(), levelStr, file, line, msg.message.c_str());
}

int LogRegisterMessageReceiver(LogMessageReceiver receiver) {
    std::lock_guard<std::mutex> lock(g_mutex);
    int token = g_nextToken++;
    g_receivers.push_back({token, std::move(receiver)});
    return token;
}

void LogUnregisterMessageReceiver(int token) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_receivers.erase(
        std::remove_if(g_receivers.begin(), g_receivers.end(),
            [token](const ReceiverEntry& entry) { return entry.token == token; }),
        g_receivers.end());
}

void LogSetMinimumLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_minLevel = level;
}

LogLevel LogGetMinimumLevel() {
    return g_minLevel.load();
}

void LogClearMessageReceivers() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_receivers.clear();
}

void LogSetConsoleOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_consoleOutput = enabled;
}

}
