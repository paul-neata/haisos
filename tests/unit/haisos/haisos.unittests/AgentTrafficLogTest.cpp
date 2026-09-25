#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "src/haisos/AgentTrafficLog.h"
#include "src/haisos/ReopeningLogFile.h"
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

TEST(AgentTrafficLogTest, ParsesItsThreeTypes) {
    AgentTrafficLogType type = AgentTrafficLogType::Full;
    EXPECT_TRUE(ParseAgentTrafficLogType("xdiff", type));
    EXPECT_EQ(type, AgentTrafficLogType::XDiff);
    EXPECT_STREQ(AgentTrafficLogTypeName(type), "xdiff");
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
    under.log->OnSend({"agent_1"}, kFirstRequest);
    under.log->OnReceive({"agent_1"}, R"({"message":{"content":"a"}})");
    under.log->OnSend({"agent_1"}, kSecondRequest);

    const std::string text = under.Text();
    EXPECT_EQ(Count(text, ">>>>>>>> SEND    [agent_1]"), 2u);
    EXPECT_EQ(Count(text, "<<<<<<<< RECEIVE [agent_1]"), 1u);
    EXPECT_EQ(Count(text, "(diff against the previous send of [agent_1])"), 1u);
    EXPECT_EQ(Count(text, "-- precedent array --"), 1u);
    EXPECT_EQ(Count(text, "-- same --"), 1u);
}

TEST(AgentTrafficLogTest, DiffModeKeepsEachAgentsHistorySeparate) {
    LogUnderTest under(AgentTrafficLogType::Diff);
    under.log->OnSend({"agent_1"}, kFirstRequest);
    under.log->OnSend({"agent_1", "agent_2"}, kSecondRequest);

    // agent_2's first request is not a diff against agent_1's.
    EXPECT_EQ(Count(under.Text(), "(diff against"), 0u);
}

TEST(AgentTrafficLogTest, FullModeWritesEveryRequestInFull) {
    LogUnderTest under(AgentTrafficLogType::Full);
    under.log->OnSend({"agent_1"}, kFirstRequest);
    under.log->OnSend({"agent_1"}, kSecondRequest);
    under.log->OnReceive({""}, "(no response: HTTP request failed: status=0)");

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
    LogAgentSend({"a"}, "{}");

    RegisterLogAgentSendCallback([&](const std::vector<std::string>& path, const std::string& json) { events.push_back("send " + FormatAgentPath(path) + " " + json); });
    LogAgentSend({"a", "b"}, "{1}");
    LogAgentReceive({"a"}, "{2}");
    RegisterLogAgentReceiveCallback([&](const std::vector<std::string>& path, const std::string& json) { events.push_back("receive " + FormatAgentPath(path) + " " + json); });
    LogAgentReceive({"a"}, "{3}");

    RegisterLogAgentSendCallback(nullptr);
    RegisterLogAgentReceiveCallback(nullptr);
    LogAgentSend({"a"}, "{4}");
    LogAgentReceive({"a"}, "{5}");

    EXPECT_EQ(events, (std::vector<std::string>{"send a>b {1}", "receive a {3}"}));
}

TEST(AgentTrafficLogTest, FormatsAgentPathsWithAngleBrackets) {
    EXPECT_EQ(FormatAgentPath({"main", "agent_1", "agent_4"}), "main>agent_1>agent_4");
    EXPECT_EQ(FormatAgentPath({"main", ""}), "main>(unnamed)");
    EXPECT_EQ(FormatAgentPath({}), "(unnamed)");
}

TEST(AgentTrafficLogTest, IndentsTwoTabsAndABarPerLevelOfDepth) {
    EXPECT_EQ(IndentForAgentDepth("a\nb\n", 0), "a\nb\n");
    EXPECT_EQ(IndentForAgentDepth("a\n\nb", 1), "\t\t| a\n\t\t|\n\t\t| b");
    EXPECT_EQ(IndentForAgentDepth("a\n", 2), "\t\t|\t\t| a\n");
}

TEST(AgentTrafficLogTest, EveryModeHeadsEntriesWithTheAgentPathAndIndentsByDepth) {
    for (auto type : {AgentTrafficLogType::XDiff, AgentTrafficLogType::Diff, AgentTrafficLogType::Full}) {
        LogUnderTest under(type);
        under.log->OnSend({"main"}, kFirstRequest);
        under.log->OnSend({"main", "agent_1"}, kFirstRequest);
        under.log->OnReceive({"main", "agent_1", "agent_2"}, R"({"done":true})");

        const std::string text = under.Text();
        EXPECT_EQ(text.rfind(">>>>>>>> SEND    [main] ", 0), 0u) << AgentTrafficLogTypeName(type) << "\n" << text;
        EXPECT_NE(text.find("\n\t\t| >>>>>>>> SEND    [main>agent_1] "), std::string::npos) << AgentTrafficLogTypeName(type) << "\n" << text;
        EXPECT_NE(text.find("\n\t\t|\t\t| <<<<<<<< RECEIVE [main>agent_1>agent_2] "), std::string::npos) << AgentTrafficLogTypeName(type) << "\n" << text;
    }
}

