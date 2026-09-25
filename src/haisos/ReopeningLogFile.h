#pragma once
#include <fstream>
#include <mutex>
#include <string>

namespace Haisos {

// A log file that comes back if it is deleted while Haisos is writing to it.
//
// An open file keeps working after its name is removed (on POSIX, deleting a
// file only unlinks the name; the open descriptor goes on writing into a file
// nothing can reach any more), so a plain std::ofstream would carry on logging
// into nowhere. Before each write this checks that the path still exists, and
// if it does not, opens it again -- which creates it -- and goes on there.
// Only a missing path is noticed: a path that now names some other file (as
// after a log rotation that renames the file and creates a new one) is not.
//
// Thread-safe: log receivers run on whichever thread logged.
class ReopeningLogFile {
public:
    // truncate empties an existing file on the first open (a fresh log);
    // otherwise it is appended to. A re-created file is always new.
    ReopeningLogFile(std::string path, bool truncate);

    ReopeningLogFile(const ReopeningLogFile&) = delete;
    ReopeningLogFile& operator=(const ReopeningLogFile&) = delete;

    const std::string& Path() const { return m_path; }

    // Whether the file could be opened (at construction, or since).
    bool IsOpen() const;

    // Re-creates the file if its path is gone. Returns true if it did, so a
    // caller whose entries refer back to earlier ones can start afresh.
    bool EnsureOpen();

    // Writes text and flushes it, re-creating the file first if need be.
    void Write(const std::string& text);

private:
    // Must be called with m_mutex held.
    bool EnsureOpenLocked();

    mutable std::mutex m_mutex;
    std::string m_path;
    std::ofstream m_out;
};

} // namespace Haisos
