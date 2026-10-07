#pragma once
#include <atomic>
#include <memory>
#include "interfaces/IFileDescriptor.h"

namespace Haisos {

// A descriptor that counts its own destructions, so a test can see exactly
// when a descriptor table lets a file go. Reads end at once; writes take
// everything.
class ReleaseCountingDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ReleaseCountingDescriptor> Create(std::shared_ptr<std::atomic<int>> releases) {
        return std::shared_ptr<ReleaseCountingDescriptor>(new ReleaseCountingDescriptor(std::move(releases)));
    }
    ~ReleaseCountingDescriptor() override { ++*m_releases; }

    ssize_t Read(void*, size_t) override { return 0; }
    ssize_t Write(const void*, size_t count) override { return static_cast<ssize_t>(count); }
    bool IsTerminal() const override { return false; }

private:
    explicit ReleaseCountingDescriptor(std::shared_ptr<std::atomic<int>> releases) : m_releases(std::move(releases)) {}

    std::shared_ptr<std::atomic<int>> m_releases;
};

} // namespace Haisos
