#pragma once

#include <memory>

#include "interfaces/IFileDescriptor.h"

namespace Haisos::Hsh {

struct UnboundedPipeEnds {
    std::shared_ptr<IFileDescriptor> readEnd;
    std::shared_ptr<IFileDescriptor> writeEnd;
};

// A pipe without a capacity: Write appends and never blocks; Read blocks
// while it is empty and its write end is held, returns 0 once the write end
// is released and the bytes are drained. Write after the read end is
// released returns kIOBrokenPipe. Used where the reader runs only after the
// writer (two pipeline stages inside the shell; $(...)), where a bounded pipe
// would deadlock. No thread anywhere and no method waits in a destructor, so
// an end may be released on any thread.
UnboundedPipeEnds CreateUnboundedPipe();

} // namespace Haisos::Hsh
