#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "interfaces/IProcess.h"
#include "ProcessFileIO.h"
#include "src/components/libheaders/CurrentProcessHandle.h"
#include "src/components/libheaders/StopToken.h"
#include "interfaces/IHaisosOS.h"

struct lua_State;

namespace Haisos {

// An ICurrentProcess whose runtime is a Lua script. Each OS tool is exposed as
// a Lua global function returning (content, is_error); Lua's print() writes its
// line plus '\n' to the process's stdout (descriptor 1), and a load or runtime
// error goes to its stderr (descriptor 2). Runs on its own background thread,
// matching Agent's lifecycle shape.
class LuaProcess : public ICurrentProcess {
public:
    // options' streams become the process's descriptors 0, 1 and 2 before the
    // script's thread starts; nullptr (after a log line) if they do not.
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
        const StartProcessOptions& options);
    ~LuaProcess() override;

    // IProcess
    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Path() const override;
    std::string StartingAgentName() const override;
    std::shared_ptr<IEnvironment> GetEnvironment() const override;
    void TriggerStop() override;
    bool WaitToFinish(uint64_t timeoutMs) override;
    // 0 for a script that ran to the end, the code exit(code) asked for, 1 for
    // a load or runtime error, 143 when it was stopped (see IProcess::ExitCode).
    std::optional<int> ExitCode() const override;

    // ICurrentProcess
    std::shared_ptr<IFileIO> IO() const override;
    std::shared_ptr<IAgent> AsAgent() override;
    std::shared_ptr<IHaisosOS> OS() const override;
    // The script's own output paths (print and the error line) call this when a
    // write hit a pipe with no reader: the script stops through the kill-hook
    // machinery, quietly, with exit code 141 -- as SIGPIPE would have it.
    void StopForBrokenPipe() override;

    // Internal to this component. Kill aborts the script through the Lua
    // instruction-count hook -- a Lua interpreter, unlike an agent, really can
    // be interrupted mid-instruction. TriggerStop() is the same thing here:
    // a script has no command queue to close, so stopping it is aborting it.
    void Kill();
    bool IsFinished() const;
    void WaitToFinish();

    // Used by the Lua kill-hook (a free function, since lua_Debug is not
    // available where this class is declared). The exit flag is there too:
    // exit() aborts through the same hook, so a pcall cannot swallow either.
    bool IsKillRequested() const;
    bool IsExitRequested() const;

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
        std::shared_ptr<IToolFactory> toolFactory);

    // Starts the script's thread. Called by Create() once the process is fully
    // built, so the thread never observes a half-constructed object.
    void Start();

    void RunThread();
    void RegisterBindings(lua_State* L);
    // Whether the calling thread is the script's own.
    bool IsOwnThread();
    // All of bytes to the process's descriptor fd, wherever it points now; an
    // empty slot drops them.
    void WriteToDescriptor(int fd, const std::string& bytes);

    static int LuaToolTrampoline(lua_State* L);
    static int LuaPrintTrampoline(lua_State* L);
    static int LuaExitTrampoline(lua_State* L);

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::shared_ptr<IEnvironment> m_environment;
    std::string m_path;
    // Weak: the OS owns its processes, so a strong reference back would be a
    // cycle neither could escape.
    std::weak_ptr<IHaisosOS> m_os;
    // This process's file I/O, and the only route it has to a filesystem.
    // Concrete, so RunThread can reach the descriptor table's
    // ReleaseAllDescriptors, which is not on IFileIO.
    std::shared_ptr<ProcessFileIO> m_io;
    std::string m_scriptContent;
    std::vector<std::string> m_args;
    std::shared_ptr<IToolFactory> m_toolFactory;

    lua_State* m_luaState = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_killed{false};
    // Set by StopForBrokenPipe, from the script's own thread as it writes: an
    // output pipe lost its reader. Decides 141, checked before everything else.
    std::atomic<bool> m_brokenPipe{false};
    // What wakes a pipe Read/Write blocked on this process's behalf: installed
    // on the script's thread by RunThread, signalled by Kill. A script's exit()
    // does not touch it: exit() ends the script by unwinding, not by blocking.
    std::shared_ptr<StopToken> m_stopToken = StopToken::Create();
    // Set by the script's own call to the exit() global: the flag is what the
    // hook keeps raising on, the code what ExitCode() reports for it.
    std::atomic<bool> m_exitRequested{false};
    std::atomic<int> m_exitCodeRequested{0};
    // Set under m_finishedMutex in the same critical section that marks the
    // process finished, so whoever sees finished finds it already in place.
    std::optional<int> m_exitCode;
    std::condition_variable m_finishedCv;
    mutable std::mutex m_finishedMutex;
    std::mutex m_joinMutex;
};

}
