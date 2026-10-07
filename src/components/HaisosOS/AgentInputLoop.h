#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include "interfaces/IFileDescriptor.h"
#include "interfaces/ILLMService.h"
#include "src/components/libheaders/StopToken.h"

namespace Haisos {

// What makes an interactive agent process interactive: a thread that reads the
// process's stdin a line at a time and posts each line to the agent as its
// next user message, for as long as the agent is running.
//
// The loop ends when:
//   * the agent has closed (it called self_close, or was asked to stop). A
//     line already being read cannot be abandoned -- a read blocked on the
//     console's input cannot be interrupted at all (D7), though a pipe's can --
//     so this is noticed when the next line arrives, that line being dropped
//     rather than posted to an agent that is no longer listening; or
//   * the input reaches its end. Nothing more can ever arrive, so the agent is
//     asked to stop: an interactive agent that can no longer be spoken to would
//     otherwise wait forever.
class AgentInputLoop {
public:
    // Neither agent nor input may be null: input is the descriptor the loop
    // reads the agent's lines from. stopToken is the calling agent process's
    // stop token: installed on the loop's thread while it runs, so a read
    // blocked on a pipe stdin is interrupted by kIOInterrupted (which the line
    // reader reports as end of input) as soon as the agent closes itself or is
    // asked to stop. Null installs nothing: console input stays
    // uninterruptible (D7: the console's blocking read is the known
    // exception). The loop does not run until Start().
    static std::shared_ptr<AgentInputLoop> Create(std::shared_ptr<IAgent> agent, std::shared_ptr<IFileDescriptor> input, std::shared_ptr<StopToken> stopToken = nullptr);

    // Waits for the thread, however long it takes: it may be blocked reading a
    // line, and it uses members this destructor is about to free.
    ~AgentInputLoop();

    AgentInputLoop(const AgentInputLoop&) = delete;
    AgentInputLoop& operator=(const AgentInputLoop&) = delete;

    // Starts reading. Separate from Create so the caller can first post the
    // agent's program: a line typed early must not overtake it.
    void Start();

    // Waits up to timeoutMs for the loop to end, returning whether it has.
    // WaitToFinish(0) just asks. A loop that was never started has not ended.
    bool WaitToFinish(uint64_t timeoutMs);

private:
    AgentInputLoop(std::shared_ptr<IAgent> agent, std::shared_ptr<IFileDescriptor> input, std::shared_ptr<StopToken> stopToken);

    void Run();

    std::shared_ptr<IAgent> m_agent;
    // The process's stdin. The loop holds its own reference, so the descriptor
    // table being released at the conversation's end never pulls it out from
    // under a blocked read.
    std::shared_ptr<IFileDescriptor> m_input;
    // The agent process's stop token: Run installs it on the loop's thread, so
    // a read blocked on a pipe stdin ends when the agent is asked to stop.
    std::shared_ptr<StopToken> m_stopToken;

    std::mutex m_threadMutex;
    std::thread m_thread;

    std::mutex m_finishedMutex;
    std::condition_variable m_finishedCv;
    bool m_finished = false;
};

}
