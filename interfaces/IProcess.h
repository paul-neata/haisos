#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "IEnvironment.h"
#include "ILLMService.h"

namespace Haisos {

// A single running process under an IHaisosOS: either agent-backed (.md) or,
// in the future, backed by other runtimes (.lua, wasm, a real OS process acting
// as a driver, ...). A top-most process has a GetParentPid() of 0; process
// parentage is being redefined along with the process/agent split, so for now
// everything started through IHaisosOS::StartProcess is top-most.
class IProcess {
public:
    virtual ~IProcess() = default;

    virtual uint64_t GetPid() const = 0;
    virtual uint64_t GetParentPid() const = 0;
    virtual std::string Name() const = 0;

    // The environment this process was started with -- its own, not the OS's
    // (see IHaisosOS::StartProcess).
    virtual std::shared_ptr<IEnvironment> GetEnvironment() const = 0;

    virtual bool IsFinished() const = 0;
    virtual void WaitToFinish() = 0;
    virtual bool WaitToFinish(uint64_t timeoutMs) = 0;
    virtual bool Stop(unsigned timeoutMs) = 0;
    virtual void Kill() = 0;

    // Non-null only for a process whose runtime is an LLM agent.
    virtual std::shared_ptr<IAgent> AsAgent() const = 0;
};

}
