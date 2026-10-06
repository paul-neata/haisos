#include "ConsoleDescriptors.h"
#include <algorithm>

namespace Haisos {

// --- Output ---

std::shared_ptr<ConsoleOutputDescriptor> ConsoleOutputDescriptor::Create(std::shared_ptr<IPhysicalConsole> console) {
    return std::shared_ptr<ConsoleOutputDescriptor>(new ConsoleOutputDescriptor(std::move(console)));
}

ConsoleOutputDescriptor::ConsoleOutputDescriptor(std::shared_ptr<IPhysicalConsole> console)
    : m_console(std::move(console)) {}

ssize_t ConsoleOutputDescriptor::Read(void*, size_t) {
    return kIOError;
}

ssize_t ConsoleOutputDescriptor::Write(const void* buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    if (m_console) {
        m_console->Write(std::string(static_cast<const char*>(buf), count));
    }
    // No console: the bytes are discarded, as on a closed /dev/tty line.
    return static_cast<ssize_t>(count);
}

bool ConsoleOutputDescriptor::IsTerminal() const {
    return true;
}

// --- Error ---

std::shared_ptr<ConsoleErrorDescriptor> ConsoleErrorDescriptor::Create(std::shared_ptr<IPhysicalConsole> console) {
    return std::shared_ptr<ConsoleErrorDescriptor>(new ConsoleErrorDescriptor(std::move(console)));
}

ConsoleErrorDescriptor::ConsoleErrorDescriptor(std::shared_ptr<IPhysicalConsole> console)
    : m_console(std::move(console)) {}

ssize_t ConsoleErrorDescriptor::Read(void*, size_t) {
    return kIOError;
}

ssize_t ConsoleErrorDescriptor::Write(const void* buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    if (m_console) {
        m_console->WriteError(std::string(static_cast<const char*>(buf), count));
    }
    return static_cast<ssize_t>(count);
}

bool ConsoleErrorDescriptor::IsTerminal() const {
    return true;
}

// --- Input ---

std::shared_ptr<ConsoleInputDescriptor> ConsoleInputDescriptor::Create(std::shared_ptr<IPhysicalConsole> console) {
    return std::shared_ptr<ConsoleInputDescriptor>(new ConsoleInputDescriptor(std::move(console)));
}

ConsoleInputDescriptor::ConsoleInputDescriptor(std::shared_ptr<IPhysicalConsole> console)
    : m_console(std::move(console)) {
    // A null console has nothing to give: end of file from the start.
    m_atEnd = (m_console == nullptr);
}

ssize_t ConsoleInputDescriptor::Read(void* buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.empty() && !m_atEnd) {
        // Blocking and uninterruptible, as console input is (goal.md D7).
        auto line = m_console->ReadLine();
        if (!line) {
            m_atEnd = true;
        } else {
            m_pending = *line + "\n";
        }
    }
    const size_t copied = std::min(count, m_pending.size());
    if (copied > 0) {
        std::copy_n(m_pending.begin(), copied, static_cast<char*>(buf));
        m_pending.erase(0, copied);
    }
    return static_cast<ssize_t>(copied);
}

ssize_t ConsoleInputDescriptor::Write(const void*, size_t) {
    return kIOError;
}

bool ConsoleInputDescriptor::IsTerminal() const {
    return true;
}

// --- Empty input ---

std::shared_ptr<EmptyInputDescriptor> EmptyInputDescriptor::Create() {
    return std::shared_ptr<EmptyInputDescriptor>(new EmptyInputDescriptor());
}

ssize_t EmptyInputDescriptor::Read(void*, size_t) {
    return 0;
}

ssize_t EmptyInputDescriptor::Write(const void*, size_t) {
    return kIOError;
}

bool EmptyInputDescriptor::IsTerminal() const {
    return false;
}

}
