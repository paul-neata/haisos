#include "commands/hsh/HshUnboundedPipe.h"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

#include "src/components/libheaders/StopToken.h"

namespace Haisos::Hsh {

namespace {

// One unbounded pipe's byte queue and wait set, shared by its two ends, as
// PipeService's PipeBuffer without the capacity. Each end object is the one
// and only descriptor object for its side, so the buffer tracks two booleans,
// not counts.
class UnboundedPipeBuffer {
public:
    static std::shared_ptr<UnboundedPipeBuffer> Create() {
        return std::shared_ptr<UnboundedPipeBuffer>(new UnboundedPipeBuffer());
    }

    ssize_t Read(void* buf, size_t count) {
        if (count == 0) {
            return 0;
        }
        // The wake for an interruption, constructed BEFORE m_mutex is taken
        // and destroyed AFTER it is released -- the only consistent lock
        // order (see PipeBuffer::Read in the PipeService component).
        const std::shared_ptr<StopToken> token = StopToken::Current();
        StopCallback wake(token, [this] {
            std::lock_guard<std::mutex> wakeLock(m_mutex);
            m_canRead.notify_all();
        });
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            if (m_bytes.size() > m_readOffset) {
                // Data is returned even when a stop has been requested; a read
                // never waits for more once some is there.
                const size_t n = std::min(count, m_bytes.size() - m_readOffset);
                std::memcpy(buf, m_bytes.data() + m_readOffset, n);
                m_readOffset += n;
                if (m_readOffset == m_bytes.size()) {
                    // Drained: free the whole buffer.
                    m_bytes.clear();
                    m_readOffset = 0;
                } else if (m_readOffset >= kCompactionThreshold &&
                           m_readOffset > m_bytes.size() / 2) {
                    // The consumed prefix dominates: drop it once, so draining
                    // many bytes stays linear instead of quadratic in erases.
                    m_bytes.erase(0, m_readOffset);
                    m_readOffset = 0;
                }
                return static_cast<ssize_t>(n);
            }
            if (!m_writeEndOpen) {
                // End of file: the write end has been released and the bytes
                // are drained.
                return 0;
            }
            if (token && token->StopRequested()) {
                return kIOInterrupted;
            }
            m_canRead.wait(lock);
        }
    }

    ssize_t Write(const void* buf, size_t count) {
        // As Linux: a zero-length write succeeds at once, even with no reader.
        if (count == 0) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_readEndOpen) {
            return kIOBrokenPipe;
        }
        m_bytes.append(static_cast<const char*>(buf), count);
        m_canRead.notify_all();
        return static_cast<ssize_t>(count);
    }

    void CloseReadEnd() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_readEndOpen = false;
        // Nobody can read them any more: drop the bytes.
        std::string().swap(m_bytes);
        m_readOffset = 0;
    }

    void CloseWriteEnd() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writeEndOpen = false;
        m_canRead.notify_all();
    }

private:
    UnboundedPipeBuffer() = default;

    // Read past this many consumed bytes, with the prefix over half of the
    // buffer, and the prefix is erased once (a read below it only moves
    // m_readOffset, an O(1) memmove-free operation).
    static constexpr size_t kCompactionThreshold = 64 * 1024;

    std::mutex m_mutex;
    std::condition_variable m_canRead;
    std::string m_bytes;
    size_t m_readOffset = 0;  // into m_bytes; bytes before it are consumed
    bool m_readEndOpen = true;
    bool m_writeEndOpen = true;
};

class UnboundedReadEnd : public IFileDescriptor {
public:
    static std::shared_ptr<UnboundedReadEnd> Create(std::shared_ptr<UnboundedPipeBuffer> buffer) {
        return std::shared_ptr<UnboundedReadEnd>(new UnboundedReadEnd(std::move(buffer)));
    }
    ~UnboundedReadEnd() override { m_buffer->CloseReadEnd(); }
    ssize_t Read(void* buf, size_t count) override { return m_buffer->Read(buf, count); }
    ssize_t Write(const void*, size_t) override { return kIOError; }  // not open for writing, as EBADF
    bool IsTerminal() const override { return false; }

private:
    explicit UnboundedReadEnd(std::shared_ptr<UnboundedPipeBuffer> buffer) : m_buffer(std::move(buffer)) {}
    std::shared_ptr<UnboundedPipeBuffer> m_buffer;
};

class UnboundedWriteEnd : public IFileDescriptor {
public:
    static std::shared_ptr<UnboundedWriteEnd> Create(std::shared_ptr<UnboundedPipeBuffer> buffer) {
        return std::shared_ptr<UnboundedWriteEnd>(new UnboundedWriteEnd(std::move(buffer)));
    }
    ~UnboundedWriteEnd() override { m_buffer->CloseWriteEnd(); }
    ssize_t Read(void*, size_t) override { return kIOError; }  // not open for reading, as EBADF
    ssize_t Write(const void* buf, size_t count) override { return m_buffer->Write(buf, count); }
    bool IsTerminal() const override { return false; }

private:
    explicit UnboundedWriteEnd(std::shared_ptr<UnboundedPipeBuffer> buffer) : m_buffer(std::move(buffer)) {}
    std::shared_ptr<UnboundedPipeBuffer> m_buffer;
};

} // namespace

UnboundedPipeEnds CreateUnboundedPipe() {
    const std::shared_ptr<UnboundedPipeBuffer> buffer = UnboundedPipeBuffer::Create();
    return {UnboundedReadEnd::Create(buffer), UnboundedWriteEnd::Create(buffer)};
}

} // namespace Haisos::Hsh
