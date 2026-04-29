#include "Console.h"
#include <iostream>
#include "interfaces/IAgent.h"

namespace Haisos {

Console::Console(bool registerAsLogMessageReceiver)
{
    if (registerAsLogMessageReceiver) {
        m_logReceiverToken = LogRegisterMessageReceiver([this](const LogMessage& msg) {
            Write(msg.message);
        });
    }
}

Console::~Console() {
    Stop();
    if (m_logReceiverToken >= 0) {
        LogUnregisterMessageReceiver(m_logReceiverToken);
        m_logReceiverToken = -1;
    }
}

void Console::Write(const std::string& message) {
    m_queue.Post(message);
}

void Console::Write(const IAgent& agent, const std::string& message) {
    m_queue.Post("[" + agent.Name() + "] " + message);
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
