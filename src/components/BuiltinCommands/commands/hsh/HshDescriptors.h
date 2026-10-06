#pragma once

#include <memory>

#include "interfaces/IFileDescriptor.h"

namespace Haisos::Hsh {

// What a child gets in place of an empty (closed) slot: StartProcessOptions
// takes a null stream as "use the console", which a closed descriptor must
// not become. Read and Write fail (kIOError); not a terminal.
class ClosedDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ClosedDescriptor> Create();
    ssize_t Read(void* buf, size_t count) override;         // kIOError
    ssize_t Write(const void* buf, size_t count) override;  // kIOError
    bool IsTerminal() const override;                        // false
private:
    ClosedDescriptor() = default;
};

// The standard input of a background command (dash: an asynchronous list's
// stdin is /dev/null when there is no job control): Read ends at once,
// Write takes the bytes and drops them. Not a terminal.
class NullInputDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<NullInputDescriptor> Create();
    ssize_t Read(void* buf, size_t count) override;           // 0: end of input
    ssize_t Write(const void* buf, size_t count) override;    // count: the bytes vanish
    bool IsTerminal() const override;                          // false
private:
    NullInputDescriptor() = default;
};

} // namespace Haisos::Hsh
