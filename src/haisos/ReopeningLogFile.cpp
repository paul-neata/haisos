#include "ReopeningLogFile.h"
#include <filesystem>
#include <system_error>

namespace Haisos {

ReopeningLogFile::ReopeningLogFile(std::string path, bool truncate)
    : m_path(std::move(path))
    , m_out(m_path, std::ios::out | (truncate ? std::ios::trunc : std::ios::app))
{
}

bool ReopeningLogFile::IsOpen() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_out.is_open();
}

bool ReopeningLogFile::EnsureOpen() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EnsureOpenLocked();
}

void ReopeningLogFile::Write(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureOpenLocked();
    if (m_out.is_open()) {
        m_out << text;
        m_out.flush();
    }
}

bool ReopeningLogFile::EnsureOpenLocked() {
    std::error_code error;
    if (m_out.is_open() && std::filesystem::exists(m_path, error)) {
        return false;
    }
    // Gone (or never opened): open it afresh. The old stream, if any, still
    // points at the deleted file, whose last contents vanish with it.
    m_out.close();
    m_out.clear();
    m_out.open(m_path, std::ios::out | std::ios::app);
    return m_out.is_open();
}

} // namespace Haisos
