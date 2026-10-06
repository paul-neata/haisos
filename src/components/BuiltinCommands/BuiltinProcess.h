#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "interfaces/IBuiltinCommands.h"
#include "interfaces/IProcess.h"
#include "BuiltinCommand.h"

namespace Haisos {

class ProcessFileIO;

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
        const std::string& workingDirectory,
        const StartProcessOptions& options);
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
    // The command's own result modulo 256 once it has returned, or 143 when it
    // was asked to stop first (see IProcess::ExitCode).
    std::optional<int> ExitCode() const override;

    // ICurrentProcess
    std::shared_ptr<IFileIO> IO() const override;
    std::shared_ptr<IAgent> AsAgent() override;
    std::shared_ptr<IHaisosOS> OS() const override;

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
    // Concrete, so RunThread (and the tests) can reach the descriptor table's
    // ReleaseAllDescriptors, which is not on IFileIO.
    std::shared_ptr<ProcessFileIO> m_io;
    std::shared_ptr<IBuiltinCommand> m_command;
    std::vector<std::string> m_args;

    std::thread m_thread;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_finished{false};
    // Set under m_finishedMutex in the same critical section that marks the
    // process finished, so whoever sees finished finds it already in place.
    std::optional<int> m_exitCode;
    mutable std::mutex m_finishedMutex;
    std::condition_variable m_finishedCv;
    std::mutex m_joinMutex;
};

} // namespace Haisos
