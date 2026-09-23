#pragma once
#include <memory>
#include <string>
#include "OSProcess.h"
#include "ProcessFileIO.h"
#include "src/components/libheaders/CurrentProcessHandle.h"
#include "src/components/Agent/Agent.h"

namespace Haisos {

// An ICurrentProcess whose runtime is an LLM agent. It holds the concrete Agent
// because it owns the agent's lifetime: the agent is destroyed with the process.
// It does not override IOSProcess::Kill -- an agent cannot be forced down, so
// the inherited default (ask again) is all there is.
class AgentProcess : public IOSProcess {
public:
    // Returns nullptr if agent or environment is null: both are required for
    // the life of the process, so every method here may assume them.
    static std::shared_ptr<AgentProcess> Create(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::weak_ptr<IHaisosOS> os,
        std::shared_ptr<CurrentProcessHandle> selfHandle,
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
    std::shared_ptr<IFileIO> IO() const override;
    std::shared_ptr<IAgent> AsAgent() override;
    std::shared_ptr<IHaisosOS> OS() const override;

private:
    AgentProcess(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::weak_ptr<IHaisosOS> os,
        std::shared_ptr<Agent> agent);

    uint64_t m_pid;
    uint64_t m_parentPid;
    std::shared_ptr<IEnvironment> m_environment;
    std::string m_path;
    // Weak: the OS owns its processes, so a strong reference back would be a
    // cycle neither could escape.
    std::weak_ptr<IHaisosOS> m_os;
    // This process's file I/O, and the only route it has to a filesystem.
    std::shared_ptr<IFileIO> m_io;
    std::shared_ptr<Agent> m_agent;
};

}
