#include <gtest/gtest.h>
#include <algorithm>
#include <mutex>
#include "Environment.h"
#include "LuaProcess.h"
#include "tests/mocks/MockAgentConsole.h"

using namespace Haisos;
using namespace Haisos::Mocks;

namespace {

// A tiny IToolFactory test double. Its tools exercise the Lua <-> JSON bridge:
//   echo           returns its "text" argument as a plain string
//   list_things    returns a JSON array (handed to the script as a table)
//   fail_tool      fails
//   record_args    keeps the arguments it was called with (see LastArgs)
//   echo_args      returns its arguments as JSON (so they come back as tables)
//   nested_result  returns a JSON array nested "depth" levels deep
class TestTool : public ITool {
public:
    explicit TestTool(std::function<ToolResult(const nlohmann::json&)> impl) : m_impl(std::move(impl)) {}
    ToolResult Call(std::shared_ptr<IAgent>, const nlohmann::json& args) override { return m_impl(args); }
    nlohmann::json GetParametersSchema() const override { return nlohmann::json::object(); }

private:
    std::function<ToolResult(const nlohmann::json&)> m_impl;
};

// What record_args saw. Shared, since a tool outlives the factory call that
// made it, and locked, since the script calls it on a thread of its own.
struct RecordedArgs {
    std::mutex mutex;
    int calls = 0;
    nlohmann::json args;
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
        if (name == "record_args") {
            auto recorded = m_recorded;
            return std::make_shared<TestTool>([recorded](const nlohmann::json& args) {
                std::lock_guard<std::mutex> lock(recorded->mutex);
                ++recorded->calls;
                recorded->args = args;
                return ToolResult{"recorded", false};
            });
        }
        if (name == "echo_args") {
            return std::make_shared<TestTool>([](const nlohmann::json& args) {
                return ToolResult{args.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false};
            });
        }
        if (name == "nested_result") {
            return std::make_shared<TestTool>([](const nlohmann::json& args) {
                const size_t depth = args.value("depth", 1);
                return ToolResult{std::string(depth, '[') + std::string(depth, ']'), false};
            });
        }
        return nullptr;
    }
    bool HasTool(const std::string& name) const override {
        const auto tools = GetAvailableTools();
        return std::find(tools.begin(), tools.end(), name) != tools.end();
    }
    std::vector<std::string> GetAvailableTools() const override {
        return {"echo", "list_things", "fail_tool", "record_args", "echo_args", "nested_result"};
    }
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> GetAvailableToolDescriptions() const override { return {}; }

    int RecordedCalls() const {
        std::lock_guard<std::mutex> lock(m_recorded->mutex);
        return m_recorded->calls;
    }
    nlohmann::json LastArgs() const {
        std::lock_guard<std::mutex> lock(m_recorded->mutex);
        return m_recorded->args;
    }

private:
    std::shared_ptr<RecordedArgs> m_recorded = std::make_shared<RecordedArgs>();
};

std::shared_ptr<LuaProcess> RunScript(std::shared_ptr<TestToolFactory> toolFactory, std::shared_ptr<MockAgentConsole> console,
                                       const std::string& script, std::vector<std::string> args = {}) {
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "test_lua.lua", /*workingDirectory=*/"",
        /*os=*/std::weak_ptr<IHaisosOS>{}, /*selfHandle=*/nullptr, script, std::move(args), std::move(toolFactory), std::move(console));
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
        1, 0, CreateEnvironment(), "busy_lua.lua", /*workingDirectory=*/"",
        /*os=*/std::weak_ptr<IHaisosOS>{}, /*selfHandle=*/nullptr, "while true do end", std::vector<std::string>{}, toolFactory, console);

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

// --- The Lua <-> JSON bridge ---
//
// Every tool a script calls goes through ToJson (the script's table becomes
// the tool's JSON arguments) and PushJson (a JSON result becomes Lua tables).
// Both recurse once per level of nesting. Before they were bounded, a table
// containing itself or nested a few dozen levels deep -- or a file of deeply
// nested JSON the script merely read -- overflowed the Lua stack (silent heap
// corruption) or the C++ one (a crash of the whole program), and strings were
// cut at their first NUL byte. A script is untrusted input: an agent can write
// one and start it.

TEST(LuaProcessTest, ArgumentsContainingThemselvesAreRefusedAndTheScriptGoesOn) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    auto process = RunScript(toolFactory, console, R"(
        local t = {}
        t.self = t
        local result, is_error = record_args(t)
        print(tostring(is_error) .. "|" .. result)
        print("after")
    )");

    EXPECT_TRUE(process->IsFinished());
    const auto messages = console->GetMessages();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0], "true|record_args: arguments contain a cycle (a table that contains itself)");
    EXPECT_EQ(messages[1], "after");
    // Refused before the tool was even looked up.
    EXPECT_EQ(toolFactory->RecordedCalls(), 0);
}

