#pragma once
#include <cstddef>
#include <memory>
#include "IFileDescriptor.h"

namespace Haisos {

// Linux's default pipe capacity (pipe(7)): what CreatePipe(0) gives.
constexpr size_t kDefaultPipeCapacity = 65536;
// Writes of up to this many bytes are atomic (POSIX PIPE_BUF).
constexpr size_t kPipeAtomicWriteSize = 4096;

// The two ends of one pipe. Releasing the last shared_ptr to an end closes it.
struct PipeEnds {
    std::shared_ptr<IFileDescriptor> readEnd;
    std::shared_ptr<IFileDescriptor> writeEnd;
};

// Makes unnamed pipes: a bounded, blocking, unidirectional byte channel, as
// Linux's pipe(7), between whoever holds its two ends -- typically a process's
// stdout fed into another's stdin through StartProcessOptions. Both ends are
// ordinary IFileDescriptors; each is closed when its last shared_ptr is let go
// (several descriptor slots and several processes may hold the same end, as
// duplicated descriptors do on POSIX).
//
// The pipe's rules:
//   * bounded: Write blocks while the pipe is full, Read blocks while it is
//     empty;
//   * Read returns 0 (end of file) once the write end has been released and
//     the buffered bytes drained; Write returns kIOBrokenPipe once the read
//     end has been released;
//   * a write of up to kPipeAtomicWriteSize bytes (capped at the capacity) is
//     atomic: never interleaved with another writer's bytes; a larger write is
//     written in parts and may interleave;
//   * a Read or Write blocked on a pipe returns kIOInterrupted when the
//     process whose runtime thread is blocked is asked to stop (TriggerStop;
//     for an agent also self_close) -- the wait is woken by the calling
//     thread's stop token (StopToken.h), not polled. A thread carrying no stop
//     token (the haisos main thread, a test's thread) is never interrupted.
//
// capacity 0 means kDefaultPipeCapacity; any other value is taken as given (a
// heredoc asks for the size of its text). CreatePipe never fails: both ends
// are always non-null.
//
// OpenPipeCount() is the number of pipes this service created that still have
// at least one end held -- for tests and diagnostics.
//
// A pipe does not depend on the service that made it: it keeps working after
// the service is gone, and its ends may be handed to a process of another OS
// (a sub-OS).
class IPipeService {
public:
    virtual ~IPipeService() = default;
    virtual PipeEnds CreatePipe(size_t capacity = 0) = 0;
    virtual size_t OpenPipeCount() const = 0;
};

}
