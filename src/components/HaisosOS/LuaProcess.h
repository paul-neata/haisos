#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "OSProcess.h"
#include "ProcessFileIO.h"
#include "src/components/libheaders/CurrentProcessHandle.h"
#include "interfaces/IHaisosOS.h"

struct lua_State;

namespace Haisos {

// An ICurrentProcess whose runtime is a Lua script. Each OS tool is exposed as
// a Lua global function returning (content, is_error); output from Lua's print()
// routes through the process's IAgentConsole. Runs on its own background
// thread, matching Agent's lifecycle shape.
class LuaProcess : public IOSProcess {
public:
    static std::shared_ptr<LuaProcess> Create(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::weak_ptr<IHaisosOS> os,
        std::shared_ptr<CurrentProcessHandle> selfHandle,
        std::string scriptContent,
        std::vector<std::string> args,
        std::shared_ptr<IToolFactory> toolFactory,
        std::shared_ptr<IAgentConsole> console);
    ~LuaProcess() override;

    // IProcess
    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Path() const override;
    std::string StartingAgentName() const override;
    std::shared_ptr<IEnvironment> GetEnvironment() const override;
    void TriggerStop() override;
    bool WaitToFinish(uint64_t timeoutMs) override;

    // ICurrentProcess
    std::shared_ptr<IFileIO> IO() const override;
    std::shared_ptr<IAgent> AsAgent() override;
    std::shared_ptr<IHaisosOS> OS() const override;

    // IOSProcess: forcing the process down, which no IProcess handle can do.
    void Kill() override;

    // Internal to this component.
    bool Stop(unsigned timeoutMs);
    bool IsFinished() const;
    void WaitToFinish();

    // Used by the Lua kill-hook (a free function, since lua_Debug is not
    // available where this class is declared).
    bool IsKillRequested() const;

private:
    LuaProcess(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::weak_ptr<IHaisosOS> os,
        std::string scriptContent,
        std::vector<std::string> args,
        std::shared_ptr<IToolFactory> toolFactory,
        std::shared_ptr<IAgentConsole> console);

    // Starts the script's thread. Called by Create() once the process is fully
    // built, so the thread never observes a half-constructed object.
    void Start();

    void RunThread();
    void RegisterBindings(lua_State* L);

    static int LuaToolTrampoline(lua_State* L);
    static int LuaPrintTrampoline(lua_State* L);

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::shared_ptr<IEnvironment> m_environment;
    std::string m_path;
    // Weak: the OS owns its processes, so a strong reference back would be a
    // cycle neither could escape.
    std::weak_ptr<IHaisosOS> m_os;
    // This process's file I/O, and the only route it has to a filesystem.
    std::shared_ptr<IFileIO> m_io;
    std::string m_scriptContent;
    std::vector<std::string> m_args;
    std::shared_ptr<IToolFactory> m_toolFactory;
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
