#include "Console.h"
#include <iostream>

namespace Haisos {

Console::Console() = default;

Console::~Console() {
    Stop();
}

void Console::Write(const std::string& bytes) {
    if (bytes.empty()) {
        return;
    }
    m_queue.Post(ConsoleOutput{false, bytes});
}

void Console::WriteError(const std::string& bytes) {
    if (bytes.empty()) {
        return;
    }
    m_queue.Post(ConsoleOutput{true, bytes});
}

std::optional<std::string> Console::ReadLine() {
    std::lock_guard<std::mutex> lock(m_readMutex);
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }
    // A line typed on Windows, or piped in from a file written there, still
    // carries its '\r'; it is part of the line ending, not of the line.
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

void Console::Start() {
    if (!m_backgroundThread.joinable()) {
        m_backgroundThread = std::thread(&Console::ProcessQueue, this);
    }
}

void Console::Stop() {
    m_queue.Close();
    if (m_backgroundThread.joinable()) {
        m_backgroundThread.join();
    }
}

void Console::ProcessQueue() {
    while (true) {
        ConsoleOutput output;
        if (!m_queue.Pop(output)) {
            break;
        }
        // The bytes as they are: nothing added, and the stream is flushed
        // after every write, so a partial line (a prompt) shows at once.
        std::ostream& stream = output.toError ? std::cerr : std::cout;
        stream.write(output.bytes.data(), static_cast<std::streamsize>(output.bytes.size()));
        stream.flush();
    }
}

}
