#include <gtest/gtest.h>
#include <algorithm>
#include <mutex>
#include "Environment.h"
#include "LuaProcess.h"
#include "tests/mocks/MockFileDescriptor.h"

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

// A finished script and the three standard streams it ran with: stdin is an
// empty input, stdout and stderr mocks hold everything the script printed or
// failed with.
struct ScriptRun {
    std::shared_ptr<LuaProcess> process;
    std::shared_ptr<MockFileDescriptor> out;
    std::shared_ptr<MockFileDescriptor> err;
};

// The stream mocks are read from the test thread while the script's own thread
// may still write them: always wait for the script first (RunScript does).
ScriptRun RunScript(std::shared_ptr<TestToolFactory> toolFactory,
                    const std::string& script, std::vector<std::string> args = {}) {
    auto in = std::make_shared<MockFileDescriptor>();
    in->EndInput();
    auto out = std::make_shared<MockFileDescriptor>();
    auto err = std::make_shared<MockFileDescriptor>();
    StartProcessOptions options;
    options.stdIn = in;
    options.stdOut = out;
    options.stdErr = err;
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "test_lua.lua", /*workingDirectory=*/"",
        /*os=*/std::weak_ptr<IHaisosOS>{}, /*selfHandle=*/nullptr, script, std::move(args), std::move(toolFactory), options);
    process->WaitToFinish(2000);
    return {process, out, err};
}

} // namespace

TEST(LuaProcessTest, PrintWritesItsLineAndNewlineToStdout) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "print('a', 1)");

    EXPECT_EQ(run.out->Written(), "a\t1\n");
    EXPECT_EQ(run.err->Written(), "");
}

TEST(LuaProcessTest, AScriptErrorGoesToStderr) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "error('boom')");

    EXPECT_TRUE(run.process->IsFinished());
    EXPECT_EQ(run.out->Written(), "");
    ASSERT_NE(run.err->Written().find("boom"), std::string::npos);
    // One line: 'lua: <path>:<line>: ...' plus its newline.
    EXPECT_EQ(run.err->Written().back(), '\n');
}

TEST(LuaProcessTest, ARuntimeErrorPrintsLuaStyleAndExitsOne) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    // As the standalone lua prints it (without the traceback): "lua: " then
    // the error as its msghandler renders it, then a newline.
    auto run = RunScript(toolFactory, "\nerror(\"boom\")");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written(), "lua: test_lua.lua:2: boom\n");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);
}

TEST(LuaProcessTest, ASyntaxErrorPrintsLuaStyleAndExitsOne) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "this is not lua");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written().rfind("lua: test_lua.lua:1:", 0), 0u) << run.err->Written();
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);
}

TEST(LuaProcessTest, ANonStringErrorIsDescribed) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    // An error value that is no string (and has no __tostring): described, as
    // lua.c's msghandler describes it.
    auto run = RunScript(toolFactory, "error({})");

    EXPECT_EQ(run.err->Written(), "lua: (error object is a table value)\n");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);
}

// An error object's __tostring is the message; one that itself raises is only
// another error of the script -- never an unprotected Lua error, on which Lua
// aborts the whole host.
TEST(LuaProcessTest, AnErrorObjectsTostringRunsProtected) {
    auto toolFactory = std::make_shared<TestToolFactory>();

    auto run = RunScript(toolFactory,
        "error(setmetatable({}, {__tostring = function() return 'custom' end}))");
    EXPECT_EQ(run.err->Written(), "lua: custom\n");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);

    run = RunScript(toolFactory,
        "error(setmetatable({}, {__tostring = function() error('inner') end}))");
    EXPECT_EQ(run.err->Written().rfind("lua: ", 0), 0u) << run.err->Written();
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);
}

TEST(LuaProcessTest, ExitEndsTheScriptWithItsCode) {
    auto toolFactory = std::make_shared<TestToolFactory>();

    auto run = RunScript(toolFactory, "print('a') exit(3) print('b')");
    EXPECT_EQ(run.out->Written(), "a\n");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 3);

    for (const auto [script, expected] : {
            std::pair{"exit()", 0}, {"exit(false)", 1}, {"exit(true)", 0},
            {"exit(256 + 7)", 7}, {"exit(-1)", 255}}) {
        run = RunScript(toolFactory, script);
        ASSERT_TRUE(run.process->ExitCode().has_value()) << script;
        EXPECT_EQ(*run.process->ExitCode(), expected) << script;
        EXPECT_EQ(run.err->Written(), "") << script;
    }
}

