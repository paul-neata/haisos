#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include "interfaces/IFactory.h"
#include "src/components/libheaders/SynchronizedQueue.h"

namespace Haisos {

// The real, physical console: raw program output to the host's stdout and
// stderr, on a background thread so writers never block on the terminal. The
// bytes given are written exactly as they are -- no newline is added, nothing
// is held back, and the stream is flushed after every write, so a prompt
// without a newline shows at once. It is not a log sink -- where the log goes
// is the Logger's business (LogSetConsoleOutput, LogRegisterMessageReceiver),
// and a console that quietly mirrored it would put diagnostics into the
// program's own output stream.
class Console : public IPhysicalConsole {
public:
    static std::shared_ptr<Console> Create() {
        return std::shared_ptr<Console>(new Console());
    }
    ~Console() override;

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    void Write(const std::string& bytes) override;
    void WriteError(const std::string& bytes) override;
    // Reads a line from stdin. Serialized, so two readers never split one line
    // between them; which of them gets the next line is first come, first served.
    std::optional<std::string> ReadLine() override;

    void Start() override;
    void Stop() override;

private:
    Console();

    void ProcessQueue();

    // One queued write: the stream it goes to, and the bytes as given.
    struct ConsoleOutput {
        bool toError;
        std::string bytes;
    };

    // One queue for both streams, so stdout and stderr writes keep their
    // relative order.
    SynchronizedQueue<ConsoleOutput> m_queue;
    std::thread m_backgroundThread;
    std::mutex m_readMutex;
};

}
