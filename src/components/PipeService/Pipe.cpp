#include "Pipe.h"
#include <algorithm>
#include <cstring>
#include "interfaces/IPipeService.h"
#include "src/components/libheaders/StopToken.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<PipeBuffer> PipeBuffer::Create(size_t capacity, std::shared_ptr<std::atomic<size_t>> openPipes) {
    return std::shared_ptr<PipeBuffer>(new PipeBuffer(capacity, std::move(openPipes)));
}

PipeBuffer::PipeBuffer(size_t capacity, std::shared_ptr<std::atomic<size_t>> openPipes)
    : m_capacity(capacity)
    , m_openPipes(std::move(openPipes))
{
    ++(*m_openPipes);
    LogVerboseDebug("PipeBuffer: created (capacity %zu)", m_capacity);
}

PipeBuffer::~PipeBuffer() {
    LogVerboseDebug("PipeBuffer: destroyed (capacity %zu)", m_capacity);
    --(*m_openPipes);
}

size_t PipeBuffer::Capacity() const {
    return m_capacity;
}

ssize_t PipeBuffer::Read(void* buf, size_t count) {
    if (count == 0) {
        return 0;
    }
    // The wake for an interruption: locks this mutex and notifies both wait
    // sets -- the lock makes a wake landing between a waiter's check and its
    // wait impossible to miss (the waiter holds the mutex from its check until
    // it is inside wait()). Constructed BEFORE m_mutex is taken below and
    // destroyed AFTER it is released, because its construction, destruction
    // and RequestStop() all take the token's mutex while the callback takes
    // this one -- the only consistent lock order.
    const std::shared_ptr<StopToken> token = StopToken::Current();
    StopCallback wake(token, [this] {
        std::lock_guard<std::mutex> wakeLock(m_mutex);
        m_canRead.notify_all();
        m_canWrite.notify_all();
    });
    bool interrupted = false;
    ssize_t result;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            if (m_size > 0) {
                // Data is returned even when a stop has been requested; a read
                // never waits for more once some is there.
                const size_t n = std::min(count, m_size);
                char* out = static_cast<char*>(buf);
                const size_t first = std::min(n, m_storage.size() - m_head);
                std::memcpy(out, m_storage.data() + m_head, first);
                if (n > first) {
                    std::memcpy(out + first, m_storage.data(), n - first);
                }
                m_head = (m_head + n) % m_storage.size();
                m_size -= n;
                m_canWrite.notify_all();
                result = static_cast<ssize_t>(n);
                break;
            }
            if (!m_writeEndOpen) {
                // End of file: the write end has been released and the buffer
                // is drained.
                result = 0;
                break;
            }
            if (token && token->StopRequested()) {
                interrupted = true;
                result = kIOInterrupted;
                break;
            }
            m_canRead.wait(lock);
        }
    }
    if (interrupted) {
        LogDebug("PipeBuffer: a blocked read was interrupted by the caller's stop token");
    }
    return result;
}

