#include <gtest/gtest.h>
#include <utility>
#include <vector>
#include "LLMCommunicator.h"
#include "tests/mocks/MockHTTPClient.h"

using namespace Haisos;
using namespace Haisos::Mocks;

TEST(LLMCommunicatorTest, ResponseParsing) {
    auto mockHttp = std::make_shared<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"content": "Test response", "done": true})");

    auto llm = LLMCommunicator::Create(std::move(mockHttp), "http://localhost:11434/api/chat", "llama3", "");

    std::vector<LLMMessage> messages;
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "User prompt";
    messages.push_back(userMsg);

    LLMResponse response = llm->Call(messages, {});

    EXPECT_EQ(response.message.content, "Test response");
    EXPECT_TRUE(response.done);
}

TEST(LLMCommunicatorTest, CallPostsToEndpoint) {
    auto mockHttp = std::make_shared<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"content": "ok"})");

    auto llm = LLMCommunicator::Create(std::move(mockHttp), "http://test.com/api", "test-model", "api-key");

    std::vector<LLMMessage> messages;
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "user";
    messages.push_back(userMsg);

    llm->Call(messages, {});

    auto* rawHttp = llm->GetHttpClient();
    ASSERT_NE(rawHttp, nullptr);
    auto* mock = static_cast<MockHTTPClient*>(rawHttp);
    EXPECT_EQ(mock->GetLastUrl(), "http://test.com/api");
    EXPECT_EQ(mock->GetLastMethod(), "POST");
    EXPECT_NE(mock->GetLastBody(), "");
}

TEST(LLMCommunicatorTest, GetLastAssembledMessage) {
    auto mockHttp = std::make_shared<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"content": "assembled message content"})");

    auto llm = LLMCommunicator::Create(std::move(mockHttp), "http://localhost:11434/api/chat", "llama3", "");

    std::vector<LLMMessage> messages;
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "user";
    messages.push_back(userMsg);

    LLMResponse response = llm->Call(messages, {});

    EXPECT_EQ(response.message.content, "assembled message content");
}

namespace {

// Registers the Logger's agent callbacks for the length of a test, recording
// what they are given, and removes them again after.
class AgentTrafficRecorder {
public:
    AgentTrafficRecorder() {
        RegisterLogAgentSendCallback([this](const std::vector<std::string>& path, const std::string& json) { sent.emplace_back(path, json); });
        RegisterLogAgentReceiveCallback([this](const std::vector<std::string>& path, const std::string& json) { received.emplace_back(path, json); });
    }
    ~AgentTrafficRecorder() {
        RegisterLogAgentSendCallback(nullptr);
        RegisterLogAgentReceiveCallback(nullptr);
    }
    std::vector<std::pair<std::vector<std::string>, std::string>> sent;
    std::vector<std::pair<std::vector<std::string>, std::string>> received;
};

std::vector<LLMMessage> OneUserMessage() {
    LLMMessage userMsg;
    userMsg.role = "user";
    userMsg.content = "User prompt";
    return {userMsg};
}

} // namespace

TEST(LLMCommunicatorTest, ReportsEachRequestAndResponseUnderItsAgentPath) {
    AgentTrafficRecorder recorder;
    auto mockHttp = std::make_shared<MockHTTPClient>();
    const std::string responseBody = R"({"message": {"role": "assistant", "content": "hi"}, "done": true})";
    mockHttp->SetPostResponse(responseBody);
    auto llm = LLMCommunicator::Create(mockHttp, "http://localhost:11434/api/chat", "llama3", "", {"main", "agent_7"});

    llm->Call(OneUserMessage(), {});

    ASSERT_EQ(recorder.sent.size(), 1u);
    EXPECT_EQ(recorder.sent[0].first, (std::vector<std::string>{"main", "agent_7"}));
    EXPECT_EQ(recorder.sent[0].second, mockHttp->GetLastBody());
    ASSERT_EQ(recorder.received.size(), 1u);
    EXPECT_EQ(recorder.received[0].first, (std::vector<std::string>{"main", "agent_7"}));
    EXPECT_EQ(recorder.received[0].second, responseBody);
}

TEST(LLMCommunicatorTest, ReportsAFailedRequestAsReceivedToo) {
    AgentTrafficRecorder recorder;
    auto mockHttp = std::make_shared<MockHTTPClient>();
    mockHttp->SetPostResponse(HTTPResponse{0, "", "Couldn't connect to server"});
    auto llm = LLMCommunicator::Create(mockHttp, "http://localhost:9999/api/chat", "llama3", "", {"main", "agent_7"});

    llm->Call(OneUserMessage(), {});

    ASSERT_EQ(recorder.received.size(), 1u);
    EXPECT_NE(recorder.received[0].second.find("Couldn't connect to server"), std::string::npos)
        << recorder.received[0].second;
}

// Bytes that are not UTF-8 anywhere in the history -- a Latin-1 file an agent
// read with os_read_file, say -- reach the LLM as U+FFFD. Serializing them
// used to throw, which ended the agent without a word.
TEST(LLMCommunicatorTest, BuildRequestJsonReplacesBytesThatAreNotUtf8) {
    std::vector<LLMMessage> messages(1);
    messages[0].role = "tool";
    messages[0].name = "os_read_file";
    messages[0].content = "caf" "\xe9" " cr" "\xe8" "me";

    std::string request;
    ASSERT_NO_THROW(request = LLMCommunicator::BuildRequestJson("llama3", messages, {}));
    const auto parsed = nlohmann::json::parse(request);
    EXPECT_EQ(parsed["messages"][0]["content"], "caf" "\xef\xbf\xbd" " cr" "\xef\xbf\xbd" "me");
}

TEST(LLMCommunicatorTest, CallSendsAHistoryHoldingBytesThatAreNotUtf8) {
    auto mockHttp = std::make_shared<MockHTTPClient>();
    mockHttp->SetPostResponse(R"({"message": {"role": "assistant", "content": "fine"}, "done": true})");
    auto llm = LLMCommunicator::Create(mockHttp, "http://localhost:11434/api/chat", "llama3", "");

    std::vector<LLMMessage> messages = OneUserMessage();
    messages[0].content += " \xff\xfe";
    LLMResponse response;
    ASSERT_NO_THROW(response = llm->Call(messages, {}));

    EXPECT_EQ(response.message.content, "fine");
    const auto sent = nlohmann::json::parse(mockHttp->GetLastBody());
    EXPECT_EQ(sent["messages"][0]["content"], "User prompt \xef\xbf\xbd\xef\xbf\xbd");
}
