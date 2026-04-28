#pragma once
#include <string>
#include <functional>
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/IToolFactory.h"

namespace Haisos::Mocks {

class MockLLMCommunicator : public ILLMCommunicator {
public:
    MockLLMCommunicator()
        : m_callCount(0) {}

    void SetMessageResponse(const std::string& message) { m_messageResponse = message; }

    LLMResponse Call(
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& /*availableTools*/,
        const JsonSendReceiveCallbacks& /*callbacks*/) override
    {
        m_lastMessages = messages;
        ++m_callCount;

        LLMResponse response;
        response.message.role = "assistant";
        response.message.content = m_messageResponse;
        response.done = true;

        return response;
    }

    const std::vector<LLMMessage>& GetLastMessages() const { return m_lastMessages; }
    int GetCallCount() const { return m_callCount; }

private:
    std::string m_messageResponse;
    std::vector<LLMMessage> m_lastMessages;
    int m_callCount;
};

}
