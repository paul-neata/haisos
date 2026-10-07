#include "commands/hsh/HshSubshell.h"

#include <algorithm>

#include "commands/hsh/HshRedirection.h"
#include "interfaces/IProcess.h"

namespace Haisos::Hsh {

SubshellScope::SubshellScope(Shell& shell)
    : m_shell(shell),
      m_state(shell.State()),
      m_workingDirectory(shell.IO().GetCurrentDirectory()),
      m_jobs(shell.m_jobs),
      m_functions(shell.m_functions),
      m_loopDepth(shell.m_loopDepth),
      m_functionDepth(shell.m_functionDepth),
      m_dotDepth(shell.m_dotDepth),
      m_errexitSuppressed(shell.m_errexitSuppressed) {
    m_slots.reserve(IFileIO::kMaxDescriptors);
    for (int fd = 0; fd < IFileIO::kMaxDescriptors; ++fd) {
        m_slots.push_back(shell.IO().GetDescriptor(fd));
    }
    ++shell.m_subshellDepth;
}

SubshellScope::~SubshellScope() {
    --m_shell.m_subshellDepth;
    m_shell.State() = m_state;
    // Jobs the subshell added are about to be dropped with its job list;
    // their processes leave m_liveChildren with them -- else a
    // `( true & )` would leak one live child per run, and `wait` could
    // never reach them anyway.
    for (Shell::Job& job : m_shell.m_jobs) {
        const auto saved = std::find_if(m_jobs.begin(), m_jobs.end(),
            [&](const Shell::Job& kept) { return kept.pid == job.pid; });
        if (saved != m_jobs.end()) {
            continue;
        }
        for (const std::shared_ptr<IProcess>& process : job.processes) {
            m_shell.ForgetLiveChild(process);
        }
    }
    m_shell.m_jobs = std::move(m_jobs);
    m_shell.m_functions = std::move(m_functions);
    m_shell.m_loopDepth = m_loopDepth;
    m_shell.m_functionDepth = m_functionDepth;
    m_shell.m_dotDepth = m_dotDepth;
    m_shell.m_errexitSuppressed = m_errexitSuppressed;
    IFileIO& io = m_shell.IO();
    // The working directory first, then every slot whose content the subshell
    // changed. A failure to put a slot back leaves it as it is -- restoring
    // never writes and never throws.
    io.ChangeDirectory(m_workingDirectory);
    for (int fd = 0; fd < IFileIO::kMaxDescriptors; ++fd) {
        if (io.GetDescriptor(fd) != m_slots[fd]) {
            PlaceDescriptor(io, fd, m_slots[fd]);
        }
    }
}

} // namespace Haisos::Hsh
