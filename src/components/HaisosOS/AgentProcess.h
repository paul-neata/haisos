#pragma once
#include <memory>
#include <string>
#include "interfaces/IProcess.h"
#include "AgentInputLoop.h"
#include "ProcessFileIO.h"
#include "src/components/libheaders/CurrentProcessHandle.h"
#include "src/components/Agent/Agent.h"

namespace Haisos {

// An ICurrentProcess whose runtime is an LLM agent. It holds the concrete Agent
// because it owns the agent's lifetime: the agent is destroyed with the process,
// and ~Agent is what waits the agent's thread out. There is nothing stronger
// than TriggerStop to do to an agent, so nothing here offers one.
//
// An interactive process also owns the AgentInputLoop feeding its agent the
// lines typed on its console, and is not finished until that loop is: see
// StartProcessOptions::interactiveAgent.
class AgentProcess : public ICurrentProcess {
public:
    // Returns nullptr if agent or environment is null: both are required for
    // the life of the process, so every method here may assume them.
    //
    // Once the process exists, program is posted to the agent as its first
    // command, so the process is running when this returns. interactiveInput,
    // when non-null, is the console the process's agent is then fed from, a
    // line at a time; pass null for an agent that just runs its program.
    static std::shared_ptr<AgentProcess> Create(
        uint64_t pid,
        uint64_t parentPid,
        std::shared_ptr<IEnvironment> environment,
        const std::string& path,
        const std::string& workingDirectory,
        std::weak_ptr<IHaisosOS> os,
        std::shared_ptr<CurrentProcessHandle> selfHandle,
        std::shared_ptr<Agent> agent,
        const std::string& program,
        std::shared_ptr<IAgentConsole> interactiveInput);
    ~AgentProcess() override;

    // IProcess
    //
    // TriggerStop asks the agent to stop. For an interactive process,
    // WaitToFinish also waits for the input loop, which notices the agent has
    // closed only once the next line arrives (see AgentInputLoop).
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
        std::shared_ptr<Agent> agent,
        std::shared_ptr<IAgentConsole> interactiveInput);

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
    // Null for a non-interactive process. Declared after m_agent so it is
    // destroyed first: it holds the agent too, and waits its thread out.
    std::shared_ptr<AgentInputLoop> m_inputLoop;
};

}
