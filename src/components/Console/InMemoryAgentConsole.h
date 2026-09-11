#pragma once
#include <mutex>
#include <string>
#include "interfaces/ILLMService.h"

namespace Haisos {

// A standalone IAgentConsole that just accumulates what was written to it in
// memory, with no physical/real console involved.
class InMemoryAgentConsole : public IAgentConsole {
public:
    void Write(const std::string& message) override;
    std::string GetContents() const;

private:
    mutable std::mutex m_mutex;
    std::string m_contents;
};

}
