#pragma once
#include <memory>
#include <mutex>
#include <string>
#include "OSProcess.h"
#include "interfaces/IFileSystemService.h"
#include "src/components/Agent/Agent.h"

namespace Haisos {

// An ICurrentProcess whose runtime is an LLM agent. It holds the concrete Agent
// rather than an IAgent: killing is deliberately not part of IAgent, and a
// process is exactly the thing that has to be able to do it.
class AgentProcess : public IOSProcess {
public:
    static std::shared_ptr<AgentProcess> Create(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::weak_ptr<IHaisosOS> os,
        std::shared_ptr<Agent> agent);
    ~AgentProcess() override;

    // IProcess
    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Path() const override;
    std::string StartingAgentName() const override;
    std::shared_ptr<IEnvironment> GetEnvironment() const override;
    void TriggerStop() override;
    bool WaitToFinish(uint64_t timeoutMs) override;

    // ICurrentProcess
    std::string GetCurrentDirectory() const override;
    int ChangeDirectory(const std::string& path) override;
    std::shared_ptr<IAgent> AsAgent() override;
    std::shared_ptr<IHaisosOS> GetHaisosOS() const override;

    // IOSProcess: forcing the process down, which no IProcess handle can do.
    void Kill() override;

    // Internal to this component, for a bounded stop-then-wait.
    bool Stop(unsigned timeoutMs);
    bool IsFinished() const;

private:
    AgentProcess(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::shared_ptr<IFileSystem> rootFileSystem,
        std::weak_ptr<IHaisosOS> os,
        std::shared_ptr<Agent> agent);

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::shared_ptr<IEnvironment> m_environment;
    std::string m_path;
    std::shared_ptr<IFileSystem> m_rootFileSystem;
    // Weak: the OS owns its processes, so a strong reference back would be a
    // cycle neither could escape.
    std::weak_ptr<IHaisosOS> m_os;
    std::shared_ptr<Agent> m_agent;

    // The process's own working directory. Guarded because the agent's thread
    // and whoever inspects the process run concurrently.
    mutable std::mutex m_workingDirectoryMutex;
    std::string m_workingDirectory;
};

}
