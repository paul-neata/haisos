#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include "src/haisos/CliParser.h"

using namespace Haisos;

class CliParserTest : public ::testing::Test {
protected:
    std::vector<std::unique_ptr<char[]>> argStorage;
    std::vector<char*> argvVec;

    void SetArgs(const std::vector<std::string>& args) {
        argStorage.clear();
        argvVec.clear();
        argvVec.push_back(const_cast<char*>("haisos"));
        for (const auto& a : args) {
            auto ptr = std::make_unique<char[]>(a.size() + 1);
            std::strcpy(ptr.get(), a.c_str());
            argvVec.push_back(ptr.get());
            argStorage.push_back(std::move(ptr));
        }
    }

    ParseResult Parse(const std::vector<std::string>& args) {
        SetArgs(args);
        return ParseArguments(static_cast<int>(argvVec.size()), argvVec.data());
    }
};

TEST_F(CliParserTest, NoArgsDefaultsHaisosFilePathToEmpty) {
    auto result = Parse({});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.haisosFilePath.empty());
    EXPECT_TRUE(result.options.argOverrides.empty());
}

TEST_F(CliParserTest, PositionalHaisosFilePath) {
    auto result = Parse({"myfile"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.haisosFilePath, "myfile");
}

TEST_F(CliParserTest, MultiplePositionalArgsReturnsError) {
    auto result = Parse({"file1", "file2"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, ArgOverridesAfterDoubleDash) {
    auto result = Parse({"myfile", "--", "name=value", "other=thing"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.haisosFilePath, "myfile");
    ASSERT_EQ(result.options.argOverrides.size(), 2u);
    EXPECT_EQ(result.options.argOverrides[0].first, "name");
    EXPECT_EQ(result.options.argOverrides[0].second, "value");
    EXPECT_EQ(result.options.argOverrides[1].first, "other");
    EXPECT_EQ(result.options.argOverrides[1].second, "thing");
}

TEST_F(CliParserTest, ArgOverridesWithoutHaisosFilePath) {
    auto result = Parse({"--", "name=value"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.haisosFilePath.empty());
    ASSERT_EQ(result.options.argOverrides.size(), 1u);
    EXPECT_EQ(result.options.argOverrides[0].first, "name");
}

TEST_F(CliParserTest, ArgOverrideMissingEqualsReturnsError) {
    auto result = Parse({"--", "notkeyvalue"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, HelpFlag) {
    auto result = Parse({"--help"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.help);
}

TEST_F(CliParserTest, HelpShortFlag) {
    auto result = Parse({"-h"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.help);
}

TEST_F(CliParserTest, VersionFlag) {
    auto result = Parse({"--version"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.version);
}

TEST_F(CliParserTest, InitFlag) {
    auto result = Parse({"--init"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.init);
}

TEST_F(CliParserTest, LogToConsole) {
    auto result = Parse({"--log-to-console"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.logToConsole);
}

TEST_F(CliParserTest, UnknownFlagReturnsError) {
    auto result = Parse({"--unknown-flag"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, LogLevel) {
    auto result = Parse({"--log-level", "debug"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.logLevel, LogLevel::Debug);
}

TEST_F(CliParserTest, LogJsonInTemp) {
    auto result = Parse({"--log-json-in-temp"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.logJsonInTemp);
}

TEST_F(CliParserTest, LogToFile) {
    auto result = Parse({"--log-to-file", "/tmp/log.txt"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.logFilePath, "/tmp/log.txt");
}

TEST_F(CliParserTest, LogToFileShortFlag) {
    auto result = Parse({"-l", "/tmp/log.txt"});
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.options.logFilePath, "/tmp/log.txt");
    EXPECT_FALSE(Parse({"-l"}).error.empty());
}

TEST_F(CliParserTest, LogToFileMissingValue) {
    auto result = Parse({"--log-to-file"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, LogLevelMissingValue) {
    auto result = Parse({"--log-level"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, FlagsBeforeAndArgOverridesAfterDoubleDash) {
    auto result = Parse({"myfile", "--log-to-console", "--", "name=value"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.haisosFilePath, "myfile");
    EXPECT_TRUE(result.options.logToConsole);
    ASSERT_EQ(result.options.argOverrides.size(), 1u);
}

TEST_F(CliParserTest, LogAgentToFileDefaultsToXDiff) {
    auto result = Parse({"--log-agent-to-file", "/tmp/agents.log"});
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.options.logAgentFilePath, "/tmp/agents.log");
    EXPECT_EQ(result.options.logAgentFileType, AgentTrafficLogType::XDiff);
}

TEST_F(CliParserTest, LogAgentToFileShortFlag) {
    auto result = Parse({"-L", "/tmp/agents.log", "--log-agent-to-file-type", "diff"});
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.options.logAgentFilePath, "/tmp/agents.log");
    EXPECT_EQ(result.options.logAgentFileType, AgentTrafficLogType::Diff);
    EXPECT_FALSE(Parse({"-L"}).error.empty());
}

TEST_F(CliParserTest, LogAgentToFileTypeFull) {
    auto result = Parse({"--log-agent-to-file-type", "full", "--log-agent-to-file", "/tmp/agents.log"});
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.options.logAgentFileType, AgentTrafficLogType::Full);
}

TEST_F(CliParserTest, LogAgentToFileRequiresAPath) {
    EXPECT_FALSE(Parse({"--log-agent-to-file"}).error.empty());
}

TEST_F(CliParserTest, LogAgentToFileTypeRejectsUnknownValues) {
    EXPECT_FALSE(Parse({"--log-agent-to-file", "/tmp/a.log", "--log-agent-to-file-type", "partial"}).error.empty());
    EXPECT_FALSE(Parse({"--log-agent-to-file", "/tmp/a.log", "--log-agent-to-file-type"}).error.empty());
}

TEST_F(CliParserTest, LogAgentToFileTypeWithoutAFileIsError) {
    auto result = Parse({"--log-agent-to-file-type", "full"});
    EXPECT_NE(result.error.find("--log-agent-to-file"), std::string::npos) << result.error;
}

TEST_F(CliParserTest, UsageDescribesTheAgentLogOptions) {
    auto usage = FormatUsage("haisos");
    EXPECT_NE(usage.find("-L, --log-agent-to-file <path>"), std::string::npos);
    EXPECT_NE(usage.find("-l, --log-to-file <path>"), std::string::npos);
    EXPECT_NE(usage.find("xdiff"), std::string::npos);
    EXPECT_NE(usage.find("--log-agent-to-file-type <type>"), std::string::npos);
}
