#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>
#include "interfaces/IFileDescriptor.h"

namespace Haisos {

// One pipe's bounded buffer, as Linux's pipe(7): a ring of at most Capacity()
// bytes, written into through Write and read out of through Read, blocking
// while full or empty. The full blocking / end of file / broken pipe / atomic
// write / interruption rules are those of IPipeService (interfaces/IPipeService.h);
// interruption goes through the calling thread's StopToken (StopToken.h), and
// a thread carrying none is never interrupted.
//
// Shared by the pipe's two ends. Nothing here owns a thread, and no method
// waits in a destructor: CloseReadEnd/CloseWriteEnd only notify, so an end may
// be released on any thread -- a runtime thread, and
// ProcessFileIO::ReleaseAllDescriptors() included. That is why no
// DestroyOffRuntimeThreads deleter is needed anywhere in this component.
class PipeBuffer {
public:
    // openPipes is the service's count of pipes with at least one end still
    // held: this constructor increments it, the destructor decrements it.
    // Shared rather than raw so a pipe outliving its service decrements a live
    // counter.
    static std::shared_ptr<PipeBuffer> Create(size_t capacity, std::shared_ptr<std::atomic<size_t>> openPipes);
    ~PipeBuffer();

    ssize_t Read(void* buf, size_t count);
    ssize_t Write(const void* buf, size_t count);
    void CloseReadEnd();
    void CloseWriteEnd();
    size_t Capacity() const;

private:
    PipeBuffer(size_t capacity, std::shared_ptr<std::atomic<size_t>> openPipes);

    std::mutex m_mutex;
    std::condition_variable m_canRead;
    std::condition_variable m_canWrite;
    // The ring. Allocated lazily -- sized to the capacity on the first write
    // that copies bytes in, so a pipe nobody writes to costs no 64 KiB -- and
    // freed when the read end closes, since nobody can then read it.
    std::vector<char> m_storage;
    size_t m_head = 0;
    size_t m_size = 0;
    bool m_readEndOpen = true;
    bool m_writeEndOpen = true;
    size_t m_capacity;
    std::shared_ptr<std::atomic<size_t>> m_openPipes;
};

// The one and only descriptor object for its side of a pipe: Dup, Dup2 and
// passing the end to a child process all share the same shared_ptr, so
// "every read end released" is exactly "this object was destroyed" -- which is
// why PipeBuffer tracks two booleans rather than counts.
class PipeReadEnd : public IFileDescriptor {
public:
    static std::shared_ptr<PipeReadEnd> Create(std::shared_ptr<PipeBuffer> buffer);
    ~PipeReadEnd() override;
    ssize_t Read(void* buf, size_t count) override;
    ssize_t Write(const void* buf, size_t count) override; // kIOError: not open for writing
    bool IsTerminal() const override;

private:
    explicit PipeReadEnd(std::shared_ptr<PipeBuffer> buffer);
    std::shared_ptr<PipeBuffer> m_buffer;
};

class PipeWriteEnd : public IFileDescriptor {
public:
    static std::shared_ptr<PipeWriteEnd> Create(std::shared_ptr<PipeBuffer> buffer);
    ~PipeWriteEnd() override;
    ssize_t Read(void* buf, size_t count) override; // kIOError: not open for reading
    ssize_t Write(const void* buf, size_t count) override;
    bool IsTerminal() const override;

private:
    explicit PipeWriteEnd(std::shared_ptr<PipeBuffer> buffer);
    std::shared_ptr<PipeBuffer> m_buffer;
};

}
