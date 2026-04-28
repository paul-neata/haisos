#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "src/components/Console/VirtualConsole.h"

using namespace Haisos;

TEST(VirtualConsoleTest, WriteAndGetContents) {
    VirtualConsole console;
    console.Write("Hello, world!");
    EXPECT_EQ(console.GetContents(), "Hello, world!\n");
}

TEST(VirtualConsoleTest, WriteMultipleMessages) {
    VirtualConsole console;
    console.Write("First");
    console.Write("Second");
    console.Write("Third");
    EXPECT_EQ(console.GetContents(), "First\nSecond\nThird\n");
}

TEST(VirtualConsoleTest, ClearEmptiesBuffer) {
    VirtualConsole console;
    console.Write("Before clear");
    EXPECT_FALSE(console.GetContents().empty());
    console.Clear();
    EXPECT_EQ(console.GetContents(), "");
}

TEST(VirtualConsoleTest, ThreadSafety) {
    VirtualConsole console;
    const int numThreads = 4;
    const int messagesPerThread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&console, t, messagesPerThread]() {
            for (int i = 0; i < messagesPerThread; ++i) {
                console.Write("Thread " + std::to_string(t) + " Message " + std::to_string(i));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::string contents = console.GetContents();
    int lineCount = 0;
    size_t pos = 0;
    while ((pos = contents.find('\n', pos)) != std::string::npos) {
        ++lineCount;
        ++pos;
    }

    EXPECT_EQ(lineCount, numThreads * messagesPerThread);

    for (int t = 0; t < numThreads; ++t) {
        for (int i = 0; i < messagesPerThread; ++i) {
            std::string expected = "Thread " + std::to_string(t) + " Message " + std::to_string(i);
            EXPECT_NE(contents.find(expected), std::string::npos)
                << "Missing: " << expected;
        }
    }
}
