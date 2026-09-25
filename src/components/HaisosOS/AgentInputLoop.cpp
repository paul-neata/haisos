#include "AgentInputLoop.h"
#include <chrono>
#include "src/components/Logger/Logger.h"

namespace Haisos {

// How long each pass of the destructor's wait gives the thread before reporting
// that it is still blocked. Only the reporting interval -- the wait itself
// never gives up.
constexpr uint64_t INPUT_LOOP_DESTRUCTION_WAIT_INTERVAL_MS = 5000;

std::shared_ptr<AgentInputLoop> AgentInputLoop::Create(std::shared_ptr<IAgent> agent, std::shared_ptr<IAgentConsole> console) {
    if (!agent || !console) {
        LogError("AgentInputLoop: refusing to create an input loop without %s", agent ? "a console" : "an agent");
        return nullptr;
    }
    return std::shared_ptr<AgentInputLoop>(new AgentInputLoop(std::move(agent), std::move(console)));
}

AgentInputLoop::AgentInputLoop(std::shared_ptr<IAgent> agent, std::shared_ptr<IAgentConsole> console)
    : m_agent(std::move(agent))
    , m_console(std::move(console))
{
}

AgentInputLoop::~AgentInputLoop() {
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        if (!m_thread.joinable()) {
            return;
        }
    }
    while (!WaitToFinish(INPUT_LOOP_DESTRUCTION_WAIT_INTERVAL_MS)) {
        LogWarning("AgentInputLoop: the input loop of agent '%s' is still waiting for a line in its destructor; "
            "it ends once one is entered (or input ends)", m_agent->Name().c_str());
    }
}

void AgentInputLoop::Start() {
    std::lock_guard<std::mutex> lock(m_threadMutex);
    if (!m_thread.joinable()) {
        m_thread = std::thread(&AgentInputLoop::Run, this);
    }
}

bool AgentInputLoop::WaitToFinish(uint64_t timeoutMs) {
    std::unique_lock<std::mutex> lock(m_finishedMutex);
    bool finished = m_finishedCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_finished; });
    lock.unlock();
    if (finished) {
        std::lock_guard<std::mutex> threadLock(m_threadMutex);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }
    return finished;
}

void AgentInputLoop::Run() {
    const std::string name = m_agent->Name();
    LogDebug("AgentInputLoop: reading input for interactive agent '%s'", name.c_str());

    // WaitToFinish(0) does not wait; it just asks whether the agent has closed.
    while (!m_agent->WaitToFinish(0)) {
        std::optional<std::string> line = m_console->ReadLine();
        if (!line) {
            LogInfo("AgentInputLoop: end of input for interactive agent '%s', asking it to stop", name.c_str());
            m_agent->TriggerStop();
            break;
        }
        // The agent may have closed while the line was being typed. Posting
        // to it would be harmless, but the line would silently go nowhere.
        if (m_agent->WaitToFinish(0)) {
            LogDebug("AgentInputLoop: agent '%s' closed while a line was being read; dropping it", name.c_str());
            break;
        }
        LogVerboseDebug("AgentInputLoop: posting a line to agent '%s'", name.c_str());
        m_agent->Post(*line);
    }

    {
        std::lock_guard<std::mutex> lock(m_finishedMutex);
        m_finished = true;
    }
    m_finishedCv.notify_all();
    LogDebug("AgentInputLoop: input loop for agent '%s' finished", name.c_str());
}

}
