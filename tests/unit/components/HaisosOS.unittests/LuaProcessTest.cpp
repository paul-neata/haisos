#include <gtest/gtest.h>
#include "Environment.h"
#include "LuaProcess.h"
#include "tests/mocks/MockAgentConsole.h"

using namespace Haisos;
using namespace Haisos::Mocks;

namespace {

// A tiny IToolFactory test double exposing two tools: "echo" (returns its
// "text" argument as a plain string) and "list_things" (returns a JSON array,
// to exercise LuaProcess's JSON-to-Lua-table conversion).
class TestTool : public ITool {
public:
    explicit TestTool(std::function<ToolResult(const nlohmann::json&)> impl) : m_impl(std::move(impl)) {}
    ToolResult Call(std::shared_ptr<IAgent>, const nlohmann::json& args) override { return m_impl(args); }
    nlohmann::json GetParametersSchema() const override { return nlohmann::json::object(); }

private:
    std::function<ToolResult(const nlohmann::json&)> m_impl;
};

class TestToolFactory : public IToolFactory {
public:
    std::shared_ptr<ITool> CreateTool(const std::string& name, std::shared_ptr<IAgent>) override {
        if (name == "echo") {
            return std::make_shared<TestTool>([](const nlohmann::json& args) {
                return ToolResult{args.value("text", ""), false};
            });
        }
        if (name == "list_things") {
            return std::make_shared<TestTool>([](const nlohmann::json&) {
                nlohmann::json arr = nlohmann::json::array({"a", "b", "c"});
                return ToolResult{arr.dump(), false};
            });
        }
        if (name == "fail_tool") {
            return std::make_shared<TestTool>([](const nlohmann::json&) {
                return ToolResult{"boom", true};
            });
        }
        return nullptr;
    }
    bool HasTool(const std::string& name) const override {
        return name == "echo" || name == "list_things" || name == "fail_tool";
    }
    std::vector<std::string> GetAvailableTools() const override { return {"echo", "list_things", "fail_tool"}; }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override { return {}; }
};

std::shared_ptr<LuaProcess> RunScript(std::shared_ptr<TestToolFactory> toolFactory, std::shared_ptr<MockAgentConsole> console,
                                       const std::string& script, std::vector<std::string> args = {}) {
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "test_lua.lua", /*workingDirectory=*/"", /*rootFileSystem=*/nullptr,
        /*os=*/std::weak_ptr<IHaisosOS>{}, script, std::move(args), std::move(toolFactory), std::move(console));
    process->WaitToFinish(2000);
    return process;
}

} // namespace

TEST(LuaProcessTest, PrintRoutesToConsole) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, "print('hello from lua')");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("hello from lua"), std::string::npos);
}

TEST(LuaProcessTest, ToolCallReturnsStringAndErrorFlag) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local result, is_error = echo({text = "round-trip"})
        print(result .. "|" .. tostring(is_error))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("round-trip|false"), std::string::npos);
}

TEST(LuaProcessTest, ToolCallErrorFlagIsTrueOnFailure) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local result, is_error = fail_tool({})
        print(tostring(is_error))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("true"), std::string::npos);
}

TEST(LuaProcessTest, JsonArrayResultBecomesLuaTable) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local things = list_things({})
        print(#things .. ":" .. things[1] .. things[2] .. things[3])
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("3:abc"), std::string::npos);
}

TEST(LuaProcessTest, ArgsAreExposedAsArgGlobal) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, "print(arg[1] .. arg[2])", {"foo", "bar"});

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("foobar"), std::string::npos);
}

TEST(LuaProcessTest, ScriptErrorFinishesWithoutCrashing) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    auto process = RunScript(toolFactory, console, "error('boom')");

    EXPECT_TRUE(process->IsFinished());
}

TEST(LuaProcessTest, KillStopsABusyLoop) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "busy_lua.lua", /*workingDirectory=*/"", /*rootFileSystem=*/nullptr,
        /*os=*/std::weak_ptr<IHaisosOS>{}, "while true do end", std::vector<std::string>{}, toolFactory, console);

    process->Kill();
    EXPECT_TRUE(process->WaitToFinish(2000));
}

// The Lua sandbox is a security boundary: a .lua program is untrusted input,
// since an agent can write one with os_write_file and launch it with
// os_start_process. These tests pin the boundary so it cannot regress silently.

TEST(LuaProcessTest, SandboxOmitsHostAccessLibraries) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        print(tostring(io) .. "|" .. tostring(os) .. "|" .. tostring(package) .. "|" .. tostring(debug))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("nil|nil|nil|nil"), std::string::npos);
}

TEST(LuaProcessTest, SandboxRemovesChunkLoadingGlobals) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        print(tostring(dofile) .. "|" .. tostring(loadfile) .. "|" .. tostring(load) .. "|" .. tostring(warn))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("nil|nil|nil|nil"), std::string::npos);
}

TEST(LuaProcessTest, SandboxKeepsSafeLibraries) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        print(#table.concat({"a","b"}) .. string.upper("x") .. tostring(math.floor(1.5)))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_NE(console->GetMessages()[0].find("2X1"), std::string::npos);
}

TEST(LuaProcessTest, PrecompiledBytecodeIsRefused) {
    // Lua's undump does not validate untrusted bytecode, so accepting a binary
    // chunk would hand a script arbitrary memory access. A chunk starting with
    // the Lua binary signature ("\x1bLua") must be rejected at load time, which
    // means the script never runs and nothing reaches the console.
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    std::string bytecode = "\x1b" "Lua" "\x54\x00\x19\x93\r\n\x1a\n";
    auto process = RunScript(toolFactory, console, bytecode);

    EXPECT_TRUE(process->IsFinished());
    EXPECT_TRUE(console->GetMessages().empty() ||
                console->GetMessages()[0].find("Error") != std::string::npos);
}
