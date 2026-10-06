#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include "src/components/libheaders/StopToken.h"

using namespace Haisos;

TEST(StopTokenTest, CurrentIsNullWithoutAScope) {
    EXPECT_EQ(StopToken::Current(), nullptr);
}

TEST(StopTokenTest, AScopeInstallsAndRestoresTheToken) {
    auto outer = StopToken::Create();
    auto inner = StopToken::Create();
    {
        StopTokenScope outerScope(outer);
        EXPECT_EQ(StopToken::Current(), outer);
        {
            StopTokenScope innerScope(inner);
            EXPECT_EQ(StopToken::Current(), inner);
        }
        EXPECT_EQ(StopToken::Current(), outer);
    }
    EXPECT_EQ(StopToken::Current(), nullptr);

    // A null token installs nothing: the thread stays uninterruptible.
    {
        StopTokenScope nullScope(nullptr);
        EXPECT_EQ(StopToken::Current(), nullptr);
    }
}

TEST(StopTokenTest, AnotherThreadSeesNull) {
    auto token = StopToken::Create();
    std::atomic<bool> sawNull{false};
    {
        StopTokenScope scope(token);
        std::thread other([&] { sawNull = StopToken::Current() == nullptr; });
        other.join();
    }
    EXPECT_TRUE(sawNull);
}

TEST(StopTokenTest, RequestStopRunsEachCallbackOnce) {
    auto token = StopToken::Create();
    std::atomic<int> first{0};
    std::atomic<int> second{0};
    {
        StopCallback firstCallback(token, [&] { ++first; });
        StopCallback secondCallback(token, [&] { ++second; });
        token->RequestStop();
        EXPECT_EQ(first.load(), 1);
        EXPECT_EQ(second.load(), 1);
        // Idempotent: a second request runs nothing again.
        token->RequestStop();
        EXPECT_EQ(first.load(), 1);
        EXPECT_EQ(second.load(), 1);
    }
    EXPECT_TRUE(token->StopRequested());
}

TEST(StopTokenTest, ACallbackRegisteredAfterTheStopRunsAtOnce) {
    auto token = StopToken::Create();
    token->RequestStop();
    std::atomic<int> ran{0};
    {
        StopCallback callback(token, [&] { ++ran; });
        EXPECT_EQ(ran.load(), 1);
    }
}

TEST(StopTokenTest, ADestroyedCallbackNeverRuns) {
    auto token = StopToken::Create();
    std::atomic<int> ran{0};
    {
        StopCallback callback(token, [&] { ++ran; });
    }
    token->RequestStop();
    EXPECT_EQ(ran.load(), 0);
}

TEST(StopTokenTest, ANullTokenCallbackDoesNothing) {
    std::atomic<int> ran{0};
    {
        StopCallback callback(nullptr, [&] { ++ran; });
    }
    EXPECT_EQ(ran.load(), 0);
}
