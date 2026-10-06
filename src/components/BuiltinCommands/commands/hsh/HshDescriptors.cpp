#include "commands/hsh/HshDescriptors.h"

namespace Haisos::Hsh {

std::shared_ptr<ClosedDescriptor> ClosedDescriptor::Create() {
    return std::shared_ptr<ClosedDescriptor>(new ClosedDescriptor());
}

ssize_t ClosedDescriptor::Read(void*, size_t) {
    return kIOError;
}

ssize_t ClosedDescriptor::Write(const void*, size_t) {
    return kIOError;
}

bool ClosedDescriptor::IsTerminal() const {
    return false;
}

std::shared_ptr<NullInputDescriptor> NullInputDescriptor::Create() {
    return std::shared_ptr<NullInputDescriptor>(new NullInputDescriptor());
}

ssize_t NullInputDescriptor::Read(void*, size_t) {
    return 0;
}

ssize_t NullInputDescriptor::Write(const void*, size_t count) {
    return static_cast<ssize_t>(count);
}

bool NullInputDescriptor::IsTerminal() const {
    return false;
}

} // namespace Haisos::Hsh
