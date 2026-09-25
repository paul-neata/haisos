#include <gtest/gtest.h>
#include <sstream>
#include <nlohmann/json.hpp>
#include "src/haisos/AgentTrafficLog.h"
#include "src/components/Logger/Logger.h"

using namespace Haisos;

namespace {

const std::string kFirstRequest =
    R"({"model":"m","stream":false,"messages":[{"role":"system","content":"s"},{"role":"user","content":"u"}],"tools":[{"name":"t"}]})";
const std::string kSecondRequest =
    R"({"model":"m","stream":false,"messages":[{"role":"system","content":"s"},{"role":"user","content":"u"},{"role":"assistant","content":"a"}],"tools":[{"name":"t"}]})";

// An AgentTrafficLog writing to a stream the test can read back.
struct LogUnderTest {
    explicit LogUnderTest(AgentTrafficLogType type) {
        auto stream = std::make_unique<std::ostringstream>();
        out = stream.get();
        log = std::make_unique<AgentTrafficLog>(std::move(stream), type);
    }
    std::string Text() const { return out->str(); }

    std::ostringstream* out = nullptr;
    std::unique_ptr<AgentTrafficLog> log;
};

size_t Count(const std::string& text, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) {
        ++count;
    }
    return count;
}

} // namespace

TEST(AgentTrafficLogTest, ParsesItsTwoTypes) {
    AgentTrafficLogType type = AgentTrafficLogType::Full;
    EXPECT_TRUE(ParseAgentTrafficLogType("diff", type));
    EXPECT_EQ(type, AgentTrafficLogType::Diff);
    EXPECT_TRUE(ParseAgentTrafficLogType("full", type));
    EXPECT_EQ(type, AgentTrafficLogType::Full);
    EXPECT_FALSE(ParseAgentTrafficLogType("FULL", type));
    EXPECT_EQ(type, AgentTrafficLogType::Full);
}

TEST(AgentTrafficLogTest, SmartDiffShowsOnlyWhatAGrowingRequestAdded) {
    auto diff = nlohmann::ordered_json::parse(ComputeSmartDiff(kFirstRequest, kSecondRequest));

    EXPECT_EQ(diff["model"], "m");
    EXPECT_EQ(diff["stream"], false);
    EXPECT_EQ(diff["tools"], "-- same --");
    ASSERT_TRUE(diff["messages"].is_array());
    ASSERT_EQ(diff["messages"].size(), 2u);
    EXPECT_EQ(diff["messages"][0], "-- precedent array --");
    EXPECT_EQ(diff["messages"][1]["content"], "a");
}

TEST(AgentTrafficLogTest, SmartDiffShowsARewrittenArrayInFull) {
    const std::string rewritten = R"({"model":"m","messages":[{"role":"user","content":"other"}]})";
    auto diff = nlohmann::ordered_json::parse(ComputeSmartDiff(kFirstRequest, rewritten));
    ASSERT_EQ(diff["messages"].size(), 1u);
    EXPECT_EQ(diff["messages"][0]["content"], "other");
}

TEST(AgentTrafficLogTest, SmartDiffOfNonJsonFallsBackToTheCurrentText) {
    EXPECT_EQ(ComputeSmartDiff("not json", "also not json"), "also not json");
}

TEST(AgentTrafficLogTest, DiffModeWritesTheFirstRequestInFullThenOnlyTheDifference) {
    LogUnderTest under(AgentTrafficLogType::Diff);
    under.log->OnSend("agent_1", kFirstRequest);
    under.log->OnReceive("agent_1", R"({"message":{"content":"a"}})");
    under.log->OnSend("agent_1", kSecondRequest);

    const std::string text = under.Text();
    EXPECT_EQ(Count(text, ">>>>>>>> SEND    [agent_1]"), 2u);
    EXPECT_EQ(Count(text, "<<<<<<<< RECEIVE [agent_1]"), 1u);
    EXPECT_EQ(Count(text, "(diff against the previous send of [agent_1])"), 1u);
    EXPECT_EQ(Count(text, "-- precedent array --"), 1u);
    EXPECT_EQ(Count(text, "-- same --"), 1u);
}

TEST(AgentTrafficLogTest, DiffModeKeepsEachAgentsHistorySeparate) {
    LogUnderTest under(AgentTrafficLogType::Diff);
    under.log->OnSend("agent_1", kFirstRequest);
    under.log->OnSend("agent_2", kSecondRequest);

    // agent_2's first request is not a diff against agent_1's.
    EXPECT_EQ(Count(under.Text(), "(diff against"), 0u);
}

TEST(AgentTrafficLogTest, FullModeWritesEveryRequestInFull) {
    LogUnderTest under(AgentTrafficLogType::Full);
    under.log->OnSend("agent_1", kFirstRequest);
    under.log->OnSend("agent_1", kSecondRequest);
    under.log->OnReceive("", "(no response: HTTP request failed: status=0)");

    const std::string text = under.Text();
    EXPECT_EQ(Count(text, "(diff against"), 0u);
    EXPECT_EQ(Count(text, "-- precedent array --"), 0u);
    // Pretty-printed, with the second request's new message present in full.
    EXPECT_NE(text.find("\"content\": \"a\""), std::string::npos);
    // A nameless source is still labelled, and non-JSON is written as it is.
    EXPECT_NE(text.find("<<<<<<<< RECEIVE [(unnamed)]"), std::string::npos);
    EXPECT_NE(text.find("(no response: HTTP request failed: status=0)"), std::string::npos);
}

TEST(AgentTrafficLogTest, LoggerCallsOnlyTheRegisteredCallbacks) {
    std::vector<std::string> events;
    // Nothing registered: reporting is a no-op.
    LogAgentSend("a", "{}");

    RegisterLogAgentSendCallback([&](const std::string& name, const std::string& json) { events.push_back("send " + name + " " + json); });
    LogAgentSend("a", "{1}");
    LogAgentReceive("a", "{2}");
    RegisterLogAgentReceiveCallback([&](const std::string& name, const std::string& json) { events.push_back("receive " + name + " " + json); });
    LogAgentReceive("a", "{3}");

    RegisterLogAgentSendCallback(nullptr);
    RegisterLogAgentReceiveCallback(nullptr);
    LogAgentSend("a", "{4}");
    LogAgentReceive("a", "{5}");

    EXPECT_EQ(events, (std::vector<std::string>{"send a {1}", "receive a {3}"}));
}
