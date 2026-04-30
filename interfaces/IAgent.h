#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Haisos {

class IAgent {
public:
    virtual ~IAgent() = default;
    virtual void Post(const std::string& command) = 0;
    virtual void Send(const std::string& command) = 0;
    virtual bool Stop(unsigned timeoutMs) = 0;
    virtual void Kill() = 0;
    virtual std::shared_ptr<IAgent> GetParent() const = 0;
    virtual std::string Name() const = 0;
    virtual void WaitToFinish() = 0;
    virtual bool WaitToFinish(uint64_t timeoutMs) = 0;
    virtual std::vector<std::shared_ptr<IAgent>> GetChildren() const = 0;
    virtual nlohmann::json GetHistory() const = 0;
    virtual std::string GetConsoleOutput() const = 0;
    virtual bool IsFinished() const = 0;
    virtual bool IsKilled() const = 0;
    virtual std::string GetStartTime() const = 0;
    virtual int GetDepth() const = 0;
    virtual bool IsLongRunning() const = 0;
    virtual void AddChild(std::shared_ptr<IAgent> child) = 0;
};

}
