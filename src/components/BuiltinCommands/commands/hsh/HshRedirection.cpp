#include "commands/hsh/HshRedirection.h"

#include <algorithm>

#include "src/components/Filesystem/FilesystemUtils.h"
#include "commands/hsh/HshExpansion.h"
#include "commands/hsh/HshShell.h"

namespace Haisos::Hsh {

bool PlaceDescriptor(IFileIO& io, int fd, std::shared_ptr<IFileDescriptor> descriptor) {
    if (!descriptor) {
        io.CloseDescriptor(fd);
        return true;
    }
    const int slot = io.AddDescriptor(descriptor);
    if (slot < 0) {
        return false;  // the table is full
    }
    if (slot == fd) {
        return true;
    }
    const bool placed = io.Dup2(slot, fd) == fd;
    io.CloseDescriptor(slot);
    return placed;
}

RedirectionScope::RedirectionScope(Shell& shell) : m_shell(shell) {}

RedirectionScope::~RedirectionScope() {
    Restore();
}

std::optional<std::string> RedirectionScope::Apply(const std::vector<Redirection>& redirections) {
    const size_t first = m_saved.size();
    m_applyFirst = first;
    for (const Redirection& redirection : redirections) {
        if (std::optional<std::string> error = ApplyOne(redirection)) {
            RestoreFrom(first);
            return error;
        }
    }
    return std::nullopt;
}

void RedirectionScope::Restore() {
    RestoreFrom(0);
}

void RedirectionScope::RestoreFrom(size_t first) {
    while (m_saved.size() > first) {
        // Latest change undone first; a failure here leaves the slot as it is.
        PlaceDescriptor(m_shell.IO(), m_saved.back().first, m_saved.back().second);
        m_saved.pop_back();
    }
}

void RedirectionScope::Keep() {
    m_saved.clear();
}

void RedirectionScope::Save(int fd) {
    // De-duplicated within the current Apply only: a slot saved in an earlier
    // Apply of this scope is saved again (Restore is LIFO, so a slot saved
    // twice ends at its first saved value).
    const auto first = m_saved.begin() + static_cast<std::ptrdiff_t>(m_applyFirst);
    const auto it = std::find_if(first, m_saved.end(),
        [fd](const std::pair<int, std::shared_ptr<IFileDescriptor>>& saved) { return saved.first == fd; });
    if (it == m_saved.end()) {
        m_saved.emplace_back(fd, m_shell.IO().GetDescriptor(fd));
    }
}

std::optional<std::string> RedirectionScope::OpenAndPlace(int fd, const std::string& target,
                                                          int flags, bool clobberCheck) {
    IFileIO& io = m_shell.IO();
    if (clobberCheck && m_shell.State().options.noclobber) {
        // dash's noclobber refusals reach only a regular file: an existing
        // device (such as /dev/null) or directory is spared it.
        FileStatus status;
        if (io.Stat(target, status) == 0 && status.type == DirectoryEntryType::File) {
            return "cannot create " + target + ": File exists";
        }
    }
    auto descriptor = io.OpenFile(target, flags, kFileCreateMode);
    if (!descriptor) {
        return "cannot create " + target + ": " + OpenFailureReason(io, target, true);
    }
    Save(fd);
    if (!PlaceDescriptor(io, fd, std::move(descriptor))) {
        return std::to_string(fd) + ": Too many open files";
    }
    return std::nullopt;
}

std::optional<std::string> RedirectionScope::PipeInput(int fd, const std::string& text) {
    IFileIO& io = m_shell.IO();
    // Save before CreatePipe, which takes the two lowest free slots -- slot
    // |fd| may be one of them.
    Save(fd);
    // Sized exactly to the text, so the write below, into an empty pipe, never
    // blocks and needs no thread.
    auto slots = io.CreatePipe(std::max<size_t>(text.size(), 1));
    if (!slots) {
        return "Pipe call failed";  // dash's wording
    }
    auto readEnd = io.GetDescriptor(slots->first);
    auto writeEnd = io.GetDescriptor(slots->second);
    io.CloseDescriptor(slots->first);
    io.CloseDescriptor(slots->second);
    size_t written = 0;
    while (written < text.size()) {
        const ssize_t result = writeEnd->Write(text.data() + written, text.size() - written);
        if (result < 0) {
            break;  // kIOBrokenPipe cannot happen: nobody has the read end yet
        }
        written += static_cast<size_t>(result);
    }
    writeEnd.reset();  // the reader sees the text, then end of file
    if (!PlaceDescriptor(io, fd, std::move(readEnd))) {
        return std::to_string(fd) + ": Too many open files";
    }
    return std::nullopt;
}

std::optional<std::string> RedirectionScope::ApplyOne(const Redirection& redirection) {
    IFileIO& io = m_shell.IO();
    const int n = redirection.fd;  // the kind's default when no IO number was given
    switch (redirection.kind) {
    case RedirectionKind::Input: {
        const std::string target = m_shell.Expansion().ExpandToString(redirection.target);
        auto descriptor = io.OpenFile(target, kFileOpenReadOnly);
        if (!descriptor) {
            return "cannot open " + target + ": " + OpenFailureReason(io, target, false);
        }
        Save(n);
        if (!PlaceDescriptor(io, n, std::move(descriptor))) {
            return std::to_string(n) + ": Too many open files";
        }
        return std::nullopt;
    }
    case RedirectionKind::Output:
    case RedirectionKind::OutputAndError: {
        const std::string target = m_shell.Expansion().ExpandToString(redirection.target);
        if (std::optional<std::string> error =
                OpenAndPlace(n, target, kFileOpenWriteCreateTruncate, true)) {
            return error;
        }
        if (redirection.kind == RedirectionKind::OutputAndError) {
            Save(IFileIO::kStdErr);
            io.Dup2(n, IFileIO::kStdErr);  // n is 1: &> is stdout and stderr
        }
        return std::nullopt;
    }
    case RedirectionKind::OutputClobber: {  // >|: overrides noclobber
        const std::string target = m_shell.Expansion().ExpandToString(redirection.target);
        return OpenAndPlace(n, target, kFileOpenWriteCreateTruncate, false);
    }
    case RedirectionKind::Append: {
        const std::string target = m_shell.Expansion().ExpandToString(redirection.target);
        return OpenAndPlace(n, target, kFileOpenWriteCreateAppend, false);
    }
    case RedirectionKind::ReadWrite: {
        const std::string target = m_shell.Expansion().ExpandToString(redirection.target);
        return OpenAndPlace(n, target, kFileOpenReadWriteCreate, false);
    }
    case RedirectionKind::DupInput:
    case RedirectionKind::DupOutput: {  // the same handling, as dash
        const std::string target = m_shell.Expansion().ExpandToString(redirection.target);
        if (target == "-") {
            Save(n);
            io.CloseDescriptor(n);  // closing an empty slot is fine (exec 9>&-)
            return std::nullopt;
        }
        // "-" aside, the target must be exactly one digit, as in dash --
        // so " 3" or "+1" is no fd, and neither is "3x".
        if (target.size() != 1 || target[0] < '0' || target[0] > '9') {
            m_shell.Fail("Syntax error: Bad fd number");  // dash exits
        }
        const int source = target[0] - '0';
        if (!io.GetDescriptor(source)) {
            return std::to_string(source) + ": Bad file descriptor";
        }
        if (source == n) {
            return std::nullopt;  // a slot duped onto itself: nothing
        }
        Save(n);
        io.Dup2(source, n);
        return std::nullopt;
    }
    case RedirectionKind::HereDoc:
        return PipeInput(n, m_shell.Expansion().ExpandHereDocument(*redirection.hereDoc));
    case RedirectionKind::HereString:
        return PipeInput(n, m_shell.Expansion().ExpandToString(redirection.target) + "\n");
    }
    return std::nullopt;
}

} // namespace Haisos::Hsh
