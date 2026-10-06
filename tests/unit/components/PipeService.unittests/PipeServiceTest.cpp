#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "PipeService.h"
#include "src/components/libheaders/StopToken.h"

using namespace Haisos;

namespace {

// How long a "it blocks" check waits before deciding a call has not returned;
// every such check is followed by unblocking the call, never by an unbounded
// wait.
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

class PipeServiceTest : public ::testing::Test {
protected:
    std::shared_ptr<IPipeService> m_service = PipeService::Create();
};

TEST_F(PipeServiceTest, BytesComeOutInTheOrderTheyWentIn) {
    auto ends = m_service->CreatePipe();
    EXPECT_EQ(ends.writeEnd->Write("ab", 2), 2);
    EXPECT_EQ(ends.writeEnd->Write("cd", 2), 2);
    EXPECT_EQ(ends.writeEnd->Write("ef", 2), 2);

    char buf[8] = {};
    // A read returns what is there, short.
    EXPECT_EQ(ends.readEnd->Read(buf, 4), 4);
    EXPECT_EQ(std::string(buf, 4), "abcd");
    std::memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ends.readEnd->Read(buf, 4), 2);
    EXPECT_EQ(std::string(buf, 2), "ef");
}

TEST_F(PipeServiceTest, ReadBlocksWhileEmpty) {
    auto ends = m_service->CreatePipe();
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
}

