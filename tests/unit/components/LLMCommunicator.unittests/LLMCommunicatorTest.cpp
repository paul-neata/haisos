#include <gtest/gtest.h>
#include "LLMCommunicator.h"
#include "tests/mocks/MockHTTPClient.h"

using namespace Haisos;
using namespace Haisos::Mocks;

TEST(LLMCommunicatorTest, ResponseParsing) {
    auto mockHttp = std::make_unique<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"content": "Test response", "done": true})");

    LLMCommunicator llm(std::move(mockHttp), "http://localhost:11434/api/chat", "llama3", "");

    std::vector<LLMMessage> messages;
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "User prompt";
    messages.push_back(userMsg);

    LLMResponse response = llm.Call(messages, {}, {});

    EXPECT_EQ(response.message.content, "Test response");
    EXPECT_TRUE(response.done);
}

TEST(LLMCommunicatorTest, CallPostsToEndpoint) {
    auto mockHttp = std::make_unique<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"content": "ok"})");

    LLMCommunicator llm(std::move(mockHttp), "http://test.com/api", "test-model", "api-key");

    std::vector<LLMMessage> messages;
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "user";
    messages.push_back(userMsg);

    llm.Call(messages, {}, {});

    auto* rawHttp = llm.GetHttpClient();
    ASSERT_NE(rawHttp, nullptr);
    auto* mock = static_cast<MockHTTPClient*>(rawHttp);
    EXPECT_EQ(mock->GetLastUrl(), "http://test.com/api");
    EXPECT_EQ(mock->GetLastMethod(), "POST");
    EXPECT_NE(mock->GetLastBody(), "");
}

TEST(LLMCommunicatorTest, GetLastAssembledMessage) {
    auto mockHttp = std::make_unique<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"content": "assembled message content"})");

    LLMCommunicator llm(std::move(mockHttp), "http://localhost:11434/api/chat", "llama3", "");

    std::vector<LLMMessage> messages;
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "user";
    messages.push_back(userMsg);

    LLMResponse response = llm.Call(messages, {}, {});

    EXPECT_EQ(response.message.content, "assembled message content");
}