TEST(AgentTrafficLogTest, ExtremeDiffOfAFirstRequestKeepsJsonButShortensToolsAndCalls) {
    const std::string request = R"({"model":"m","stream":false,"tools":[)"
        R"({"type":"function","function":{"name":"t1","description":"Reads\n a file.","parameters":{"type":"object"}}},)"
        R"({"type":"function","function":{"name":"t2","parameters":{}}}],)"
        R"("messages":[{"content":"s","role":"system"},)"
        R"({"content":"","role":"assistant","tool_calls":[{"id":"c0","function":{"index":0,"name":"f","arguments":{"path":"/a","n":2}}}]},)"
        R"({"content":"r","name":"f","role":"tool","tool_call_id":"c0"}]})";
    EXPECT_EQ(ComputeExtremeDiff("", request),
        "{\n"
        "  \"model\": \"m\",\n"
        "  \"stream\": false,\n"
        "  \"tools\": [\n"
        "    t1:\n"
        "        Reads\n"
        "         a file.\n"
        "    t2\n"
        "  ],\n"
        "  \"m\": [\n"
        "    {\n"
        "      \"role\": \"system\",\n"
        "      \"content\": \"s\"\n"
        "    },\n"
        "    {\n"
        "      \"role\": \"assistant\",\n"
        "      m[1].tool_calls[0].id = \"c0\"\n"
        "      m[1].tool_calls[0].function.name = \"f\"\n"
        "      m[1].tool_calls[0].function.arguments.path = \"/a\"\n"
        "      m[1].tool_calls[0].function.arguments.n = 2\n"
        "    },\n"
        "    {\n"
        "      \"role\": \"tool\",\n"
        "      \"name\": \"f\",\n"
        "      \"content\": \"r\",\n"
        "      \"tool_call_id\": \"c0\"\n"
        "    }\n"
        "  ]\n"
        "}");
}

TEST(AgentTrafficLogTest, ExtremeDiffShowsOnlyWhatChanged) {
    // Same model, stream and tools; one message more.
    EXPECT_EQ(ComputeExtremeDiff(kFirstRequest, kSecondRequest),
        "{\n"
        "  \"m\": [\n"
        "    -- m[0..1] as before --\n"
        "    {\n"
        "      \"role\": \"assistant\",\n"
        "      \"content\": \"a\"\n"
        "    }\n"
        "  ]\n"
        "}");
    EXPECT_EQ(ComputeExtremeDiff(kSecondRequest, kSecondRequest), "{}");
}

TEST(AgentTrafficLogTest, ExtremeDiffWritesChangedToolsAndRewrittenArraysInFull) {
    const std::string before = R"({"model":"m","tools":[{"name":"a"}],"messages":[{"content":"x"}],"gone":1})";
    const std::string after = R"({"model":"n","tools":[{"name":"b","description":"d"}],"messages":[{"content":"y"}]})";
    EXPECT_EQ(ComputeExtremeDiff(before, after),
        "{\n"
        "  \"model\": \"n\",\n"
        "  \"tools\": [\n"
        "    b:\n"
        "        d\n"
        "  ],\n"
        "  \"m\": [\n"
        "    {\n"
        "      \"content\": \"y\"\n"
        "    }\n"
        "  ],\n"
        "  \"gone\": \"-- removed --\"\n"
        "}");
}

TEST(AgentTrafficLogTest, ExtremeWrapsLongAndMultiLineTextTo80Characters) {
    const std::string word9 = "abcdefgh ";   // 9 characters with its space
    std::string longLine;
    for (int i = 0; i < 10; ++i) {
        longLine += word9;
    }
    longLine += "end";                        // 93 characters in all
    const std::string request = R"({"messages":[{"content":")" + longLine + R"(\n\nnext \"line\""}]})";
    // 9 words are exactly 80 characters, so the wrap falls at the space after
    // the 9th; the line breaks in the text are kept.
    EXPECT_EQ(ComputeExtremeDiff("", request),
        "{\n"
        "  \"m\": [\n"
        "    {\n"
        "      \"content\": \"\"\"\n"
        "          abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh\n"
        "          abcdefgh end\n"
        "\n"
        "          next \"line\"\n"
        "      \"\"\"\n"
        "    }\n"
        "  ]\n"
        "}");
}

