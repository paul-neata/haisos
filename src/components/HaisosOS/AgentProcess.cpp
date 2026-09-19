#include "AgentProcess.h"

namespace Haisos {

std::shared_ptr<AgentProcess> AgentProcess::Create(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& name,
    std::shared_ptr<Agent> agent)
{
    return std::shared_ptr<AgentProcess>(
        new AgentProcess(pid, parentPid, std::move(environment), name, std::move(agent)));
}

AgentProcess::AgentProcess(
    uint64_t pid,
    uint64_t parentPid,
    std::shared_ptr<IEnvironment> environment,
    const std::string& name,
    std::shared_ptr<Agent> agent)
    : m_pid(pid)
    , m_parentPid(parentPid)
    , m_environment(std::move(environment))
    , m_name(name)
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

std::string AgentProcess::Name() const {
    return m_name;
}

std::shared_ptr<IEnvironment> AgentProcess::GetEnvironment() const {
    return m_environment;
}

bool AgentProcess::IsFinished() const {
    return m_agent->IsFinished();
}

void AgentProcess::WaitToFinish() {
    m_agent->WaitToFinish();
}

bool AgentProcess::WaitToFinish(uint64_t timeoutMs) {
    return m_agent->WaitToFinish(timeoutMs);
}

bool AgentProcess::Stop(unsigned timeoutMs) {
    return m_agent->Stop(timeoutMs);
}

void AgentProcess::Kill() {
    m_agent->Kill();
}

std::shared_ptr<IAgent> AgentProcess::AsAgent() const {
    return m_agent;
}

}
