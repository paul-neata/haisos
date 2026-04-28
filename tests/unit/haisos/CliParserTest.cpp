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

TEST_F(CliParserTest, FileLongFlag) {
    auto result = Parse({"--file", "prompt.md"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.useFile);
    EXPECT_EQ(result.options.userPrompt, "prompt.md");
}

TEST_F(CliParserTest, FileShortFlag) {
    auto result = Parse({"-f", "prompt.md"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.useFile);
    EXPECT_EQ(result.options.userPrompt, "prompt.md");
}

TEST_F(CliParserTest, PromptLongFlag) {
    auto result = Parse({"--prompt", "Hello world"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_FALSE(result.options.useFile);
    EXPECT_EQ(result.options.userPrompt, "Hello world");
}

TEST_F(CliParserTest, PromptShortFlag) {
    auto result = Parse({"-p", "Hello"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_FALSE(result.options.useFile);
    EXPECT_EQ(result.options.userPrompt, "Hello");
}

TEST_F(CliParserTest, SystemPromptLongFlag) {
    auto result = Parse({"--prompt", "Hello", "--system-prompt", "Be concise"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.systemPrompt, "Be concise");
}

TEST_F(CliParserTest, PositionalArgument) {
    auto result = Parse({"prompt.md"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.useFile);
    EXPECT_EQ(result.options.userPrompt, "prompt.md");
}

TEST_F(CliParserTest, MissingInputReturnsError) {
    auto result = Parse({});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, PromptAndTakeStdinReturnsError) {
    auto result = Parse({"--prompt", "Hello", "--take-stdin"});
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

TEST_F(CliParserTest, LogToConsole) {
    auto result = Parse({"--prompt", "Hello", "--log-to-console"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.logToConsole);
}

TEST_F(CliParserTest, UnknownFlagReturnsError) {
    auto result = Parse({"--unknown-flag"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, LogLevel) {
    auto result = Parse({"--prompt", "Hello", "--log-level", "debug"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.logLevel, LogLevel::Debug);
}

TEST_F(CliParserTest, LogJsonInTemp) {
    auto result = Parse({"--prompt", "Hello", "--log-json-in-temp"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.logJsonInTemp);
}

TEST_F(CliParserTest, LogToFile) {
    auto result = Parse({"--prompt", "Hello", "--log-to-file", "/tmp/log.txt"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.logFilePath, "/tmp/log.txt");
}

TEST_F(CliParserTest, SystemPromptFile) {
    auto result = Parse({"--prompt", "Hello", "--system-prompt-file", "system.txt"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.options.systemPromptFile, "system.txt");
}

TEST_F(CliParserTest, TakeStdinFlag) {
    auto result = Parse({"--take-stdin"});
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.options.takeStdin);
    EXPECT_FALSE(result.options.useFile);
    EXPECT_TRUE(result.options.userPrompt.empty());
}

TEST_F(CliParserTest, MultiplePositionalArgsReturnsError) {
    auto result = Parse({"file1.md", "file2.md"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, FileFlagMissingValue) {
    auto result = Parse({"--file"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, PromptFlagMissingValue) {
    auto result = Parse({"--prompt"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, SystemPromptFlagMissingValue) {
    auto result = Parse({"--system-prompt"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, SystemPromptFileMissingValue) {
    auto result = Parse({"--system-prompt-file"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, LogToFileMissingValue) {
    auto result = Parse({"--log-to-file"});
    EXPECT_FALSE(result.error.empty());
}

TEST_F(CliParserTest, LogLevelMissingValue) {
    auto result = Parse({"--log-level"});
    EXPECT_FALSE(result.error.empty());
}
