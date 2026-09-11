#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "ILLMService.h"

namespace Haisos {

// A single running process under an IHaisosOS: either agent-backed (.md) or,
// in the future, backed by other runtimes (.lua, wasm, a real OS process acting
// as a driver, ...). Every process has a parent, except top-most ones, whose
// GetParentPid() is 0.
class IProcess {
public:
    virtual ~IProcess() = default;

    virtual uint64_t GetPid() const = 0;
    virtual uint64_t GetParentPid() const = 0;
    virtual std::string Name() const = 0;

    virtual bool IsFinished() const = 0;
    virtual void WaitToFinish() = 0;
    virtual bool WaitToFinish(uint64_t timeoutMs) = 0;
    virtual bool Stop(unsigned timeoutMs) = 0;
    virtual void Kill() = 0;

    // Non-null only for a process whose runtime is an LLM agent.
    virtual std::shared_ptr<IAgent> AsAgent() const = 0;
};

}