TEST(LuaProcessTest, ExitWithABadArgumentIsAnOrdinaryScriptError) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "exit('now')");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written().rfind("lua: test_lua.lua:1: bad argument #1", 0), 0u) << run.err->Written();
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);
}

TEST(LuaProcessTest, ExitCannotBeCaughtByPcall) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    // exit() aborts through the latched hook, as a kill does: the pcall strips
    // one level and the hook raises again before any further instruction -- so
    // neither the pcall nor the print after it survives.
    auto run = RunScript(toolFactory, "pcall(function() exit(4) end) print('after')");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 4);
}

// exit() taken one coroutine deep: resume catches the "exit" error it raises,
// but the latched hook is armed on the main thread too, so the resumer stops
// before its next instruction rather than running on.
TEST(LuaProcessTest, ExitInsideACoroutineEndsTheScript) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory,
        "coroutine.resume(coroutine.create(function() exit(2) end)) print('after')");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 2);
}

// Nested coroutines (main -> co1 -> co2): co1 is neither the coroutine that
// exited nor the main thread, so the resume wrapper arms it as its resume
// returns -- print('mid') must not run either.
TEST(LuaProcessTest, ExitInsideANestedCoroutineEndsTheScript) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory,
        "coroutine.resume(coroutine.create(function() coroutine.resume(coroutine.create(function() exit(5) end)) print('mid') end)) print('after')");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 5);
}

// The same through coroutine.wrap: the wrapped call raises the coroutine's
// error in the caller, and the pcall around it cannot swallow it.
TEST(LuaProcessTest, ExitInsideAWrappedCoroutineEndsTheScript) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory,
        "pcall(coroutine.wrap(function() exit(6) end)) print('after')");

    EXPECT_EQ(run.out->Written(), "");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 6);
}

// The resume/wrap wrappers change nothing when nothing latched: yields,
// values, statuses and caught ordinary errors all behave as before.
TEST(LuaProcessTest, CoroutinesStillWork) {
    auto toolFactory = std::make_shared<TestToolFactory>();

    auto run = RunScript(toolFactory,
        "local co = coroutine.create(function(a) local b = coroutine.yield(a + 1) print(b) end) "
        "local _, x = coroutine.resume(co, 1) print(x) coroutine.resume(co, 'z') "
        "print(coroutine.status(co)) "
        "local g = coroutine.wrap(function() coroutine.yield(7) end) print(g())");
    EXPECT_EQ(run.out->Written(), "2\nz\ndead\n7\n");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 0);

    // A plain error inside a coroutine is still caught by resume.
    run = RunScript(toolFactory,
        "print(coroutine.resume(coroutine.create(function() error('x', 0) end)))");
    EXPECT_EQ(run.out->Written(), "false\tx\n");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 0);

    // A wrapped coroutine's error reaches the caller with the caller's
    // position prefixed, as the original wrap does it; arguments to wrap after
    // the function are ignored, as the original ignores them.
    run = RunScript(toolFactory,
        "print(pcall(function() local r = coroutine.wrap(function() error('x', 0) end)() return r end))\n"
        "print(coroutine.wrap(function(a) return a end, 9)(3))\n"
        "print(select(2, pcall(coroutine.wrap)))\n"
        "print(select(2, pcall(coroutine.resume, 1)))");
    EXPECT_EQ(run.out->Written(),
        "false\ttest_lua.lua:1: x\n"
        "3\n"
        "bad argument #1 to 'coroutine.wrap' (function expected, got no value)\n"
        "bad argument #1 to 'coroutine.resume' (coroutine expected, got number)\n");
    EXPECT_EQ(run.err->Written(), "");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 0);
}

TEST(LuaProcessTest, AStoppedScriptExits143) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto in = std::make_shared<MockFileDescriptor>();
    in->EndInput();
    auto out = std::make_shared<MockFileDescriptor>();
    auto err = std::make_shared<MockFileDescriptor>();
    StartProcessOptions options;
    options.stdIn = in;
    options.stdOut = out;
    options.stdErr = err;
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "busy_lua.lua", /*workingDirectory=*/"",
        /*os=*/std::weak_ptr<IHaisosOS>{}, /*selfHandle=*/nullptr, "while true do end", std::vector<std::string>{}, toolFactory, options);

    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(2000));
    // 143 is 128 + SIGTERM: TriggerStop is Haisos's SIGTERM (see ExitCodes.h).
    ASSERT_TRUE(process->ExitCode().has_value());
    EXPECT_EQ(*process->ExitCode(), 143);
    // A stop is not a fault: nothing is reported on either stream.
    EXPECT_EQ(out->Written(), "");
    EXPECT_EQ(err->Written(), "");
}