TEST_F(PipeServiceTest, WriteBlocksWhileFullAndResumesAsTheReaderDrains) {
    auto ends = m_service->CreatePipe(8);
    EXPECT_EQ(ends.writeEnd->Write("aaaaaaaa", 8), 8);

    AsyncCall call;
    std::thread writer([&] {
        call.result = ends.writeEnd->Write("bbbb", 4);
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    char buf[16] = {};
    EXPECT_EQ(ends.readEnd->Read(buf, 4), 4);
    EXPECT_TRUE(call.FinishedWithin(5000));
    writer.join();
    EXPECT_EQ(call.result, 4);

    std::string all(buf, 4);
    while (all.size() < 12) {
        const ssize_t n = ends.readEnd->Read(buf, 12 - all.size());
        ASSERT_GT(n, 0);
        all.append(buf, static_cast<size_t>(n));
    }
    EXPECT_EQ(all, "aaaaaaaabbbb");
}

TEST_F(PipeServiceTest, ALargeWriteGoesInPartsAndBlocksBetweenThem) {
    auto ends = m_service->CreatePipe(16);
    std::string payload;
    for (int i = 0; i < 100; ++i) {
        payload.push_back(static_cast<char>(i));
    }
    AsyncCall call;
    std::thread writer([&] {
        call.result = ends.writeEnd->Write(payload.data(), payload.size());
        call.done = true;
    });
    std::string received;
    char buf[64];
    while (received.size() < payload.size()) {
        const ssize_t n = ends.readEnd->Read(buf, sizeof(buf));
        ASSERT_GT(n, 0);
        received.append(buf, static_cast<size_t>(n));
    }
    EXPECT_TRUE(call.FinishedWithin(5000));
    writer.join();
    EXPECT_EQ(call.result, 100);
    EXPECT_EQ(received, payload);
}

TEST_F(PipeServiceTest, ReadReturnsEndOfFileOnceTheWriteEndIsReleasedAndDrained) {
    auto ends = m_service->CreatePipe();
    EXPECT_EQ(ends.writeEnd->Write("hi", 2), 2);
    ends.writeEnd.reset();

    char buf[8] = {};
    EXPECT_EQ(ends.readEnd->Read(buf, sizeof(buf)), 2);
    EXPECT_EQ(std::string(buf, 2), "hi");
    EXPECT_EQ(ends.readEnd->Read(buf, sizeof(buf)), 0);
    EXPECT_EQ(ends.readEnd->Read(buf, sizeof(buf)), 0);
}

TEST_F(PipeServiceTest, ReleasingTheWriteEndWakesABlockedReader) {
    auto ends = m_service->CreatePipe();
    AsyncCall call;
    char buf[8];
    std::thread reader([&] {
        call.result = ends.readEnd->Read(buf, sizeof(buf));
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    ends.writeEnd.reset();
    EXPECT_TRUE(call.FinishedWithin(5000));
    reader.join();
    EXPECT_EQ(call.result, 0);
}

TEST_F(PipeServiceTest, WriteFailsWithBrokenPipeOnceTheReadEndIsReleased) {
    auto ends = m_service->CreatePipe();
    ends.readEnd.reset();
    EXPECT_EQ(ends.writeEnd->Write("x", 1), kIOBrokenPipe);
    // As Linux: a zero-length write succeeds even with no reader.
    EXPECT_EQ(ends.writeEnd->Write("x", 0), 0);
}

TEST_F(PipeServiceTest, ReleasingTheReadEndWakesABlockedWriter) {
    auto ends = m_service->CreatePipe(4);
    EXPECT_EQ(ends.writeEnd->Write("aaaa", 4), 4);
    AsyncCall call;
    std::thread writer([&] {
        call.result = ends.writeEnd->Write("bbbb", 4);
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    ends.readEnd.reset();
    EXPECT_TRUE(call.FinishedWithin(5000));
    writer.join();
    EXPECT_EQ(call.result, kIOBrokenPipe);
}

TEST_F(PipeServiceTest, SmallWritesFromTwoWritersAreNeverInterleaved) {
    auto ends = m_service->CreatePipe(4096);
    constexpr int kWriters = 2;
    constexpr int kChunksPerWriter = 500;
    const char kLetters[kWriters] = {'A', 'B'};
    const std::string chunks[kWriters] = {
        std::string(kPipeAtomicWriteSize, kLetters[0]),
        std::string(kPipeAtomicWriteSize, kLetters[1]),
    };
    std::vector<std::thread> writers;
    writers.reserve(kWriters);
    for (int w = 0; w < kWriters; ++w) {
        writers.emplace_back([&, w] {
            for (int i = 0; i < kChunksPerWriter; ++i) {
                const ssize_t n = ends.writeEnd->Write(chunks[w].data(), chunks[w].size());
                EXPECT_EQ(n, static_cast<ssize_t>(chunks[w].size()));
            }
        });
    }

    // Read everything: while the writers run, then until end of file.
    std::string all;
    all.reserve(kWriters * kChunksPerWriter * kPipeAtomicWriteSize);
    char buf[65536];
    for (;;) {
        // Each read takes what the pipe has, so no read waits once the writers
        // are done and the write end released.
        const ssize_t n = ends.readEnd->Read(buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        ASSERT_GT(n, 0);
        all.append(buf, static_cast<size_t>(n));
        if (all.size() == kWriters * kChunksPerWriter * kPipeAtomicWriteSize) {
            break;
        }
    }
    for (auto& writer : writers) {
        writer.join();
    }
    ends.writeEnd.reset();
    // Anything already read is all of it; the now-released end ends the stream.
    EXPECT_EQ(ends.readEnd->Read(buf, sizeof(buf)), 0);

    ASSERT_EQ(all.size(), kWriters * kChunksPerWriter * kPipeAtomicWriteSize);
    int counts[kWriters] = {0, 0};
    for (size_t offset = 0; offset < all.size(); offset += kPipeAtomicWriteSize) {
        const char letter = all[offset];
        ASSERT_TRUE(letter == kLetters[0] || letter == kLetters[1]) << "bad block at offset " << offset;
        for (size_t i = offset; i < offset + kPipeAtomicWriteSize; ++i) {
            ASSERT_EQ(all[i], letter) << "interleaved write at offset " << offset;
        }
        ++counts[letter == kLetters[0] ? 0 : 1];
    }
    EXPECT_EQ(counts[0], kChunksPerWriter);
    EXPECT_EQ(counts[1], kChunksPerWriter);
}

TEST_F(PipeServiceTest, CapacityZeroMeansTheDefault) {
    auto ends = m_service->CreatePipe();
    const std::string full(kDefaultPipeCapacity, 'z');
    // The whole default capacity goes in without blocking.
    EXPECT_EQ(ends.writeEnd->Write(full.data(), full.size()), static_cast<ssize_t>(full.size()));

    AsyncCall call;
    std::thread writer([&] {
        call.result = ends.writeEnd->Write("!", 1);
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    char one;
    EXPECT_EQ(ends.readEnd->Read(&one, 1), 1);
    EXPECT_TRUE(call.FinishedWithin(5000));
    writer.join();
    EXPECT_EQ(call.result, 1);
}

TEST_F(PipeServiceTest, TheCapacityAskedForIsTheCapacity) {
    auto ends = m_service->CreatePipe(10);
    const std::string ten("0123456789");
    EXPECT_EQ(ends.writeEnd->Write(ten.data(), ten.size()), 10);

    AsyncCall call;
    std::thread writer([&] {
        call.result = ends.writeEnd->Write("x", 1);
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    char one;
    EXPECT_EQ(ends.readEnd->Read(&one, 1), 1);
    EXPECT_TRUE(call.FinishedWithin(5000));
    writer.join();
    EXPECT_EQ(call.result, 1);

    // A write larger than the capacity completes while the reader drains: the
    // atomic size is capped at the capacity, so the parts go in as room frees.
    auto pipe = m_service->CreatePipe(10);
    const std::string twenty("abcdefghijklmnopqrst");
    AsyncCall bigCall;
    std::thread bigWriter([&] {
        bigCall.result = pipe.writeEnd->Write(twenty.data(), twenty.size());
        bigCall.done = true;
    });
    std::string received;
    char buf[32];
    while (received.size() < twenty.size()) {
        const ssize_t n = pipe.readEnd->Read(buf, sizeof(buf));
        ASSERT_GT(n, 0);
        received.append(buf, static_cast<size_t>(n));
    }
    EXPECT_TRUE(bigCall.FinishedWithin(5000));
    bigWriter.join();
    EXPECT_EQ(bigCall.result, 20);
    EXPECT_EQ(received, twenty);
}

TEST_F(PipeServiceTest, ABlockedReadIsInterruptedByItsThreadsStopToken) {
    auto ends = m_service->CreatePipe();
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
    EXPECT_TRUE(call.FinishedWithin(1000));
    reader.join();
    EXPECT_EQ(call.result, kIOInterrupted);
}

TEST_F(PipeServiceTest, ABlockedWriteIsInterruptedByItsThreadsStopToken) {
    auto ends = m_service->CreatePipe(4);
    EXPECT_EQ(ends.writeEnd->Write("aaaa", 4), 4);

    // Nothing yet written: kIOInterrupted.
    auto token = StopToken::Create();
    AsyncCall call;
    std::thread writer([&] {
        StopTokenScope scope(token);
        call.result = ends.writeEnd->Write("bbbb", 4);
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    token->RequestStop();
    EXPECT_TRUE(call.FinishedWithin(1000));
    writer.join();
    EXPECT_EQ(call.result, kIOInterrupted);

    // Partly done: the part written is reported instead.
    auto ends2 = m_service->CreatePipe(4);
    EXPECT_EQ(ends2.writeEnd->Write("aaaa", 4), 4);
    auto token2 = StopToken::Create();
    AsyncCall call2;
    std::string eight("abcdefgh");
    std::thread writer2([&] {
        StopTokenScope scope(token2);
        call2.result = ends2.writeEnd->Write(eight.data(), eight.size());
        call2.done = true;
    });
    EXPECT_FALSE(call2.FinishedWithin(kBlockedCheckMs));
    // Room for the first part; the write then blocks again for the rest.
    char four[4];
    EXPECT_EQ(ends2.readEnd->Read(four, 4), 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    token2->RequestStop();
    EXPECT_TRUE(call2.FinishedWithin(1000));
    writer2.join();
    EXPECT_EQ(call2.result, 4);
}

TEST_F(PipeServiceTest, AnotherThreadsStopDoesNotInterruptAReader) {
    auto ends = m_service->CreatePipe();
    auto tokenA = StopToken::Create();
    auto tokenB = StopToken::Create();
    AsyncCall call;
    char buf[8];
    std::thread reader([&] {
        StopTokenScope scope(tokenA);
        call.result = ends.readEnd->Read(buf, sizeof(buf));
        call.done = true;
    });
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    tokenB->RequestStop();
    EXPECT_FALSE(call.FinishedWithin(kBlockedCheckMs));
    EXPECT_EQ(ends.writeEnd->Write("y", 1), 1);
    EXPECT_TRUE(call.FinishedWithin(5000));
    reader.join();
    EXPECT_EQ(call.result, 1);
    EXPECT_EQ(buf[0], 'y');
}

TEST_F(PipeServiceTest, AStopAlreadyRequestedInterruptsOnlyACallThatWouldBlock) {
    auto token = StopToken::Create();
    token->RequestStop();

    auto withData = m_service->CreatePipe();
    EXPECT_EQ(withData.writeEnd->Write("q", 1), 1);
    {
        StopTokenScope scope(token);
        // A read with data returns the data, stop or no stop.
        char buf[8];
        EXPECT_EQ(withData.readEnd->Read(buf, sizeof(buf)), 1);
        EXPECT_EQ(buf[0], 'q');

        auto empty = m_service->CreatePipe();
        // A read that would block is interrupted at once.
        EXPECT_EQ(empty.readEnd->Read(buf, sizeof(buf)), kIOInterrupted);

        // A write with room goes through.
        EXPECT_EQ(empty.writeEnd->Write("r", 1), 1);
    }
}

TEST_F(PipeServiceTest, ManyPipesAreIndependentAndCounted) {
    constexpr int kPipes = 1000;
    std::vector<PipeEnds> pipes;
    pipes.reserve(kPipes);
    for (int i = 0; i < kPipes; ++i) {
        pipes.push_back(m_service->CreatePipe());
    }
    EXPECT_EQ(m_service->OpenPipeCount(), static_cast<size_t>(kPipes));

    for (size_t i = 0; i < pipes.size(); ++i) {
        int32_t index = static_cast<int32_t>(i);
        ASSERT_EQ(pipes[i].writeEnd->Write(&index, sizeof(index)), static_cast<ssize_t>(sizeof(index)));
    }
    for (size_t i = 0; i < pipes.size(); ++i) {
        int32_t index = -1;
        ASSERT_EQ(pipes[i].readEnd->Read(&index, sizeof(index)), static_cast<ssize_t>(sizeof(index)));
        EXPECT_EQ(index, static_cast<int32_t>(i)) << "pipe " << i;
    }

    // Releasing only the read ends keeps every pipe open: an end still held.
    for (auto& pipe : pipes) {
        pipe.readEnd.reset();
    }
    EXPECT_EQ(m_service->OpenPipeCount(), static_cast<size_t>(kPipes));

    pipes.clear();
    EXPECT_EQ(m_service->OpenPipeCount(), 0u);
}

TEST_F(PipeServiceTest, APipeOutlivesItsService) {
    PipeEnds ends;
    {
        auto service = PipeService::Create();
        ends = service->CreatePipe();
    }
    EXPECT_EQ(ends.writeEnd->Write("still here", 10), 10);
    char buf[16] = {};
    EXPECT_EQ(ends.readEnd->Read(buf, sizeof(buf)), 10);
    EXPECT_EQ(std::string(buf, 10), "still here");
}

TEST_F(PipeServiceTest, EndsAreNotTerminalsAndWorkOneWay) {
    auto ends = m_service->CreatePipe();
    EXPECT_FALSE(ends.readEnd->IsTerminal());
    EXPECT_FALSE(ends.writeEnd->IsTerminal());
    char buf[1];
    EXPECT_EQ(ends.writeEnd->Read(buf, sizeof(buf)), kIOError);
    EXPECT_EQ(ends.readEnd->Write("x", 1), kIOError);
}
