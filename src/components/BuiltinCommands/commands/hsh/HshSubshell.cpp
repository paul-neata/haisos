#include "commands/hsh/HshSubshell.h"

#include "commands/hsh/HshRedirection.h"

namespace Haisos::Hsh {

SubshellScope::SubshellScope(Shell& shell)
    : m_shell(shell),
      m_state(shell.State()),
      m_workingDirectory(shell.IO().GetCurrentDirectory()),
      m_jobs(shell.m_jobs) {
    m_slots.reserve(IFileIO::kMaxDescriptors);
    for (int fd = 0; fd < IFileIO::kMaxDescriptors; ++fd) {
        m_slots.push_back(shell.IO().GetDescriptor(fd));
    }
    ++shell.m_subshellDepth;
}

SubshellScope::~SubshellScope() {
    --m_shell.m_subshellDepth;
    m_shell.State() = m_state;
    m_shell.m_jobs = std::move(m_jobs);
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