TEST(LuaProcessTest, ExitCodeIsEmptyWhileRunning) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto in = std::make_shared<MockFileDescriptor>();
    in->EndInput();
    StartProcessOptions options;
    options.stdIn = in;
    options.stdOut = std::make_shared<MockFileDescriptor>();
    options.stdErr = std::make_shared<MockFileDescriptor>();
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "busy_lua.lua", /*workingDirectory=*/"",
        /*os=*/std::weak_ptr<IHaisosOS>{}, /*selfHandle=*/nullptr, "while true do end", std::vector<std::string>{}, toolFactory, options);

    EXPECT_FALSE(process->ExitCode().has_value());

    // Clean up: leave no busy script behind.
    process->TriggerStop();
    EXPECT_TRUE(process->WaitToFinish(2000));
}

// exit() is the sandbox's one added global; io and os stay nil.
TEST(LuaProcessTest, IoAndOsStayNil) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "print(tostring(io) .. '|' .. tostring(os) .. '|' .. type(exit))");

    EXPECT_EQ(run.out->Written(), "nil|nil|function\n");
}

TEST(LuaProcessTest, ToolCallReturnsStringAndErrorFlag) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local result, is_error = echo({text = "round-trip"})
        print(result .. "|" .. tostring(is_error))
    )");

    EXPECT_EQ(run.out->Written(), "round-trip|false\n");
}

TEST(LuaProcessTest, ToolCallErrorFlagIsTrueOnFailure) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local result, is_error = fail_tool({})
        print(tostring(is_error))
    )");

    EXPECT_EQ(run.out->Written(), "true\n");
}

TEST(LuaProcessTest, JsonArrayResultBecomesLuaTable) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local things = list_things({})
        print(#things .. ":" .. things[1] .. things[2] .. things[3])
    )");

    EXPECT_EQ(run.out->Written(), "3:abc\n");
}

TEST(LuaProcessTest, ArgsAreExposedAsArgGlobal) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "print(arg[1] .. arg[2])", {"foo", "bar"});

    EXPECT_EQ(run.out->Written(), "foobar\n");
}

TEST(LuaProcessTest, ScriptErrorFinishesWithoutCrashing) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "error('boom')");

    EXPECT_TRUE(run.process->IsFinished());
}

TEST(LuaProcessTest, KillStopsABusyLoop) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto in = std::make_shared<MockFileDescriptor>();
    in->EndInput();
    auto out = std::make_shared<MockFileDescriptor>();
    auto err = std::make_shared<MockFileDescriptor>();
    StartProcessOptions options;
    options.stdIn = in;
    options.stdOut = out;
    options.stdErr = err;
    auto process = LuaProcess::Create(
        1, 0, CreateEnvironment(), "busy_lua.lua", /*workingDirectory=*/"",
        /*os=*/std::weak_ptr<IHaisosOS>{}, /*selfHandle=*/nullptr, "while true do end", std::vector<std::string>{}, toolFactory, options);

    process->Kill();
    EXPECT_TRUE(process->WaitToFinish(2000));
    // A kill is not a fault: nothing is reported on either stream.
    EXPECT_EQ(out->Written(), "");
    EXPECT_EQ(err->Written(), "");
}

// The Lua sandbox is a security boundary: a .lua program is untrusted input,
// since an agent can write one with os_write_file and launch it with
// os_start_process. These tests pin the boundary so it cannot regress silently.

TEST(LuaProcessTest, SandboxOmitsHostAccessLibraries) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        print(tostring(io) .. "|" .. tostring(os) .. "|" .. tostring(package) .. "|" .. tostring(debug))
    )");

    EXPECT_EQ(run.out->Written(), "nil|nil|nil|nil\n");
}

TEST(LuaProcessTest, SandboxRemovesChunkLoadingGlobals) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        print(tostring(dofile) .. "|" .. tostring(loadfile) .. "|" .. tostring(load) .. "|" .. tostring(warn))
    )");

    EXPECT_EQ(run.out->Written(), "nil|nil|nil|nil\n");
}

TEST(LuaProcessTest, SandboxKeepsSafeLibraries) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        print(#table.concat({"a","b"}) .. string.upper("x") .. tostring(math.floor(1.5)))
    )");

    EXPECT_EQ(run.out->Written(), "2X1\n");
}

