#include "AgentProcess.h"
#include "ProcessWorkingDirectory.h"

namespace Haisos {

std::shared_ptr<AgentProcess> AgentProcess::Create(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::weak_ptr<IHaisosOS> os,
    std::shared_ptr<Agent> agent)
{
    return std::shared_ptr<AgentProcess>(new AgentProcess(
        pid, parentPid, std::move(environment), path, workingDirectory,
        std::move(rootFileSystem), std::move(os), std::move(agent)));
}

AgentProcess::AgentProcess(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& path,
    const std::string& workingDirectory,
    std::shared_ptr<IFileSystem> rootFileSystem,
    std::weak_ptr<IHaisosOS> os,
    std::shared_ptr<Agent> agent)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_path(path)
    , m_rootFileSystem(std::move(rootFileSystem))
    , m_os(std::move(os))
    , m_agent(std::move(agent))
    , m_workingDirectory(NormalizeWorkingDirectory(workingDirectory))
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

std::string AgentProcess::GetCurrentDirectory() const {
    std::lock_guard<std::mutex> lock(m_workingDirectoryMutex);
    return m_workingDirectory;
}

int AgentProcess::ChangeDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_workingDirectoryMutex);
    return ChangeWorkingDirectory(m_rootFileSystem.get(), m_workingDirectory, path);
}

std::shared_ptr<IAgent> AgentProcess::AsAgent() {
    return m_agent;
}

std::shared_ptr<IHaisosOS> AgentProcess::GetHaisosOS() const {
    // The one door out of this process: everything it reaches beyond its own
    // memory comes from here (see ICurrentProcess).
    return m_os.lock();
}

bool AgentProcess::Stop(unsigned timeoutMs) {
    return m_agent->Stop(timeoutMs);
}

void AgentProcess::Kill() {
    m_agent->Kill();
}

bool AgentProcess::IsFinished() const {
    return m_agent->IsFinished();
}

}
