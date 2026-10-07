#pragma once
#include <atomic>
#include <memory>
#include <string>
#include "interfaces/ILLMService.h"
#include "src/components/libheaders/CurrentProcessHandle.h"

namespace Haisos {

// An agent process's IAgentConsole: what the agent says goes to its process's
// stdout, its diagnostics to its stderr -- through the process's own IFileIO,
// looked up at every write, so a process whose descriptors were replaced or
// released writes wherever its table says (or nowhere).
//
// The console exists before the process (the agent is created first), which is
// why it goes through the same CurrentProcessHandle the OS tools use: the
// handle is filled in by AgentProcess::Create before the agent is given its
// program, so no write can find it empty.
class ProcessAgentConsole : public IAgentConsole {
public:
    static std::shared_ptr<ProcessAgentConsole> Create(std::shared_ptr<CurrentProcessHandle> process);

    void Write(const std::string& message) override;
    void WriteError(const std::string& message) override;

private:
    explicit ProcessAgentConsole(std::shared_ptr<CurrentProcessHandle> process);

    // message + "\n" to the process's descriptor fd, wherever it points now.
    // A write the pipe's reader is gone for (kIOBrokenPipe) stops the process
    // quietly with exit code 141, as SIGPIPE would -- stderr's pipe as well as
    // stdout's -- and every line after is dropped. What the agent keeps in its
    // history and message buffer is unchanged either way: this console only
    // decides where the bytes go.
    void WriteLineToDescriptor(int fd, const std::string& message);

    std::shared_ptr<CurrentProcessHandle> m_process;
    // Set once a write has hit a broken pipe and the process was told to stop
    // for it: the agent is dying quietly, so nothing more is written.
    std::atomic<bool> m_brokenPipe{false};
};

}
