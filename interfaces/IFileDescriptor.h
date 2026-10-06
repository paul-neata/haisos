#pragma once
#include <cstddef>
#include <memory>

#ifdef _WIN32
using ssize_t = std::ptrdiff_t;
#else
#include <sys/types.h>
#endif

namespace Haisos {

// What IFileDescriptor::Read and ::Write return on failure: a negative value
// telling the failures apart where telling them apart matters.
constexpr ssize_t kIOError = -1;        // any failure (EBADF, EIO, EISDIR, ...)
constexpr ssize_t kIOBrokenPipe = -2;   // a write to a pipe no one can read any more
constexpr ssize_t kIOInterrupted = -3;  // the calling process was asked to stop while blocked

// One open file, as an object: POSIX's "open file description". Every holder
// of it -- several descriptor slots, several processes -- shares the one
// position, the way duplicated descriptors do on POSIX. Releasing the last
// shared_ptr closes it; there is no Close() to call.
//
// Read and Write behave as read() and write(): the bytes transferred (at most
// count), 0 at end of file for Read, or a negative value above on failure.
// A descriptor enforces the access mode it was opened with: Write on one
// opened read-only, or Read on one opened write-only, fails with kIOError, as
// EBADF would.
//
// Only a pipe, console input or console output may block in Read or Write --
// none exist yet, so nothing here blocks. kIOBrokenPipe and kIOInterrupted are
// reserved for pipes (the pipes--pipe-service task); no filesystem descriptor
// returns them.
//
// A descriptor keeps working after its file's filesystem is unmounted, and
// after the last outside reference to that filesystem is gone, until the
// descriptor itself is released.
class IFileDescriptor {
public:
    virtual ~IFileDescriptor() = default;
    // As read(): the bytes read (at most count), 0 at end of file, or a negative IOResult.
    virtual ssize_t Read(void* buf, size_t count) = 0;
    // As write(): the bytes written (at most count), or a negative IOResult.
    virtual ssize_t Write(const void* buf, size_t count) = 0;
    // As isatty(): true only for a console descriptor.
    virtual bool IsTerminal() const = 0;
};

}
