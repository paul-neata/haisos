#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include "ProcessFileIO.h"
#include "src/components/Filesystem/FilesystemUtils.h"

using namespace Haisos;

namespace {

// A descriptor that counts its own destructions, so a test can see exactly
// when the table lets a file go. Reads end at once; writes take everything.
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

// The table works without an OS (see ProcessFileIO): an empty weak_ptr here,
// so these tests exercise the table alone.
std::shared_ptr<ProcessFileIO> MakeIO() {
    return ProcessFileIO::Create(std::weak_ptr<IHaisosOS>(), "/");
}

std::shared_ptr<std::atomic<int>> MakeCounter() {
    return std::make_shared<std::atomic<int>>(0);
}

} // namespace

TEST(HaisosOSDescriptorTableTest, AddDescriptorTakesTheLowestFreeSlot) {
    auto io = MakeIO();

    EXPECT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), 0);
    EXPECT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), 1);
    EXPECT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), 2);

    EXPECT_EQ(io->CloseDescriptor(1), 0);
    EXPECT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), 1);

    EXPECT_EQ(io->AddDescriptor(nullptr), -1);
}

TEST(HaisosOSDescriptorTableTest, GetDescriptorReturnsWhatIsThereOrNull) {
    auto io = MakeIO();
    auto placed = ReleaseCountingDescriptor::Create(MakeCounter());
    ASSERT_EQ(io->AddDescriptor(placed), 0);

    EXPECT_EQ(io->GetDescriptor(0), placed);
    EXPECT_EQ(io->GetDescriptor(1), nullptr);
    EXPECT_EQ(io->GetDescriptor(-1), nullptr);
    EXPECT_EQ(io->GetDescriptor(IFileIO::kMaxDescriptors), nullptr);
}

TEST(HaisosOSDescriptorTableTest, DupSharesTheDescriptorInTheLowestFreeSlot) {
    auto io = MakeIO();
    ASSERT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), 0);

    EXPECT_EQ(io->Dup(0), 1);
    // The same open file in both slots: one position, as dup() says.
    EXPECT_EQ(io->GetDescriptor(1), io->GetDescriptor(0));

    EXPECT_EQ(io->Dup(5), -1);
    EXPECT_EQ(io->Dup(-1), -1);
}

TEST(HaisosOSDescriptorTableTest, Dup2ReplacesAndReleasesWhatWasThere) {
    auto io = MakeIO();
    auto bReleases = MakeCounter();
    auto a = ReleaseCountingDescriptor::Create(MakeCounter());
    ASSERT_EQ(io->AddDescriptor(a), 0);
    ASSERT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(bReleases)), 1);

    // Replacing slot 1 releases B; slot 1 then holds A.
    EXPECT_EQ(io->Dup2(0, 1), 1);
    EXPECT_EQ(bReleases->load(), 1);
    EXPECT_EQ(io->GetDescriptor(1), a);

    // Same slot, holding a descriptor: nothing changes.
    EXPECT_EQ(io->Dup2(0, 0), 0);
    EXPECT_EQ(io->GetDescriptor(0), a);

    // A gap is grown through, up to the target slot.
    EXPECT_EQ(io->Dup2(0, 500), 500);
    EXPECT_EQ(io->GetDescriptor(500), a);

    // An empty oldFd changes nothing -- slot 1 still holds A.
    EXPECT_EQ(io->Dup2(7, 1), -1);
    EXPECT_EQ(io->GetDescriptor(1), a);

    // An out-of-range newFd changes nothing.
    EXPECT_EQ(io->Dup2(0, IFileIO::kMaxDescriptors), -1);
    EXPECT_EQ(io->GetDescriptor(0), a);
}

TEST(HaisosOSDescriptorTableTest, CloseDescriptorReleasesWhenTheLastSlotLetsGo) {
    auto io = MakeIO();
    auto releases = MakeCounter();
    ASSERT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(releases)), 0);
    ASSERT_EQ(io->Dup(0), 1);

    // Two slots share the one open file: the first close releases nothing.
    EXPECT_EQ(io->CloseDescriptor(0), 0);
    EXPECT_EQ(releases->load(), 0);

    // The last holder letting go closes it.
    EXPECT_EQ(io->CloseDescriptor(1), 0);
    EXPECT_EQ(releases->load(), 1);

    EXPECT_EQ(io->CloseDescriptor(1), -1);
    EXPECT_EQ(io->CloseDescriptor(-1), -1);
}

TEST(HaisosOSDescriptorTableTest, TheTableHoldsAtMostkMaxDescriptors) {
    auto io = MakeIO();
    for (int i = 0; i < IFileIO::kMaxDescriptors; ++i) {
        ASSERT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), i);
    }
    // The table being full fails a dup of a live slot just as it fails an add.
    EXPECT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), -1);
    EXPECT_EQ(io->Dup(0), -1);
}

TEST(HaisosOSDescriptorTableTest, ReleaseAllDescriptorsEmptiesTheTable) {
    auto io = MakeIO();
    std::vector<std::shared_ptr<std::atomic<int>>> counters;
    for (int i = 0; i < 3; ++i) {
        counters.push_back(MakeCounter());
        ASSERT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(counters.back())), i);
    }

    io->ReleaseAllDescriptors();

    for (const auto& counter : counters) {
        EXPECT_EQ(counter->load(), 1);
    }
    // The table is left empty and still usable.
    EXPECT_EQ(io->GetDescriptor(0), nullptr);
    EXPECT_EQ(io->GetDescriptor(1), nullptr);
    EXPECT_EQ(io->GetDescriptor(2), nullptr);
    EXPECT_EQ(io->AddDescriptor(ReleaseCountingDescriptor::Create(MakeCounter())), 0);
}

TEST(HaisosOSDescriptorTableTest, OpenFileFailsWithoutAnOS) {
    auto io = MakeIO();
    EXPECT_EQ(io->OpenFile("/x", kFileOpenReadOnly), nullptr);
    EXPECT_EQ(io->GetDescriptor(0), nullptr);
}
