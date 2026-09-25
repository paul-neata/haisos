#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include "interfaces/IFactory.h"
#include "src/components/libheaders/SynchronizedQueue.h"

namespace Haisos {

// The real, physical console: program output, on a background thread so writers
// never block on stdout. It is not a log sink -- where the log goes is the
// Logger's business (LogSetConsoleOutput, LogRegisterMessageReceiver), and a
// console that quietly mirrored it would put diagnostics into the program's
// own output stream.
class Console : public IPhysicalConsole {
public:
    static std::shared_ptr<Console> Create() {
        return std::shared_ptr<Console>(new Console());
    }
    ~Console() override;

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    void Write(const std::string& message) override;
    // Reads a line from stdin. Serialized, so two readers never split one line
    // between them; which of them gets the next line is first come, first served.
    std::optional<std::string> ReadLine() override;

    void Start() override;
    void Stop() override;

private:
    Console();

    void ProcessQueue();

    SynchronizedQueue<std::string> m_queue;
    std::thread m_backgroundThread;
    std::mutex m_readMutex;
};

}
