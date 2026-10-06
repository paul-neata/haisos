#pragma once
#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include "interfaces/IFileDescriptor.h"

namespace Haisos::Mocks {

// A descriptor entirely driven by the test: Write appends to a string (a
// forced failure is one SetWriteResult away), Read hands out bytes given with
// Feed(), blocking until there are some or EndInput() was called.
class MockFileDescriptor : public IFileDescriptor {
public:
    explicit MockFileDescriptor(bool isTerminal = false) : m_isTerminal(isTerminal) {}

    ssize_t Read(void* buf, size_t count) override {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_inputCv.wait(lock, [this] { return !m_input.empty() || m_inputEnded; });
        if (count == 0) {
            return 0;
        }
        const size_t copied = std::min(count, m_input.size());
        std::copy_n(m_input.begin(), copied, static_cast<char*>(buf));
        m_input.erase(0, copied);
        return static_cast<ssize_t>(copied);
    }

    ssize_t Write(const void* buf, size_t count) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_forcedWriteResult) {
            return *m_forcedWriteResult;
        }
        m_written.append(static_cast<const char*>(buf), count);
        ++m_writeCalls;
        ++m_writtenCalls;
        return static_cast<ssize_t>(count);
    }

    bool IsTerminal() const override { return m_isTerminal; }

    // Adds bytes to what Read will hand out, and wakes a reader.
    void Feed(const std::string& bytes) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_input += bytes;
        }
        m_inputCv.notify_all();
    }
    // No more input will ever come: Read returns 0 once the fed bytes are out.
    void EndInput() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_inputEnded = true;
        }
        m_inputCv.notify_all();
    }
    // Every later Write returns this (e.g. kIOError) instead of taking bytes.
    void SetWriteResult(ssize_t result) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_forcedWriteResult = result;
    }
    void ClearWriteResult() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_forcedWriteResult = std::nullopt;
    }

    // Everything written so far, as one string.
    std::string Written() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_written;
    }
    // How many times Write was called, successful or not.
    int WriteCalls() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_writeCalls;
    }

private:
    const bool m_isTerminal;
    mutable std::mutex m_mutex;
    std::condition_variable m_inputCv;
    std::string m_written;
    int m_writeCalls = 0;   // every Write call, failed ones included
    int m_writtenCalls = 0; // calls that took bytes
    std::string m_input;
    bool m_inputEnded = false;
    std::optional<ssize_t> m_forcedWriteResult;
};

}