TEST(LuaProcessTest, ATableReachedTwiceIsNotACycle) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local shared = {x = 1}
        local result, is_error = record_args({a = shared, b = shared})
        print(tostring(is_error))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_EQ(console->GetMessages()[0], "false");
    EXPECT_EQ(toolFactory->LastArgs(), nlohmann::json::parse(R"({"a": {"x": 1}, "b": {"x": 1}})"));
}

TEST(LuaProcessTest, DeeplyNestedArgumentsWithinTheLimitArriveWhole) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local t = {}
        for i = 1, 30 do t = {t} end
        local result, is_error = record_args({nested = t})
        print(tostring(is_error))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_EQ(console->GetMessages()[0], "false");
    const nlohmann::json args = toolFactory->LastArgs();
    ASSERT_TRUE(args.contains("nested"));
    // Thirty one-element tables became thirty one-element arrays; the empty
    // table at the bottom has no keys to make it an array, so it is an object.
    const nlohmann::json* level = &args["nested"];
    int arrays = 0;
    while (level->is_array() && level->size() == 1) {
        level = &(*level)[0];
        ++arrays;
    }
    EXPECT_EQ(arrays, 30);
    EXPECT_TRUE(level->is_object() && level->empty());
}

TEST(LuaProcessTest, ArgumentsNestedBeyondTheLimitAreRefusedAndTheScriptGoesOn) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    auto process = RunScript(toolFactory, console, R"(
        local t = {}
        for i = 1, 5000 do t = {t} end
        local result, is_error = record_args(t)
        print(tostring(is_error) .. "|" .. result)
        print("after")
    )");

    EXPECT_TRUE(process->IsFinished());
    const auto messages = console->GetMessages();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0], "true|record_args: arguments nested too deeply (more than 100 levels of tables)");
    EXPECT_EQ(messages[1], "after");
    EXPECT_EQ(toolFactory->RecordedCalls(), 0);
}

TEST(LuaProcessTest, ArgumentsMayNestExactlyAsDeepAsTheLimit) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local t = {}
        for i = 1, 99 do t = {t} end
        local _, at_limit = record_args(t)
        local _, beyond = record_args({t})
        print(tostring(at_limit) .. "|" .. tostring(beyond))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    // 100 levels of tables pass; the 101st does not.
    EXPECT_EQ(console->GetMessages()[0], "false|true");
    EXPECT_EQ(toolFactory->RecordedCalls(), 1);
}

TEST(LuaProcessTest, AJsonResultNestedBeyondTheLimitIsHandedBackAsText) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    auto process = RunScript(toolFactory, console, R"(
        local shallow = nested_result({depth = 50})
        local levels = 0
        local t = shallow
        while type(t) == "table" do
            levels = levels + 1
            t = t[1]
        end
        local deep, is_error = nested_result({depth = 300})
        print(levels .. "|" .. type(deep) .. "|" .. tostring(is_error) .. "|" .. #deep)
    )");

    EXPECT_TRUE(process->IsFinished());
    ASSERT_FALSE(console->GetMessages().empty());
    // 50 levels become tables as ever; 300 come back as the 600 characters they are.
    EXPECT_EQ(console->GetMessages()[0], "50|string|false|600");
}

TEST(LuaProcessTest, NulBytesSurviveTheBridgeBothWays) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, R"(
        local raw = echo({text = "a\0b"})
        local tables = echo_args({["k\0y"] = "v\0w"})
        local recorded = record_args({text = "x\0y\0z"})
        print(#raw .. "|" .. tostring(raw == "a\0b") .. "|" .. tostring(tables["k\0y"] == "v\0w"))
    )");

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_EQ(console->GetMessages()[0], "3|true|true");
    EXPECT_EQ(toolFactory->LastArgs().value("text", ""), std::string("x\0y\0z", 5));
}

TEST(LuaProcessTest, ArgKeepsNulBytes) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto console = std::make_shared<MockAgentConsole>();
    RunScript(toolFactory, console, "print(#arg[1] .. '|' .. tostring(arg[1] == 'x\\0y'))", {std::string("x\0y", 3)});

    ASSERT_FALSE(console->GetMessages().empty());
    EXPECT_EQ(console->GetMessages()[0], "3|true");
}
