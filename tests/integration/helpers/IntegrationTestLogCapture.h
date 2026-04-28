#pragma once

#include "src/components/Logger/Logger.h"
#include "tests/integration/helpers/IntegrationTestHelpers.h"
#include <vector>
#include <mutex>
#include <iostream>

namespace Haisos::IntegrationTest {

class IntegrationTestLogCapture {
public:
    IntegrationTestLogCapture() {
        LogSetConsoleOutput(false);
        LogSetMinimumLevel(LogLevel::VerboseDebug);
        LogClearMessageReceivers();
        LogRegisterMessageReceiver([this](const LogMessage& msg) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messages.push_back(msg);
            if (msg.level >= LogLevel::Error) {
                m_hasError = true;
            }
        });
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
};

}
