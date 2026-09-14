#pragma once
#include <string>

namespace Haisos {

// The real, physical console (stdout, shared across whatever writes to it).
// Distinct from IAgentConsole, which is the minimal per-agent view onto it (or
// onto nothing at all, for an in-memory-only console).
class IPhysicalConsole {
public:
    virtual ~IPhysicalConsole() = default;
    virtual void Write(const std::string& message) = 0;
    virtual void Write(const std::string& sourceName, const std::string& message) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
};

}
