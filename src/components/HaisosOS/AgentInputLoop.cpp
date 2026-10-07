#include "AgentInputLoop.h"
#include <chrono>
#include "src/components/libheaders/DescriptorLineReader.h"
#include "src/components/libheaders/DestroyOffRuntimeThreads.h"
#include "src/components/Logger/Logger.h"

namespace Haisos {

// How long each pass of the destructor's wait gives the thread before reporting
// that it is still blocked. Only the reporting interval -- the wait itself
// never gives up.
constexpr uint64_t INPUT_LOOP_DESTRUCTION_WAIT_INTERVAL_MS = 5000;

std::shared_ptr<AgentInputLoop> AgentInputLoop::Create(std::shared_ptr<IAgent> agent, std::shared_ptr<IFileDescriptor> input, std::shared_ptr<StopToken> stopToken) {
    if (!agent || !input) {
        LogError("AgentInputLoop: refusing to create an input loop without %s", agent ? "an input" : "an agent");
        return nullptr;
    }
    return std::shared_ptr<AgentInputLoop>(new AgentInputLoop(std::move(agent), std::move(input), std::move(stopToken)));
}

AgentInputLoop::AgentInputLoop(std::shared_ptr<IAgent> agent, std::shared_ptr<IFileDescriptor> input, std::shared_ptr<StopToken> stopToken)
    : m_agent(std::move(agent))
    , m_input(std::move(input))
    , m_stopToken(std::move(stopToken))
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
    // A runtime thread too (see DestroyOffRuntimeThreads.h), since it runs for
    // the agent's process; this also names it in every log line.
    RuntimeThreadScope runtimeThread("input " + name);
    // The process's stop token on this thread: a read blocked on a pipe stdin
    // returns kIOInterrupted -- end of input to DescriptorLineReader -- as soon
    // as the agent closes itself or is asked to stop. A null token installs
    // nothing interruptible (console input, D7).
    StopTokenScope stopTokenScope(m_stopToken);
    LogDebug("AgentInputLoop: reading input for interactive agent '%s'", name.c_str());

    // The lines of the process's stdin, one at a time; reads ahead by chunks,
    // so a line never splits two reads.
    DescriptorLineReader lines(m_input);
    // WaitToFinish(0) does not wait; it just asks whether the agent has closed.
    while (!m_agent->WaitToFinish(0)) {
        std::optional<std::string> line = lines.ReadLine();
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
