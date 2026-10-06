#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
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
// lines read from its stdin, and is not finished until that loop is: see
// StartProcessOptions::interactive.
class AgentProcess : public ICurrentProcess {
public:
    // Returns nullptr if agent or environment is null: both are required for
    // the life of the process, so every method here may assume them. Also
    // nullptr if options' standard streams cannot be installed.
    //
    // options' streams become the process's descriptors 0, 1 and 2 before the
    // agent is given anything to do. Once the process exists, program is
    // posted to the agent as its first command, so the process is running when
    // this returns. With options.interactive set, the agent is then fed the
    // lines of options.stdIn, a line at a time.
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
        const StartProcessOptions& options);
    ~AgentProcess() override;

    // IProcess
    //
    // TriggerStop asks the agent to stop. For an interactive process,
    // WaitToFinish also waits for the input loop, which notices the agent has
    // closed only once the next line arrives (see AgentInputLoop). ExitCode is
    // empty until the process has finished; then 143 when it was stopped from
    // outside, else 1 when the agent's last command failed, else 0.
    uint64_t GetPid() const override;
    uint64_t GetParentPid() const override;
    std::string Path() const override;
    std::string StartingAgentName() const override;
    std::shared_ptr<IEnvironment> GetEnvironment() const override;
    void TriggerStop() override;
    bool WaitToFinish(uint64_t timeoutMs) override;
    std::optional<int> ExitCode() const override;

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
    // Concrete, so Create can hook the descriptor table's
    // ReleaseAllDescriptors, which is not on IFileIO, onto the agent's end.
    std::shared_ptr<ProcessFileIO> m_io;
    std::shared_ptr<Agent> m_agent;
    // Set by TriggerStop while the process was still running: that is a stop
    // from outside, which the exit code reports as 143. (The input loop stops
    // the agent directly at end of input, which does not count.)
    std::atomic<bool> m_stopRequested{false};
    // Computed once, the first time ExitCode is asked for on a finished
    // process, and latched under this mutex.
    mutable std::mutex m_exitCodeMutex;
    mutable std::optional<int> m_exitCode;
    // Null for a non-interactive process. Declared after m_agent so it is
    // destroyed first: it holds the agent too, and waits its thread out.
    std::shared_ptr<AgentInputLoop> m_inputLoop;
};

}
