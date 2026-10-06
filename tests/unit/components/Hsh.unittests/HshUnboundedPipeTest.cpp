#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "commands/hsh/HshUnboundedPipe.h"
#include "src/components/libheaders/StopToken.h"

namespace Haisos {

using Hsh::CreateUnboundedPipe;

namespace {

// How long a "it blocks" check waits before deciding a call has not returned;
// every such check is followed by unblocking the call, so a wrong "it never
// blocks" still fails here once it is unblocked.
constexpr int kBlockedCheckMs = 100;

// The progress of a Read/Write run on a helper thread: done once the call has
// returned. The thread is always joined before the test ends.
struct AsyncCall {
    std::atomic<bool> done{false};
    ssize_t result = kIOError;

    bool FinishedWithin(int ms) {
        for (int waited = 0; waited < ms && !done.load(); waited += 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return done.load();
    }
};

} // namespace

TEST(HshUnboundedPipeTest, WritesNeverBlock) {
    auto ends = Hsh::CreateUnboundedPipe();
    // 1 MiB in 4 KiB writes before any read: past every bounded pipe's
    // capacity, so this deadlocks a bounded pipe and passes only here.
    std::string written;
    for (int chunk = 0; chunk < 256; ++chunk) {
        const std::string bytes(4096, static_cast<char>(chunk));
        written += bytes;
        EXPECT_EQ(ends.writeEnd->Write(bytes.data(), bytes.size()),
            static_cast<ssize_t>(bytes.size()));
    }
    ends.writeEnd.reset();  // the read below ends at end of file
    std::string read;
    char buffer[65536];
    for (;;) {
        const ssize_t count = ends.readEnd->Read(buffer, sizeof(buffer));
        ASSERT_GE(count, 0);
        if (count == 0) {
            break;  // end of file: the write end released, the bytes drained
        }
        read.append(buffer, static_cast<size_t>(count));
    }
    EXPECT_EQ(read, written);
}

TEST(HshUnboundedPipeTest, ReadBlocksUntilDataOrEndOfFile) {
    auto ends = Hsh::CreateUnboundedPipe();
    AsyncCall call;
    char buf[8];
    std::thread reader([&] {
        call.result = ends.readEnd->Read(buf, sizeof(buf));
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    EXPECT_EQ(ends.writeEnd->Write("x", 1), 1);
    EXPECT_TRUE(call.FinishedWithin(5000));
    reader.join();
    EXPECT_EQ(call.result, 1);
    EXPECT_EQ(buf[0], 'x');

    // The byte read, the write end still held: the next read blocks until the
    // write end is released, then returns 0.
    AsyncCall eof;
    std::thread reader2([&] {
        eof.result = ends.readEnd->Read(buf, sizeof(buf));
        eof.done = true;
    });
    EXPECT_FALSE(eof.FinishedWithin(kBlockedCheckMs));
    ends.writeEnd.reset();
    EXPECT_TRUE(eof.FinishedWithin(5000));
    reader2.join();
    EXPECT_EQ(eof.result, 0);
}

TEST(HshUnboundedPipeTest, WriteAfterTheReaderIsGoneIsABrokenPipe) {
    auto ends = Hsh::CreateUnboundedPipe();
    ends.readEnd.reset();
    EXPECT_EQ(ends.writeEnd->Write("x", 1), kIOBrokenPipe);
    // A zero-length write succeeds even with no reader, as on Linux.
    EXPECT_EQ(ends.writeEnd->Write("", 0), 0);
}

TEST(HshUnboundedPipeTest, ABlockedReadIsInterruptedByItsStopToken) {
    auto ends = Hsh::CreateUnboundedPipe();
    auto token = StopToken::Create();
    AsyncCall call;
    char buf[8];
    std::thread reader([&] {
        StopTokenScope scope(token);
        call.result = ends.readEnd->Read(buf, sizeof(buf));
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    token->RequestStop();
    EXPECT_TRUE(call.FinishedWithin(5000));
    reader.join();
    EXPECT_EQ(call.result, kIOInterrupted);
}

TEST(HshUnboundedPipeTest, EndsWorkOneWay) {
    auto ends = Hsh::CreateUnboundedPipe();
    char buf[4];
    EXPECT_EQ(ends.writeEnd->Read(buf, sizeof(buf)), kIOError);
    EXPECT_EQ(ends.readEnd->Write("x", 1), kIOError);
    EXPECT_FALSE(ends.readEnd->IsTerminal());
    EXPECT_FALSE(ends.writeEnd->IsTerminal());
}

} // namespace Haisos
