#pragma once
#include <string>
#include <functional>
#include "interfaces/ILLMCommunicator.h"
#include "interfaces/ILLMService.h"

namespace Haisos::Mocks {

class MockLLMCommunicator : public ILLMCommunicator {
public:
    MockLLMCommunicator()
        : m_callCount(0) {}

    void SetMessageResponse(const std::string& message) { m_messageResponse = message; }

    // Makes the next response ask for |toolName|, so an agent's tool-calling
    // path can be exercised without a real LLM.
    void SetToolCallResponse(const std::string& toolName) { m_toolCallName = toolName; }

    // Runs inside Call(), i.e. on the agent's own thread mid-round -- the only
    // point where something can happen "while the agent is busy".
    void SetOnCall(std::function<void()> onCall) { m_onCall = std::move(onCall); }

    LLMResponse Call(
        const std::vector<LLMMessage>& messages,
        const std::vector<std::tuple<std::string, std::string, nlohmann::json>>& /*availableTools*/) override
    {
        m_lastMessages = messages;
        ++m_callCount;

        if (m_onCall) {
            m_onCall();
        }

        LLMResponse response;
        response.message.role = "assistant";
        response.message.content = m_messageResponse;
        response.done = true;

        if (!m_toolCallName.empty()) {
            nlohmann::json toolCall;
            toolCall["id"] = "call_1";
            toolCall["function"]["name"] = m_toolCallName;
            toolCall["function"]["arguments"] = nlohmann::json::object();
            response.message.toolCallsJson.push_back(std::move(toolCall));
            // One round of tool calls is enough for any test using this.
            m_toolCallName.clear();
        }

        return response;
    }

    const std::vector<LLMMessage>& GetLastMessages() const { return m_lastMessages; }
    int GetCallCount() const { return m_callCount; }

private:
    std::string m_messageResponse;
    std::string m_toolCallName;
    std::function<void()> m_onCall;
    std::vector<LLMMessage> m_lastMessages;
    int m_callCount;
};

}
