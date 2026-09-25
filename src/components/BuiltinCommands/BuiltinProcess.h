#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "interfaces/IBuiltinCommands.h"
#include "interfaces/IProcess.h"
#include "BuiltinCommand.h"

namespace Haisos {

// An ICurrentProcess whose runtime is a builtin command: the command runs on a
// thread of its own, the process finishing when it returns. Shaped like
// LuaProcess -- built, then started by Create() once it is whole, so the
// thread never sees a half-constructed object.
class BuiltinProcess : public ICurrentProcess {
public:
    static std::shared_ptr<BuiltinProcess> Create(
        const BuiltinCommandHost& host,
        std::shared_ptr<IEnvironment> environment,
        std::shared_ptr<IBuiltinCommand> command,
        std::vector<std::string> args,
        const std::string& workingDirectory);
    ~BuiltinProcess() override;

    // IProcess
    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Path() const override;
    std::string StartingAgentName() const override;
    std::shared_ptr<IEnvironment> GetEnvironment() const override;
    // Asks the command to finish early. A command checks between steps of its
    // work, so this is a request, as it is for every process.
    void TriggerStop() override;
    bool WaitToFinish(uint64_t timeoutMs) override;

    // ICurrentProcess
    std::shared_ptr<IFileIO> IO() const override;
    std::shared_ptr<IAgent> AsAgent() override;
    std::shared_ptr<IHaisosOS> OS() const override;

    // Internal to this component (and its tests): the command's exit status,
    // meaningful once the process has finished. IProcess has no exit status yet.
    int ExitStatus() const;

private:
    BuiltinProcess(
        const BuiltinCommandHost& host,
        std::shared_ptr<IEnvironment> environment,
        std::shared_ptr<IBuiltinCommand> command,
        std::vector<std::string> args,
        const std::string& workingDirectory);

    void Start();
    void RunThread();
    // Whether the calling thread is the command's own.
    bool IsOwnThread();

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::string m_path;
    std::shared_ptr<IEnvironment> m_environment;
    // Weak: the OS owns its processes.
    std::weak_ptr<IHaisosOS> m_os;
    std::shared_ptr<IFileIO> m_io;
    std::shared_ptr<IAgentConsole> m_console;
    std::shared_ptr<IBuiltinCommand> m_command;
    std::vector<std::string> m_args;

    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_finished{false};
    std::atomic<int> m_exitStatus{0};
    std::mutex m_finishedMutex;
    std::condition_variable m_finishedCv;
    std::mutex m_joinMutex;
};

} // namespace Haisos
