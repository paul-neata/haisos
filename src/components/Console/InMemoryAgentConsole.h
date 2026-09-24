#pragma once
#include <memory>
#include <mutex>
#include <string>
#include "interfaces/ILLMService.h"

namespace Haisos {

// A standalone IAgentConsole that just accumulates what was written to it in
// memory, with no physical/real console involved. It has nothing to read from,
// so ReadLine reports end of input straight away.
class InMemoryAgentConsole : public IAgentConsole {
public:
    static std::shared_ptr<InMemoryAgentConsole> Create() {
        return std::shared_ptr<InMemoryAgentConsole>(new InMemoryAgentConsole());
    }

    void Write(const std::string& message) override;
    std::optional<std::string> ReadLine() override;
    std::string GetContents() const;

private:
    InMemoryAgentConsole() = default;

    mutable std::mutex m_mutex;
    std::string m_contents;
};

}
