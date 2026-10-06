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

} // namespace Haisos::Hsh
