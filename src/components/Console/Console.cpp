#include "Console.h"
#include <iostream>

namespace Haisos {

Console::Console() = default;

Console::~Console() {
    Stop();
}

void Console::Write(const std::string& message) {
    m_queue.Post(message);
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
        std::string message;
        if (!m_queue.Pop(message)) {
            break;
        }
        if (!message.empty()) {
            std::cout << message << '\n';
        }
    }
}

}
