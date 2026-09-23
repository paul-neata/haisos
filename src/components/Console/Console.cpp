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
