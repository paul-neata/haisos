#include "AgentProcess.h"

namespace Haisos {

std::shared_ptr<AgentProcess> AgentProcess::Create(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::weak_ptr<IHaisosOS> os,
    std::shared_ptr<CurrentProcessHandle> selfHandle,
    std::shared_ptr<Agent> agent)
{
    auto process = std::shared_ptr<AgentProcess>(new AgentProcess(
        pid, parentPid, std::move(environment), path, workingDirectory, std::move(os), std::move(agent)));
    // The process's own tools reach it through this handle. Filling it in here
    // -- before the agent is given anything to do -- is what guarantees no tool
    // can ever observe it empty.
    if (selfHandle) {
        selfHandle->Set(process);
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
    std::shared_ptr<Agent> agent)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_path(path)
    , m_os(os)
    , m_io(ProcessFileIO::Create(std::move(os), workingDirectory))
    , m_agent(std::move(agent))
{
}

AgentProcess::~AgentProcess() = default;

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
    return m_agent ? m_agent->Name() : std::string();
}

std::shared_ptr<IEnvironment> AgentProcess::GetEnvironment() const {
    // A clone: reading a process's environment from outside must never be a way
    // to change what the process itself sees.
    return m_environment ? m_environment->Clone() : nullptr;
}

void AgentProcess::TriggerStop() {
    m_agent->TriggerStop();
}

bool AgentProcess::WaitToFinish(uint64_t timeoutMs) {
    return m_agent->WaitToFinish(timeoutMs);
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

void AgentProcess::Kill() {
    m_agent->Kill();
}

bool AgentProcess::IsFinished() const {
    return m_agent->IsFinished();
}

}
