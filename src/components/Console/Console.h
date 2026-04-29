#pragma once
#include <string>
#include <memory>
#include <thread>
#include "interfaces/IConsole.h"
#include "src/components/libheaders/SynchronizedQueue.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

class Console : public IConsole {
public:
    explicit Console(bool registerAsLogMessageReceiver = false);
    ~Console() override;

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    void Write(const std::string& message) override;
    void Write(const IAgent& agent, const std::string& message) override;

    void Start() override;
    void Stop() override;

private:
    void ProcessQueue();

    SynchronizedQueue<std::string> m_queue;
    std::thread m_backgroundThread;
    int m_logReceiverToken = -1;
};

}
