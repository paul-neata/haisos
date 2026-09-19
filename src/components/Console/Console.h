#pragma once
#include <string>
#include <memory>
#include <thread>
#include "interfaces/IFactory.h"
#include "src/components/libheaders/SynchronizedQueue.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

// The real, physical console. Background-thread-driven so writers never block
// on stdout; optionally mirrors the Logger's messages too.
class Console : public IPhysicalConsole {
public:
    static std::shared_ptr<Console> Create(bool registerAsLogMessageReceiver = false) {
        return std::shared_ptr<Console>(new Console(registerAsLogMessageReceiver));
    }
    ~Console() override;

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    void Write(const std::string& message) override;
    void Write(const std::string& sourceName, const std::string& message) override;

    void Start() override;
    void Stop() override;

private:
    explicit Console(bool registerAsLogMessageReceiver);

    void ProcessQueue();

    SynchronizedQueue<std::string> m_queue;
    std::thread m_backgroundThread;
    int m_logReceiverToken = -1;
};

}
