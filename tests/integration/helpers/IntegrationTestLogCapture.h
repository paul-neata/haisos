#pragma once

#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include <vector>
#include <mutex>
#include <iostream>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace Haisos::IntegrationTest {

inline LogLevel ParseTestLogLevel(const std::string& level) {
    std::string lower;
    lower.reserve(level.size());
    for (char c : level) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "verbose_debug" || lower == "verbosedebug") return LogLevel::VerboseDebug;
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "trace") return LogLevel::Trace;
    if (lower == "info") return LogLevel::Info;
    if (lower == "warning") return LogLevel::Warning;
    if (lower == "error") return LogLevel::Error;
    return LogLevel::VerboseDebug;
}

class IntegrationTestLogCapture {
public:
    IntegrationTestLogCapture() {
        LogSetConsoleOutput(false);

        LogLevel minLevel = LogLevel::VerboseDebug;
        if (const char* envLevel = std::getenv("HAISOS_TEST_LOG_LEVEL")) {
            minLevel = ParseTestLogLevel(envLevel);
        }
        LogSetMinimumLevel(minLevel);

        LogClearMessageReceivers();
        LogRegisterMessageReceiver([this](const LogMessage& msg) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messages.push_back(msg);
            if (msg.level >= LogLevel::Error) {
                m_hasError = true;
            }
        });

        if (const char* envFile = std::getenv("HAISOS_TEST_LOG_FILE")) {
            m_logFile = std::make_unique<std::ofstream>(envFile, std::ios::out | std::ios::app);
            if (m_logFile && m_logFile->is_open()) {
                LogRegisterMessageReceiver([this](const LogMessage& msg) {
                    if (!m_logFile || !m_logFile->is_open()) return;
                    const char* levelStr =
                        msg.level == LogLevel::VerboseDebug ? "VERBOSE_DEBUG" :
                        msg.level == LogLevel::Debug ? "DEBUG" :
                        msg.level == LogLevel::Trace ? "TRACE" :
                        msg.level == LogLevel::Info ? "INFO" :
                        msg.level == LogLevel::Warning ? "WARNING" :
                        msg.level == LogLevel::Error ? "ERROR" : "UNKNOWN";
                    *m_logFile << "[" << msg.timestamp << "][" << levelStr << "] " << msg.message << "\n" << std::flush;
                });
            }
        }
    }

    ~IntegrationTestLogCapture() {
        LogSetConsoleOutput(true);
    }

    bool HasError() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hasError;
    }

    void DumpIfFailed(bool testFailed) const {
        if (!testFailed && !HasError()) {
            return;
        }

        ConsoleLock clock;
        std::cerr << "\n========== CAPTURED LOGS ==========\n";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& msg : m_messages) {
                const char* levelStr =
                    msg.level == LogLevel::VerboseDebug ? "VERBOSE_DEBUG" :
                    msg.level == LogLevel::Debug ? "DEBUG" :
                    msg.level == LogLevel::Trace ? "TRACE" :
                    msg.level == LogLevel::Info ? "INFO" :
                    msg.level == LogLevel::Warning ? "WARNING" :
                    msg.level == LogLevel::Error ? "ERROR" : "UNKNOWN";
                std::cerr << "[" << msg.timestamp << "][" << levelStr << "] " << msg.message << "\n";
            }
        }
        std::cerr << "===================================\n\n" << std::flush;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<LogMessage> m_messages;
    bool m_hasError = false;
    std::unique_ptr<std::ofstream> m_logFile;
};

}