TEST(LuaProcessTest, PrecompiledBytecodeIsRefused) {
    // Lua's undump does not validate untrusted bytecode, so accepting a binary
    // chunk would hand a script arbitrary memory access. A chunk starting with
    // the Lua binary signature ("\x1bLua") must be rejected at load time, which
    // means the script never runs and nothing reaches its stdout -- the refusal
    // is an error line on its stderr and an exit code of 1.
    auto toolFactory = std::make_shared<TestToolFactory>();
    std::string bytecode = "\x1b" "Lua" "\x54\x00\x19\x93\r\n\x1a\n";
    auto run = RunScript(toolFactory, bytecode);

    EXPECT_TRUE(run.process->IsFinished());
    EXPECT_EQ(run.out->Written(), "");
    // No position in this one: the refusal fires before any frame exists.
    EXPECT_EQ(run.err->Written(), "lua: attempt to load a binary chunk (mode is 't')\n");
    ASSERT_TRUE(run.process->ExitCode().has_value());
    EXPECT_EQ(*run.process->ExitCode(), 1);
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
    auto run = RunScript(toolFactory, R"(
        local t = {}
        t.self = t
        local result, is_error = record_args(t)
        print(tostring(is_error) .. "|" .. result)
        print("after")
    )");

    EXPECT_TRUE(run.process->IsFinished());
    EXPECT_EQ(run.out->Written(),
        "true|record_args: arguments contain a cycle (a table that contains itself)\n"
        "after\n");
    // Refused before the tool was even looked up.
    EXPECT_EQ(toolFactory->RecordedCalls(), 0);
}

TEST(LuaProcessTest, ATableReachedTwiceIsNotACycle) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local shared = {x = 1}
        local result, is_error = record_args({a = shared, b = shared})
        print(tostring(is_error))
    )");

    EXPECT_EQ(run.out->Written(), "false\n");
    EXPECT_EQ(toolFactory->LastArgs(), nlohmann::json::parse(R"({"a": {"x": 1}, "b": {"x": 1}})"));
}

TEST(LuaProcessTest, DeeplyNestedArgumentsWithinTheLimitArriveWhole) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local t = {}
        for i = 1, 30 do t = {t} end
        local result, is_error = record_args({nested = t})
        print(tostring(is_error))
    )");

    EXPECT_EQ(run.out->Written(), "false\n");
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
    auto run = RunScript(toolFactory, R"(
        local t = {}
        for i = 1, 5000 do t = {t} end
        local result, is_error = record_args(t)
        print(tostring(is_error) .. "|" .. result)
        print("after")
    )");

    EXPECT_TRUE(run.process->IsFinished());
    EXPECT_EQ(run.out->Written(),
        "true|record_args: arguments nested too deeply (more than 100 levels of tables)\n"
        "after\n");
    EXPECT_EQ(toolFactory->RecordedCalls(), 0);
}

TEST(LuaProcessTest, ArgumentsMayNestExactlyAsDeepAsTheLimit) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local t = {}
        for i = 1, 99 do t = {t} end
        local _, at_limit = record_args(t)
        local _, beyond = record_args({t})
        print(tostring(at_limit) .. "|" .. tostring(beyond))
    )");

    // 100 levels of tables pass; the 101st does not.
    EXPECT_EQ(run.out->Written(), "false|true\n");
    EXPECT_EQ(toolFactory->RecordedCalls(), 1);
}

TEST(LuaProcessTest, AJsonResultNestedBeyondTheLimitIsHandedBackAsText) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
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

    EXPECT_TRUE(run.process->IsFinished());
    // 50 levels become tables as ever; 300 come back as the 600 characters they are.
    EXPECT_EQ(run.out->Written(), "50|string|false|600\n");
}

TEST(LuaProcessTest, NulBytesSurviveTheBridgeBothWays) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, R"(
        local raw = echo({text = "a\0b"})
        local tables = echo_args({["k\0y"] = "v\0w"})
        local recorded = record_args({text = "x\0y\0z"})
        print(#raw .. "|" .. tostring(raw == "a\0b") .. "|" .. tostring(tables["k\0y"] == "v\0w"))
    )");

    EXPECT_EQ(run.out->Written(), "3|true|true\n");
    EXPECT_EQ(toolFactory->LastArgs().value("text", ""), std::string("x\0y\0z", 5));
}

TEST(LuaProcessTest, ArgKeepsNulBytes) {
    auto toolFactory = std::make_shared<TestToolFactory>();
    auto run = RunScript(toolFactory, "print(#arg[1] .. '|' .. tostring(arg[1] == 'x\\0y'))", {std::string("x\0y", 3)});

    EXPECT_EQ(run.out->Written(), "3|true\n");
}