TEST(AgentTrafficLogTest, ExtremeResponseDropsNoiseAndFlattensToolCalls) {
    const std::string response = R"({"model":"m","created_at":"t","message":{"role":"assistant","content":"",)"
        R"("tool_calls":[{"function":{"name":"f","arguments":"{\"q\":\"tab\\there \\\"x\\\"\"}"}}]},)"
        R"("done":true,"total_duration":5,"eval_count":7})";
    EXPECT_EQ(FormatExtremeResponse(response),
        "{\n"
        "  \"message\": {\n"
        "    \"role\": \"assistant\",\n"
        "    message.tool_calls[0].function.name = \"f\"\n"
        "    message.tool_calls[0].function.arguments.q = \"tab\there \\\"x\\\"\"\n"
        "  },\n"
        "  \"done\": true,\n"
        "  \"eval_count\": 7\n"
        "}");
    EXPECT_EQ(FormatExtremeResponse("not json"), "not json");
}

TEST(AgentTrafficLogTest, XDiffModeWritesRequestsAsChangesAndResponsesShortened) {
    LogUnderTest under(AgentTrafficLogType::XDiff);
    under.log->OnSend({"agent_1"}, kFirstRequest);
    under.log->OnReceive({"agent_1"}, R"({"model":"m","message":{"role":"assistant","content":"a"}})");
    under.log->OnSend({"agent_1"}, kSecondRequest);

    const std::string text = under.Text();
    EXPECT_EQ(Count(text, "(diff against the previous send of [agent_1])"), 1u);
    EXPECT_EQ(Count(text, "\"model\": \"m\""), 1u) << text;
    EXPECT_EQ(Count(text, "-- m[0..1] as before --"), 1u) << text;
    EXPECT_EQ(Count(text, "\"content\": \"a\""), 2u) << text;
}

// --- A log file deleted while Haisos runs ---

namespace {

const std::string kReopenDir = "/tmp/haisos_reopening_log_test";

std::string ReadFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

class ReopeningLogFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kReopenDir);
        std::filesystem::create_directories(kReopenDir);
    }
    void TearDown() override {
        std::filesystem::remove_all(kReopenDir);
    }
    const std::string path = kReopenDir + "/log.txt";
};

} // namespace

TEST_F(ReopeningLogFileTest, TruncatesOrAppendsOnTheFirstOpen) {
    std::ofstream(path) << "old\n";
    {
        ReopeningLogFile appended(path, /*truncate=*/false);
        appended.Write("more\n");
    }
    EXPECT_EQ(ReadFile(path), "old\nmore\n");
    {
        ReopeningLogFile truncated(path, /*truncate=*/true);
        ASSERT_TRUE(truncated.IsOpen());
        truncated.Write("fresh\n");
    }
    EXPECT_EQ(ReadFile(path), "fresh\n");
}

TEST_F(ReopeningLogFileTest, ADeletedFileIsRecreatedOnTheNextWrite) {
    ReopeningLogFile log(path, /*truncate=*/true);
    log.Write("before\n");
    EXPECT_FALSE(log.EnsureOpen());
    std::filesystem::remove(path);

    log.Write("after\n");
    ASSERT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(ReadFile(path), "after\n");
    // Once back, it is simply open again.
    EXPECT_FALSE(log.EnsureOpen());
}

TEST_F(ReopeningLogFileTest, EnsureOpenSaysWhenItRecreatedTheFile) {
    ReopeningLogFile log(path, /*truncate=*/true);
    std::filesystem::remove(path);
    EXPECT_TRUE(log.EnsureOpen());
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST_F(ReopeningLogFileTest, AgentTrafficLogStartsAfreshInARecreatedFile) {
    auto file = std::make_shared<ReopeningLogFile>(path, /*truncate=*/true);
    AgentTrafficLog log(file, AgentTrafficLogType::Diff);
    const std::string first = R"({"model":"m","messages":[{"role":"user","content":"hi"}]})";
    const std::string second = R"({"model":"m","messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"hello"}]})";
    log.OnSend({"agent_1"}, first);
    std::filesystem::remove(path);

    log.OnSend({"agent_1"}, second);
    ASSERT_TRUE(std::filesystem::exists(path));
    const std::string content = ReadFile(path);
    EXPECT_NE(content.find("was deleted while Haisos was running and has been re-created"), std::string::npos) << content;
    // Written in full: the request it would be a diff of is not in this file.
    EXPECT_EQ(content.find("diff against the previous send"), std::string::npos) << content;
    EXPECT_NE(content.find("\"hello\""), std::string::npos) << content;

    // After that, diffs go on as usual.
    log.OnSend({"agent_1"}, second);
    EXPECT_NE(ReadFile(path).find("diff against the previous send"), std::string::npos);
}