ssize_t PipeBuffer::Write(const void* buf, size_t count) {
    // As Linux: a zero-length write succeeds at once, even with no reader.
    if (count == 0) {
        return 0;
    }
    const std::shared_ptr<StopToken> token = StopToken::Current();
    // Constructed before m_mutex is taken, destroyed after it is released --
    // see Read for why.
    StopCallback wake(token, [this] {
        std::lock_guard<std::mutex> wakeLock(m_mutex);
        m_canRead.notify_all();
        m_canWrite.notify_all();
    });
    // A write of up to min(kPipeAtomicWriteSize, capacity) is atomic (POSIX
    // PIPE_BUF): copied in one piece or not at all, so two writers' small
    // writes never interleave. Capping at the capacity keeps a pipe smaller
    // than PIPE_BUF from blocking such a write forever.
    const bool atomic = count <= std::min(kPipeAtomicWriteSize, m_capacity);
    size_t written = 0;
    ssize_t result;
    bool interrupted = false;
    bool broken = false;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            if (!m_readEndOpen) {
                broken = written == 0;
                // A partial count is reported; the caller's next write (the
                // WriteAll loops) gets the negative result on that next call.
                result = written > 0 ? static_cast<ssize_t>(written) : kIOBrokenPipe;
                break;
            }
            const size_t room = m_capacity - m_size;
            const size_t need = atomic ? count : 1;
            if (room >= need) {
                // A write that does not have to wait goes through even after a
                // stop was requested -- only a call that would block is
                // interrupted (EINTR's rule). A write larger than the room
                // left is written in parts, blocking between them.
                if (m_storage.empty()) {
                    m_storage.resize(m_capacity);
                }
                const size_t n = std::min(room, count - written);
                const char* in = static_cast<const char*>(buf) + written;
                const size_t offset = (m_head + m_size) % m_storage.size();
                const size_t first = std::min(n, m_storage.size() - offset);
                std::memcpy(m_storage.data() + offset, in, first);
                if (n > first) {
                    std::memcpy(m_storage.data(), in + first, n - first);
                }
                m_size += n;
                written += n;
                m_canRead.notify_all();
                if (written == count) {
                    result = static_cast<ssize_t>(written);
                    break;
                }
                // The pipe is now full; the next pass waits.
                continue;
            }
            if (token && token->StopRequested()) {
                interrupted = written == 0;
                result = written > 0 ? static_cast<ssize_t>(written) : kIOInterrupted;
                break;
            }
            m_canWrite.wait(lock);
        }
    }
    if (interrupted) {
        LogDebug("PipeBuffer: a blocked write was interrupted by the caller's stop token");
    }
    if (broken) {
        LogDebug("PipeBuffer: a write found no reader (broken pipe)");
    }
    return result;
}

void PipeBuffer::CloseReadEnd() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_readEndOpen = false;
    // Nobody can read the buffered bytes any more: drop them and free the ring.
    m_storage.clear();
    m_storage.shrink_to_fit();
    m_head = 0;
    m_size = 0;
    LogVerboseDebug("PipeBuffer: read end closed");
    m_canWrite.notify_all();
}

void PipeBuffer::CloseWriteEnd() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_writeEndOpen = false;
    LogVerboseDebug("PipeBuffer: write end closed");
    m_canRead.notify_all();
}

// --- The ends ---------------------------------------------------------------

std::shared_ptr<PipeReadEnd> PipeReadEnd::Create(std::shared_ptr<PipeBuffer> buffer) {
    return std::shared_ptr<PipeReadEnd>(new PipeReadEnd(std::move(buffer)));
}

PipeReadEnd::PipeReadEnd(std::shared_ptr<PipeBuffer> buffer) : m_buffer(std::move(buffer)) {}

PipeReadEnd::~PipeReadEnd() {
    m_buffer->CloseReadEnd();
}

ssize_t PipeReadEnd::Read(void* buf, size_t count) {
    return m_buffer->Read(buf, count);
}

ssize_t PipeReadEnd::Write(const void*, size_t) {
    return kIOError; // not open for writing, as EBADF
}

bool PipeReadEnd::IsTerminal() const {
    return false;
}

std::shared_ptr<PipeWriteEnd> PipeWriteEnd::Create(std::shared_ptr<PipeBuffer> buffer) {
    return std::shared_ptr<PipeWriteEnd>(new PipeWriteEnd(std::move(buffer)));
}

PipeWriteEnd::PipeWriteEnd(std::shared_ptr<PipeBuffer> buffer) : m_buffer(std::move(buffer)) {}

PipeWriteEnd::~PipeWriteEnd() {
    m_buffer->CloseWriteEnd();
}

ssize_t PipeWriteEnd::Read(void*, size_t) {
    return kIOError; // not open for reading, as EBADF
}

ssize_t PipeWriteEnd::Write(const void* buf, size_t count) {
    return m_buffer->Write(buf, count);
}

bool PipeWriteEnd::IsTerminal() const {
    return false;
}

}
