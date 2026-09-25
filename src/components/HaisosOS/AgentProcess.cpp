#include "AgentProcess.h"
#include <chrono>
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

std::shared_ptr<AgentProcess> AgentProcess::Create(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::weak_ptr<IHaisosOS> os,
    std::shared_ptr<CurrentProcessHandle> selfHandle,
    std::shared_ptr<Agent> agent,
    const std::string& program,
    std::shared_ptr<IAgentConsole> interactiveInput)
{
    // The two things this process cannot be without, refused here so that every
    // method below may simply use them. An agent process with no agent has no
    // runtime to be a process for, and one with no environment would silently
    // run with nothing in it -- both are caller bugs, not states to tolerate.
    if (!agent) {
        LogError("AgentProcess: refusing to create a process for '%s': no agent was passed", path.c_str());
        return nullptr;
    }
    if (!environment) {
        LogError("AgentProcess: refusing to create a process for '%s': no environment was passed", path.c_str());
        return nullptr;
    }

    // The agent's own thread can hold the last reference to the process --
    // inside an os_* tool call -- and destroying it waits for the input loop
    // and stops the agent, whose thread that is.
    auto process = std::shared_ptr<AgentProcess>(
        new AgentProcess(
            pid, parentPid, std::move(environment), path, workingDirectory, std::move(os), std::move(agent),
            std::move(interactiveInput)),
        DestroyOffRuntimeThreads<AgentProcess>("AgentProcess '" + path + "' pid=" + std::to_string(pid)));
    // The process's own tools reach it through this handle. Filling it in here
    // -- before the agent is given anything to do -- is what guarantees no tool
    // can ever observe it empty.
    if (selfHandle) {
        selfHandle->Set(process);
    }

    // Only now: the agent's first command may call a tool, and a tool must
    // find the process it acts for, which the handle now provides.
    process->m_agent->Post(program);
    // And only after the program: a line typed early must not overtake it.
    if (process->m_inputLoop) {
        process->m_inputLoop->Start();
    }
    return process;
}

AgentProcess::AgentProcess(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::weak_ptr<IHaisosOS> os,
    std::shared_ptr<Agent> agent,
    std::shared_ptr<IAgentConsole> interactiveInput)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_path(path)
    , m_os(os)
    , m_io(ProcessFileIO::Create(std::move(os), workingDirectory))
    , m_agent(std::move(agent))
    , m_inputLoop(interactiveInput ? AgentInputLoop::Create(m_agent, std::move(interactiveInput)) : nullptr)
{
}

AgentProcess::~AgentProcess() {
    LogDebug("AgentProcess '%s' pid=%llu: destroying", m_path.c_str(), static_cast<unsigned long long>(m_pid));
    // Nothing will feed the agent once the process is gone, so it is asked to
    // stop before the input loop's destructor waits for it to be closed.
    m_agent->TriggerStop();
}

uint64_t AgentProcess::GetPid() const {
    return m_pid;
}

uint64_t AgentProcess::GetParentPid() const {
    return m_parentPid;
}

std::string AgentProcess::Path() const {
    return m_path;
}

std::string AgentProcess::StartingAgentName() const {
    return m_agent->Name();
}

std::shared_ptr<IEnvironment> AgentProcess::GetEnvironment() const {
    // A clone: reading a process's environment from outside must never be a way
    // to change what the process itself sees.
    return m_environment->Clone();
}

void AgentProcess::TriggerStop() {
    m_agent->TriggerStop();
}

bool AgentProcess::WaitToFinish(uint64_t timeoutMs) {
    const auto start = std::chrono::steady_clock::now();
    if (!m_agent->WaitToFinish(timeoutMs)) {
        return false;
    }
    if (!m_inputLoop) {
        return true;
    }
    // One budget for both: whatever the agent's wait used up is not given to
    // the input loop again.
    const auto elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
    return m_inputLoop->WaitToFinish(elapsedMs >= timeoutMs ? 0 : timeoutMs - elapsedMs);
}

std::shared_ptr<IFileIO> AgentProcess::IO() const {
    return m_io;
}

std::shared_ptr<IAgent> AgentProcess::AsAgent() {
    return m_agent;
}

std::shared_ptr<IHaisosOS> AgentProcess::OS() const {
    // The one door out of this process: everything it reaches beyond its own
    // memory comes from here or from IO() (see ICurrentProcess).
    return m_os.lock();
}

}
