#include "ProcessAgentConsole.h"
#include "interfaces/IFileIO.h"

namespace Haisos {

std::shared_ptr<ProcessAgentConsole> ProcessAgentConsole::Create(std::shared_ptr<CurrentProcessHandle> process) {
    return std::shared_ptr<ProcessAgentConsole>(new ProcessAgentConsole(std::move(process)));
}

ProcessAgentConsole::ProcessAgentConsole(std::shared_ptr<CurrentProcessHandle> process)
    : m_process(std::move(process))
{
}

void ProcessAgentConsole::Write(const std::string& message) {
    WriteLineToDescriptor(IFileIO::kStdOut, message);
}

void ProcessAgentConsole::WriteError(const std::string& message) {
    WriteLineToDescriptor(IFileIO::kStdErr, message);
}

void ProcessAgentConsole::WriteLineToDescriptor(int fd, const std::string& message) {
    if (m_brokenPipe) {
        // The agent is already dying quietly for an earlier broken pipe:
        // nothing more is written.
        return;
    }
    // This shared_ptr may be the last reference to the process, on the agent's
    // own thread: safe because AgentProcess uses the DestroyOffRuntimeThreads
    // deleter and the agent's thread runs in a RuntimeThreadScope.
    auto process = m_process->Get();
    if (!process) {
        // The process is gone; there is nothing to write to.
        return;
    }
    auto io = process->IO();
    auto descriptor = io ? io->GetDescriptor(fd) : nullptr;
    if (!descriptor) {
        // The slot is empty (the table was released, or the descriptor
        // replaced): the line is dropped, not redirected anywhere else.
        return;
    }

    const std::string bytes = message + "\n";
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = descriptor->Write(bytes.data() + written, bytes.size() - written);
        if (n == kIOBrokenPipe) {
            // The pipe's reader is gone: the agent stops quietly, exit code
            // 141, as a program stopped by SIGPIPE does. Releasing this
            // shared_ptr to the process here is safe for the reason above.
            m_brokenPipe = true;
            process->StopForBrokenPipe();
            return;
        }
        if (n < 0) {
            return;
        }
        written += static_cast<size_t>(n);
    }
}

}
