#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "interfaces/IHaisosOS.h"

struct lua_State;

namespace Haisos {

// An IProcess whose runtime is a Lua script. Each OS tool is exposed as a Lua
// global function returning (content, is_error); output from Lua's print()
// routes through the process's IAgentConsole. Runs on its own background
// thread, matching Agent's lifecycle shape.
class LuaProcess : public IProcess {
public:
    LuaProcess(
        uint64_t pid,
        uint64_t parentPid,
        const std::string& name,
        std::string scriptContent,
        std::vector<std::string> args,
        IToolFactory& toolFactory,
        std::shared_ptr<IAgentConsole> console);
    ~LuaProcess() override;

    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Name() const override;

    bool IsFinished() const override;
    void WaitToFinish() override;
    bool WaitToFinish(uint64_t timeoutMs) override;
    bool Stop(unsigned timeoutMs) override;
    void Kill() override;

    std::shared_ptr<IAgent> AsAgent() const override;

    // Used by the Lua kill-hook (a free function, since lua_Debug is not
    // available where this class is declared).
    bool IsKillRequested() const;

private:
    void RunThread();
    void RegisterBindings(lua_State* L);

    static int LuaToolTrampoline(lua_State* L);
    static int LuaPrintTrampoline(lua_State* L);

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::string m_name;
    std::string m_scriptContent;
    std::vector<std::string> m_args;
    IToolFactory& m_toolFactory;
    std::shared_ptr<IAgentConsole> m_console;

    lua_State* m_luaState = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_killed{false};
    std::condition_variable m_finishedCv;
    std::mutex m_finishedMutex;
    std::mutex m_joinMutex;
};

}
